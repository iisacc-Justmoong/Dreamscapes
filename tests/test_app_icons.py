"""Validate shipped icon dimensions, alpha policy, platform slots and source provenance."""
import hashlib
import json
from pathlib import Path
import struct
import shutil
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
ICONS = ROOT / 'resources/Appicon/generated'


def png(path):
    data = path.read_bytes()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise AssertionError(f'Invalid PNG: {path}')
    width, height, depth, color = struct.unpack('>IIBB', data[16:26])
    return width, height, depth, color


class AppIconsTest(unittest.TestCase):
    def test_assets_match_the_approved_source_and_manifest(self):
        manifest = json.loads((ICONS / 'manifest.json').read_text())
        self.assertEqual(manifest['source_sha256'], hashlib.sha256((ICONS.parent / manifest['source']).read_bytes()).hexdigest())
        self.assertGreater(len(manifest['files']), 60)
        for name, entry in manifest['files'].items():
            with self.subTest(file=name):
                path = ICONS / name
                self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), entry['sha256'])
                if name.endswith('.png'):
                    self.assertEqual(png(path)[:2], (entry['width'], entry['height']))

    def test_ios_slots_are_complete_square_and_opaque(self):
        directory = ICONS / 'ios/Assets.xcassets/AppIcon.appiconset'
        slots = json.loads((directory / 'Contents.json').read_text())['images']
        expected = {('iphone', '60x60', '2x'), ('iphone', '60x60', '3x'),
                    ('ipad', '76x76', '1x'), ('ipad', '76x76', '2x'),
                    ('ipad', '83.5x83.5', '2x'), ('ios-marketing', '1024x1024', '1x')}
        self.assertTrue(expected.issubset({(s['idiom'], s['size'], s['scale']) for s in slots}))
        for slot in slots:
            pixels = round(float(slot['size'].split('x')[0]) * int(slot['scale'][0]))
            self.assertEqual(png(directory / slot['filename']), (pixels, pixels, 8, 2))

    def test_windows_contains_small_and_high_dpi_frames(self):
        data = (ICONS / 'windows/Dreamscapes.ico').read_bytes()
        reserved, kind, count = struct.unpack('<HHH', data[:6])
        self.assertEqual((reserved, kind), (0, 1))
        sizes = set()
        for index in range(count):
            width, height, _, _, _, _, length, offset = struct.unpack('<BBBBHHII', data[6 + index*16:22 + index*16])
            self.assertEqual(width, height)
            self.assertLessEqual(offset + length, len(data))
            sizes.add(width or 256)
        self.assertTrue({16, 24, 32, 48, 256}.issubset(sizes))

    def test_android_adaptive_and_legacy_resources_resolve(self):
        base = ICONS / 'android/res'
        for density, size in [('ldpi', 36), ('mdpi', 48), ('hdpi', 72), ('xhdpi', 96), ('xxhdpi', 144), ('xxxhdpi', 192)]:
            self.assertEqual(png(base / f'mipmap-{density}/ic_launcher.png')[:2], (size, size))
        self.assertEqual(png(base / 'drawable-xxxhdpi/ic_launcher_foreground.png')[:2], (432, 432))
        for version in (26, 33):
            icon = ET.parse(base / f'mipmap-anydpi-v{version}/ic_launcher.xml').getroot()
            self.assertEqual(icon.tag, 'adaptive-icon')
            self.assertIsNotNone(icon.find('background'))
            self.assertIsNotNone(icon.find('foreground'))
            if version == 33:
                self.assertIsNotNone(icon.find('monochrome'))

    def test_mac_linux_and_web_packages_have_real_icons(self):
        data = (ICONS / 'macos/Dreamscapes.icns').read_bytes()
        self.assertEqual(data[:4], b'icns')
        self.assertEqual(struct.unpack('>I', data[4:8])[0], len(data))
        tags, offset = set(), 8
        while offset < len(data):
            tag, length = struct.unpack('>4sI', data[offset:offset+8])
            self.assertGreater(length, 8)
            tags.add(tag)
            offset += length
        self.assertEqual(offset, len(data))
        self.assertIn(b'ic10', tags)  # 1024px Retina representation.
        self.assertEqual(png(ICONS / 'linux/hicolor/512x512/apps/com.iisacc.dreamscapes.png')[:2], (512, 512))
        manifest = json.loads((ICONS / 'web/manifest.webmanifest').read_text())
        for icon in manifest['icons']:
            self.assertEqual(png(ICONS / 'web' / icon['src'])[:2], tuple(map(int, icon['sizes'].split('x'))))
        self.assertEqual({s['purpose'] for s in manifest['icons']}, {'any', 'maskable'})

    def test_web_packaging_preserves_the_app_and_is_repeatable(self):
        build = ROOT / 'build'
        build.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='icon-web-', dir=build) as temporary:
            output = Path(temporary)
            page = output / 'Dreamscapes.html'
            page.write_text('<!doctype html><html><head><title>Dreamscapes</title></head>'
                            '<body><script src="Dreamscapes.js"></script></body></html>')
            command = ['cmake', f'-DDREAMSCAPES_WEB_OUTPUT={output}',
                       f'-DDREAMSCAPES_WEB_ICONS={ICONS / "web"}', '-P', str(ROOT / 'cmake/WebIcons.cmake')]
            subprocess.run(command, check=True, capture_output=True)
            first = page.read_bytes()
            subprocess.run(command, check=True, capture_output=True)
            self.assertEqual(page.read_bytes(), first)
            self.assertIn(b'<script src="Dreamscapes.js"></script>', first)
            for name in ('favicon.ico', 'apple-touch-icon.png', 'manifest.webmanifest'):
                self.assertIn(f'href="{name}"'.encode(), first)
                self.assertEqual((output / name).read_bytes(), (ICONS / 'web' / name).read_bytes())
            manifest = json.loads((output / 'manifest.webmanifest').read_text())
            for icon in manifest['icons']:
                self.assertTrue((output / icon['src']).is_file())

    def test_android_staging_keeps_activity_provider_and_authored_sources(self):
        build = ROOT / 'build'
        build.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='icon-android-', dir=build) as temporary:
            source = Path(temporary) / 'source'
            source.mkdir()
            (source / 'resources').symlink_to(ROOT / 'resources', target_is_directory=True)
            shutil.copytree(ROOT / 'platform/android', source / 'platform/android')
            authored = source / 'platform/android/AndroidManifest.xml'
            original = authored.read_bytes()
            java = source / 'platform/android/src/com/iisacc/dreamscapes/DreamscapesActivity.java'
            (source / 'CMakeLists.txt').write_text(
                'cmake_minimum_required(VERSION 3.31)\nproject(IconPackage NONE)\n'
                'function(qt_add_resources)\nendfunction()\n'
                'set(APPLE FALSE)\nset(IOS FALSE)\nset(ANDROID TRUE)\n'
                'add_library(Dreamscapes INTERFACE)\n'
                'set_property(TARGET Dreamscapes PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR '
                '"${CMAKE_CURRENT_SOURCE_DIR}/platform/android")\n'
                f'include("{ROOT / "cmake/AppIcons.cmake"}")\n'
                'dreamscapes_add_app_icons(Dreamscapes)\n')
            binary = Path(temporary) / 'build'
            subprocess.run(['cmake', '-S', str(source), '-B', str(binary)], check=True, capture_output=True)
            package = binary / 'app-icons/android'
            android = '{http://schemas.android.com/apk/res/android}'
            app = ET.parse(package / 'AndroidManifest.xml').getroot().find('application')
            self.assertEqual(app.get(android + 'icon'), '@mipmap/ic_launcher')
            self.assertEqual(app.get(android + 'roundIcon'), '@mipmap/ic_launcher_round')
            self.assertEqual(app.find('activity').get(android + 'name'), 'com.iisacc.dreamscapes.DreamscapesActivity')
            self.assertEqual(app.find('provider').get(android + 'authorities'), '${applicationId}.qtprovider')
            for asset in (ICONS / 'android/res').rglob('*'):
                if asset.is_file():
                    self.assertEqual((package / 'res' / asset.relative_to(ICONS / 'android/res')).read_bytes(), asset.read_bytes())
            java.write_text(java.read_text() + '\n// Packaging dependency check.\n')
            subprocess.run(['cmake', '--build', str(binary)], check=True, capture_output=True)
            self.assertEqual((package / java.relative_to(source / 'platform/android')).read_bytes(), java.read_bytes())
            self.assertEqual(authored.read_bytes(), original)
            self.assertFalse((source / 'platform/android/res').exists())


if __name__ == '__main__':
    unittest.main()
