"""Lock the unmodified SVG exports of Figma Dreamscapes 103:1211."""
import hashlib
from pathlib import Path
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / 'src/App/Views/Home/Assets/Navigation'
HASHES = {
    'home': 'cc72b69ed2dfaf036198f814c06418e697c746a91f9b81cbd91cc8f025a39bb6',
    'tools': 'a40aa3b412968dbe63f6602c066f97e626dc38dcbeca389fd3387401027fbc91',
    'storage': '7522eec88f1d2dd9a116dc4364eb30540e2fccaf7d142e025ba3f6adc926b8cf',
    'notification': '59428c31908d201bf107eb40a234b6247329d44b1a7fb88dd34026f22cede693',
    'account': '5c76f72646949f93d59ea2bb0c57ddbf764caee9ab500e6b5fa6c5391fb63d6c',
    'search': '9198eace6312db1b55a7c7913d4bddb625f1e85993b37925a502b29151285a39',
}


class MobileNavigationAssetsTest(unittest.TestCase):
    def test_exact_exports_dimensions_and_bundled_callsites(self):
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        qml = (ROOT / 'src/App/Views/Home/MobileHome.qml').read_text()
        for name, digest in HASHES.items():
            with self.subTest(icon=name):
                data = (ASSETS / f'{name}.svg').read_bytes()
                self.assertEqual(hashlib.sha256(data).hexdigest(), digest)
                svg = ET.fromstring(data)
                expected = (17.5, 20.5545) if name == 'storage' else (24, 24)
                self.assertEqual((float(svg.attrib['width']), float(svg.attrib['height'])), expected)
                self.assertIn(f'src/App/Views/Home/Assets/Navigation/{name}.svg', cmake)
                self.assertIn(f'Qt.resolvedUrl("Assets/Navigation/{name}.svg")', qml)
        self.assertNotIn('figma.com/api/mcp/asset', qml)
        self.assertEqual(qml.count('preserveIconColors: true'), 6)


if __name__ == '__main__':
    unittest.main()
