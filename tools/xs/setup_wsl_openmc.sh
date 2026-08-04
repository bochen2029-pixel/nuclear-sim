#!/usr/bin/env bash
# Set up openmc.data in WSL (conda-forge) for the fast4 ENDF collapse (M1-T4a-2).
# openmc is not on PyPI and has no conda win-64 build, so it is installed under WSL
# via miniforge; the produced fast4.json is plain data consumed by the Windows build.
# Idempotent: skips miniforge / the env if already present.
set -euo pipefail

MF="$HOME/miniforge3"
if [ ! -x "$MF/bin/conda" ]; then
  echo "== installing miniforge =="
  curl -L -o /tmp/mf.sh \
    https://github.com/conda-forge/miniforge/releases/latest/download/Miniforge3-Linux-x86_64.sh
  bash /tmp/mf.sh -b -p "$MF"
fi

# shellcheck disable=SC1091
source "$MF/etc/profile.d/conda.sh"

if ! conda env list | awk '{print $1}' | grep -qx omc; then
  echo "== creating 'omc' env (openmc + numpy/scipy) from conda-forge =="
  mamba create -y -n omc -c conda-forge openmc python=3.12
fi

conda activate omc
echo "== verify =="
python -c "import openmc, openmc.data, numpy, scipy; print('openmc', openmc.__version__, '| numpy', numpy.__version__, '| scipy', scipy.__version__)"
echo "SETUP_OK"
