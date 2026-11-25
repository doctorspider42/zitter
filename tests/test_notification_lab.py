import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("notification_lab",
                                              ROOT / "tools/notification_lab.py")
lab = importlib.util.module_from_spec(spec)
spec.loader.exec_module(lab)


def reader(values):
    return lambda path, name: values.get((path, name))


APP = lab.STABLE_APP_ID
SETTINGS = rf"{lab.NOTIFICATION_SETTINGS}\{APP}"
REGISTRATION = rf"{lab.APP_USER_MODEL_IDS}\{APP}"


class NotificationLabTests(unittest.TestCase):
    def levels(self, values, shortcuts=()):
        return [level for level, _ in lab.diagnose(APP, reader(values), list(shortcuts))]

    def test_missing_registration_fails(self):
        self.assertIn("fail", self.levels({}))

    def test_registry_registration_counts_like_a_shortcut(self):
        registered = {(REGISTRATION, "DisplayName"): "Zitter", (SETTINGS, None): True}
        self.assertNotIn("fail", self.levels(registered))
        self.assertNotIn("fail", self.levels({(SETTINGS, None): True}, [Path("Zitter.lnk")]))
        self.assertEqual(self.levels({(SETTINGS, None): True}, [Path("Zitter.lnk")])[:3],
                         ["ok", "ok", "ok"])

    def test_switched_off_notifications_fail(self):
        self.assertIn("fail", self.levels({(lab.PUSH_NOTIFICATIONS, "ToastEnabled"): 0},
                                          [Path("Zitter.lnk")]))
        self.assertIn("fail", self.levels({(SETTINGS, "Enabled"): 0}, [Path("Zitter.lnk")]))

    def test_toast_script_quotes_text(self):
        script = lab.powershell_toast_script(APP, "It's", "a <b>body</b>")
        self.assertIn("'It''s'", script)
        self.assertIn("CreateTextNode('a <b>body</b>')", script)
        self.assertIn("CreateToastNotifier('Zitter.Browser')", script)

    def test_service_worker_is_served_as_javascript(self):
        self.assertEqual(lab.LabHandler.extensions_map[".js"], "text/javascript")
        self.assertTrue((lab.TESTDATA / "notification_sw.js").exists())
        self.assertIn("notification_sw.js", (lab.TESTDATA / lab.PAGE).read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
