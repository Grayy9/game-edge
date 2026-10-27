import json
import os
import socket
import subprocess
import sys
import time
import unittest

from test_gateway import RoomServer, free_port


class BenchTests(unittest.TestCase):
    def test_reports_workload_and_latency(self):
        room = RoomServer()
        worker = __import__("threading").Thread(target=room.serve_forever, daemon=True)
        worker.start()
        port = free_port()
        gateway = subprocess.Popen(
            [os.environ.get("GAME_EDGE_BIN", "./game_edge"), str(port), "127.0.0.1",
             str(room.server_address[1])], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
        )
        try:
            deadline = time.monotonic() + 3
            while True:
                try:
                    with socket.create_connection(("127.0.0.1", port), timeout=0.1):
                        break
                except OSError:
                    if time.monotonic() >= deadline:
                        self.fail("gateway did not start")
                    time.sleep(0.03)
            result = subprocess.run(
                [sys.executable, "scripts/bench.py", "--host", "127.0.0.1",
                 "--port", str(port), "--clients", "2", "--requests", "3"],
                capture_output=True, text=True, timeout=10, check=True,
            )
            report = json.loads(result.stdout)
            self.assertEqual(report["requests_completed"], 6)
            self.assertEqual(report["clients"], 2)
            self.assertIn("p99_ms", report)
            self.assertIn("throughput_rps", report)
            self.assertIn("platform", report)
        finally:
            gateway.terminate()
            gateway.communicate(timeout=3)
            room.shutdown()
            room.server_close()


if __name__ == "__main__":
    unittest.main()
