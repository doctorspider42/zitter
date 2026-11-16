import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import forget_dev_apps


class ForgetDevAppsTest(unittest.TestCase):
    def test_keeps_the_installed_app_and_drops_development_copies(self):
        dump = '\n'.join([
            'path:                       /Applications/Zitter.app',
            'path:                       /Applications/Zitter.app/Contents/Frameworks/'
            'Zitter Content Lab Framework.framework/Helpers/Zitter Content Lab Helper.app',
            'path:                       /Applications/scoped_dirAB12/Zitter.app (0x1a2b)',
            'path:                       /Applications/Zitter Updater E2E.app',
            'path:                       /private/var/folders/x/T/probe/Zitter Content Lab.app',
            'path:                       /Applications/Safari.app',
            'name:                       Zitter',
        ])
        self.assertEqual(forget_dev_apps.registered_copies(dump), [
            '/Applications/Zitter Updater E2E.app',
            '/Applications/scoped_dirAB12/Zitter.app',
            '/private/var/folders/x/T/probe/Zitter Content Lab.app',
        ])


if __name__ == '__main__':
    unittest.main()
