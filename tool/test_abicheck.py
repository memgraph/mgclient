"""Unit tests for the pure policy core of abicheck.py.

These exercise `evaluate()` and the version helpers directly, with no build and
no abidiff, so the whole policy matrix runs instantly. The I/O shell (build,
classify, ELF/ctypes reads) is covered by running `tool/abicheck.py` in CI.
"""
import pytest

from abicheck import (
    AbiChange,
    Code,
    Severity,
    Versions,
    evaluate,
    has_failure,
    version_gt,
    version_tuple,
)


def codes(findings):
    return {f.code for f in findings}


# --- evaluate(): the full policy matrix ------------------------------------ #
@pytest.mark.parametrize(
    "change, base, built, expected",
    [
        # incompatible: both SOVERSION and VERSION must increase
        (AbiChange.INCOMPATIBLE, Versions("1.7.0", 2), Versions("2.0.0", 3), set()),
        (AbiChange.INCOMPATIBLE, Versions("1.7.0", 2), Versions("2.0.0", 2),
         {Code.SOVERSION_NOT_BUMPED}),
        (AbiChange.INCOMPATIBLE, Versions("1.7.0", 2), Versions("1.7.0", 3),
         {Code.VERSION_NOT_BUMPED}),
        (AbiChange.INCOMPATIBLE, Versions("1.7.0", 2), Versions("1.7.0", 2),
         {Code.SOVERSION_NOT_BUMPED, Code.VERSION_NOT_BUMPED}),
        # compatible additions: VERSION must increase, SOVERSION should not
        (AbiChange.COMPATIBLE, Versions("1.6.0", 2), Versions("1.7.0", 2), set()),
        (AbiChange.COMPATIBLE, Versions("1.6.0", 2), Versions("1.6.0", 2),
         {Code.VERSION_NOT_BUMPED}),
        (AbiChange.COMPATIBLE, Versions("1.6.0", 2), Versions("1.7.0", 3),
         {Code.UNNECESSARY_SOVERSION_BUMP}),
        # no change: nothing required, but a stray SOVERSION bump warns
        (AbiChange.NONE, Versions("1.7.0", 2), Versions("1.7.0", 2), set()),
        (AbiChange.NONE, Versions("1.7.0", 2), Versions("1.8.0", 3),
         {Code.UNNECESSARY_SOVERSION_BUMP}),
    ],
)
def test_evaluate_matrix(change, base, built, expected):
    assert codes(evaluate(change, base, built)) == expected


def test_unnecessary_soversion_bump_is_warning_not_failure():
    findings = evaluate(AbiChange.COMPATIBLE, Versions("1.6.0", 2), Versions("1.7.0", 3))
    assert not has_failure(findings)
    assert all(f.severity is Severity.WARN for f in findings)


def test_soversion_not_bumped_is_failure():
    findings = evaluate(AbiChange.INCOMPATIBLE, Versions("1.7.0", 2), Versions("2.0.0", 2))
    assert has_failure(findings)


# --- version helpers ------------------------------------------------------- #
@pytest.mark.parametrize(
    "a, b, expected",
    [
        ("1.7.0", "1.6.0", True),
        ("1.6.0", "1.7.0", False),
        ("1.7.0", "1.7.0", False),
        ("1.10.0", "1.9.0", True),   # numeric, not lexical
        ("2.0.0", "1.99.99", True),
    ],
)
def test_version_gt(a, b, expected):
    assert version_gt(a, b) is expected


@pytest.mark.parametrize("bad", ["1.7.0-rc1", "v1.7.0", "1.7.x", ""])
def test_version_tuple_rejects_non_numeric(bad):
    with pytest.raises(ValueError):
        version_tuple(bad)
