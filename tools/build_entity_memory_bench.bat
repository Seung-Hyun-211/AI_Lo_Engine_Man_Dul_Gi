@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0.."
if not exist build\tools mkdir build\tools
cl /nologo /std:c++20 /EHsc /O2 /W3 ^
   /Fo:build\tools\ /Fe:build\tools\entity_memory_bench.exe ^
   tools\entity_memory_bench.cpp
echo BUILD_DONE errorlevel=%errorlevel%
