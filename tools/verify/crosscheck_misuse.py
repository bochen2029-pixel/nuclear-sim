#!/usr/bin/env python3
"""Static check: no compute-path source references a crosscheck constant.

01 §8.3 and 11 §4 require this. C-042/C-043 (kt/kg) use the Sher & Beck total
energy basis, ~5% above C-040's prompt-deposited basis; C-050/C-090/C-09x are
published figures the simulation is supposed to *predict*, not consume. Letting
one into a tally or a gate would make the model agree with the answer by
construction — the failure mode that is invisible in review and fatal to every
claim built on it.

04 §1 puts them in `ns::consts::crosscheck` precisely so this is greppable.

Compute paths are everything under src/ except the readout surfaces listed in
READOUT_ALLOWED. Tests may reference crosscheck constants freely — comparing
against them is what they are for.

Exit 0 if clean, 1 with file:line for every violation.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "src"

# Readout surfaces: they display crosscheck values next to computed ones, which
# is the sanctioned use. Physics never lives here (D7 — a physics calculation in
# src/app/ is already a defect on its own terms).
READOUT_ALLOWED = (
    "src/app/nukestudio/",
    "src/app/nukecinema/",
)

SUFFIXES = {".h", ".hpp", ".cuh", ".cpp", ".cu", ".cc"}
REFERENCE = re.compile(r"\bcrosscheck::")


def is_readout(path: Path) -> bool:
    rel = path.relative_to(ROOT).as_posix()
    return any(rel.startswith(prefix) for prefix in READOUT_ALLOWED)


def main() -> int:
    if not SRC.is_dir():
        print("crosscheck_misuse: OK — no src/ tree yet, nothing to check")
        return 0

    violations: list[str] = []
    scanned = 0
    for path in sorted(SRC.rglob("*")):
        if path.suffix not in SUFFIXES or not path.is_file():
            continue
        # The generated header is where the namespace is DEFINED.
        if path.name in {"constants.h", "constants.cuh"}:
            continue
        if is_readout(path):
            continue
        scanned += 1
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            if line.lstrip().startswith("//"):
                continue
            if REFERENCE.search(line):
                rel = path.relative_to(ROOT).as_posix()
                violations.append(f"{rel}:{number}: {line.strip()}")

    if violations:
        print(
            f"crosscheck_misuse: FAIL — {len(violations)} compute-path reference(s) to a "
            "crosscheck constant (01 §8.3, 11 §4)",
            file=sys.stderr,
        )
        for violation in violations:
            print(f"  - {violation}", file=sys.stderr)
        return 1

    print(f"crosscheck_misuse: OK — {scanned} compute-path source(s) scanned, no references")
    return 0


if __name__ == "__main__":
    sys.exit(main())
