"""Packages a wkbai build for release: dist/wkbai-<date>-<commit>.zip with the exe, its DLLs, the
redata folder, the licence, the README, THIRD-PARTY-NOTICES.md, the licence texts of every
component in licenses/ and SOURCE.txt (where the Corresponding Source is). Never any game file:
players point wkbai at their own copy of Warrior Kings: Battles (found by itself in a Steam
install, else "gamePath" in wkconfig.json). No matchmaking plugins either (plugins/eos/CMakeLists.txt
says why). The debug symbols (wkbai.pdb) go into a separate zip, for reading crash reports.

Usage: python -I tools/package.py [build folder, default build/wkbai/RelWithDebInfo]
"""
import datetime
import re
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
REPO_URL = 'https://github.com/TobiWahnKenobi/wkbai'

# vcpkg ports in the release (their licence: vcpkg_installed/<triplet>/share/<port>/copyright)
VCPKG_PORTS = ('sdl3', 'openal-soft', 'mpg123', 'fmt', 'lua', 'bzip2', 'enet', 'nlohmann-json', 'sol2', 'stb')

# Diligent and the libraries it links in: (licence file in DiligentCore's source, name in licenses/)
DILIGENT_LICENSES = (
    ('License.txt', 'DiligentCore.txt'),
    ('ThirdParty/SPIRV-Tools/LICENSE', 'SPIRV-Tools.txt'),
    ('ThirdParty/SPIRV-Cross/LICENSE', 'SPIRV-Cross.txt'),
    ('ThirdParty/SPIRV-Headers/LICENSE', 'SPIRV-Headers.txt'),
    ('ThirdParty/glslang/LICENSE.txt', 'glslang.txt'),
    ('ThirdParty/Vulkan-Headers/LICENSE.md', 'Vulkan-Headers.txt'),
    ('ThirdParty/volk/LICENSE.md', 'volk.txt'),
    ('ThirdParty/xxHash/LICENSE', 'xxHash.txt'),
    ('ThirdParty/GPUOpenShaderUtils/License.txt', 'GPUOpenShaderUtils.txt'),
)


def cmake_cache(build_root):
    cache = {}
    path = build_root / 'CMakeCache.txt'
    if path.exists():
        for line in path.read_text(encoding='utf-8', errors='replace').splitlines():
            m = re.match(r'^([A-Za-z0-9_.-]+):[A-Z]+=(.*)$', line)
            if m:
                cache[m.group(1)] = m.group(2)
    return cache


def license_files(build_root):
    """(source path, name in licenses/) for every component; exits if one is missing."""
    out = [
        (ROOT / 'wkbai' / 'imgui' / 'LICENSE.txt', 'DearImGui.txt'),
        (ROOT / 'wkbai' / 'FidelityFX' / 'LICENSE.txt', 'AMD-FidelityFX.txt'),
    ]
    cache = cmake_cache(build_root)
    deps = build_root / '_deps'
    diligent = Path(cache.get('DiligentCore_SOURCE_DIR') or cache.get('diligentcore_SOURCE_DIR') or deps / 'diligentcore-src')
    out += [(diligent / src, name) for src, name in DILIGENT_LICENSES]
    nvapi = Path(cache.get('DILIGENT_NVAPI_PATH') or deps / 'nvapi-src')
    out.append((nvapi / 'License.txt', 'NVAPI.txt'))
    out.append((deps / 'tracy-src' / 'LICENSE', 'Tracy.txt'))
    share = build_root / 'vcpkg_installed' / 'x64-windows' / 'share'
    out += [(share / port / 'copyright', f'{port}.txt') for port in VCPKG_PORTS]
    missing = [str(src) for src, _ in out if not src.exists()]
    if missing:
        sys.exit('licence files not found (a release must carry them all):\n  ' + '\n  '.join(missing))
    return out


def main():
    build = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'build' / 'wkbai' / 'RelWithDebInfo'
    exe = build / 'wkbai.exe'
    if not exe.exists():
        sys.exit(f'{exe} not found: build wkbai first')
    # the build folder's root (with CMakeCache.txt), above build/wkbai/<config>
    build_root = next((p for p in [build, *build.parents] if (p / 'CMakeCache.txt').exists()), ROOT / 'build')
    git = lambda *args: subprocess.run(['git', '-C', str(ROOT), *args], capture_output=True, text=True).stdout.strip()
    commit = git('rev-parse', '--short', 'HEAD') or 'unknown'
    full_commit = git('rev-parse', 'HEAD') or 'unknown'
    dirty = bool(git('status', '--porcelain', '--untracked-files=no'))
    name = f'wkbai-{datetime.date.today():%Y%m%d}-{commit}'
    dist = ROOT / 'dist'
    dist.mkdir(exist_ok=True)

    files = [(exe, 'wkbai.exe')]
    files += [(dll, dll.name) for dll in sorted(build.glob('*.dll'))]
    for path in sorted((ROOT / 'redata').rglob('*')):
        if path.is_file():
            files.append((path, 'redata/' + path.relative_to(ROOT / 'redata').as_posix()))
    for doc in ('LICENSE', 'README.md', 'THIRD-PARTY-NOTICES.md'):
        files.append((ROOT / doc, doc))
    files += [(src, 'licenses/' + arc) for src, arc in license_files(build_root)]

    source = (
        f'wkbai is free software under the GNU General Public License, version 3 (LICENSE).\n\n'
        f'This build was made from commit {full_commit}{" (with local changes)" if dirty else ""}.\n'
        f'Its source code (the Corresponding Source) is at no charge at\n'
        f'  {REPO_URL}/tree/{full_commit}\n'
        f'and as an archive at\n'
        f'  {REPO_URL}/archive/{full_commit}.zip\n\n'
        f'The libraries it is built with, their versions and where their sources are: '
        f'THIRD-PARTY-NOTICES.md; their licences: licenses/.\n'
    )
    if dirty:
        print('warning: the working tree has uncommitted changes; commit and push before releasing')

    out = dist / f'{name}.zip'
    with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
        for src, arc in files:
            z.write(src, f'{name}/{arc}')
        z.writestr(f'{name}/SOURCE.txt', source)
    print(f'{out}: {len(files) + 1} files, {out.stat().st_size / 1e6:.1f} MB')

    pdb = build / 'wkbai.pdb'
    if pdb.exists():
        sym = dist / f'{name}-symbols.zip'
        with zipfile.ZipFile(sym, 'w', zipfile.ZIP_DEFLATED) as z:
            z.write(pdb, 'wkbai.pdb')
        print(f'{sym}: debug symbols, {sym.stat().st_size / 1e6:.1f} MB')


if __name__ == '__main__':
    main()
