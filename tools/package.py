"""Packages a wkbre2 build for release: dist/wkbre2-<date>-<commit>.zip with the exe, its DLLs, the
redata folder, the licence and the README. Never any game file: players point wkbre2 at their own
copy of Warrior Kings: Battles (found by itself in a Steam install, else "gamePath" in
wkconfig.json). The debug symbols (wkbre2.pdb) go into a separate zip, for reading crash reports.

Usage: python -I tools/package.py [build folder, default build/wkbre2/RelWithDebInfo]
"""
import datetime
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def main():
    build = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'build' / 'wkbre2' / 'RelWithDebInfo'
    exe = build / 'wkbre2.exe'
    if not exe.exists():
        sys.exit(f'{exe} not found: build wkbre2 first')
    commit = subprocess.run(['git', '-C', str(ROOT), 'rev-parse', '--short', 'HEAD'], capture_output=True, text=True).stdout.strip() or 'unknown'
    name = f'wkbre2-{datetime.date.today():%Y%m%d}-{commit}'
    dist = ROOT / 'dist'
    dist.mkdir(exist_ok=True)

    files = [(exe, 'wkbre2.exe')]
    files += [(dll, dll.name) for dll in sorted(build.glob('*.dll'))]
    for path in sorted((ROOT / 'redata').rglob('*')):
        if path.is_file():
            files.append((path, 'redata/' + path.relative_to(ROOT / 'redata').as_posix()))
    for doc in ('LICENSE', 'README.md'):
        if (ROOT / doc).exists():
            files.append((ROOT / doc, doc))

    out = dist / f'{name}.zip'
    with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
        for src, arc in files:
            z.write(src, f'{name}/{arc}')
    print(f'{out}: {len(files)} files, {out.stat().st_size / 1e6:.1f} MB')

    pdb = build / 'wkbre2.pdb'
    if pdb.exists():
        sym = dist / f'{name}-symbols.zip'
        with zipfile.ZipFile(sym, 'w', zipfile.ZIP_DEFLATED) as z:
            z.write(pdb, 'wkbre2.pdb')
        print(f'{sym}: debug symbols, {sym.stat().st_size / 1e6:.1f} MB')


if __name__ == '__main__':
    main()
