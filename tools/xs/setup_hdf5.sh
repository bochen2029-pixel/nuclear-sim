#!/usr/bin/env bash
# Fetch the ENDF/B-VIII.0 PRE-RECONSTRUCTED HDF5 (openmc format, Doppler-broadened to
# 293.6 K) and extract only the 8 fast4 a-set nuclides + cross_sections.xml. This openmc
# build lacks the resonance-reconstruction extension, so raw ENDF misses the resonance
# region -- the pre-reconstructed library is required for a defensible collapse. ~3.0 GB.
# Public ENDF/B-VIII.0 (NOT ICSBEP). M1-T4a-2a. Usage: bash setup_hdf5.sh [DEST]
set -euo pipefail

DEST="${1:-$HOME/nukesim_hdf5}"
URL="https://zenodo.org/records/8410375/files/endfb80-lowtemp.tar.xz"
mkdir -p "$DEST"
cd "$DEST"

if [ ! -s endfb80.tar.xz ]; then
  echo "== downloading endfb80-lowtemp.tar.xz (3.0 GB) =="
  curl -fL -C - -o endfb80.tar.xz "$URL"
fi
echo "== archive size: $(stat -c %s endfb80.tar.xz) bytes =="

echo "== extracting the 8 a-set nuclides + cross_sections.xml =="
tar -xJf endfb80.tar.xz --wildcards --no-anchored \
  'U234.h5' 'U235.h5' 'U238.h5' 'Pu239.h5' 'Pu240.h5' 'Pu241.h5' 'Ga69.h5' 'Ga71.h5' \
  'cross_sections.xml'

echo "== extracted HDF5 files =="
find "$DEST" -name '*.h5' -printf '%p  %s bytes\n' | sort
echo "HDF5_OK"
