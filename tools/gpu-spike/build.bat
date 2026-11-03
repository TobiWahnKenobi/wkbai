@echo off
rem Builds pathvis_bench.exe (the GPU path finding and visibility spike) with the VS 2026 x64 tools.
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
cl /nologo /O2 /EHsc /std:c++20 /W3 pathvis_bench.cpp /I ..\..\wkbai /Fe:pathvis_bench.exe d3d11.lib dxgi.lib d3dcompiler.lib
