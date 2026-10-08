@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist build-chuchu-tests mkdir build-chuchu-tests
cl /nologo /std:c++17 /EHsc /bigobj /Ishared /Imod\Royale /Ithird_party\Shipwright-Android\libultraship\include /Febuild-chuchu-tests\renderer.exe /Fobuild-chuchu-tests\renderer.obj tools\chuchu\check_renderer.cpp
if errorlevel 1 exit /b 1
build-chuchu-tests\renderer.exe
