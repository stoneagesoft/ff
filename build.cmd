@echo off
rem ---------------------------------------------------------------------
rem ff Windows build script. Configures and builds the library plus the
rem ffsh example shell using Visual Studio 2026 (or the latest installed
rem MSVC toolchain) and the default CMake generator.
rem
rem Outputs land under build\ next to this file. Pass --tests to also
rem build the regression-test driver and run ctest.
rem ---------------------------------------------------------------------

setlocal enabledelayedexpansion

set "REPO_ROOT=%~dp0"
if "%REPO_ROOT:~-1%"=="\" set "REPO_ROOT=%REPO_ROOT:~0,-1%"
set "BUILD_DIR=%REPO_ROOT%\build"

set "RUN_TESTS=0"
if "%1"=="--tests" set "RUN_TESTS=1"

rem -----------------------------------------------------------------
rem Locate the Visual Studio install via vswhere. -prerelease lets
rem newly released versions (VS 2026, etc.) be picked up before
rem vswhere ships an updated default range.
rem -----------------------------------------------------------------
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo ERROR: vswhere.exe not found at:
    echo   %VSWHERE%
    echo Install Visual Studio 2026 ^(or any 2017+ release^) with the
    echo "Desktop development with C++" workload.
    exit /b 1
)

set "VS_INSTALL="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -prerelease ^
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 ^
        -property installationPath`) do (
    set "VS_INSTALL=%%i"
)

if "%VS_INSTALL%"=="" (
    echo ERROR: No Visual Studio install with the C++ build tools was
    echo found. Run the VS installer and add the "Desktop development
    echo with C++" workload.
    exit /b 1
)

echo Using Visual Studio at: %VS_INSTALL%

set "VCVARS=%VS_INSTALL%\VC\Auxiliary\Build\vcvarsall.bat"
if not exist "%VCVARS%" (
    echo ERROR: vcvarsall.bat not found under the located VS install.
    echo   %VCVARS%
    exit /b 1
)

call "%VCVARS%" x64
if errorlevel 1 (
    echo ERROR: vcvarsall.bat failed to set up the toolchain.
    exit /b %errorlevel%
)

rem -----------------------------------------------------------------
rem Configure + build. Default generator is Visual Studio's native
rem multi-config one; pass -G "Ninja" via the CMAKE_GENERATOR env var
rem or edit below if you'd rather use Ninja.
rem -----------------------------------------------------------------
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

set "CONFIG_FLAGS=-S "%REPO_ROOT%" -B "%BUILD_DIR%" -DFF_BUILD_STATIC=ON -DFF_BUILD_SHARED=ON"
if "%RUN_TESTS%"=="1" set "CONFIG_FLAGS=%CONFIG_FLAGS% -DFF_BUILD_TESTS=ON"

cmake %CONFIG_FLAGS%
if errorlevel 1 exit /b %errorlevel%

cmake --build "%BUILD_DIR%" --config Release --parallel
if errorlevel 1 exit /b %errorlevel%

if "%RUN_TESTS%"=="1" (
    pushd "%BUILD_DIR%"
    ctest --output-on-failure -C Release
    set "TEST_RC=!errorlevel!"
    popd
    exit /b !TEST_RC!
)

echo.
echo Build succeeded. Artefacts in:
echo   %BUILD_DIR%

endlocal
