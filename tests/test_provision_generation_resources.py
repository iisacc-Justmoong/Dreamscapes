"""Verified, repeatable installation into the Society-owned resource store."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location('provision_resources',
    Path(__file__).resolve().parents[1] / 'scripts/provision_generation_resources.py')
provision = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(provision)


class ProvisionResourcesTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[1] / 'build')
        self.addCleanup(self.temp.cleanup)
        root = Path(self.temp.name)
        self.source, self.society = root / 'source', root / 'society'
        self.source.mkdir()
        (self.society / 'Models').mkdir(parents=True)
        (self.society / '.society-drive.json').write_text('{"schemaVersion":1}')
        weights, config = b'fixture tensor data', b'{"fixture":true}'
        def entry(name, data):
            path = self.source / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            return {'file': name, 'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
        vae = entry('vae/model.safetensors', weights)
        vae['config'] = entry('vae/config.json', config)
        self.manifest = {'version': 1, 'fallback_vae': vae}
        self.save_manifest()
        self.target = self.society / 'Models/.generation-resources/iiLocalDiffusion'

    def save_manifest(self):
        (self.source / 'generation-defaults.json').write_text(json.dumps(self.manifest))

    def test_install_check_and_repeat_preserve_existing_bytes(self):
        first = provision.provision(self.source, self.society)
        self.assertEqual(first['installedFiles'], 3)
        original = (self.target / 'vae/model.safetensors').stat().st_ino
        self.assertEqual(provision.provision(self.source, self.society)['installedFiles'], 0)
        self.assertEqual(provision.provision(self.source, self.society, check=True)['verifiedFiles'], 3)
        self.assertEqual((self.target / 'vae/model.safetensors').stat().st_ino, original)
        (self.target / 'vae/config.json').unlink()
        self.assertEqual(provision.provision(self.source, self.society)['installedFiles'], 1)

    def test_missing_check_has_no_side_effects(self):
        with self.assertRaises(ValueError):
            provision.provision(self.source, self.society, check=True)
        self.assertFalse(self.target.exists())

    def test_bad_source_and_conflicting_destination_are_preserved(self):
        weights = self.source / 'vae/model.safetensors'
        original = weights.read_bytes()
        weights.write_bytes(b'corrupted')
        with self.assertRaises(ValueError):
            provision.provision(self.source, self.society)
        self.assertFalse(self.target.exists())
        weights.write_bytes(original)
        (self.target / 'vae').mkdir(parents=True)
        conflict = self.target / 'vae/model.safetensors'
        conflict.write_bytes(b'user resource')
        with self.assertRaises(ValueError):
            provision.provision(self.source, self.society)
        self.assertEqual(conflict.read_bytes(), b'user resource')
        self.assertFalse((self.target / 'generation-defaults.json').exists())

    def test_manifest_is_published_only_after_complete_resources(self):
        with patch.object(provision.shutil, 'copyfile', side_effect=OSError('copy interrupted')):
            with self.assertRaises(OSError):
                provision.provision(self.source, self.society)
        self.assertFalse((self.target / 'generation-defaults.json').exists())
        self.assertEqual(provision.provision(self.source, self.society)['installedFiles'], 3)

    def test_source_and_destination_symlinks_are_rejected(self):
        outside = self.source.parent / 'outside'
        outside.mkdir()
        self.target.parent.mkdir(parents=True)
        self.target.symlink_to(outside, target_is_directory=True)
        with self.assertRaises(ValueError):
            provision.provision(self.source, self.society)
        self.assertEqual(list(outside.iterdir()), [])
        self.target.unlink()
        weights = self.source / 'vae/model.safetensors'
        real = outside / 'model.safetensors'
        weights.rename(real)
        weights.symlink_to(real)
        with self.assertRaises(ValueError):
            provision.provision(self.source, self.society)

    def test_path_escape_and_invalid_container_are_rejected(self):
        for name in ('../outside', '/absolute', 'vae/../../outside'):
            self.manifest['fallback_vae']['file'] = name
            self.save_manifest()
            with self.assertRaises(ValueError):
                provision.provision(self.source, self.society)
        (self.society / '.society-drive.json').unlink()
        with self.assertRaises(ValueError):
            provision.provision(self.source, self.society)


if __name__ == '__main__':
    unittest.main()
