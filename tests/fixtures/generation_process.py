#!/usr/bin/env python3
import json
import os
import signal
import subprocess
import sys
import time

mode = sys.argv[1]
if mode == '--worker':
    signal.signal(signal.SIGTERM, signal.SIG_IGN)
    print('IILD_READY ' + json.dumps({'schema': 'iild-worker-v1', 'capabilities': ['foreground-residency']}), flush=True)
    for line in sys.stdin:
        request = json.loads(line)
        if request.get('action', 'generate') == 'generate':
            arguments = request['arguments']
            model = arguments[arguments.index('--model-path') + 1]
            record = os.path.join(os.path.dirname(model), '.held-process.json')
            with open(record, 'w') as output:
                json.dump({'pid': os.getpid(), 'request': request, 'runtime': os.getcwd()}, output)
            while True:
                time.sleep(0.1)
        print('IILD_RESULT ' + json.dumps({'schema': 'iild-worker-result-v1', 'id': request['id'],
              'ok': True, 'pid': os.getpid(), 'residency': {'ready': True, 'state': 'ready'}}), flush=True)
elif mode == 'arguments':
    print(json.dumps({'arguments': sys.argv[2:], 'directory': os.getcwd(),
                      'value': os.environ.get('DREAMSCAPES_PROCESS_VALUE'),
                      'removed': os.environ.get('DREAMSCAPES_PROCESS_REMOVED')}))
elif mode == 'copy':
    while True:
        data = os.read(0, 16384)
        if not data:
            break
        os.write(1, data)
elif mode == 'lines':
    for line in sys.stdin.buffer:
        sys.stdout.buffer.write(line)
        sys.stdout.buffer.flush()
elif mode in ('flood', 'small-flood'):
    for _ in range(256 if mode == 'flood' else 8):
        os.write(1, b'x' * 16384)
elif mode == 'exit':
    os.write(1, 'last bytes 끝\n'.encode())
    sys.exit(int(sys.argv[2]))
elif mode in ('hold', 'tree', 'leaf'):
    signal.signal(signal.SIGTERM, signal.SIG_IGN)
    if mode == 'tree':
        child = subprocess.Popen([sys.executable, __file__, 'leaf', sys.argv[2]])
        with open(sys.argv[2], 'w') as record:
            json.dump({'parent': os.getpid(), 'child': child.pid}, record)
    print('ready', flush=True)
    while True:
        time.sleep(0.1)
