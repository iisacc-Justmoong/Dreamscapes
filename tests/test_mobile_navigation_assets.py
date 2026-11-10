"""Mobile navigation refers to reusable LVRS assets rather than product SVG copies."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

class MobileNavigationAssetsTest(unittest.TestCase):
    def test_navigation_uses_shared_icon_roles(self):
        qml = (ROOT / 'src/App/Views/Home/MobileHome.qml').read_text(encoding='utf-8')
        for name in ('home', 'collection-asset', 'sqlFile', 'toolwindownotifications', 'role', 'generalsearch'):
            with self.subTest(icon=name):
                self.assertIn('iconName: "' + name + '"', qml)
        self.assertNotIn('Assets/Navigation/', qml)
        self.assertNotIn('preserveIconColors: true', qml)

if __name__ == '__main__':
    unittest.main()
