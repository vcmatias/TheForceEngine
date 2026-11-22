#!/bin/sh
# The Force Engine VR for Meta Quest - DFMOD weapon style installer (Linux, macOS).
set -e
cd "$(dirname "$0")"
if [ ! -x .venv/bin/python ]; then
  echo "Preparing Python packages (numpy, scipy, pillow)..."
  python3 -m venv .venv
  .venv/bin/python -m pip install --quiet --disable-pip-version-check numpy scipy pillow
fi
exec .venv/bin/python install_dfmod.py "$@"
