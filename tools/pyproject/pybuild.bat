@echo off
setlocal

set VENV=.\_build\host-deps\py_package_venv

:: capture the args in `scriptArgs`
for /f "usebackq tokens=1*" %%i in (`echo %*`) DO @ set scriptArgs=%%j

REM setup the build environment
if exist "%VENV%\Scripts\activate" (
    call "%VENV%\Scripts\activate"
) else (
    echo Building: %VENV%
    .\_build\target-deps\python\python -m venv %VENV%
    call "%VENV%\Scripts\activate"
    python -m pip install -r .\tools\pyproject\dev-requirements.txt
)

REM do the build
poetry --version
poetry build %scriptArgs%

endlocal
