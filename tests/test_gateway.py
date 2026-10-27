import os
import signal
import socket
import socketserver
import struct
import subprocess
import threading
import time
import unittest


BIN = os.environ.get("GAME_EDGE_BIN", "./game_edge")


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def frame(kind, payload=b""):
    return struct.pack("!I", len(payload) + 1) + bytes([kind]) + payload


def read_exact(sock, size):
    result = bytearray()
    while len(result) < size:
        data = sock.recv(size - len(result))
        if not data:
            raise ConnectionError("socket closed")
        result.extend(data)
    return bytes(result)


def read_frame(sock):
    length = struct.unpack("!I", read_exact(sock, 4))[0]
    body = read_exact(sock, length)
    return body[0], body[1:]


class RoomHandler(socketserver.BaseRequestHandler):
    def handle(self):
        self.request.settimeout(3)
        try:
            while True:
                kind, payload = read_frame(self.request)
                self.server.seen.append((kind, payload))
                if payload == b"drop":
                    return
                if payload == b"reply-and-close":
                    self.request.sendall(frame(kind, payload))
                    return
                if payload == b"burst-close":
                    self.request.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 4 * 1024 * 1024)
                    self.request.settimeout(10)
                    for index in range(24):
                        self.request.sendall(frame(1, bytes([index]) + b"x" * 60000))
                    return
                if payload == b"flood":
                    self.request.settimeout(0.2)
                    self.server.flood_started.set()
                    try:
                        for _ in range(1000):
                            self.request.sendall(frame(1, b"x" * 60000))
                    except OSError:
                        pass
                    return
                self.request.sendall(frame(kind, payload))
        except (ConnectionError, OSError):
            return


class RoomServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True

    def __init__(self):
        super().__init__(("127.0.0.1", 0), RoomHandler)
        self.seen = []
        self.flood_started = threading.Event()


class GatewayTests(unittest.TestCase):
    def setUp(self):
        self.room = RoomServer()
        self.room_thread = threading.Thread(target=self.room.serve_forever, daemon=True)
        self.room_thread.start()
        self.port = free_port()
        self.proc = subprocess.Popen(
            [BIN, str(self.port), "127.0.0.1", str(self.room.server_address[1]), "--idle-ms", "1000"],
            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
        )
        deadline = time.monotonic() + 3
        last_error = None
        while time.monotonic() < deadline:
            try:
                sock = socket.create_connection(("127.0.0.1", self.port), timeout=0.1)
                sock.close()
                return
            except OSError as error:
                last_error = error
                time.sleep(0.03)
        status = self.proc.poll()
        detail = self.proc.communicate(timeout=1)[1] if status is not None else b"still running"
        self.fail(f"gateway did not start: port={self.port}, exit={status}, error={last_error!r}, stderr={detail!r}")

    def tearDown(self):
        self.proc.terminate()
        try:
            self.proc.communicate(timeout=3)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.communicate()
        self.room.shutdown()
        self.room.server_close()

    def connect(self):
        sock = socket.create_connection(("127.0.0.1", self.port), timeout=2)
        sock.settimeout(2)
        return sock

    def test_fragmented_and_coalesced_data(self):
        with self.connect() as sock:
            message = frame(1, b"hello")
            sock.sendall(message[:2])
            sock.sendall(message[2:] + frame(1, b"world"))
            self.assertEqual(read_frame(sock), (1, b"hello"))
            self.assertEqual(read_frame(sock), (1, b"world"))

    def test_ping_is_local_and_bad_frame_is_isolated(self):
        with self.connect() as first, self.connect() as second:
            first.sendall(frame(2))
            self.assertEqual(read_frame(first), (3, b""))
            first.sendall(b"\x00\x00\x00\x01\x09")
            self.assertEqual(first.recv(1), b"")
            second.sendall(frame(1, b"still alive"))
            self.assertEqual(read_frame(second), (1, b"still alive"))
        self.assertNotIn((2, b""), self.room.seen)

    def test_oversized_length_is_rejected_before_body_arrives(self):
        with self.connect() as sock:
            sock.sendall(struct.pack("!I", 65538))
            self.assertEqual(sock.recv(1), b"")

    def test_upstream_drop_does_not_stop_other_sessions(self):
        with self.connect() as first, self.connect() as second:
            first.sendall(frame(1, b"drop"))
            self.assertEqual(first.recv(1), b"")
            second.sendall(frame(1, b"ok"))
            self.assertEqual(read_frame(second), (1, b"ok"))

    def test_final_upstream_reply_is_delivered_before_close(self):
        with self.connect() as sock:
            sock.sendall(frame(1, b"reply-and-close"))
            self.assertEqual(read_frame(sock), (1, b"reply-and-close"))
            self.assertEqual(sock.recv(1), b"")

    def test_slow_client_receives_backend_burst_before_close(self):
        with self.connect() as sock:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 8192)
            sock.settimeout(10)
            sock.sendall(frame(1, b"burst-close"))
            time.sleep(0.2)
            for index in range(24):
                kind, payload = read_frame(sock)
                self.assertEqual(kind, 1)
                self.assertEqual(payload, bytes([index]) + b"x" * 60000)
            self.assertEqual(sock.recv(1), b"")

    def test_idle_connection_closes(self):
        with self.connect() as sock:
            self.assertEqual(sock.recv(1), b"")

    def test_heartbeat_renews_client_idle_time(self):
        with self.connect() as sock:
            time.sleep(0.6)
            sock.sendall(frame(2))
            self.assertEqual(read_frame(sock), (3, b""))
            time.sleep(0.6)
            sock.sendall(frame(1, b"awake"))
            self.assertEqual(read_frame(sock), (1, b"awake"))

    def test_slow_client_does_not_block_another_session(self):
        with self.connect() as slow, self.connect() as fast:
            slow.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1024)
            slow.sendall(frame(1, b"flood"))
            self.assertTrue(self.room.flood_started.wait(2))
            fast.sendall(frame(1, b"responsive"))
            self.assertEqual(read_frame(fast), (1, b"responsive"))
            self.assertIsNone(self.proc.poll())

    def test_heartbeat_flood_does_not_starve_another_session(self):
        stop = threading.Event()
        with self.connect() as noisy, self.connect() as fast:
            noisy.settimeout(0.1)

            def flood():
                packet = frame(3) * 1024
                while not stop.is_set():
                    try:
                        noisy.sendall(packet)
                    except OSError:
                        return

            worker = threading.Thread(target=flood, daemon=True)
            worker.start()
            try:
                time.sleep(0.1)
                fast.sendall(frame(1, b"priority"))
                self.assertEqual(read_frame(fast), (1, b"priority"))
            finally:
                stop.set()
                worker.join(timeout=1)

    @unittest.skipIf(os.name == "nt", "SIGTERM graceful shutdown is checked on Linux CI")
    def test_signal_shutdown_drains_within_five_seconds(self):
        with self.connect():
            self.proc.send_signal(signal.SIGTERM)
            self.assertEqual(self.proc.wait(timeout=6), 0)
            self.assertIn(b"accepted=", self.proc.communicate()[1])


if __name__ == "__main__":
    unittest.main()
