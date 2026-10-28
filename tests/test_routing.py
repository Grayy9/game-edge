import os
import socket
import subprocess
import threading
import time
import unittest

from test_gateway import RoomServer, frame, free_port, read_frame


class RoutingTests(unittest.TestCase):
    def setUp(self):
        self.rooms = [RoomServer(), RoomServer()]
        self.workers = [threading.Thread(target=room.serve_forever, daemon=True)
                        for room in self.rooms]
        for worker in self.workers:
            worker.start()
        self.port = free_port()
        ports = ",".join(str(room.server_address[1]) for room in self.rooms)
        self.proc = subprocess.Popen(
            [os.environ.get("GAME_EDGE_BIN", "./game_edge"), str(self.port),
             "--rooms", ports], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
        )
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            try:
                with socket.create_connection(("127.0.0.1", self.port), timeout=0.1):
                    return
            except OSError:
                time.sleep(0.03)
        self.fail(f"gateway did not start: {self.proc.communicate(timeout=1)[1]!r}")

    def tearDown(self):
        self.proc.terminate()
        self.proc.communicate(timeout=3)
        for room in self.rooms:
            room.shutdown()
            room.server_close()

    def connect(self):
        sock = socket.create_connection(("127.0.0.1", self.port), timeout=2)
        sock.settimeout(2)
        return sock

    def test_room_selection_pins_session_and_is_not_forwarded(self):
        with self.connect() as first, self.connect() as second:
            first.sendall(frame(4, b"\x00\x00\x00\x00") + frame(1, b"alpha"))
            second.sendall(frame(4, b"\x00\x00\x00\x01") + frame(1, b"beta"))
            self.assertEqual(read_frame(first), (1, b"alpha"))
            self.assertEqual(read_frame(second), (1, b"beta"))
            first.sendall(frame(1, b"again"))
            self.assertEqual(read_frame(first), (1, b"again"))
        self.assertEqual(self.rooms[0].seen, [(1, b"alpha"), (1, b"again")])
        self.assertEqual(self.rooms[1].seen, [(1, b"beta")])

    def test_data_before_room_and_second_selection_close_session(self):
        with self.connect() as early:
            early.sendall(frame(1, b"early"))
            self.assertEqual(early.recv(1), b"")
        with self.connect() as duplicate:
            duplicate.sendall(frame(4, b"\x00\x00\x00\x00"))
            duplicate.sendall(frame(4, b"\x00\x00\x00\x01"))
            self.assertEqual(duplicate.recv(1), b"")
        self.assertEqual(self.rooms[0].seen, [])
        self.assertEqual(self.rooms[1].seen, [])

    def test_ping_before_selection_is_local(self):
        with self.connect() as sock:
            sock.sendall(frame(2))
            self.assertEqual(read_frame(sock), (3, b""))
            sock.sendall(frame(4, b"\x00\x00\x00\x01") + frame(1, b"ok"))
            self.assertEqual(read_frame(sock), (1, b"ok"))
        self.assertEqual(self.rooms[1].seen, [(1, b"ok")])

    def test_room_port_with_trailing_characters_is_rejected(self):
        process = subprocess.Popen(
            [os.environ.get("GAME_EDGE_BIN", "./game_edge"), str(free_port()),
             "--rooms", "9001,9002x"], stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
        )
        try:
            self.assertEqual(process.wait(timeout=1), 2)
        finally:
            if process.poll() is None:
                process.terminate()
            process.communicate(timeout=3)


if __name__ == "__main__":
    unittest.main()
