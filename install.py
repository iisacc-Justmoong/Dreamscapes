"""Verify and atomically install the canonical macOS app without closing user apps."""
import argparse
import ctypes
import datetime
try:
    import fcntl
except ImportError:
    fcntl = None
import hashlib
import json
import os
from pathlib import Path
import plistlib
import shutil
import stat
import subprocess
import sys
import uuid


def digest(path):
    result = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()


def manifest(root):
    result = {}
    for base, directories, files in os.walk(root, followlinks=False):
        for name in sorted(directories + files):
            path = Path(base) / name
            mode = stat.S_IMODE(path.lstat().st_mode)
            if path.is_symlink(): entry = {'symlink': os.readlink(path), 'mode': mode}
            elif path.is_file(): entry = {'size': path.stat().st_size, 'sha256': digest(path), 'mode': mode}
            else: entry = {'directory': True, 'mode': mode}
            result[path.relative_to(root).as_posix()] = entry
    return result


def bundle_info(root):
    data = plistlib.loads((root / 'Contents/Info.plist').read_bytes())
    executable = root / 'Contents/MacOS' / data['CFBundleExecutable']
    return {'version': data['CFBundleShortVersionString'], 'build': data['CFBundleVersion'], 'executable_sha256': digest(executable)}


def check_version(previous, candidate):
    if (previous['version'], previous['build']) != (candidate['version'], candidate['build']):
        raise RuntimeError('This installer preserves the existing version and build; explicit release packaging is required to change them')


def exchange(left, right):
    native = ctypes.CDLL('/usr/lib/libSystem.B.dylib', use_errno=True)
    native.renamex_np.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint]
    native.renamex_np.restype = ctypes.c_int
    if native.renamex_np(os.fsencode(left), os.fsencode(right), 2):
        raise OSError(ctypes.get_errno(), 'Atomic app exchange failed')


def active_pids():
    result = subprocess.run(['/usr/bin/pgrep', '-x', 'Dreamscapes'], capture_output=True, text=True)
    return sorted(int(value) for value in result.stdout.split())


def install(app, destination, evidence):
    if sys.platform != 'darwin': raise RuntimeError('This installer targets macOS')
    app, destination, evidence = app.resolve(), destination.resolve(), evidence.resolve()
    if app.name != 'Dreamscapes.app' or destination.name != 'Dreamscapes.app': raise ValueError('Expected Dreamscapes.app')
    evidence.mkdir(parents=True, exist_ok=True)
    stage = destination.parent / ('.Dreamscapes-install-' + uuid.uuid4().hex + '.app')
    report = {'canonical': str(app), 'installed': str(destination), 'stage': str(stage), 'preserved_pids': active_pids()}
    def run(*arguments):
        result = subprocess.run(arguments, capture_output=True, text=True, timeout=1800)
        with (evidence / 'install-commands.log').open('a') as log:
            log.write(' '.join(str(value) for value in arguments) + '\n' + result.stdout + result.stderr + '\n')
        if result.returncode: raise RuntimeError(str(arguments[0]) + ' failed: ' + result.stderr[-2000:])
    with (app.parent / ('.' + app.name + '.deploy.lock')).open('a') as lock:
        fcntl.flock(lock.fileno(), fcntl.LOCK_EX)
        run('/usr/bin/codesign', '--verify', '--deep', '--strict', str(app))
        report['candidate'] = bundle_info(app)
        before = manifest(destination) if destination.exists() else None
        if before is not None:
            report['before'] = bundle_info(destination)
            check_version(report['before'], report['candidate'])
            backup = evidence / ('Dreamscapes-before-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S') + '-' + uuid.uuid4().hex[:8] + '.zip')
            run('/usr/bin/ditto', '-c', '-k', '--sequesterRsrc', '--keepParent', str(destination), str(backup))
            report['backup'] = str(backup)
        expected = manifest(app)
        run('/usr/bin/ditto', str(app), str(stage))
        if manifest(stage) != expected: raise RuntimeError('Staged bundle differs from the canonical app')
        run('/usr/bin/codesign', '--verify', '--deep', '--strict', str(stage))
        report['staged_signature_verified'] = True
        if not set(report['preserved_pids']).issubset(active_pids()): raise RuntimeError('An existing app process changed; inspect user state')
        if before is not None:
            if manifest(destination) != before: raise RuntimeError('Another writer changed the installed app')
            exchange(stage, destination)
        else: os.rename(stage, destination)
        report['atomic_swap'] = True
        if manifest(destination) != expected: raise RuntimeError('Installed bundle differs from the verified stage')
        run('/usr/bin/codesign', '--verify', '--deep', '--strict', str(destination))
        report['after'] = bundle_info(destination)
        report['preserved_pids_after_swap'] = active_pids()
        if not set(report['preserved_pids']).issubset(report['preserved_pids_after_swap']): raise RuntimeError('An existing app process changed')
        report['installed_matches_canonical'] = True
        report['installed_signature_verified'] = True
        (evidence / 'canonical-manifest.json').write_text(json.dumps(expected, indent=2))
        (evidence / 'installation.json').write_text(json.dumps(report, indent=2))
        if stage.exists(): shutil.rmtree(stage)
    return report


if __name__ == '__main__':
    root = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', type=Path, default=root / 'build/bin/Dreamscapes.app')
    parser.add_argument('--destination', type=Path, default=Path('/Applications/Dreamscapes.app'))
    parser.add_argument('--evidence-directory', type=Path, default=root / 'build/install')
    args = parser.parse_args()
    print(json.dumps(install(args.app, args.destination, args.evidence_directory), indent=2))
