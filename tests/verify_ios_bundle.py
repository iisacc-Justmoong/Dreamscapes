#!/usr/bin/env python3
"""Verify a real signed iOS bundle before device installation."""
import argparse
from datetime import datetime, timezone
from fnmatch import fnmatchcase
import json
from pathlib import Path
import plistlib
import subprocess
from verify_generation_resources import verify_bundle


def output(*command):
    return subprocess.check_output(command, stderr=subprocess.DEVNULL)


def verify(bundle, device, allow_runtime_probe=False):
    generation_resources = verify_bundle(bundle)
    info = plistlib.loads((bundle / 'Info.plist').read_bytes())
    assert info['CFBundleSupportedPlatforms'] == ['iPhoneOS'], 'Expected a device bundle'
    assert info['CFBundleIdentifier'] == 'com.iisacc.dreamscapes'
    assert info.get('NSSupportsLiveActivities') is True
    assert info.get('NSPhotoLibraryAddUsageDescription'), 'Missing add-only Photos permission purpose'
    assert info.get('UIBackgroundModes') == ['processing'], 'Missing continued background processing mode'
    assert info.get('BGTaskSchedulerPermittedIdentifiers') == ['com.iisacc.dreamscapes.generation.*']
    assert info.get('NSLocalNetworkUsageDescription'), 'Society SDK requires local-network usage description'
    assert '_society-pair._udp' in info.get('NSBonjourServices', [])
    assert info.get('NSBluetoothAlwaysUsageDescription')
    assert 'society' in info.get('LSApplicationQueriesSchemes', []), 'View all must be able to open Society'
    assert set(info['UIDeviceFamily']) == {1, 2}
    group = info['SocietyAppGroup']
    assert group == 'group.com.iisacc.society'
    executable = bundle / info['CFBundleExecutable']
    assert executable.is_file()
    assert '/ActivityKit.framework/' in output('otool', '-L', str(executable)).decode()
    widget = bundle / 'PlugIns/DreamscapesLiveActivity.appex'
    widget_info = plistlib.loads((widget / 'Info.plist').read_bytes())
    assert widget_info['CFBundleIdentifier'] == 'com.iisacc.dreamscapes.liveactivity'
    assert widget_info['NSExtension']['NSExtensionPointIdentifier'] == 'com.apple.widgetkit-extension'
    widget_profile = plistlib.loads(output('security', 'cms', '-D', '-i', str(widget / 'embedded.mobileprovision')))
    assert widget_profile['ExpirationDate'].replace(tzinfo=timezone.utc) > datetime.now(timezone.utc)
    assert device in widget_profile['ProvisionedDevices']
    widget_rights = plistlib.loads(output('codesign', '-d', '--entitlements', ':-', str(widget)))
    assert widget_rights['application-identifier'].endswith('.' + widget_info['CFBundleIdentifier'])
    assert fnmatchcase(widget_rights['application-identifier'], widget_profile['Entitlements']['application-identifier'])
    assert '/Photos.framework/' in output('otool', '-L', str(executable)).decode(), 'Missing PhotoKit backend'
    assert '/BackgroundTasks.framework/' in output('otool', '-L', str(executable)).decode(), 'Missing BackgroundTasks backend'
    # Release LTO can keep Qt's registration and qrc constructors as local symbols.
    symbols = output('nm', str(executable)).decode()
    if not allow_runtime_probe:
        assert 'dreamscapesLocalRuntimeProbe' not in symbols, 'Disable DREAMSCAPES_LOCAL_RUNTIME_PROBE for the final app'
        assert 'nativeGenerationScreenStatus' not in symbols, 'Remove diagnostic UIKit state inspection from the final app'
        assert b'DREAMSCAPES_PROBE_EXTENT' not in executable.read_bytes(), 'Remove diagnostic generation overrides from the final app'
    assert 'RemoteGenerationClient' not in symbols, 'Remove the obsolete app-owned remote generation client'
    assert 'iiSocietyGeneration6Remote' in symbols, 'Missing Society SDK host-first generation client'
    assert 'iiSocietyGeneration4Host' not in symbols, 'The mobile bundle must not host peer inference jobs'
    assert 'startNative' in symbols, 'Missing in-process image generation path'
    assert 'generateNativeImageWithPreview' in symbols, 'Missing native live image preview entry point'
    assert 'qml_register_types_LVRS' in symbols, 'Missing LVRS QML registration'
    assert 'qt_static_plugin_QFFmpegMediaPlugin' not in symbols, 'Unpackaged FFmpeg media backend must not be imported'
    assert ('qInitResources_qmake_LVRS' in symbols
            or '__GLOBAL__sub_I_qrc_qmake_LVRS.cpp' in symbols), 'Missing LVRS QML resources'
    assert '@executable_path/Frameworks' in output('otool', '-l', str(executable)).decode(), \
        'The executable cannot resolve its embedded runtime libraries'
    subprocess.run(['codesign', '--verify', '--deep', '--strict', str(bundle)], check=True)
    rights = plistlib.loads(output('codesign', '-d', '--entitlements', ':-', str(bundle)))
    assert rights['com.apple.security.application-groups'] == [group]
    memory_entitlements = ('com.apple.developer.kernel.extended-virtual-addressing',
                           'com.apple.developer.kernel.increased-memory-limit',
                           'com.apple.developer.background-tasks.continued-processing.gpu')
    for key in memory_entitlements:
        assert rights.get(key) is True, f'Missing native inference entitlement: {key}'
    profile = plistlib.loads(output('security', 'cms', '-D', '-i', str(bundle / 'embedded.mobileprovision')))
    assert profile['ExpirationDate'].replace(tzinfo=timezone.utc) > datetime.now(timezone.utc)
    assert device in profile['ProvisionedDevices'], 'The signing profile does not include the device'
    assert group in profile['Entitlements']['com.apple.security.application-groups']
    for key in memory_entitlements:
        assert profile['Entitlements'].get(key) is True, f'Provisioning profile does not authorize: {key}'
    assert rights['application-identifier'] == profile['Entitlements']['application-identifier']
    libraries = sorted((bundle / 'Frameworks').glob('*.dylib'))
    expected = {'libiiCSMIDI', 'libiiFileProvider', 'libiiLicenseManager',
                'libiiLocalDiffusion', 'libiiPaintEngine', 'libiiUpdateManager'}
    assert 'iiSocietyClient' in symbols, 'Missing in-process Society storage client'
    for name in expected:
        assert any(lib.name.startswith(name + '.') for lib in libraries), f'Missing embedded {name}'
    diffusion = next(lib for lib in libraries if lib.name.startswith('libiiLocalDiffusion.'))
    assert 'generateNativeImageWithExecutionControl' in output('nm', '-gU', str(diffusion)).decode(), \
        'Embedded iiLocalDiffusion does not support pause/resume'
    assert 'generateNativeImageWithBackend' in output('nm', '-gU', str(diffusion)).decode(), \
        'Embedded iiLocalDiffusion does not support explicit CPU inference'
    assert b'vae=cpu\0' in diffusion.read_bytes(), \
        'Embedded iiLocalDiffusion is missing the iOS CPU VAE rendering policy'
    assert b'te=disk,diffusion=disk,vae=cpu\0' in diffusion.read_bytes(), \
        'Embedded iiLocalDiffusion must keep GPU weights evictable with explicit VAE placement'
    assert b'ios-cpu-vae-headroom\0' in diffusion.read_bytes(), \
        'Embedded iiLocalDiffusion must reserve shared memory for iOS CPU VAE and system work'
    assert b'adaptive-sdxl-v1' in diffusion.read_bytes(), \
        'Embedded iiLocalDiffusion must select SDXL VAE tiles from its memory budget'
    assert b'embedded-validated' in diffusion.read_bytes(), \
        'Embedded iiLocalDiffusion must validate the complete VAE tensor layout'
    assert b'VAE auto-mount-v2' in diffusion.read_bytes(), \
        'Embedded iiLocalDiffusion must select the VAE from the input model tensor contract'
    assert b'source=fallback-validated' in diffusion.read_bytes(), \
        'Embedded iiLocalDiffusion must validate the selected external VAE before mounting'
    assert b'VAE configuration changed while reading.' in diffusion.read_bytes(), \
        'Embedded iiLocalDiffusion must include VAE configuration in its validation and cache identity'
    assert b'VAE decode produced nonfinite pixels before normalization' in diffusion.read_bytes(), \
        'Embedded iiLocalDiffusion must reject nonfinite VAE output before RGB clamping'
    for binary in [executable, *libraries]:
        platform = output('xcrun', 'vtool', '-show-build', str(binary)).decode()
        assert 'platform IOS\n' in platform, f'Wrong platform: {binary}'
        assert 'arm64' in output('lipo', '-archs', str(binary)).decode()
        for line in output('otool', '-L', str(binary)).decode().splitlines()[1:]:
            dependency = line.strip().split(' (', 1)[0]
            if dependency.startswith(('/System/Library/', '/usr/lib/')):
                continue
            if dependency.startswith('@rpath/libswift'):
                continue  # Supplied by the deployment target's Swift runtime.
            if dependency.startswith('@rpath/'):
                resolved = bundle / 'Frameworks' / dependency[len('@rpath/'):]
            elif dependency.startswith('@loader_path/'):
                resolved = binary.parent / dependency[len('@loader_path/'):]
            elif dependency.startswith('@executable_path/'):
                resolved = bundle / dependency[len('@executable_path/'):]
            else:
                raise AssertionError(f'External development dependency: {dependency}')
            assert resolved.is_file(), f'Unresolved runtime library: {dependency}'
    return {'bundle': str(bundle), 'identifier': info['CFBundleIdentifier'],
            'appGroup': group, 'profile': profile['UUID'],
            'embeddedLibraries': [lib.name for lib in libraries],
            'nativeVaeBackend': 'cpu',
            'nativeWeightResidency': 'te=disk,diffusion=disk,vae=cpu',
            'nativeMemoryPolicy': 'ios-cpu-vae-headroom',
            'nativeVaePolicy': 'adaptive-sdxl-v1',
            'nativeVaeMountPolicy': 'auto-mount-v2',
            'generationResources': generation_resources}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('bundle', type=Path)
    parser.add_argument('--device', required=True)
    parser.add_argument('--allow-runtime-probe', action='store_true', help='Verify an opt-in device test build')
    args = parser.parse_args()
    print(json.dumps(verify(args.bundle.resolve(), args.device, args.allow_runtime_probe), indent=2))
