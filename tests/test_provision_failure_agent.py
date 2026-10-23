import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('provision_failure_agent', Path(__file__).parents[1] / 'tools/provision_failure_agent.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class ProvisionTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(dir=Path(__file__).parents[1] / 'build')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.package = self.root / 'package'
        self.package.mkdir()
        (self.package / 'model.gguf').write_bytes(b'owned fixture')
        self.manifest = {'id': 'diagnostic-model', 'format': 'gguf', 'capabilities': ['chat'], 'entry_point': 'model.gguf',
                         'files': [{'path': 'model.gguf', 'size': 13, 'sha256': hashlib.sha256(b'owned fixture').hexdigest()}]}
        self.container = self.root / 'owned.societycontainer'
        self.container.mkdir()

    def save(self):
        (self.package / 'manifest.json').write_text(json.dumps(self.manifest))

    def test_verified_assets_use_no_additional_weight_bytes_and_reuse(self):
        self.save()
        result = module.provision(self.package, self.container)
        target = Path(result['catalog']) / self.manifest['id'] / 'model.gguf'
        self.assertEqual(target.stat().st_ino, (self.package / 'model.gguf').stat().st_ino)
        self.assertEqual(result['additional_weight_bytes'], 0)
        self.assertTrue(module.provision(self.package, self.container)['reused'])

    def test_integrity_failure_preserves_existing_data(self):
        self.manifest['files'][0]['sha256'] = '0' * 64
        self.save()
        with self.assertRaisesRegex(ValueError, 'integrity'):
            module.provision(self.package, self.container)
        self.assertEqual((self.package / 'model.gguf').read_bytes(), b'owned fixture')
        self.assertFalse((self.container / 'Models').exists())

    def test_path_traversal_and_non_chat_packages_are_rejected(self):
        self.manifest['id'] = '../outside'
        self.save()
        with self.assertRaises(ValueError): module.provision(self.package, self.container)
        self.manifest['id'] = 'valid'
        self.manifest['capabilities'] = ['image-generation']
        self.save()
        with self.assertRaises(ValueError): module.provision(self.package, self.container)


if __name__ == '__main__':
    unittest.main()
