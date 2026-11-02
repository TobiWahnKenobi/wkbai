"""Test of the control groups (ClientInterface::controlGroupKeys): Ctrl+digit stores the
selection, the digit selects it again, twice quickly centres the camera, lost units drop out.

Runs a benchmark game (wkconfig.json "benchmark" with "select" and "keys", which simulates the
key presses, no real keyboard or mouse) in a temporary folder with its own "userPath", and checks
the CONTROLGROUP lines the game prints:

    frame 200  Ctrl+1        the selected peasant stored as group 1
    frame 210  Deselect      the selection emptied
    frame 220  2             group 2 is empty: nothing happens
    frame 230  1, 232 1      group 1 selected again, the second press centres the camera on it
    frame 300  Delete        the peasant terminated
    frame 400  1             group 1 is now empty: nothing selected

Run: python tests/test_control_groups.py [-v]   (WKBRE2_EXE overrides the path of wkbai.exe,
WKB_GAME the game folder, by default C:\\Dev\\wkb-orig with saved\\Save_Games\\s00.sav)
"""
import json
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
EXE = Path(os.environ.get('WKBRE2_EXE', REPO / 'build' / 'wkbai' / 'RelWithDebInfo' / 'wkbai.exe'))
GAME = Path(os.environ.get('WKB_GAME', r'C:\Dev\wkb-orig'))
SAVE = 's00.sav'


@unittest.skipUnless(EXE.exists() and (GAME / 'saved' / 'Save_Games' / SAVE).exists(), 'needs wkbai.exe and the game data')
class ControlGroupsTest(unittest.TestCase):
    def test_store_recall_centre_drop(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            shutil.copytree(REPO / 'redata', tmp / 'redata')
            config = {
                'gamePath': str(GAME),
                'gameVersion': 2,
                'userPath': str(tmp / 'user'),
                'benchmark': {
                    'saveGame': SAVE,
                    'select': 'Peasant',
                    'warmupFrames': 100,
                    'frames': 400,
                    'report': str(tmp / 'benchmark.json'),
                    'keys': [[200, 'Ctrl+1'], [210, 'Deselect'], [220, '2'], [230, '1'], [232, '1'],
                             [300, 'Delete'], [400, '1']],
                },
            }
            (tmp / 'wkconfig.json').write_text(json.dumps(config, indent=1))
            run = subprocess.run([str(EXE)], cwd=tmp, capture_output=True, text=True, errors='replace', timeout=300)
            lines = [l.strip() for l in run.stdout.splitlines() if l.startswith('CONTROLGROUP')]
            print('\n'.join(lines))
            # the run went past frame 400 (the report is written at its end)
            self.assertTrue((tmp / 'benchmark.json').exists(), run.stdout[-2000:])
            self.assertEqual(lines[0], 'CONTROLGROUP store 1: 1')
            self.assertEqual(lines[1], 'CONTROLGROUP recall 1: 1')
            self.assertEqual(lines[2], 'CONTROLGROUP recall 1: 1')
            self.assertTrue(lines[3].startswith('CONTROLGROUP centre 1: group at'), lines[3])
            # no recall from the empty group 2, nor from group 1 once its peasant is gone
            self.assertEqual(len(lines), 4, lines)


if __name__ == '__main__':
    unittest.main()
