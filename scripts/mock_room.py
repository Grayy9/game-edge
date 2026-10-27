"""Echo room service for local gateway demos. It is not game logic."""

import argparse
import socketserver
import struct


def read_exact(sock, size):
    result = bytearray()
    while len(result) < size:
        data = sock.recv(size - len(result))
        if not data:
            return None
        result.extend(data)
    return bytes(result)


class Handler(socketserver.BaseRequestHandler):
    def handle(self):
        while True:
            header = read_exact(self.request, 4)
            if header is None:
                return
            length = struct.unpack("!I", header)[0]
            if length < 1 or length > 65537:
                return
            body = read_exact(self.request, length)
            if body is None or body[0] != 1:
                return
            self.request.sendall(header + body)


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=9001)
    args = parser.parse_args()
    with Server(("127.0.0.1", args.port), Handler) as server:
        print(f"mock room listening on 127.0.0.1:{args.port}", flush=True)
        server.serve_forever()


if __name__ == "__main__":
    main()
