#!/usr/bin/env python3
"""Deterministic process-protocol fixture. This does not perform inference."""
import argparse
import json
import os
import pathlib
import struct
import sys
import time
import zlib

parser = argparse.ArgumentParser()
parser.add_argument('--model-path', required=True)
parser.add_argument('--prompt', required=True)
parser.add_argument('--width', type=int, required=True)
parser.add_argument('--height', type=int, required=True)
parser.add_argument('--steps', type=int, required=True)
parser.add_argument('--device', required=True)
parser.add_argument('--output-dir', type=pathlib.Path, required=True)
parser.add_argument('--work-dir', type=pathlib.Path)
parser.add_argument('--cache-dir', type=pathlib.Path)
parser.add_argument('--preview-dir', type=pathlib.Path)
parser.add_argument('--backend', choices=['local'])
parser.add_argument('--generation-resources', type=pathlib.Path, required=True)
parser.add_argument('--default-modifiers', action=argparse.BooleanOptionalAction, default=True)

def generate(arguments, request_count=1):
    args = parser.parse_args(arguments)
    if args.prompt == 'crash':
        os._exit(9)
    if args.prompt == 'long-error':
        sys.exit('오류' * 2000)
    if args.prompt == 'hold':
        time.sleep(20)
    if args.prompt.startswith('slow'):
        time.sleep(0.3)
    if args.prompt == 'fail':
        sys.exit('inference fixture rejected this model')
    if args.prompt == 'native-progress':
        for stage, step, total in [('loading', 200, 685), ('encoding', 0, 0),
                                   ('denoising', 1, args.steps), ('denoising', args.steps, args.steps),
                                   ('decoding', 1, 18)]:
            event = 'IILD_NATIVE_PROGRESS ' + json.dumps({'schema': 'iild-native-progress-v1',
                    'stage': stage, 'step': step, 'total': total}) + '\n'
            sys.stdout.write('progress\r' + event[:28])
            sys.stdout.flush()
            time.sleep(0.03)
            sys.stdout.write(event[28:])
            sys.stdout.flush()
            time.sleep(0.05)
        for stage, step, total in [('denoising', 2, 685), ('denoising', 0, args.steps),
                                   ('denoising', args.steps + 1, args.steps), ('unknown', 0, 0)]:
            print('IILD_NATIVE_PROGRESS ' + json.dumps({'schema': 'iild-native-progress-v1',
                  'stage': stage, 'step': step, 'total': total}), flush=True)

    record = {key: str(value) if isinstance(value, pathlib.Path) else value for key, value in vars(args).items()}
    record.update(worker_pid=os.getpid(), request_count=request_count,
                  python_cache_prefix=os.environ.get('PYTHONPYCACHEPREFIX'),
                  dont_write_bytecode=os.environ.get('PYTHONDONTWRITEBYTECODE'),
                  worker_temporary=os.environ.get('TMPDIR'),
                  resource_environment=os.environ.get('IILD_GENERATION_RESOURCES'),
                  hf_home=os.environ.get('HF_HOME'), hf_offline=os.environ.get('HF_HUB_OFFLINE'))
    (args.output_dir / 'generation.json').write_text(json.dumps(record))
    if args.prompt == 'empty':
        sys.exit(0)

    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))

    def png_image(width, height, color):
        header = struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)
        pixels = (b'\0' + color * width) * height
        return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', header) + chunk(b'IDAT', zlib.compress(pixels)) + chunk(b'IEND', b'')

    if args.prompt.startswith('live') and args.preview_dir:
        time.sleep(0.3)
        for step in range(1, 4):
            name = f'step-{step:06d}.png'
            temporary = args.preview_dir / (name + '.tmp')
            temporary.write_bytes(png_image(64, 64, bytes((step * 60, 114, 164))))
            temporary.replace(args.preview_dir / name)
            event = 'IILD_PREVIEW ' + json.dumps({'schema': 'iild-preview-v1', 'step': step, 'total_steps': 3, 'image': name}) + '\n'
            # Exercise partial QProcess reads and a carriage-return progress prefix.
            sys.stdout.write('progress\r' + event[:25])
            sys.stdout.flush()
            time.sleep(0.03)
            sys.stdout.write(event[25:])
            sys.stdout.flush()
            if args.prompt == 'live-invalid':
                print('IILD_PREVIEW {invalid json', flush=True)
                for image in ('../outside.png', '/tmp/outside.png', 'step-999999.png'):
                    print('IILD_PREVIEW ' + json.dumps({'schema': 'iild-preview-v1', 'step': 4,
                          'total_steps': 3, 'image': image}), flush=True)
                print(event, end='', flush=True)  # Duplicate/out-of-order events are ignored.
            if args.prompt == 'live-hold':
                time.sleep(20)
            if args.prompt == 'live-gui':
                deadline = time.monotonic() + 10
                while not (args.preview_dir / f'continue-{step}').exists() and time.monotonic() < deadline:
                    time.sleep(0.01)
            time.sleep(0.3)
        if args.prompt == 'live-fail':
            sys.exit('inference failed after preview')
        if args.prompt == 'live-empty':
            sys.exit(0)

    if args.prompt == 'wrong-size':
        args.width = 1
    png = png_image(args.width, args.height, b'\x41\x72\xa4')
    name = '0000-checkpoint_00001_.png' if args.prompt == 'checkpoint' else 'image-0001.png'
    (args.output_dir / name).write_bytes(png)

    if args.prompt in ('multiple', 'mixed-invalid'):
        width = 1 if args.prompt == 'mixed-invalid' else args.width
        (args.output_dir / 'image-0002.png').write_bytes(png_image(width, args.height, b'\x91\x72\xa4'))


