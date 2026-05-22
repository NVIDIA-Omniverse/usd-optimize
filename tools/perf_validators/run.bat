@echo off
rem Windows wrapper that sets up PATH / PYTHONPATH and invokes perf_validators.py
rem under the build's bundled Python. Pass through all args to the python script.
rem
rem Examples:
rem   tools\perf_validators\run.bat run path\to\asset.usd --summary out.json
rem   tools\perf_validators\run.bat compare a.json b.json
setlocal

set "SCRIPT_DIR=%~dp0"
rem Strip trailing backslash from SCRIPT_DIR
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"

for %%i in ("%SCRIPT_DIR%\..\..") do set "REPO_ROOT=%%~fi"

if "%SO_CONFIG%"=="" set "SO_CONFIG=release"
if "%SO_PLATFORM%"=="" set "SO_PLATFORM=windows-x86_64"

set "BUILD_DIR=%REPO_ROOT%\_build\%SO_PLATFORM%\%SO_CONFIG%"
set "USD_DIR=%REPO_ROOT%\_build\target-deps\usd\%SO_CONFIG%"
set "PYTHON=%REPO_ROOT%\_build\target-deps\python\python.exe"

if not exist "%BUILD_DIR%" (
    echo Build not found at %BUILD_DIR% -- run repo.bat build first. 1>&2
    exit /b 1
)

rem On Windows, DLL resolution uses PATH (no LD_LIBRARY_PATH).
set "PATH=%BUILD_DIR%\bin;%BUILD_DIR%\lib;%BUILD_DIR%\extraLibs;%USD_DIR%\bin;%USD_DIR%\lib;%PATH%"
set "PYTHONPATH=%BUILD_DIR%\python;%USD_DIR%\lib\python;%PYTHONPATH%"

"%PYTHON%" -c "import omni.asset_validator" >NUL 2>&1
if errorlevel 1 (
    "%PYTHON%" -m pip install --quiet --disable-pip-version-check "omniverse-asset-validator>=1.15.1"
)

"%PYTHON%" "%SCRIPT_DIR%\perf_validators.py" %*
exit /b %errorlevel%
