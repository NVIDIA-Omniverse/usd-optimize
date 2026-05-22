@echo off

SET PATH=%PATH%;%~dp0\lib;%~dp0\extraLibs

call "%~dp0sceneOptimizer.exe" %*
