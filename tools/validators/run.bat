@echo off
rem Windows wrapper that sets up PATH / PYTHONPATH and invokes validators.py
rem under the build's bundled Python. Pass through all args to the python script.
setlocal

set "SCRIPT_DIR=%~dp0"
rem Strip trailing backslash from SCRIPT_DIR
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"

for %%i in ("%SCRIPT_DIR%\..\..") do set "REPO_ROOT=%%~fi"

if "%USD_OPTIMIZE_CONFIG%"=="" set "USD_OPTIMIZE_CONFIG=release"
if "%USD_OPTIMIZE_PLATFORM%"=="" set "USD_OPTIMIZE_PLATFORM=windows-x86_64"

set "BUILD_DIR=%REPO_ROOT%\_build\%USD_OPTIMIZE_PLATFORM%\%USD_OPTIMIZE_CONFIG%"
set "USD_DIR=%REPO_ROOT%\_build\target-deps\usd\%USD_OPTIMIZE_CONFIG%"
set "PYTHON=%REPO_ROOT%\_build\target-deps\python\python.exe"

if not exist "%BUILD_DIR%" (
    echo Build not found at %BUILD_DIR% -- run repo.bat build first. 1>&2
    exit /b 1
)

rem On Windows, DLL resolution uses PATH (no LD_LIBRARY_PATH).
set "PATH=%BUILD_DIR%\bin;%BUILD_DIR%\lib;%BUILD_DIR%\extraLibs;%USD_DIR%\bin;%USD_DIR%\lib;%PATH%"
set "PYTHONPATH=%BUILD_DIR%\python;%USD_DIR%\lib\python;%PYTHONPATH%"

rem pip enforces the floor; a presence check would skip an older install.
"%PYTHON%" -m pip install --quiet --disable-pip-version-check "usd-validation-nvidia>=1.22.0,<2"
rem Exit 2 = nothing was validated, matching validators.py's guards; 1 means "issues found".
if errorlevel 1 (
    echo usd-validation-nvidia install failed -- nothing was validated. Set HTTPS_PROXY if behind a proxy. 1>&2
    exit /b 2
)

"%PYTHON%" "%SCRIPT_DIR%\validators.py" %*
exit /b %errorlevel%
