#!/usr/bin/env python3
"""Assert the bijection between appendix/constants.md and constants.data.toml (03 §1).

The human appendix is what a reader trusts; the strict file is what the code
compiles. This asserts they cannot disagree:

  1. every appendix table row has exactly one strict entry, and vice versa
  2. each entry's `appendix_text` matches the appendix Value cell verbatim
     (after markdown normalisation) — so widening "1.7–1.8" to "1.7–1.9" in
     the appendix fails here instead of silently not reaching the code
  3. unit / status / cite agree wherever the appendix carries those columns
  4. the numeric value agrees wherever the Value cell parses as a number
  5. every species named in data/materials/*.json resolves to a molar mass —
     a HARD error per appendix §3, because a missing mass silently corrupts
     every macroscopic cross section (04 §5)

Exit 0 on success, 1 with a report of every mismatch found (not just the first).
"""

from __future__ import annotations

import json
import re
import sys
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
APPENDIX = ROOT / "spec" / "appendix" / "constants.md"
STRICT = ROOT / "spec" / "appendix" / "constants.data.toml"
MATERIALS = ROOT / "data" / "materials"

# Value is column 2 in every appendix table; the rest vary by section.
# None means "the appendix does not carry that column here".
SECTION_COLUMNS = {
    1: {"value": 2, "unit": 3, "status": 4, "cite": 6},
    2: {"value": 2, "unit": None, "status": 4, "cite": 5, "mass": 3},
    3: {"value": 2, "unit": 3, "status": 4, "cite": 5},
    4: {"value": 2, "unit": None, "status": None, "cite": None},
    5: {"value": 2, "unit": None, "status": None, "cite": 3},
}

ROW = re.compile(r"^\|\s*(C-\d+[a-z]?)\s*\|")
HEADING = re.compile(r"^##\s+(\d+)\.")


def normalise(text: str) -> str:
    """Strip markdown emphasis/code ticks and collapse whitespace."""
    return re.sub(r"\s+", " ", text.replace("**", "").replace("`", "")).strip()


def normalise_unit(text: str) -> str:
    """Treat the appendix's dimensionless dash and a plain hyphen as the same."""
    t = normalise(text)
    return "-" if t in {"—", "–", "-", ""} else t


def parse_appendix() -> dict[str, dict]:
    rows: dict[str, dict] = {}
    section = 0
    for line in APPENDIX.read_text(encoding="utf-8").splitlines():
        heading = HEADING.match(line)
        if heading:
            section = int(heading.group(1))
            continue
        match = ROW.match(line)
        if not match or section not in SECTION_COLUMNS:
            continue
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        cols = SECTION_COLUMNS[section]
        cid = cells[0]
        if cid in rows:
            raise SystemExit(f"constants_roundtrip: appendix has duplicate row {cid}")
        rows[cid] = {
            "section": section,
            "value_text": cells[cols["value"]],
            "unit": cells[cols["unit"]] if cols["unit"] is not None else None,
            "status": cells[cols["status"]] if cols["status"] is not None else None,
            "cite": cells[cols["cite"]] if cols["cite"] is not None else None,
            "mass_text": cells[cols["mass"]] if cols.get("mass") is not None else None,
        }
    return rows


_NUM = r"[-+]?\d+(?:\.\d+)?(?:[eE][-+]?\d+)?"
# The appendix writes ranges with an en-dash, so "16–17" is a range while
# "1.8 (1.7–1.8)" is a nominal followed by one. Anchoring at the start keeps
# those apart.
_RANGE = re.compile(rf"^({_NUM})\s*–\s*({_NUM})")


def leading_number(text: str) -> float | None:
    """First number in a Value cell, or None if the cell is not numeric-led."""
    match = re.match(rf"^[±]?\s*({_NUM})", normalise(text))
    return float(match.group(1)) if match else None


def leading_range(text: str) -> tuple[float, float] | None:
    """(lo, hi) if the cell is a bare range with no nominal, else None.

    Such a cell has no meaningful `value`; the strict entry records the band
    and a midpoint, so the band is what must agree with the appendix.
    """
    match = _RANGE.match(normalise(text))
    return (float(match.group(1)), float(match.group(2))) if match else None


def load_strict() -> tuple[dict[str, dict], dict]:
    with STRICT.open("rb") as handle:
        doc = tomllib.load(handle)
    entries: dict[str, dict] = {}
    for kind in ("constant", "band", "registry"):
        for entry in doc.get(kind, []):
            entry["_kind"] = kind
            entries[entry["id"]] = entry
    return entries, doc


