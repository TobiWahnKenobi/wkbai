"""Tests for matchmaking (wkbai/matchmaking): two wkbai instances on this machine, one hosting,
one finding the game in the list and joining it. Loopback only ("hostBind": "127.0.0.1", the
discovery sent to 127.0.0.1), so no firewall prompt; each instance has its own user folder; the
null renderer, so no window waits on the screen (the screenshot test opens one for a moment).

    local      the local network's discovery: the guest finds the host's game (name, map,
               players, mods) and joins it; the host starts, the guest plays its position
    nakama     the same through a Nakama server (tests/nakama_mock.py, a stand-in that speaks
               the parts of Nakama's API wkbai uses): listed there, joined directly by ENet
    relay      through Nakama with "relay": "always": the game's packets go through the server
    plugin     the Epic Online Services plugin (stub without the SDK) is loaded and listed

Run: python tests/test_matchmaking.py [-v]   (WKB_NAKAMA=http://127.0.0.1:7350 uses a real Nakama
    server instead of the mock)
       (WKBRE2_EXE overrides wkbai.exe, WKB_GAME_DATA the
    game folder; WKB_SCREENSHOT=<path.bmp> also saves the multiplayer page's game list)
"""
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
EXE = Path(os.environ.get('WKBRE2_EXE', REPO / 'build' / 'wkbai' / 'RelWithDebInfo' / 'wkbai.exe'))
GAME = os.environ.get('WKB_GAME_DATA', r'C:\Dev\wkb-orig')
LEVEL = '2 Player 4 Peaks.lvl'


def free_port():
    with socket.socket() as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]


class Instance:
    def __init__(self, root, name, config, renderer='null'):
        self.dir = Path(root) / name
        self.dir.mkdir()
        base = {
            'gamePath': GAME, 'gameVersion': 2, 'userPath': str(self.dir / 'user'), 'musicEnabled': False,
            'renderer': renderer, 'hostBind': '127.0.0.1', 'playerName': name, 'windowWidth': 1280, 'windowHeight': 720,
        }
        base.update(config)
        (self.dir / 'wkconfig.json').write_text(json.dumps(base, indent=1))
        shutil.copytree(REPO / 'redata', self.dir / 'redata')
        self.log = open(self.dir / 'log.txt', 'w+', encoding='utf-8', errors='replace')
        self.proc = subprocess.Popen([str(EXE)], cwd=self.dir, stdout=self.log, stderr=subprocess.STDOUT)

    def text(self):
        self.log.flush()
        return (self.dir / 'log.txt').read_text(encoding='utf-8', errors='replace')

    def stop(self):
        if self.proc.poll() is None:
            self.proc.kill()
            self.proc.wait()
        self.log.close()


def wait_for(check, timeout):
    end = time.time() + timeout
    while time.time() < end:
        if check():
            return True
        time.sleep(0.5)
    return False


