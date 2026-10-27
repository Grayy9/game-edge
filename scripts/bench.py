"""Small reproducible request/response workload for a GameEdge endpoint."""

import argparse
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
import json
import math
import os
import platform
import socket
import struct
import time


def read_exact(sock, count):
    result = bytearray()
    while len(result) < count:
        chunk = sock.recv(count - len(result))
        if not chunk:
            raise ConnectionError("connection closed during benchmark")
        result.extend(chunk)
    return result


def run_client(host, port, requests, payload):
    packet = struct.pack("!I", len(payload) + 1) + b"\x01" + payload
    latencies = []
    with socket.create_connection((host, port), timeout=5) as sock:
        sock.settimeout(5)
        for _ in range(requests):
            started = time.perf_counter_ns()
            sock.sendall(packet)
            length = struct.unpack("!I", read_exact(sock, 4))[0]
            body = read_exact(sock, length)
            if body[0] != 1 or body[1:] != payload:
                raise ValueError("unexpected response")
            latencies.append((time.perf_counter_ns() - started) / 1_000_000)
    return latencies


def percentile(sorted_values, fraction):
    return sorted_values[max(0, math.ceil(fraction * len(sorted_values)) - 1)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--clients", type=int, default=16)
    parser.add_argument("--requests", type=int, default=100)
    parser.add_argument("--payload-bytes", type=int, default=128)
    args = parser.parse_args()
    if not (1 <= args.clients <= 10000 and args.requests > 0 and 0 <= args.payload_bytes <= 65536):
        parser.error("clients, requests or payload size outside valid range")
    payload = b"x" * args.payload_bytes
    started = time.perf_counter()
    with ThreadPoolExecutor(max_workers=args.clients) as pool:
        batches = list(pool.map(lambda _: run_client(args.host, args.port, args.requests, payload),
                                range(args.clients)))
    elapsed = time.perf_counter() - started
    latencies = sorted(value for batch in batches for value in batch)
    report = {
        "timestamp_utc": datetime.now(timezone.utc).isoformat(),
        "platform": platform.platform(),
        "python": platform.python_version(),
        "logical_cpus": os.cpu_count(),
        "host": args.host,
        "port": args.port,
        "clients": args.clients,
        "requests_per_client": args.requests,
        "payload_bytes": args.payload_bytes,
        "requests_completed": len(latencies),
        "elapsed_seconds": round(elapsed, 3),
        "throughput_rps": round(len(latencies) / elapsed, 2),
        "p50_ms": round(percentile(latencies, 0.50), 3),
        "p99_ms": round(percentile(latencies, 0.99), 3),
    }
    print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
