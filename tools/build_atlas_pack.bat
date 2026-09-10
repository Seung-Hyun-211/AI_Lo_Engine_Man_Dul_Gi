@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0.."
if not exist build\tools mkdir build\tools
rem stb_image implementation TU: warnings off (same as the engine build)
cl /nologo /std:c++20 /EHsc /O2 /utf-8 /W0 /c /I src ^
   /Fo:build\tools\stb_image_impl.obj ^
   src\vendor\stb\stb_image_impl.cpp
if errorlevel 1 goto :done
cl /nologo /std:c++20 /EHsc /O2 /utf-8 /W4 /I src ^
   /Fe:build\tools\atlas_pack.exe /Fo:build\tools\ ^
   tools\atlas_pack.cpp src\import\ImageFile.cpp src\import\ImageData.cpp ^
   build\tools\stb_image_impl.obj
:done
echo BUILD_DONE errorlevel=%errorlevel%
