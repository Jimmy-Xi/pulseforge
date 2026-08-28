import contextlib
import io
import json
from pathlib import Path
import tempfile
import unittest

from pulseforge_sim.cli import main


class CliTests(unittest.TestCase):
    def test_generate_and_analyze(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            trace = Path(directory) / "trace.ndjson"
            stderr = io.StringIO()
            with contextlib.redirect_stderr(stderr):
                result = main([
                    "generate",
                    "--attempts", "20",
                    "--drop-every", "5",
                    "--spike-every", "7",
                    "--output", str(trace),
                ])
            self.assertEqual(result, 0)
            summary = json.loads(stderr.getvalue())
            self.assertEqual(summary["stats"]["generated"], 20)
            self.assertEqual(summary["stats"]["delivered"], 16)

            stdout = io.StringIO()
            with contextlib.redirect_stdout(stdout):
                result = main(["analyze", str(trace)])
            self.assertEqual(result, 0)
            report = json.loads(stdout.getvalue())
            self.assertEqual(report["frames"], 16)
            self.assertEqual(report["sequence_gaps"], 3)


if __name__ == "__main__":
    unittest.main()
