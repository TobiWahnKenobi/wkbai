"""Test for Space jumping the camera to the selection (ClientInterface.cpp, "spaceTest").

Loads a save game from the test game data, then wkbai's own test hook ("benchmark":
{"spaceTest": [...]}) selects units and presses Space: no real mouse or keyboard input. Checks,
with the fixed camera on and off:

    one unit: every press centres the view on it
    several units: each press goes on to the next one, round again after the last; a new
        selection starts over at the first
    a formation: one stop, centred on its members, for all of them

Run: python tests/test_space_jump.py [-v]   (WKBRE2_EXE overrides the path of wkbai.exe,
    WKB_GAME_DATA the game folder, WKB_SAVE the save game in its Save_Games)
"""
import json
import os
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
EXE = Path(os.environ.get('WKBRE2_EXE', REPO / 'build' / 'wkbai' / 'RelWithDebInfo' / 'wkbai.exe'))
GAME = os.environ.get('WKB_GAME_DATA', r'C:\Dev\wkb-orig')
SAVE = os.environ.get('WKB_SAVE', 'harness30.sav')

STEPS = [
    {'select': 'Archer', 'count': 1, 'presses': 2},
    {'select': 'Archer', 'count': 3, 'presses': 4},
    {'select': 'Spearman', 'count': 4, 'formation': True, 'presses': 2},
]

PRESS = re.compile(r'SPACE press: stop (\d+) \((.*?)\) of (\d+), target \(([-\d.]+),([-\d.]+),([-\d.]+)\), '
                   r'camera \(([-\d.]+),([-\d.]+),([-\d.]+)\) pitch ([-\d.]+) yaw ([-\d.]+), view centre \(([-\d.]+),([-\d.]+)\)')


def run(fixed_camera):
    with tempfile.TemporaryDirectory() as tmp:
        config = {
            'gamePath': GAME,
            'gameVersion': 2,
            'userPath': str(Path(tmp) / 'user'),
            'fixedCamera': fixed_camera,
            'musicEnabled': False,
            'benchmark': {'saveGame': SAVE, 'warmupFrames': 700, 'frames': 60, 'paused': True,
                          'report': str(Path(tmp) / 'benchmark.json'), 'spaceTest': STEPS},
        }
        (Path(tmp) / 'wkconfig.json').write_text(json.dumps(config, indent=1))
        shutil.copytree(REPO / 'redata', Path(tmp) / 'redata')  # found next to the working directory
        out = subprocess.run([str(EXE)], cwd=tmp, capture_output=True, text=True, errors='replace', timeout=300).stdout
    steps, current = [], None
    for line in out.splitlines():
        if line.startswith('SPACE step'):
            current = []
            steps.append(current)
        m = PRESS.match(line)
        if m and current is not None:
            g = m.groups()
            current.append({'stop': int(g[0]), 'name': g[1], 'stops': int(g[2]),
                            'target': tuple(map(float, g[3:6])), 'camera': tuple(map(float, g[6:9])),
                            'centre': (float(g[11]), float(g[12]))})
    return out, steps


class SpaceJump(unittest.TestCase):
    def check(self, fixed_camera):
        out, steps = run(fixed_camera)
        if 'SPACE test done' not in out:
            self.fail('the test hook did not finish:\n' + out[-3000:])
        for step in steps:
            for press in step:
                print(f"  fixed={fixed_camera} stop {press['stop']} {press['name']} of {press['stops']}: "
                      f"camera {press['camera']}, view centre {press['centre']}, target {press['target'][0::2]}")
                # the view centred on the stop (to a decimetre or so: the printout's rounding)
                self.assertAlmostEqual(press['centre'][0], press['target'][0], delta=0.3)
                self.assertAlmostEqual(press['centre'][1], press['target'][2], delta=0.3)
        single, several, formation = steps
        self.assertEqual([p['stops'] for p in single], [1, 1])
        self.assertEqual(single[0]['stop'], single[1]['stop'])
        # several: round the three in the order of their ids, then the first again
        ids = [p['stop'] for p in several]
        self.assertEqual([p['stops'] for p in several], [3] * 4)
        self.assertEqual(len(set(ids[:3])), 3)
        self.assertEqual(ids[:3], sorted(ids[:3]))
        self.assertEqual(ids[3], ids[0])
        # the new selection started over at its first, not after the single unit's id
        self.assertEqual(ids[0], min(ids))
        # the camera kept its height and angle between the jumps (free camera; the fixed one
        # follows the ground)
        if not fixed_camera:
            self.assertEqual(len({p['camera'][1] for p in single + several}), 1, 'height changed')
        self.assertEqual([p['stops'] for p in formation], [1, 1])
        self.assertTrue(all(p['name'] != 'Spearman' for p in formation), 'a formation member as a stop')
        self.assertEqual(formation[0]['stop'], formation[1]['stop'])

    def test_free_camera(self):
        self.check(False)

    def test_fixed_camera(self):
        self.check(True)


if __name__ == '__main__':
    unittest.main()