class Matchmaking(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix='wkbai-mm-')
        self.procs = []
        self.mock = None

    def tearDown(self):
        for p in self.procs:
            p.stop()
        if self.mock:
            self.mock.kill()
            self.mock.wait()
            self.mock_log.close()
        if os.environ.get('WKB_KEEP_LOGS'):
            print('logs kept in', self.tmp)
        else:
            shutil.rmtree(self.tmp, ignore_errors=True)

    def start(self, name, config, renderer='null'):
        p = Instance(self.tmp, name, config, renderer)
        self.procs.append(p)
        return p

    def nakama_server(self):
        """WKB_NAKAMA=http://127.0.0.1:7350: a real Nakama server, else the mock"""
        real = os.environ.get('WKB_NAKAMA')
        return real if real else 'http://127.0.0.1:%d' % self.start_mock()

    def start_mock(self):
        port = free_port()
        self.mock_log = open(Path(self.tmp) / 'nakama.txt', 'w+', encoding='utf-8')
        self.mock = subprocess.Popen([sys.executable, '-I', str(REPO / 'tests' / 'nakama_mock.py'), str(port)],
                                     stdout=self.mock_log, stderr=subprocess.STDOUT)
        time.sleep(1)
        return port

    def mock_text(self):
        if not self.mock:
            return None
        self.mock_log.flush()
        return (Path(self.tmp) / 'nakama.txt').read_text(encoding='utf-8', errors='replace')

    def play(self, matchmaking, find, offer=(), expect_link='ENet 127.0.0.1:1234'):
        host = self.start('Host', {'matchmaking': matchmaking, 'lobbyTest': {
            'host': LEVEL, 'guestSlot': 2, 'startAfter': 3000, 'offer': list(offer)}})
        time.sleep(2)
        guest = self.start('Guest', {'matchmaking': matchmaking, 'lobbyTest': {
            'find': find, 'chat': 'hello through ' + find, 'gameChat': 'in game through ' + find}})
        ok = wait_for(lambda: 'Server got message: Guest: in game through ' + find in host.text(), 120)
        h, g = host.text(), guest.text()
        if not ok:
            self.fail('the game was not played\n--- host\n' + h[-3000:] + '\n--- guest\n' + g[-3000:])
        listing = [l for l in g.splitlines() if l.startswith('MATCHMAKING listing ' + find + ':')]
        print('\n  ' + '\n  '.join(listing))
        self.assertTrue(listing, 'the guest listed no game of ' + find)
        self.assertIn('"Host\'s game" host "Host" map "4 Peaks (2)" players 1/2', listing[0])
        self.assertIn('password 0 running 0 version wkbai-net-1 mods match', listing[0])
        self.assertIn('Lobby test: joining Host\'s game listed by ' + find, g)
        self.assertIn('Connected (' + expect_link, g)
        self.assertIn('Lobby: Guest joined', h)
        self.assertIn('Server got message: Guest: hello through ' + find, h)
        self.assertIn('Client: controlling player', g)
        return h, g

    def test_local(self):
        self.play({'local': {'targets': ['127.0.0.1:1234']}, 'plugins': False}, 'local')

    def test_nakama(self):
        # the local network finds nothing (no targets): the game comes through Nakama only
        mm = {'local': {'targets': []}, 'nakama': {'server': self.nakama_server()}, 'plugins': False}
        h, g = self.play(mm, 'nakama', offer=['nakama'], expect_link='Nakama, then ENet 127.0.0.1:1234')
        self.assertIn('Matchmaking: the game is offered on Nakama', h)
        n = self.mock_text()
        if n is None:
            return
        self.assertIn('storage write wkbai_games game', n)
        self.assertIn('match op 3', n.replace('match data op', 'match op'))  # the guest's addresses for the punch

    def test_relay(self):
        mm = {'local': {'targets': []}, 'nakama': {'server': self.nakama_server(), 'relay': 'always'}, 'plugins': False}
        h, g = self.play(mm, 'nakama', offer=['nakama'], expect_link='Nakama relay')
        self.assertIn('Matchmaking: a guest joins through the Nakama relay', h)
        if self.mock:
            self.assertIn('match data relayed', self.mock_text())

    def test_plugin(self):
        plugins = EXE.parent / 'plugins'
        if not (plugins / 'wkbai_eos.dll').exists():
            self.skipTest('no plugins/wkbai_eos.dll beside the exe')
        p = self.start('Lister', {'matchmaking': {'local': {'targets': []}}, 'lobbyTest': {'find': 'eos', 'findOnly': True}})
        ok = wait_for(lambda: 'Matchmaking: plugin Epic Online Services' in p.text(), 30)
        self.assertTrue(ok, p.text()[-2000:])

    @unittest.skipUnless(os.environ.get('WKB_SCREENSHOT'), 'WKB_SCREENSHOT not set')
    def test_screenshot(self):
        server = self.nakama_server()
        targets = {'targets': ['127.0.0.1:1234', '127.0.0.1:1240']}
        nakama = {'server': server}
        # a game on the local network with a password, one offered on Nakama too
        self.start('Anna', {'matchmaking': {'local': targets, 'nakama': nakama, 'plugins': False},
                            'lobbyTest': {'host': LEVEL, 'startAfter': 600000, 'password': 'secret'}})
        self.start('Bertram', {'hostPort': 1240, 'matchmaking': {'local': targets, 'nakama': nakama, 'plugins': False},
                               'lobbyTest': {'host': LEVEL, 'startAfter': 600000, 'offer': ['nakama']}})
        time.sleep(6)
        shot = os.environ['WKB_SCREENSHOT']
        viewer = self.start('Viewer', {'matchmaking': {'local': targets, 'nakama': nakama},
                                       'menuTest': {'open': ['multiplayer.games'], 'frames': 900, 'screenshot': shot}},
                            renderer='diligent')
        ok = wait_for(lambda: viewer.proc.poll() is not None or Path(shot).exists(), 120)
        time.sleep(1)
        self.assertTrue(Path(shot).exists(), viewer.text()[-2000:])


if __name__ == '__main__':
    unittest.main()