def check_materials(entries: dict[str, dict], problems: list[str]) -> int:
    """Every species named by a material must resolve to a molar mass."""
    known = {e["species"] for e in entries.values() if "species" in e}
    files = sorted(MATERIALS.glob("*.json")) if MATERIALS.is_dir() else []
    for path in files:
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except json.JSONDecodeError as exc:
            problems.append(f"{path.name}: not valid JSON ({exc})")
            continue
        # 03 §3 names this table `isotopes` (atom fractions). This check read
        # `composition` and was vacuous until M1-T4a-1 added the first material
        # files, at which point it failed on correct data — a verifier with no
        # data to verify proves nothing about itself.
        composition = data.get("isotopes")
        if not isinstance(composition, dict):
            problems.append(f"{path.name}: no 'isotopes' table (03 §3)")
            continue
        for species in composition:
            if species not in known:
                problems.append(
                    f"{path.name}: species {species!r} has no molar-mass constant "
                    f"(appendix §3 completeness rule — hard error, not a warning)"
                )
    return len(files)


def main() -> int:
    appendix = parse_appendix()
    entries, _doc = load_strict()
    problems: list[str] = []

    for cid in sorted(set(appendix) - set(entries)):
        problems.append(f"{cid}: in appendix/constants.md but missing from constants.data.toml")
    for cid in sorted(set(entries) - set(appendix)):
        problems.append(f"{cid}: in constants.data.toml but has no appendix row")

    for cid in sorted(set(appendix) & set(entries)):
        row, entry = appendix[cid], entries[cid]

        want, got = normalise(row["value_text"]), normalise(str(entry["appendix_text"]))
        if want != got:
            problems.append(f"{cid}: appendix_text drift\n    appendix: {want!r}\n    strict:   {got!r}")

        if row["unit"] is not None and "unit" in entry:
            if normalise_unit(row["unit"]) != normalise_unit(str(entry["unit"])):
                problems.append(f"{cid}: unit {entry['unit']!r} != appendix {row['unit']!r}")

        if row["status"] is not None:
            if normalise(row["status"]) != normalise(str(entry["status"])):
                problems.append(f"{cid}: status {entry['status']!r} != appendix {row['status']!r}")

        if row["cite"] is not None:
            if normalise(row["cite"]) != normalise(str(entry["cite"])):
                problems.append(
                    f"{cid}: cite drift\n    appendix: {normalise(row['cite'])!r}"
                    f"\n    strict:   {normalise(str(entry['cite']))!r}"
                )

        # Appendix §2 carries a Mass column alongside the OD. It is tabular data,
        # not prose, so it gets the same verbatim drift check — the pit's 6.15 kg
        # is a DECLASSIFIED authoritative value that invariant 4 depends on.
        if row.get("mass_text") is not None:
            want_mass = normalise(row["mass_text"])
            got_mass = normalise(str(entry.get("appendix_mass_text", "")))
            if want_mass != got_mass:
                problems.append(
                    f"{cid}: appendix mass drift\n    appendix: {want_mass!r}"
                    f"\n    strict:   {got_mass!r}")

        if entry["_kind"] not in ("constant", "band") or entry.get("derived"):
            continue  # registries are text-compared; derived is checked by the generator

        span = leading_range(row["value_text"])
        if span is not None:
            for field, want in (("lo", span[0]), ("hi", span[1])):
                got = entry.get(field)
                if got is None or abs(got - want) > 1e-9 * max(1.0, abs(want)):
                    problems.append(f"{cid}: {field} {got} != appendix range {field} {want}")
            continue

        number = leading_number(row["value_text"])
        if number is not None and "value" in entry:
            if abs(entry["value"] - number) > 1e-9 * max(1.0, abs(number)):
                problems.append(f"{cid}: value {entry['value']} != appendix leading number {number}")

    n_materials = check_materials(entries, problems)

    if problems:
        print(f"constants_roundtrip: FAIL ({len(problems)} problem(s))", file=sys.stderr)
        for problem in problems:
            print(f"  - {problem}", file=sys.stderr)
        return 1

    print(
        f"constants_roundtrip: OK — {len(appendix)} appendix rows <-> {len(entries)} strict "
        f"entries, bijective; {n_materials} material file(s) checked for molar-mass coverage"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
