"""Keep product runtime builds isolated and reject a heavyweight backend leak."""
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest
import subprocess
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('runtime_profile', ROOT / 'scripts/build-macos-runtime.py')
profile = importlib.util.module_from_spec(spec)
spec.loader.exec_module(profile)


class RuntimeProfileTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(dir=ROOT / 'build')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.product = self.root / 'Product/Dreamscapes'
        (self.product / 'build').mkdir(parents=True)
        self.sdk = self.root / 'SDK/iiLocalDiffusion'
        self.sdk.mkdir(parents=True)
        (self.sdk / 'CMakeLists.txt').write_text('# fixture SDK\n')
        self.shared = {}
        for relative in ('mlx-src/CMakeLists.txt',
                         'mlx-src/mlx/distributed/jaccl/lib/jaccl/rdma.h',
                         'mlx-src/mlx/3rdparty/pocketfft.h',
                         'iild_sdcpp-src/ggml/src/ggml-metal/ggml-metal.cpp'):
            path = self.sdk / 'build/_deps' / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('// shared source\n')
            self.shared[path] = path.read_bytes()
        (self.product / 'build/CMakeCache.txt').write_text(
            'iiFileProvider_DIR:PATH=/selected/provider\n'
            'iiFileProviderNative_DIR:PATH=/selected/native-provider\n')
        self.calls = []

    def invoke(self, *arguments, links='@rpath/libmlx.dylib'):
        with mock.patch.object(profile, 'ROOT', self.product), \
             mock.patch.object(profile.sys, 'platform', 'darwin'), \
             mock.patch.object(profile.sys, 'argv', ['build-macos-runtime.py', *arguments]), \
             mock.patch.object(profile, 'run', side_effect=lambda *args: self.calls.append(tuple(map(str, args)))), \
             mock.patch.object(profile.subprocess, 'check_output', return_value=links):
            profile.main()

    def test_configure_only_patches_private_sources_and_preserves_shared_inputs(self):
        self.invoke('--configure-only')
        for path, original in self.shared.items():
            self.assertEqual(path.read_bytes(), original)
        owned = self.product / 'build/apple-runtime/source'
        self.assertIn('#include <memory>', (owned / 'stable-diffusion/ggml/src/ggml-metal/ggml-metal.cpp').read_text())
        self.assertIn('#include <iterator>', (owned / 'mlx/mlx/distributed/jaccl/lib/jaccl/rdma.h').read_text())
        configurations = [call for call in self.calls if call[:2] == ('cmake', '-S')]
        self.assertEqual(len(configurations), 1)
        self.assertEqual(configurations[0][2], str(self.sdk))
        self.assertIn('-DiiFileProvider_DIR=/selected/provider', configurations[0])
        self.assertEqual((self.product / 'build/CMakeCache.txt').read_text().splitlines()[0],
                         'iiFileProvider_DIR:PATH=/selected/provider')

    def test_retired_pose_backend_is_never_configured_or_advertised(self):
        self.invoke()
        configure = next(call for call in self.calls if call[:2] == ('cmake', '-S'))
        self.assertNotIn('-DIILD_ENABLE_POSE=ON', configure)
        self.assertIn('-UIILD_ENABLE_POSE', configure)
        self.assertFalse(__import__('json').loads((self.product / 'build/apple-runtime/profile.json').read_text())['pose'])

    def test_link_audit_rejects_libtorch_before_reconfiguring_the_product(self):
        with self.assertRaisesRegex(RuntimeError, 'still links'):
            self.invoke(links='@rpath/libtorch_cpu.dylib')
        self.assertFalse((self.product / 'build/apple-runtime/profile.json').exists())
        self.assertFalse(any(call[:3] == ('cmake', '-S', str(self.product)) for call in self.calls))

    def test_link_audit_rejects_retired_model_runtime(self):
        with self.assertRaisesRegex(RuntimeError, 'retired ONNX'):
            self.invoke(links='@rpath/libiiOnnxRuntime.0.1.dylib')
        self.assertFalse((self.product / 'build/apple-runtime/profile.json').exists())
        self.assertFalse(any(call[:3] == ('cmake', '-S', str(self.product)) for call in self.calls))

    def test_successful_profile_is_verified_before_the_product_uses_it(self):
        self.invoke()
        audit = self.product / 'build/apple-runtime/profile.json'
        self.assertTrue(audit.is_file())
        configure = self.calls[-1]
        self.assertEqual(configure[:3], ('cmake', '-S', str(self.product)))
        self.assertIn('-DiiLocalDiffusion_DIR=' + str(self.product / 'build/runtime-sdk/iiLocalDiffusion-apple/lib/cmake/iiLocalDiffusion'), configure)
        self.assertTrue(any(call[0] == 'ctest' for call in self.calls))

    def test_native_worker_load_is_checked_before_accepting_the_profile(self):
        self.invoke()
        probes = [call for call in self.calls if call[:3] == ('cmake', '-E', 'env')]
        self.assertEqual(len(probes), 1)
        self.assertIn('--unset=DYLD_LIBRARY_PATH', probes[0])
        self.assertIn('--unset=DYLD_FALLBACK_LIBRARY_PATH', probes[0])
        self.assertIn('-I', probes[0])
        self.assertIn(str(self.product / 'build/runtime-sdk/iiLocalDiffusion-apple/lib/libiiLocalDiffusion.dylib'), probes[0])
        self.assertLess(self.calls.index(probes[0]), len(self.calls) - 1)

    def test_native_load_failure_does_not_accept_or_configure_the_profile(self):
        def record(*arguments):
            call = tuple(map(str, arguments))
            self.calls.append(call)
            if call[:3] == ('cmake', '-E', 'env'):
                raise subprocess.CalledProcessError(1, call)

        with mock.patch.object(profile, 'ROOT', self.product), \
             mock.patch.object(profile.sys, 'platform', 'darwin'), \
             mock.patch.object(profile.sys, 'argv', ['build-macos-runtime.py']), \
             mock.patch.object(profile, 'run', side_effect=record), \
             mock.patch.object(profile.subprocess, 'check_output', return_value='@rpath/libmlx.dylib'):
            with self.assertRaises(subprocess.CalledProcessError):
                profile.main()
        self.assertFalse((self.product / 'build/apple-runtime/profile.json').exists())
        self.assertFalse(any(call[:3] == ('cmake', '-S', str(self.product)) for call in self.calls))


if __name__ == '__main__':
    unittest.main()
