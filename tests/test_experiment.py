import json
import os
import subprocess
import sys
import unittest


class ExperimentTests(unittest.TestCase):
    def test_runs_direct_and_gateway_workloads(self):
        result = subprocess.run(
            [sys.executable, "scripts/run_experiment.py", "--gateway-bin",
             os.environ.get("GAME_EDGE_BIN", "./game_edge"), "--clients", "2",
             "--requests", "3", "--rounds", "1"],
            capture_output=True, text=True, timeout=20, check=True,
        )
        report = json.loads(result.stdout)
        self.assertEqual(report["rounds"], 1)
        self.assertEqual(report["direct"][0]["requests_completed"], 6)
        self.assertEqual(report["gateway"][0]["requests_completed"], 6)
        self.assertIn("p99_ms", report["gateway"][0])


if __name__ == "__main__":
    unittest.main()
