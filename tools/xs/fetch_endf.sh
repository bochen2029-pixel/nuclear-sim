#!/usr/bin/env bash
# Download the ENDF/B-VIII.0 raw neutron evaluations for the fast4 a-set from the
# public IAEA mirror (NOT ICSBEP -- within BLK-14). M1-T4a-2a. ~36 MB total.
# Usage: bash fetch_endf.sh [DEST]   (default: $HOME/nukesim_endf)
set -euo pipefail

DEST="${1:-$HOME/nukesim_endf}"
BASE="https://www-nds.iaea.org/public/download-endf/ENDF-B-VIII.0/n"
mkdir -p "$DEST"
cd "$DEST"

# a-set: <MAT>_<Z>-<Sym>-<A>. MATs verified against the IAEA listing.
FILES=(
  n_9225_92-U-234.zip
  n_9228_92-U-235.zip
  n_9237_92-U-238.zip
  n_9437_94-Pu-239.zip
  n_9440_94-Pu-240.zip
  n_9443_94-Pu-241.zip
  n_3125_31-Ga-69.zip
  n_3131_31-Ga-71.zip
)

for f in "${FILES[@]}"; do
  echo "-- fetching $f"
  curl -sSfL -o "$f" "$BASE/$f"
  python3 -c "import zipfile,sys; zipfile.ZipFile(sys.argv[1]).extractall('.')" "$f"
done

echo "== extracted ENDF files =="
ls -la "$DEST"/*.dat "$DEST"/*.endf 2>/dev/null || ls -la "$DEST"
echo "FETCH_OK"
