#!/usr/bin/env python3
"""Reject model payloads in a Dreamscapes application bundle."""
import argparse
import json
from pathlib import Path
import zipfile

MODEL_SUFFIXES = {'.safetensors', '.safetensor', '.gguf', '.ggml', '.ckpt', '.pt',
                  '.pth', '.onnx', '.tflite', '.mlmodel', '.mlmodelc', '.mlpackage', '.npz'}

def verify_asset_path(relative):
    relative = Path(relative)
    for path in (relative, *relative.parents):
        assert path.suffix.lower() not in MODEL_SUFFIXES, f'Model belongs in Society, not Dreamscapes: {relative}'
    assert not (relative.name.endswith('.bin') and relative.name.startswith(('pytorch_model', 'diffusion_pytorch_model'))), \
        f'Model belongs in Society, not Dreamscapes: {relative}'
    assert relative.name != 'generation-defaults.json', f'Generation resources belong in Society: {relative}'

def verify_bundle(bundle):
    bundle = Path(bundle)
    assert bundle.is_dir(), f'Missing application bundle: {bundle}'
    size = 0
    for path in bundle.rglob('*'):
        relative = path.relative_to(bundle)
        verify_asset_path(relative)
        if path.is_file():
            size += path.stat().st_size
    return {'bundleBytes': size, 'modelAssetCount': 0, 'modelStorage': 'Society'}

def verify_archive(apk):
    with zipfile.ZipFile(apk) as archive:
        for entry in archive.infolist():
            verify_asset_path(entry.filename)
    return {'bundleBytes': Path(apk).stat().st_size, 'modelAssetCount': 0, 'modelStorage': 'Society'}

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('bundle', type=Path)
    args = parser.parse_args()
    print(json.dumps(verify_bundle(args.bundle), indent=2))
