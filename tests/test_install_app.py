import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('install_app', Path(__file__).parents[1] / 'install.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class InstallTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(dir=Path(__file__).parents[1] / 'build')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def test_manifest_detects_contents_permissions_and_links(self):
        item = self.root / 'executable'
        item.write_bytes(b'one')
        item.chmod(0o700)
        (self.root / 'link').symlink_to('executable')
        first = module.manifest(self.root)
        self.assertEqual(first['link']['symlink'], 'executable')
        item.write_bytes(b'two')
        self.assertNotEqual(first, module.manifest(self.root))
        item.write_bytes(b'one'); item.chmod(0o600)
        self.assertNotEqual(first, module.manifest(self.root))

    def test_version_and_build_cannot_silently_change(self):
        previous = {'version': '0.1.1', 'build': '0.1.1'}
        module.check_version(previous, dict(previous))
        for field in ('version', 'build'):
            changed = dict(previous); changed[field] = '0.1.2'
            with self.assertRaises(RuntimeError): module.check_version(previous, changed)

    @unittest.skipUnless(sys.platform == 'darwin', 'macOS atomic exchange')
    def test_atomic_exchange_preserves_both_payloads(self):
        left, right = self.root / 'left', self.root / 'right'
        left.mkdir(); right.mkdir()
        (left / 'file').write_text('candidate'); (right / 'file').write_text('installed')
        module.exchange(left, right)
        self.assertEqual((left / 'file').read_text(), 'installed')
        self.assertEqual((right / 'file').read_text(), 'candidate')


if __name__ == '__main__': unittest.main()
