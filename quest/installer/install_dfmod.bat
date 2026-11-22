@echo off
rem The Force Engine VR for Meta Quest - DFMOD weapon style installer (Windows).
rem Sets up a private Python environment next to this file and runs install_dfmod.py.
setlocal
cd /d "%~dp0"
set "PY="
where py >nul 2>nul && set "PY=py -3"
if not defined PY where python >nul 2>nul && set "PY=python"
if not defined PY (
  echo Python 3 was not found. Install it from https://www.python.org/downloads/ ^(tick "Add python.exe to PATH"^) and run this file again.
  echo Python 3 nao foi encontrado. Instale em https://www.python.org/downloads/ ^(marque "Add python.exe to PATH"^) e rode este arquivo de novo.
  pause
  exit /b 1
)
if not exist ".venv\Scripts\python.exe" (
  echo Preparing Python packages ^(numpy, scipy, pillow^)...
  %PY% -m venv .venv || goto :fail
  ".venv\Scripts\python.exe" -m pip install --quiet --disable-pip-version-check numpy scipy pillow || goto :fail
)
".venv\Scripts\python.exe" install_dfmod.py %*
echo.
pause
exit /b %errorlevel%
:fail
echo Could not prepare the Python environment. / Falha ao preparar o ambiente Python.
pause
exit /b 1
