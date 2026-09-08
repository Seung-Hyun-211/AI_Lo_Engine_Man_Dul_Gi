@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0.."
if not exist build\tools mkdir build\tools
cl /nologo /std:c++20 /EHsc /W3 /Isrc /DENGINE_WITH_2D /DENGINE_WITH_3D ^
   /Fo:build\tools\ /Fe:build\tools\fbx_probe.exe ^
   tools\fbx_probe.cpp src\import\ModelImporter.cpp src\import\CreaseLines.cpp src\import\TgaImage.cpp src\anim\AnimationSampler.cpp ^
   /Tp src\vendor\ufbx\ufbx.c
echo BUILD_DONE errorlevel=%errorlevel%
