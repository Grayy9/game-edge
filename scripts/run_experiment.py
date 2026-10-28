"""Run the same local request/response workload directly and through GameEdge."""

import argparse
import json
import socket
import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def wait_for_port(port, process):
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"server exited with status {process.returncode}")
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.1):
                return
        except OSError:
            time.sleep(0.03)
    raise TimeoutError(f"server did not listen on port {port}")


def benchmark(port, args):
    command = [sys.executable, str(ROOT / "scripts" / "bench.py"),
               "--port", str(port), "--clients", str(args.clients),
               "--requests", str(args.requests),
               "--payload-bytes", str(args.payload_bytes)]
    return json.loads(subprocess.check_output(command, text=True, timeout=120))


def stop(process):
    if process.poll() is None:
        process.terminate()
    try:
        process.communicate(timeout=7)
    except subprocess.TimeoutExpired:
        process.kill()
        process.communicate()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gateway-bin", required=True)
    parser.add_argument("--clients", type=int, default=16)
    parser.add_argument("--requests", type=int, default=100)
    parser.add_argument("--payload-bytes", type=int, default=128)
    parser.add_argument("--rounds", type=int, default=3)
    args = parser.parse_args()
    if not (1 <= args.clients <= 10000 and args.requests > 0 and
            0 <= args.payload_bytes <= 65536 and 1 <= args.rounds <= 20):
        parser.error("workload arguments outside valid range")

    room_port, gateway_port = free_port(), free_port()
    room = subprocess.Popen([sys.executable, str(ROOT / "scripts" / "mock_room.py"),
                             "--port", str(room_port)], stdout=subprocess.DEVNULL,
                            stderr=subprocess.PIPE)
    gateway = None
    try:
        wait_for_port(room_port, room)
        gateway = subprocess.Popen([args.gateway_bin, str(gateway_port), "127.0.0.1",
                                    str(room_port)], stdout=subprocess.DEVNULL,
                                   stderr=subprocess.PIPE)
        wait_for_port(gateway_port, gateway)
        direct, through_gateway = [], []
        for _ in range(args.rounds):
            direct.append(benchmark(room_port, args))
            through_gateway.append(benchmark(gateway_port, args))
        print(json.dumps({"rounds": args.rounds, "direct": direct,
                          "gateway": through_gateway}, ensure_ascii=False, indent=2))
    finally:
        if gateway is not None:
            stop(gateway)
        stop(room)


if __name__ == "__main__":
    main()
