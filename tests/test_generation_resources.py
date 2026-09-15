import tempfile
import unittest
import zipfile
from pathlib import Path
from verify_generation_resources import verify_bundle, verify_archive

class GenerationResourceTests(unittest.TestCase):
    def setUp(self):
        build = Path(__file__).resolve().parents[1] / 'build/generation-resource-tests'
        build.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=build, prefix='bundle ')
        self.addCleanup(self.temporary.cleanup)
        self.bundle = Path(self.temporary.name)

    def test_program_and_ui_assets_are_allowed(self):
        (self.bundle / 'Dreamscapes').write_bytes(b'program')
        (self.bundle / 'AppIcon.png').write_bytes(b'icon')
        self.assertEqual(verify_bundle(self.bundle),
                         {'bundleBytes': 11, 'modelAssetCount': 0, 'modelStorage': 'Society'})

    def test_model_weights_are_rejected_in_any_bundle_directory(self):
        for suffix in ('.safetensors', '.GGUF', '.pt', '.ckpt', '.onnx', '.mlpackage'):
            with self.subTest(suffix=suffix):
                weights = self.bundle / 'Contents/Resources/defaults' / ('model' + suffix)
                weights.parent.mkdir(parents=True, exist_ok=True)
                weights.write_bytes(b'weights')
                with self.assertRaisesRegex(AssertionError, 'Model belongs in Society'):
                    verify_bundle(self.bundle)
                weights.unlink()

    def test_a_resource_manifest_cannot_reintroduce_bundled_models(self):
        (self.bundle / 'generation-defaults.json').write_text('{}')
        with self.assertRaisesRegex(AssertionError, 'Generation resources belong in Society'):
            verify_bundle(self.bundle)

    def test_missing_bundle_is_rejected(self):
        with self.assertRaisesRegex(AssertionError, 'Missing application bundle'):
            verify_bundle(self.bundle / 'missing')

    def test_android_archive_rejects_compiled_and_sharded_models(self):
        apk = self.bundle / 'Dreamscapes.apk'
        for model in ('assets/model.mlmodelc/weights/weight.bin',
                      'assets/pytorch_model-00001-of-00002.bin', 'assets/model.gguf'):
            with self.subTest(model=model):
                with zipfile.ZipFile(apk, 'w') as archive:
                    archive.writestr(model, b'weights')
                with self.assertRaisesRegex(AssertionError, 'Model belongs in Society'):
                    verify_archive(apk)
        with zipfile.ZipFile(apk, 'w') as archive:
            archive.writestr('lib/arm64-v8a/libDreamscapes.so', b'code')
            archive.writestr('res/icon.png', b'icon')
        self.assertEqual(verify_archive(apk)['modelAssetCount'], 0)
