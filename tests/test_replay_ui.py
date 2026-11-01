"""Test for the replay viewer: playback controls, events announced, the Replays list.

Records a short AI-against-AI game headlessly (test 30, "aiTest" with "record"), then plays it in
the game with wkbai's test hooks ("benchmark": {"replay": ..., "replayControls": [...]}): no
real mouse or keyboard input, an own userPath in a temp folder. Checks:

    the length the controls show is the recording's (the 'S' runs' ms * count)
    pause holds the played time, play goes on, the speed buttons set the speed
    a jump forward gets there (and not back)
    events are announced (REPLAY event lines)
    the Replays list renames a recording (name.txt; the folder stays)

Screenshots (game art: not for the repository) go to --shots if given.

Run: python tests/test_replay_ui.py [-v] [--shots DIR] [--keep DIR]   (WKBRE2_EXE overrides the
    path of wkbai.exe, WKB_GAME_DATA the game folder)
"""
import argparse
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
EXE = Path(os.environ.get('WKBRE2_EXE', REPO / 'build' / 'wkbre2' / 'RelWithDebInfo' / 'wkbai.exe'))
GAME = os.environ.get('WKB_GAME_DATA', r'C:\Dev\wkb-orig')
LEVEL = os.environ.get('WKB_LEVEL', '2 Player 4 Peaks.lvl')
MINUTES = float(os.environ.get('WKB_MINUTES', '16'))
NAME = 'Replay UI test'
SHOTS = None
KEEP = None

FRAME = re.compile(r'REPLAY frame (\d+): played \S+ of \S+ \((\d+) of (\d+) ms\), speed ([\d.]+) \(reached ([\d.]+)\), (paused|playing)')


def recording_length(events):
    """The 'S' runs' ms * count, as Server::startReplay sums them."""
    data = events.read_bytes()
    assert data[:7] == b'WKBREC1'
    clients = data[7]
    at, total = 8 + 4 * clients, 0
    while at < len(data):
        t = data[at:at + 1]
        at += 1
        if t == b'S':
            ms, _paused, count = struct.unpack_from('<IBI', data, at)
            at += 9
            total += ms * count
        elif t == b'M':
            _client, length = struct.unpack_from('<BH', data, at)
            at += 3 + length
        elif t == b'R':
            at += 10
        else:
            break
    return total


def write_config(folder, extra):
    config = {'gamePath': GAME, 'gameVersion': 2, 'userPath': str(folder / 'user'), 'musicEnabled': False,
              'soundEnabled': False}
    config.update(extra)
    (folder / 'wkconfig.json').write_text(json.dumps(config, indent=1))
    if not (folder / 'redata').exists():
        shutil.copytree(REPO / 'redata', folder / 'redata')  # found next to the working directory


def run(folder, extra, stdin=None, timeout=900):
    write_config(folder, extra)
    return subprocess.run([str(EXE)], cwd=folder, input=stdin, capture_output=True, text=True, errors='replace',
                          timeout=timeout).stdout


