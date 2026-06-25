@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
if %errorlevel% neq 0 ( echo VCVARS_FAIL & exit /b 1 )
set "NoDefaultCurrentDirectoryInExePath="
set CM="C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set NINJA="C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
set TKDIR=E:\user\MitiruEngine\external\cubism\CubismSdkForNative-5-r.5\Samples\D3D11\thirdParty\DirectXTK
echo === configuring DirectXTK (cmake/ninja) ===
%CM% -S "%TKDIR%" -B "%TKDIR%\build_ninja" -G Ninja -D CMAKE_BUILD_TYPE=Release -D BUILD_TOOLS=OFF -D BUILD_XAUDIO_WIN10=OFF -D CMAKE_MAKE_PROGRAM=%NINJA%
if %errorlevel% neq 0 ( echo DXTK_CFG_FAIL & exit /b 1 )
%CM% --build "%TKDIR%\build_ninja"
if %errorlevel% neq 0 ( echo DXTK_BUILD_FAIL & exit /b 1 )
echo === placing DirectXTK.lib where Demo expects ===
set DST=%TKDIR%\Bin\Desktop_2022\x64\Release
if not exist "%DST%" mkdir "%DST%"
for /r "%TKDIR%\build_ninja" %%f in (DirectXTK.lib) do copy /y "%%f" "%DST%\DirectXTK.lib"
if not exist "%DST%\DirectXTK.lib" ( echo LIB_MISSING & exit /b 1 )
cd /d "%~dp0"
echo === building Demo ===
%CM% --build build\official_ninja
if %errorlevel% neq 0 ( echo BUILD_FAIL & exit /b 1 )
echo BUILD_DONE
