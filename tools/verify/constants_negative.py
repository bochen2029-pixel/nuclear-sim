#!/usr/bin/env python3
"""Negative tests for the constants generator (M0-T3-a DoD).

The DoD requires that gen_constants *fails* on a missing citation or status.
A validator is only worth its comments if something proves it rejects — this
asserts each guard rejects the malformed input it exists for, and that a
well-formed document still passes.

Exit 0 if every guard behaves; 1 with a report otherwise.
"""

from __future__ import annotations

import sys
from copy import deepcopy
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "gen_constants"))

from gen_constants import GenError, resolve_derived, validate  # noqa: E402

GOOD_CONSTANT = {
    "id": "C-001",
    "name": "example",
    "value": 2.0,
    "unit": "-",
    "status": "PUBLIC",
    "use": "compute",
    "cite": "Primer",
    "appendix_text": "2.0",
}


def doc(*entries: dict, kind: str = "constant") -> dict:
    return {"schema_version": 1, kind: [deepcopy(e) for e in entries]}


def without(field: str) -> dict:
    entry = deepcopy(GOOD_CONSTANT)
    entry.pop(field)
    return entry


def with_(**overrides: object) -> dict:
    entry = deepcopy(GOOD_CONSTANT)
    entry.update(overrides)
    return entry


# (label, document, should_raise)
CASES: list[tuple[str, dict, bool]] = [
    ("well-formed document passes", doc(GOOD_CONSTANT), False),

    # The two the DoD names explicitly.
    ("missing cite is rejected", doc(without("cite")), True),
    ("missing status is rejected", doc(without("status")), True),
    ("empty cite is rejected", doc(with_(cite="")), True),
    # 04 §1 lists unit alongside cite and status. A dimensionless entry writes
    # "-" explicitly, so an omitted unit is always a defect.
    ("missing unit is rejected", doc(without("unit")), True),

    ("unknown status is rejected", doc(with_(status="PROBABLY")), True),
    ("unknown use is rejected", doc(with_(use="vibes")), True),
    ("missing name is rejected", doc(without("name")), True),
    ("missing appendix_text is rejected", doc(without("appendix_text")), True),
    ("non-PENDING entry without a value is rejected", doc(without("value")), True),

    # PENDING may omit value but must name its resolving task (03 §1).
    ("PENDING without resolved_by is rejected",
     doc({**without("value"), "status": "PENDING"}), True),
    ("PENDING with resolved_by passes",
     doc({**without("value"), "status": "PENDING", "resolved_by": "M7-T2"}), False),

    ("value below its band is rejected", doc(with_(value=1.0, lo=1.5, hi=2.5)), True),
    ("value above its band is rejected", doc(with_(value=3.0, lo=1.5, hi=2.5)), True),
    ("lo without hi is rejected", doc(with_(lo=1.5)), True),
    ("inverted band is rejected", doc(with_(value=2.0, lo=2.5, hi=1.5)), True),

    ("duplicate id is rejected", doc(GOOD_CONSTANT, GOOD_CONSTANT), True),
    ("wrong schema_version is rejected", {"schema_version": 2, "constant": [GOOD_CONSTANT]}, True),
    ("empty document is rejected", {"schema_version": 1}, True),

    # A band carrying a nominal defeats the reason [[band]] exists (ADR-015).
    ("band with a value is rejected",
     doc({"id": "C-900", "name": "b", "lo": 1.0, "hi": 2.0, "value": 1.5,
          "status": "SIM", "cite": "x", "appendix_text": "[1.0, 2.0]"}, kind="band"), True),
    ("band with lo >= hi is rejected",
     doc({"id": "C-900", "name": "b", "lo": 2.0, "hi": 2.0,
          "status": "SIM", "cite": "x", "appendix_text": "[2.0, 2.0]"}, kind="band"), True),
    ("well-formed band passes",
     doc({"id": "C-900", "name": "b", "lo": 1.0, "hi": 2.0,
          "unit": "-", "status": "SIM", "cite": "x", "appendix_text": "[1.0, 2.0]"}, kind="band"), False),
    ("band without a unit is rejected",
     doc({"id": "C-900", "name": "b", "lo": 1.0, "hi": 2.0,
          "status": "SIM", "cite": "x", "appendix_text": "[1.0, 2.0]"}, kind="band"), True),

    ("registry without entries is rejected",
     doc({"id": "C-907", "name": "r", "status": "SIM", "cite": "x",
          "appendix_text": "a=1"}, kind="registry"), True),
    ("registry with a non-numeric entry is rejected",
     doc({"id": "C-907", "name": "r", "status": "SIM", "cite": "x",
          "appendix_text": "a=1", "entries": {"a": "one"}}, kind="registry"), True),
]

DERIVED_CASES: list[tuple[str, dict, bool]] = [
    ("derived referencing an unknown id is rejected",
     doc(with_(derived="C-999 * 2")), True),
    ("derived disagreeing with the stated value is rejected",
     doc(with_(id="C-002", name="two", value=99.0, derived="C-001 * 2"), GOOD_CONSTANT), True),
    ("derived agreeing with the stated value passes",
     doc(with_(id="C-002", name="two", value=4.0, derived="C-001 * 2"), GOOD_CONSTANT), False),
]


def run(label: str, document: dict, should_raise: bool, also_derive: bool) -> str | None:
    try:
        validate(document)
        if also_derive:
            resolve_derived(document)
    except GenError:
        return None if should_raise else f"{label}: rejected a document that should pass"
    return f"{label}: ACCEPTED a document that should be rejected" if should_raise else None


def main() -> int:
    failures = [
        problem
        for cases, derive in ((CASES, False), (DERIVED_CASES, True))
        for label, document, should_raise in cases
        if (problem := run(label, document, should_raise, derive))
    ]

    if failures:
        print(f"constants_negative: FAIL ({len(failures)} guard(s) misbehaved)", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1

    print(f"constants_negative: OK — {len(CASES) + len(DERIVED_CASES)} validator guards behave")
    return 0


if __name__ == "__main__":
    sys.exit(main())