class ReplayUi(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = Path(KEEP) if KEEP else Path(tempfile.mkdtemp())
        cls.tmp.mkdir(parents=True, exist_ok=True)
        cls.rec = cls.tmp / 'user' / 'saved' / 'Recordings' / NAME
        if not (cls.rec / 'events.bin').exists():
            out = run(cls.tmp, {'test': True, 'aiTest': {'level': LEVEL, 'minutes': MINUTES, 'seed': 3, 'record': str(cls.rec),
                                           'settings': {'aggressiveAI': 1}}}, stdin='30\n', timeout=1800)
            if not (cls.rec / 'events.bin').exists():
                raise RuntimeError('no recording made:\n' + out[-3000:])
        cls.total = recording_length(cls.rec / 'events.bin')

    @classmethod
    def tearDownClass(cls):
        if not KEEP:
            shutil.rmtree(cls.tmp, ignore_errors=True)

    def replay(self, controls, frames, speed=8, shot=None, warmup=None):
        bm = {'replay': NAME, 'replaySpeed': speed, 'warmupFrames': warmup or frames - 5, 'frames': 5,
              'report': str(self.tmp / 'benchmark.json'), 'replayControls': controls}
        if shot and SHOTS:
            bm['screenshot'] = str(Path(SHOTS) / shot)
        out = run(self.tmp, {'benchmark': bm})
        lines = [(int(m[0]), int(m[1]), int(m[2]), float(m[3]), float(m[4]), m[5]) for m in FRAME.findall(out)]
        if not lines:
            self.fail('no REPLAY lines:\n' + out[-3000:])
        return out, lines

    def test_controls(self):
        # 8x; paused at frame 300 for 4 lines; on at 2x; a jump to minute 10
        out, lines = self.replay([[300, 'pause'], [540, 'play'], [541, 'speed', 2], [700, 'seek', 600]], 1500,
                                 shot='replay_controls.bmp')
        by_frame = {l[0]: l for l in lines}
        print(f'  recording: {self.total} ms; lines {len(lines)}; first {lines[0]}; last {lines[-1]}')
        self.assertTrue(all(l[2] == self.total for l in lines), 'the length shown is not the recording\'s')
        # playing at 8x before the pause
        self.assertGreater(by_frame[240][1], by_frame[60][1])
        self.assertAlmostEqual(by_frame[240][3], 8.0)
        # paused: the played time holds
        self.assertEqual(by_frame[360][5], 'paused')
        self.assertEqual(by_frame[360][1], by_frame[480][1])
        # on again at 2x
        self.assertEqual(by_frame[600][5], 'playing')
        self.assertAlmostEqual(by_frame[600][3], 2.0)
        self.assertGreater(by_frame[660][1], by_frame[600][1])
        # the jump to 10:00 got there
        after = [l for l in lines if l[0] >= 900]
        self.assertTrue(after and after[-1][1] >= min(600000, self.total), f'no jump: {after[-1] if after else None}')
        events = [re.sub(r'^REPLAY event at [\d:]+: ', '', l) for l in out.splitlines() if l.startswith('REPLAY event at ')]
        print('  events announced:', len(events))
        for e in events[:40]:
            print('   ', e)
        self.assertGreater(len(events), 3, 'hardly any events announced')
        # each event once: the clock put back by the time sync once started the view (and the
        # chronicle) over, and everything came again
        founded = [e for e in events if ' founded ' in e]
        self.assertEqual(len(founded), len(set(founded)), 'a settlement announced twice')

    def test_replays_list(self):
        # the list at its Replays tab, then renamed through its dialog
        if SHOTS:
            run(self.tmp, {'menuTest': {'open': ['load.replays'], 'frames': 40, 'screenshot': str(Path(SHOTS) / 'replays_list.bmp')}})
            run(self.tmp, {'menuTest': {'open': ['load.rename=Battle at the Peaks'], 'frames': 40,
                                        'screenshot': str(Path(SHOTS) / 'replays_rename.bmp')}})
        self.assertFalse((self.rec / 'name.txt').exists())
        run(self.tmp, {'menuTest': {'open': ['load.renamed=Battle at the Peaks'], 'frames': 40,
                                    'screenshot': str(self.tmp / 'renamed.bmp')}})
        self.assertEqual((self.rec / 'name.txt').read_text(encoding='utf-8'), 'Battle at the Peaks')
        self.assertTrue((self.rec / 'events.bin').exists(), 'the folder moved')
        if SHOTS:
            run(self.tmp, {'menuTest': {'open': ['load.replays'], 'frames': 40, 'screenshot': str(Path(SHOTS) / 'replays_renamed.bmp')}})
        (self.rec / 'name.txt').unlink()


if __name__ == '__main__':
    ap = argparse.ArgumentParser(add_help=False)
    ap.add_argument('--shots')
    ap.add_argument('--keep')
    args, rest = ap.parse_known_args()
    SHOTS, KEEP = args.shots, args.keep
    if SHOTS:
        Path(SHOTS).mkdir(parents=True, exist_ok=True)
    unittest.main(argv=[sys.argv[0]] + rest)
