"""Team colours of HD models (wkbai/test.cpp Test_TeamColours, docs/engine/gltf.md in the RE repo):
runs the headless test and checks its "TEAM <check> <0/1>" lines. No game data needed.

Run: python tests/test_team_colours.py [-v]      (WKBRE2_EXE overrides the path of wkbai.exe)
"""
import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
EXE = Path(os.environ.get('WKBRE2_EXE', REPO / 'build' / 'wkbai' / 'RelWithDebInfo' / 'wkbai.exe'))
TEST_NAME = 'Team colours of HD models'


def test_number():
    """Index of the test in testList (wkbai/test.cpp), read from the source (LaunchTest() runs test 0
    on unexpected input)."""
    lines = (REPO / 'wkbai' / 'test.cpp').read_text(encoding='latin1').splitlines()
    start = next(i for i, l in enumerate(lines) if 'testList = {' in l)
    # (every entry, a lambda's too: the crash test's)
    end = next(i for i in range(start + 1, len(lines)) if lines[i].startswith('};'))
    entries = [l for l in lines[start + 1:end] if l.strip().startswith('{')]
    return next(i for i, l in enumerate(entries) if TEST_NAME in l)


class TeamColoursTest(unittest.TestCase):
    def test_team_colours(self):
        if not EXE.exists():
            raise unittest.SkipTest(f'{EXE} not found; build wkbai first')
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp) / 'work'
            game = Path(tmp) / 'game'
            work.mkdir()
            game.mkdir()
            config = {'gamePath': str(game), 'userPath': str(Path(tmp) / 'user'), 'gameVersion': 2, 'test': True}
            (work / 'wkconfig.json').write_text(json.dumps(config), encoding='utf-8')
            out = subprocess.run([str(EXE)], cwd=work, input=f'{test_number()}\n', capture_output=True,
                                 text=True, timeout=120).stdout
        results = {}
        for line in out.splitlines():
            if line.startswith('TEAM '):
                what, _, ok = line[5:].rpartition(' ')
                results[what] = ok == '1'
        self.assertTrue(results, out[-2000:])
        for what, ok in results.items():
            with self.subTest(check=what):
                self.assertTrue(ok, what)
        self.assertTrue(results.get('all'), out[-2000:])


if __name__ == '__main__':
    unittest.main()
