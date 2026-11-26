import subprocess
import sys
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools" / "remote_build"))
import workers  # noqa: E402


def done(rc, out=""):
    return subprocess.CompletedProcess([], rc, out, "")


class RemoteWorkersTest(unittest.TestCase):
    def test_state_reads_ssh_result(self):
        with mock.patch.object(workers, "ssh", return_value=done(0, "123 nativelink\n")):
            self.assertEqual(workers.state("h", "wsl"), "up")
        with mock.patch.object(workers, "ssh", return_value=done(1)):
            self.assertEqual(workers.state("h", "wsl"), "down")
        with mock.patch.object(workers, "ssh", return_value=done(255)):
            self.assertEqual(workers.state("h", "mac"), "unreachable")

    def test_ensure_starts_down_workers_only(self):
        fleet = [("a", "host-a", "wsl"), ("b", "host-b", "mac"), ("c", "host-c", "wsl")]
        started = []
        checks = {"host-a": iter(["up"]), "host-b": iter(["down", "up"]),
                  "host-c": iter(["unreachable"])}

        def fake_ssh(host, command, timeout=40):
            started.append((host, command))
            return done(0)

        with mock.patch.object(workers, "state", lambda h, k: next(checks[h])), \
                mock.patch.object(workers, "ssh", fake_ssh), \
                mock.patch.object(workers.time, "sleep", lambda s: None):
            states = workers.ensure(workers=fleet, wait=30)
        self.assertEqual(states, {"a": "up", "b": "started", "c": "unreachable"})
        self.assertEqual([h for h, _ in started], ["host-b"])
        self.assertIn("kickstart", started[0][1])

    def test_no_start_only_reports(self):
        fleet = [("a", "host-a", "wsl")]
        with mock.patch.object(workers, "state", lambda h, k: "down"), \
                mock.patch.object(workers, "ssh") as ssh:
            self.assertEqual(workers.ensure(start=False, workers=fleet), {"a": "down"})
        ssh.assert_not_called()


if __name__ == "__main__":
    unittest.main()