if sys.argv[1:] == ['--worker']:
    print('IILD_READY ' + json.dumps({'schema': 'iild-worker-v1', 'pid': os.getpid(),
                                    'capabilities': ['foreground-residency']}), flush=True)
    request_count, resident, foreground = 0, None, False
    for line in sys.stdin:
        request = json.loads(line)
        code, error, loads = 0, '', 0
        try:
            action = request.get('action', 'generate')
            arguments = request.get('arguments', [])
            if action == 'foreground':
                foreground = request['foreground']
                if foreground and not arguments:
                    resident = None
            if arguments:
                def option(name):
                    return arguments[arguments.index(name) + 1]
                model = pathlib.Path(option('--model-path'))
                if model.read_bytes().startswith(b'prepare-metadata') and os.environ.get('IILD_MODEL_VALIDATION') != 'metadata':
                    raise SystemExit('worker did not receive metadata-only model validation')
                signature = (str(model), model.stat().st_size, model.stat().st_mtime_ns)
                loads = int(resident is None or resident[0] != signature)
                resident = (signature, option('--device'))
                if action == 'foreground':
                    if model.read_bytes().startswith(b'prepare-check'):
                        for completed in (4, 8):
                            print('IILD_MODEL_PROGRESS ' + json.dumps({'schema': 'iild-model-progress-v1',
                                'completed_bytes': completed, 'total_bytes': 8}), flush=True)
                            time.sleep(0.1)
                    if model.read_bytes().startswith(b'prepare-fail'):
                        raise SystemExit('fixture model preparation failed')
                    if model.read_bytes().startswith(b'prepare-hold'):
                        time.sleep(20)
                    time.sleep(0.2)
            if action == 'generate':
                request_count += 1
                generate(arguments, request_count)
        except SystemExit as failure:
            code = failure.code if isinstance(failure.code, int) else 1
            error = '' if code == 0 else str(failure)
        if code:
            resident = None
        residency = {'foreground': foreground, 'ready': resident is not None,
                     'state': ('ready' if foreground else 'cached') if resident else 'waiting-model'}
        if resident:
            residency.update(model=resident[0][0], device=resident[1], offload='none', gpu_resident=resident[1] == 'mps')
        print('IILD_RESULT ' + json.dumps({'schema': 'iild-worker-result-v1', 'id': request['id'],
              'ok': code == 0, 'exit_code': code, 'error': error, 'pid': os.getpid(), 'residency': residency,
              'action': request.get('action', 'generate'), 'cache': {'pipeline_loads': loads, 'device_placements': loads}}), flush=True)
else:
    generate(sys.argv[1:])
