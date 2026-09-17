#!/usr/bin/env python3
"""Copy a verified SDK resource catalog into an existing local Society drive."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import tempfile

MANIFEST = 'generation-defaults.json'
RESOURCE_PATH = 'Models/.generation-resources/iiLocalDiffusion'


def safe_path(root, relative):
    relative = PurePosixPath(relative)
    if relative.is_absolute() or not relative.parts or '..' in relative.parts:
        raise ValueError(f'Invalid resource path: {relative}')
    current = root
    for part in relative.parts:
        if '\\' in part or ':' in part:
            raise ValueError(f'Invalid resource path: {relative}')
        current = current / part
        if current.is_symlink():
            raise ValueError(f'Redirected resource path: {current}')
    return current


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def verify(path, entry):
    if not path.is_file() or path.stat().st_size != entry['size'] or digest(path) != entry['sha256']:
        raise ValueError(f'Missing or changed resource: {path}')


def catalog(source):
    manifest = safe_path(source, MANIFEST)
    if not manifest.is_file() or manifest.stat().st_size > 1024 * 1024:
        raise ValueError('Missing or oversized generation resource manifest')
    payload = manifest.read_bytes()
    data = json.loads(payload)
    if type(data.get('version')) is not int or data['version'] != 1:
        raise ValueError('Unsupported generation resource manifest')
    entries = {}

    def visit(value):
        if isinstance(value, dict):
            if 'file' in value:
                name, size, sha = value.get('file'), value.get('size'), value.get('sha256')
                if not isinstance(name, str) or type(size) is not int or size < 0 \
                        or not isinstance(sha, str) or not re.fullmatch('[0-9a-f]{64}', sha):
                    raise ValueError('Invalid generation resource identity')
                safe_path(source, name)
                entry = {'size': size, 'sha256': sha}
                if name == MANIFEST or (name in entries and entries[name] != entry):
                    raise ValueError(f'Conflicting resource identity: {name}')
                entries[name] = entry
            for item in value.values():
                visit(item)
        elif isinstance(value, list):
            for item in value:
                visit(item)

    visit(data)
    if not entries:
        raise ValueError('The resource manifest contains no files')
    # Preserve accompanying license notices when the SDK supplies them.
    for path in source.rglob('*'):
        if path.is_file() and path.name.upper().startswith(('LICENSE', 'COPYING', 'NOTICE')):
            name = path.relative_to(source).as_posix()
            safe_path(source, name)
            entries[name] = {'size': path.stat().st_size, 'sha256': digest(path)}
    entries[MANIFEST] = {'size': len(payload), 'sha256': hashlib.sha256(payload).hexdigest()}
    return entries


def provision(source, society, check=False):
    source, society = Path(source).absolute(), Path(society).absolute()
    if source.resolve() != source or society.resolve() != society:
        raise ValueError('Use canonical source and Society paths without redirects')
    marker = safe_path(society, '.society-drive.json')
    if not marker.is_file() or json.loads(marker.read_text()).get('schemaVersion') != 1:
        raise ValueError('Choose an existing Society drive')
    models = safe_path(society, 'Models')
    if not models.is_dir():
        raise ValueError('The Society drive has no Models directory')
    target = safe_path(society, RESOURCE_PATH)
    entries = catalog(source)
    missing = []
    # Validate every source and existing destination before publishing anything.
    for name, entry in entries.items():
        verify(safe_path(source, name), entry)
        destination = safe_path(target, name)
        if destination.exists():
            verify(destination, entry)
        elif check:
            raise ValueError(f'Missing Society resource: {name}')
        else:
            missing.append(name)
    if missing:
        runtime = safe_path(society, 'Models/.society-runtime/iiLocalDiffusion')
        runtime.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='resource-install-', dir=runtime) as temporary:
            staging = Path(temporary)
            # Copy and verify all payloads before enabling the manifest. Staging
            # stays in Society's excluded runtime area, on the same filesystem.
            for name in missing:
                staged = safe_path(staging, name)
                staged.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(safe_path(source, name), staged)
                verify(staged, entries[name])
            for name in sorted(missing, key=lambda item: item == MANIFEST):
                destination = safe_path(safe_path(society, RESOURCE_PATH), name)
                destination.parent.mkdir(parents=True, exist_ok=True)
                try:
                    os.link(staging / name, destination)  # Atomic, never replaces existing bytes.
                except FileExistsError:
                    verify(destination, entries[name])
    for name, entry in entries.items():
        verify(safe_path(target, name), entry)
    return {'resourceDirectory': str(target), 'installedFiles': len(missing),
            'verifiedFiles': len(entries), 'verifiedBytes': sum(item['size'] for item in entries.values())}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True, help='Installed SDK generation resources')
    parser.add_argument('--society', type=Path, required=True, help='Existing local Society drive')
    parser.add_argument('--check', action='store_true', help='Verify without changing files')
    args = parser.parse_args()
    print(json.dumps(provision(args.source, args.society, args.check), indent=2))
