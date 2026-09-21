@echo off
rem Builds the headless Circular balance simulator (2D-only, no GPU/window). See docs/circular-balance.md.
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0.."
if not exist build\tools mkdir build\tools
cl /nologo /std:c++20 /EHsc /O2 /W3 /utf-8 /DENGINE_WITH_2D /DNOMINMAX /I src ^
   /Fo:build\tools\ /Fe:build\tools\balance_sim.exe ^
   tools\balance_sim.cpp ^
   src\game\Simulation.cpp src\game\MobField.cpp src\game\CircularBalance.cpp ^
   src\core\JobSystem.cpp src\core\CsvFile.cpp src\core\AssetPaths.cpp ^
   src\physics\p2d\CollisionWorld2D.cpp
echo BUILD_DONE errorlevel=%errorlevel%
