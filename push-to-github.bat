@echo off
setlocal
cd /d "%~dp0"

echo === git init ===
git init -b main
if errorlevel 1 goto :err

echo === git add . ===
git add .
if errorlevel 1 goto :err

echo === git status (short) ===
git status -s | findstr /v "^"
git status -s

echo === git commit ===
git commit -m "Initial MBTH Ghost Client commit (Fusion+ fork + KillAura/TriggerBot/RotationUtils + KILLAURA_DESIGN.md)"
if errorlevel 1 goto :err

echo === git remote add origin (delete first if exists) ===
git remote remove origin 2>nul
git remote add origin https://github.com/Adhi1908/MBTH-GHOST-CLIENT.git
if errorlevel 1 goto :err

echo === git branch -M main ===
git branch -M main

echo === git push -u origin main ===
git push -u origin main
if errorlevel 1 goto :err

echo.
echo === SUCCESS - pushed to https://github.com/Adhi1908/MBTH-GHOST-CLIENT ===
goto :eof

:err
echo.
echo === FAILED at last step (errorlevel=%errorlevel%) ===
exit /b %errorlevel%
