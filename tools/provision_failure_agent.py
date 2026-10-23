"""Register an existing verified local chat package in the drive's runtime cache.

Uses hard links on the same volume. Does not download or duplicate weight bytes.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import tempfile


def provision(package: Path, container: Path) -> dict:
    package, container = package.resolve(), container.resolve()
    if not container.is_dir() or not container.name.endswith('.societycontainer'):
        raise ValueError('An existing .societycontainer directory is required')
    manifest = json.loads((package / 'manifest.json').read_text())
    identity = manifest['id']
    if not identity or Path(identity).name != identity or identity in ('.', '..'):
        raise ValueError('Invalid model identity')
    if 'chat' not in manifest.get('capabilities', []) or manifest.get('format') != 'gguf':
        raise ValueError('A GGUF chat model package is required')
    files = manifest.get('files', [])
    if not files or manifest['entry_point'] not in [f['path'] for f in files]:
        raise ValueError('The entry point must be declared in verified package files')
    verified = []
    for item in files:
        relative = Path(item['path'])
        source = (package / relative).resolve()
        if relative.is_absolute() or '..' in relative.parts or not source.is_relative_to(package) or not source.is_file():
            raise ValueError('Invalid package asset path')
        digest = hashlib.sha256()
        with source.open('rb') as stream:
            for block in iter(lambda: stream.read(8 * 1024 * 1024), b''):
                digest.update(block)
        if source.stat().st_size != item['size'] or digest.hexdigest() != item['sha256']:
            raise ValueError('Model file integrity check failed: ' + str(relative))
        verified.append((relative, source))
    catalog = container / 'Models/.society-runtime/iiLocalLLM/Models'
    catalog.mkdir(parents=True, exist_ok=True)
    destination = catalog / identity
    if destination.exists():
        existing = json.loads((destination / 'manifest.json').read_text())
        if existing != manifest:
            raise ValueError('A different package already owns this model identity')
        for relative, source in verified:
            target = destination / relative
            if not target.is_file() or not os.path.samefile(source, target):
                raise ValueError('Existing model assets are not the verified hard links')
        return {'model': 'model://' + identity, 'catalog': str(catalog), 'reused': True, 'additional_weight_bytes': 0}
    stage = Path(tempfile.mkdtemp(prefix='.failure-agent-', dir=catalog))
    try:
        for relative, source in verified:
            target = stage / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            os.link(source, target)
        (stage / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
        os.rename(stage, destination)
    finally:
        if stage.exists():
            shutil.rmtree(stage)
    return {'model': 'model://' + identity, 'catalog': str(catalog), 'reused': False, 'additional_weight_bytes': 0}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', type=Path, required=True)
    parser.add_argument('--container', type=Path, required=True)
    arguments = parser.parse_args()
    print(json.dumps(provision(arguments.package, arguments.container), indent=2))
