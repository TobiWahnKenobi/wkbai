"""Tests for wkbre2's layered file system and mod load order (file.cpp, test "VFS").

Builds a synthetic game folder (no game data needed), runs wkbre2's VFS test mode under
several wkconfig.json variants and checks which archive/folder each file is loaded from.

    original order (no "modLoadOrder"): data.bcp and all mods form one group, the newest
        timestamp of a file wins, like the original game
    custom order ("modLoadOrder"): mods are layered on top of data.bcp, later ones win

Run: python tests/test_vfs.py [-v]      (WKBRE2_EXE overrides the path of wkbre2.exe)
"""
import json
import os
import struct
import subprocess
import sys
import tempfile
import time
import unittest
from datetime import datetime, timezone
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
EXE = Path(os.environ.get('WKBRE2_EXE', REPO / 'build' / 'wkbre2' / 'RelWithDebInfo' / 'wkbre2.exe'))
VFS_TEST_NAME = 'VFS (file system and mods)'


def vfs_test_number():
    """Index of the VFS test in testList (wkbre2/test.cpp). Read from the source rather than by
    starting wkbre2: LaunchTest() runs test 0 on any unexpected input, which shows a modal error
    without game data."""
    lines = (REPO / 'wkbre2' / 'test.cpp').read_text(encoding='latin1').splitlines()
    start = next(i for i, l in enumerate(lines) if 'testList = {' in l)
    entries = [l for l in lines[start + 1:] if l.strip().startswith('{Test_')]
    return next(i for i, l in enumerate(entries) if VFS_TEST_NAME in l)


def filetime(year, month=6, day=1):
    """Windows FILETIME (100 ns intervals since 1601) for a UTC date."""
    epoch = datetime(year, month, day, tzinfo=timezone.utc).timestamp()
    return int(epoch * 10_000_000) + 116444736000000000


def write_bcp(path, files, timestamp):
    """Minimal 'PAK File 2.01' writer: every file stored uncompressed with the given timestamp."""
    tree, entries, ids = {}, [], {}
    with open(path, 'wb') as f:
        f.write(b'PAK File 2.01 (c) Black Cactus Games Limited \xad\x91\xe4' + b'\0' * 8)
        for name, data in files.items():
            off = f.tell()
            f.write(data)
            ids[name] = len(entries)
            entries.append((off, f.tell(), len(data), 0, 1 if data else 2))
            node = tree
            *dirs, leaf = name.split('/')
            for d in dirs:
                node = node.setdefault(d, {})
            node[leaf] = name
        table = f.tell()
        f.write(struct.pack('<I', len(entries)))
        for e in entries:
            f.write(struct.pack('<5I', *e))

        def walk(node):
            leaves = [k for k, v in node.items() if isinstance(v, str)]
            subdirs = [k for k, v in node.items() if isinstance(v, dict)]
            f.write(struct.pack('<I', len(leaves)))
            for k in leaves:
                f.write(struct.pack('<IQ', ids[node[k]], timestamp) + bytes([len(k)]) + k.encode('utf-16-le'))
            f.write(struct.pack('<I', len(subdirs)))
            for k in subdirs:
                f.write(bytes([len(k)]) + k.encode('utf-16-le'))
                walk(node[k])
        walk(tree)
        size = f.tell() - table
        f.seek(0x30)
        f.write(struct.pack('<II', table, size))


def write_file(path, text, mtime=None):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding='ascii')
    if mtime is not None:
        os.utime(path, (mtime, mtime))


class VfsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not EXE.exists():
            raise unittest.SkipTest(f'{EXE} not found; build wkbre2 first')
        cls.tmp = tempfile.TemporaryDirectory()
        root = Path(cls.tmp.name)
        cls.game, cls.mods, cls.work = root / 'game', root / 'mods', root / 'work'
        cls.game.mkdir()
        cls.work.mkdir()
        write_bcp(cls.game / 'data.bcp', {'a.txt': b'base', 'b.txt': b'base', 'c.txt': b'base', 'Dir/e.txt': b'base'}, filetime(2003))
        write_bcp(cls.game / 'Plus.bcp', {'a.txt': b'plus'}, filetime(2025))
        write_bcp(cls.game / 'Zed.bcp', {'a.txt': b'zed', 'Dir/f.txt': b'zed'}, filetime(2024))
        write_bcp(cls.game / 'Old.bcp', {'b.txt': b'old'}, filetime(2001))  # older than data.bcp
        write_file(cls.game / 'data' / 'c.txt', 'loose')  # modified now
        mymod = cls.mods / 'MyMod'
        write_file(mymod / 'mod.json', json.dumps({'name': 'My Mod', 'description': 'test mod'}),
                   datetime(2000, 1, 1, tzinfo=timezone.utc).timestamp())
        write_file(mymod / 'a.txt', 'mymod', datetime(2000, 1, 1, tzinfo=timezone.utc).timestamp())
        write_bcp(mymod / 'inner.bcp', {'d.txt': b'modbcp'}, filetime(2000))
        os.utime(mymod / 'inner.bcp', (datetime(2000, 1, 1, tzinfo=timezone.utc).timestamp(),) * 2)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_vfs(self, files, **settings):
        config = {'gamePath': str(self.game), 'modsPath': str(self.mods), 'gameVersion': 2, 'test': True, **settings}
        (self.work / 'wkconfig.json').write_text(json.dumps(config), encoding='utf-8')
        env = dict(os.environ, PATH=str(EXE.parent) + os.pathsep + os.environ.get('PATH', ''))
        # LaunchTest() reads the test number, then the VFS test reads file names until an empty line
        number = vfs_test_number()
        out = subprocess.run([str(EXE)], cwd=self.work, input=f'{number}\n' + '\n'.join(files) + '\n\n',
                             capture_output=True, text=True, timeout=60, env=env).stdout
        self.assertIn(f'{number}: {VFS_TEST_NAME}', out, 'test number does not match the VFS test')
        result = {'order': None, 'mods': [], 'resolve': {}}
        for line in out.splitlines():
            if line.startswith('ORDER '):
                result['order'] = line[6:]
            elif line.startswith('MOD '):
                mod_id, name, kind, enabled = line[4:].split('|')
                result['mods'].append((mod_id, enabled == '1'))
            elif line.startswith('RESOLVE '):
                name, source, content = line[8:].split('|')
                result['resolve'][name] = (source, content)
        self.assertIsNotNone(result['order'], f'VFS test produced no output:\n{out}')
        return result

    FILES = ['a.txt', 'b.txt', 'c.txt', 'd.txt', 'Dir\\e.txt', 'Dir\\f.txt', 'A.TXT', 'missing.txt']

    def test_original_order_newest_file_wins(self):
        r = self.run_vfs(self.FILES)
        self.assertEqual(r['order'], 'original')
        # listed by newest file: MyMod (2000), Old (2001), Zed (2024), Plus (2025), data/ (now)
        self.assertEqual([m for m, _ in r['mods']], ['mods/MyMod', 'Old.bcp', 'Zed.bcp', 'Plus.bcp', 'data/'])
        self.assertTrue(all(enabled for _, enabled in r['mods']))
        res = r['resolve']
        self.assertEqual(res['a.txt'], ('Plus.bcp', 'plus'))     # 2025 beats Zed 2024, data.bcp 2003, MyMod 2000
        self.assertEqual(res['b.txt'], ('data.bcp', 'base'))     # Old.bcp (2001) is older than data.bcp
        self.assertEqual(res['c.txt'], ('data/', 'loose'))       # loose file modified now
        self.assertEqual(res['d.txt'], ('inner.bcp', 'modbcp'))  # archive inside a mod folder
        self.assertEqual(res['Dir\\e.txt'], ('data.bcp', 'base'))
        self.assertEqual(res['Dir\\f.txt'], ('Zed.bcp', 'zed'))
        self.assertEqual(res['A.TXT'], ('Plus.bcp', 'plus'))     # case-insensitive
        self.assertEqual(res['missing.txt'], ('', ''))

    def test_disabled_mod_is_skipped(self):
        r = self.run_vfs(['a.txt'], disabledMods=['Plus.bcp'])
        self.assertIn(('Plus.bcp', False), r['mods'])
        self.assertEqual(r['resolve']['a.txt'], ('Zed.bcp', 'zed'))

    def test_custom_order_later_mod_wins(self):
        r = self.run_vfs(self.FILES, modLoadOrder=['Plus.bcp', 'Zed.bcp', 'Old.bcp', 'mods/MyMod', 'data/'])
        self.assertEqual(r['order'], 'custom')
        self.assertEqual([m for m, _ in r['mods']], ['Plus.bcp', 'Zed.bcp', 'Old.bcp', 'mods/MyMod', 'data/'])
        res = r['resolve']
        self.assertEqual(res['a.txt'], ('mods/MyMod', 'mymod'))  # last mod with a.txt wins despite its old timestamp
        self.assertEqual(res['b.txt'], ('Old.bcp', 'old'))       # any mod overrides data.bcp
        self.assertEqual(res['c.txt'], ('data/', 'loose'))
        self.assertEqual(res['Dir\\e.txt'], ('data.bcp', 'base'))

    def test_custom_order_appends_unlisted_mods(self):
        r = self.run_vfs(['a.txt'], modLoadOrder=['Zed.bcp', 'Plus.bcp'])
        self.assertEqual([m for m, _ in r['mods']][:2], ['Zed.bcp', 'Plus.bcp'])
        self.assertEqual(sorted(m for m, _ in r['mods'][2:]), ['Old.bcp', 'data/', 'mods/MyMod'])
        # MyMod comes after Plus (appended), so it overrides a.txt
        self.assertEqual(r['resolve']['a.txt'], ('mods/MyMod', 'mymod'))


if __name__ == '__main__':
    unittest.main()
