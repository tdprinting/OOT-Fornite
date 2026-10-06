@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist build-map-tests mkdir build-map-tests
cl /nologo /std:c++17 /EHsc /bigobj /O2 /Ishared /Iserver /Iclient /Imod\Royale /Febuild-map-tests\convergence_tests.exe /Fobuild-map-tests\convergence_tests.obj server\tests\convergence_tests.cpp
if errorlevel 1 exit /b 1
build-map-tests\convergence_tests.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /bigobj /O2 /Ishared /Iserver /Iclient /Imod\Royale /Febuild-map-tests\royale_tests.exe /Fobuild-map-tests\tests.obj server\tests\tests.cpp
if errorlevel 1 exit /b 1
build-map-tests\royale_tests.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /bigobj /O2 /Ishared /Iserver /Iclient /Imod\Royale /Febuild-map-tests\net_tests.exe /Fobuild-map-tests\net_tests.obj server\tests\net_tests.cpp
if errorlevel 1 exit /b 1
build-map-tests\net_tests.exe
