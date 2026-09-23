@echo off
rem Builds tools\gen_templates.cpp and regenerates assets\templates\**\*_template.png from the current CSVs.
rem See docs/circular-art-guide.md 4.1. Linux twin: tools/gen_templates.sh
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0.."
if not exist build\tools mkdir build\tools
cl /nologo /std:c++20 /EHsc /O2 /W4 /utf-8 /DNOMINMAX /I src /Fo:build\tools\ /Fe:build\tools\gen_templates.exe ^
   tools\gen_templates.cpp src\game\CircularBalance.cpp src\core\CsvFile.cpp src\core\AssetPaths.cpp
if errorlevel 1 exit /b 1
build\tools\gen_templates.exe
