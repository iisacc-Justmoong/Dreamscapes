import copy
import unittest
from verify_ios_lifecycle import verify


def sample():
    rows = []
    for second, foreground, step, completed in [(0, True, 1, False), (1, False, 1, False),
                                               (45, True, 1, False), (46, True, 2, False), (50, True, 0, True)]:
        rows.append({'observedAt': f'2026-09-11T00:00:{second:02d}Z', 'foreground': foreground, 'step': step,
                     'nativeRuntime': {'applicationState': 0 if foreground else 2},
                     'backgroundExecution': {'paused': not foreground, 'waitingAtEngineBoundary': not foreground},
                     'ui': {'statusText': 'Denoising' if foreground else 'Paused — return to Dreamscapes to continue'},
                     'jobs': [{'id': 'same-request', 'startedAt': '2026-09-11T00:00:00Z',
                               'state': 'completed' if completed else 'running',
                               'finishedAt': '2026-09-11T00:00:50Z' if completed else '',
                               'image': 'Generation History/image.png' if completed else ''}]})
    return rows


class LifecycleEvidenceTests(unittest.TestCase):
    def test_accepts_real_pause_and_resume(self):
        self.assertEqual(verify(sample())['backgroundSeconds'], 44)

    def test_rejects_focus_only_and_missing_resume_and_restart(self):
        for kind in ('qt-only', 'no-resume', 'restart', 'too-short', 'wrong-ui'):
            with self.subTest(kind=kind):
                rows = copy.deepcopy(sample())
                if kind == 'qt-only': rows[1]['nativeRuntime']['applicationState'] = 1
                if kind == 'no-resume': rows[3]['step'] = 1
                if kind == 'restart': rows[3]['jobs'][0]['startedAt'] = '2026-09-11T00:00:45Z'
                if kind == 'too-short': rows[2]['observedAt'] = '2026-09-11T00:00:02Z'
                if kind == 'wrong-ui': rows[1]['ui']['statusText'] = 'Denoising 1 / 10'
                with self.assertRaises(AssertionError): verify(rows)

    def test_pause_is_not_background_inference(self):
        with self.assertRaises(AssertionError): verify(sample(), 'continued')


if __name__ == '__main__': unittest.main()
