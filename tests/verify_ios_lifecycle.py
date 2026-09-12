#!/usr/bin/env python3
"""Validate a real iOS probe timeline; Qt visibility alone is not OS evidence."""
import argparse
from datetime import datetime
import json
from pathlib import Path


def verify(rows, mode='paused', minimum_seconds=30):
    rows = [row for row in rows if row.get('jobs')]
    assert rows, 'No generation observations'
    identities = {(row['jobs'][0]['id'], row['jobs'][0]['startedAt']) for row in rows
                  if row['jobs'][0].get('startedAt')}
    assert len(identities) == 1, 'The request was restarted or replaced'
    assert all(row['jobs'][0]['state'] not in ('failed', 'interrupted', 'cancelled') for row in rows)
    completed = [row for row in rows if row['jobs'][0]['state'] == 'completed']
    assert completed and completed[-1]['jobs'][0].get('image'), 'No saved completed image'
    background = [i for i, row in enumerate(rows)
                  if row.get('nativeRuntime', {}).get('applicationState') == 2 and not row['foreground']]
    assert background, 'No actual UIKit background transition'
    first = background[0]
    before = rows[:first]
    assert before and max(row.get('step', 0) for row in before) > 0, 'Backgrounded before denoising started'
    end = next((i for i in range(first + 1, len(rows)) if rows[i]['foreground']), len(rows) - 1)
    timestamp = lambda row: datetime.fromisoformat(row['observedAt'].replace('Z', '+00:00'))
    seconds = (timestamp(rows[end]) - timestamp(rows[first])).total_seconds()
    assert seconds >= minimum_seconds, 'The background interval was too short'
    interval = rows[first:end + 1]
    if mode == 'paused':
        parked = [row for row in interval if row.get('backgroundExecution', {}).get('waitingAtEngineBoundary')
                  and row.get('backgroundExecution', {}).get('paused') and not row['foreground']]
        assert parked, 'No acknowledged engine pause'
        assert any(row.get('ui', {}).get('statusText', '').startswith('Paused') for row in parked), \
            'The UI did not report the paused request'
        assert any(row['foreground'] and row.get('step', 0) > max(p.get('step', 0) for p in parked)
                   for row in rows[end:]), 'No further denoising after foreground resumption'
    else:
        granted = [row for row in interval if not row['foreground']
                   and row.get('backgroundExecution', {}).get('allowsBackgroundExecution')]
        assert granted, 'No OS background execution grant'
        assert max(row.get('step', 0) for row in granted) > max(row.get('step', 0) for row in before), \
            'No background denoising progress'
    job = completed[-1]['jobs'][0]
    return {'mode': mode, 'job': job['id'], 'backgroundSeconds': seconds,
            'startedAt': job['startedAt'], 'finishedAt': job['finishedAt'], 'image': job['image'],
            'width': job.get('width'), 'height': job.get('height')}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('timeline', type=Path)
    parser.add_argument('--mode', choices=('paused', 'continued'), default='paused')
    parser.add_argument('--minimum-seconds', type=float, default=30)
    args = parser.parse_args()
    print(json.dumps(verify([json.loads(line) for line in args.timeline.read_text().splitlines()],
                            args.mode, args.minimum_seconds), indent=2))
