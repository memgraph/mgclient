#!/usr/bin/env python3
"""Check the shared-library ABI against an in-tree baseline and verify that
SOVERSION / project VERSION are consistent with it.

The baseline is a snapshot of the last *released* ABI:

  * tool/mgclient.abi          `abidw` XML; embeds the released SONAME
                                  (e.g. soname='libmgclient.so.2').
  * tool/mgclient.abi.version  the released project version (one line).

Ordinary PRs must NOT regenerate the baseline: it is the fixed reference that
lets us detect an incompatible change shipped under an unchanged SOVERSION.
Refresh it only when intentionally changing the ABI / cutting a release, with
`tool/abicheck.py update ...`.

In `check` mode nothing is read from CMakeLists.txt: the *built* library is the
source of truth. SOVERSION comes from its ELF SONAME and the project version
from its own mg_client_version(). CMakeLists.txt is only *written*, by `update`.

Policy (a minor bump may carry an ABI break, so SOVERSION, not a semver major,
is the compatibility guard):

  * ABI incompatible vs baseline (symbol removed or signature changed)
      -> SOVERSION MUST be higher than the baseline's, AND
         project VERSION MUST be higher than the baseline's.        [hard fail]
  * ABI compatible additions vs baseline (only new symbols)
      -> project VERSION MUST be higher than the baseline's.        [hard fail]
         SOVERSION should be unchanged (a bump is unnecessary ->    [warning]).
  * No ABI change vs baseline
      -> nothing required.

The check splits into a pure policy core (`evaluate`) and an I/O shell (fact
gathering + rendering), so the policy is testable without building anything;
see tool/test_abicheck.py.

Usage:
  tool/abicheck.py                      # check working tree vs committed baseline
  tool/abicheck.py update               # refresh baseline from current tree
  tool/abicheck.py update --version 2.0.0 --soversion 3
                                        # set CMake vars, rebuild, refresh baseline

Exit: 0 = ok, 1 = policy violation, 2 = tooling/build error.
"""
from __future__ import annotations

import argparse
import ctypes
import os
import re
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from enum import Enum, IntFlag
from pathlib import Path
from typing import NoReturn

ROOT = Path(__file__).resolve().parent.parent
CMAKELISTS = ROOT / "CMakeLists.txt"
BASELINE = ROOT / "tool" / "mgclient.abi"
BASELINE_VER = ROOT / "tool" / "mgclient.abi.version"

EXIT_OK = 0
EXIT_POLICY = 1
EXIT_TOOLING = 2


class Abidiff(IntFlag):
    """abidiff exit-code bits (libabigail)."""

    ERROR = 1
    USAGE = 2
    CHANGE = 4
    INCOMPATIBLE = 8


class AbiChange(Enum):
    NONE = "no ABI change"
    COMPATIBLE = "compatible additions"
    INCOMPATIBLE = "incompatible change"


class Severity(Enum):
    FAIL = "FAIL"
    WARN = "WARN"


class Code(Enum):
    SOVERSION_NOT_BUMPED = "soversion-not-bumped"
    VERSION_NOT_BUMPED = "version-not-bumped"
    UNNECESSARY_SOVERSION_BUMP = "unnecessary-soversion-bump"


@dataclass(frozen=True)
class Finding:
    severity: Severity
    code: Code
    message: str


@dataclass(frozen=True)
class Versions:
    version: str  # project version, "X.Y.Z"
    soversion: int


# --- pure policy core (no I/O; see tool/test_abicheck.py) ------------------- #
def version_tuple(v: str) -> tuple[int, ...]:
    if not re.fullmatch(r"\d+(\.\d+)*", v):
        raise ValueError(f"not a numeric version: {v!r}")
    return tuple(int(x) for x in v.split("."))


def version_gt(a: str, b: str) -> bool:
    return version_tuple(a) > version_tuple(b)


def evaluate(change: AbiChange, base: Versions, built: Versions) -> list[Finding]:
    """Apply the versioning policy to a classified change. Returns the findings
    (empty = consistent). Pure: the result is a function of its arguments."""
    out: list[Finding] = []
    if change is AbiChange.INCOMPATIBLE:
        if not built.soversion > base.soversion:
            out.append(Finding(
                Severity.FAIL, Code.SOVERSION_NOT_BUMPED,
                f"ABI is incompatible with the last release but SOVERSION did "
                f"not increase ({base.soversion} -> {built.soversion}). Bump it, "
                f"then `tool/abicheck.py update`."))
        if not version_gt(built.version, base.version):
            out.append(Finding(
                Severity.FAIL, Code.VERSION_NOT_BUMPED,
                f"ABI is incompatible with the last release but project VERSION "
                f"did not increase ({base.version} -> {built.version}). Bump it."))
    elif change is AbiChange.COMPATIBLE:
        if not version_gt(built.version, base.version):
            out.append(Finding(
                Severity.FAIL, Code.VERSION_NOT_BUMPED,
                f"New public symbols were added since the last release but project "
                f"VERSION did not increase ({base.version} -> {built.version}). "
                f"Bump it (at least a minor)."))
        if built.soversion != base.soversion:
            out.append(Finding(
                Severity.WARN, Code.UNNECESSARY_SOVERSION_BUMP,
                f"SOVERSION was bumped ({base.soversion} -> {built.soversion}) but "
                f"the change is backward-compatible; a bump is unnecessary."))
    else:  # AbiChange.NONE
        if built.soversion != base.soversion:
            out.append(Finding(
                Severity.WARN, Code.UNNECESSARY_SOVERSION_BUMP,
                f"SOVERSION was bumped ({base.soversion} -> {built.soversion}) but "
                f"no ABI change was detected."))
    return out


def has_failure(findings: list[Finding]) -> bool:
    return any(f.severity is Severity.FAIL for f in findings)


# --- I/O shell ------------------------------------------------------------- #
def die(msg: str, code: int = EXIT_TOOLING) -> NoReturn:
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(code)


def run(cmd: list[str]) -> subprocess.CompletedProcess:
    # LC_ALL=C keeps parsed tool output locale-stable.
    return subprocess.run(cmd, capture_output=True, text=True,
                          env={**os.environ, "LC_ALL": "C"})


def require(*tools: str) -> None:
    for tool in tools:
        if shutil.which(tool) is None:
            die(f"{tool} not found (install abigail-tools / binutils)")


def build_lib(build_dir: Path) -> Path:
    """Configure + build libmgclient.so (RelWithDebInfo, so DWARF is present)."""
    jobs = str(os.cpu_count() or 4)
    for cmd in (
        ["cmake", "-B", str(build_dir), "-S", str(ROOT),
         "-DCMAKE_BUILD_TYPE=RelWithDebInfo", "-DBUILD_TESTING=OFF"],
        ["cmake", "--build", str(build_dir), "--target", "mgclient-shared",
         "--", "-j", jobs],
    ):
        res = run(cmd)
        if res.returncode != 0:
            sys.stderr.write(res.stdout + res.stderr)
            die("build failed")
    libs = [p for p in build_dir.rglob("libmgclient.so.*")
            if re.fullmatch(r"libmgclient\.so\.\d+", p.name)]
    if len(libs) != 1:
        die(f"expected exactly one libmgclient.so.<N>, found {len(libs)}")
    return libs[0]


def elf_soname(lib: Path) -> str:
    """SONAME string from the ELF dynamic section, e.g. 'libmgclient.so.2'."""
    m = re.search(r"Library soname: \[(.+?)\]", run(["readelf", "-d", str(lib)]).stdout)
    if not m:
        die(f"no SONAME in {lib}")
    return m.group(1)


def soversion_of(soname: str) -> int:
    m = re.search(r"\.so\.(\d+)$", soname)
    if not m:
        die(f"cannot parse SOVERSION from soname {soname!r}")
    return int(m.group(1))


def lib_version(lib: Path) -> str:
    """Project version compiled into the library, read by calling its own
    mg_client_version(). No fallback: a load failure is a hard error rather
    than a guess."""
    try:
        dll = ctypes.CDLL(str(lib))
    except OSError as e:
        die(f"cannot load {lib} to read its version: {e}")
    dll.mg_client_version.restype = ctypes.c_char_p
    v = dll.mg_client_version()
    if not v:
        die(f"mg_client_version() returned nothing for {lib}")
    return v.decode()


def baseline_soname() -> str:
    m = re.search(r"soname='(libmgclient\.so\.\d+)'", BASELINE.read_text())
    if not m:
        die(f"no soname in baseline {BASELINE}")
    return m.group(1)


def abidiff_rc(baseline: Path, lib: Path, *extra: str) -> tuple[int, str]:
    res = run(["abidiff", *extra, str(baseline), str(lib)])
    if res.returncode & (Abidiff.ERROR | Abidiff.USAGE):
        sys.stderr.write(res.stdout + res.stderr)
        die(f"abidiff failed to run (exit {res.returncode})")
    return res.returncode, res.stdout


def classify(baseline: Path, lib: Path) -> tuple[AbiChange, str]:
    """Classify the ABI change vs the baseline from abidiff exit codes.

    abidiff's INCOMPATIBLE exit bit under-reports: a changed parameter type is
    reported textually but leaves the bit clear, so trusting the bit would pass
    a real break. Instead, a second run with --no-added-syms drops additions
    from the exit code, so anything that still registers is a removal or a
    changed signature, i.e. incompatible. Returns the change and the
    human-readable diff (for the log)."""
    rc, report = abidiff_rc(baseline, lib)
    if rc == 0:
        return AbiChange.NONE, report
    strict_rc, _ = abidiff_rc(baseline, lib, "--no-added-syms")
    if strict_rc == 0:
        return AbiChange.COMPATIBLE, report
    return AbiChange.INCOMPATIBLE, report


def read_versions_or_die(label: str, version: str, soversion: int) -> Versions:
    try:
        version_tuple(version)
    except ValueError as e:
        die(f"{label} version: {e}")
    return Versions(version, soversion)


def cmd_check(_args: argparse.Namespace) -> None:
    require("abidiff", "readelf")
    if not (BASELINE.exists() and BASELINE_VER.exists()):
        die("no ABI baseline found. Create it with: tool/abicheck.py update")

    base = read_versions_or_die(
        "baseline", BASELINE_VER.read_text().strip(), soversion_of(baseline_soname()))
    print(f">> baseline version/soversion:  {base.version} / {base.soversion}")

    with tempfile.TemporaryDirectory() as bd:
        lib = build_lib(Path(bd))
        built = read_versions_or_die(
            "built", lib_version(lib), soversion_of(elf_soname(lib)))
        print(f">> built    version/soversion:  {built.version} / {built.soversion}")
        print(">> abidiff (baseline -> built library)")
        change, report = classify(BASELINE, lib)

    print(report, end="")
    print(f">> classification: {change.value}")

    findings = evaluate(change, base, built)
    for f in findings:
        print(f"{f.severity.value}:  {f.message}")

    if has_failure(findings):
        print(">> RESULT: versioning is INCONSISTENT with the ABI change.")
        sys.exit(EXIT_POLICY)
    print(">> RESULT: versioning is consistent with the ABI baseline.")


def set_cmake_var(pattern: str, replacement: str, label: str) -> None:
    new, n = re.subn(pattern, replacement, CMAKELISTS.read_text(), count=1)
    if n != 1:
        die(f"could not update {label} in {CMAKELISTS}")
    CMAKELISTS.write_text(new)
    print(f">> set {label}")


def cmd_update(args: argparse.Namespace) -> None:
    require("abidw", "readelf")
    if args.version:
        if not re.fullmatch(r"\d+\.\d+\.\d+", args.version):
            die("--version must be X.Y.Z")
        set_cmake_var(r"project\(mgclient VERSION [0-9]+\.[0-9]+\.[0-9]+",
                      f"project(mgclient VERSION {args.version}",
                      f"project VERSION = {args.version}")
    if args.soversion is not None:
        set_cmake_var(r"set\(mgclient_SOVERSION [0-9]+\)",
                      f"set(mgclient_SOVERSION {args.soversion})",
                      f"mgclient_SOVERSION = {args.soversion}")

    with tempfile.TemporaryDirectory() as bd:
        lib = build_lib(Path(bd))
        res = run(["abidw", "--no-corpus-path", "--no-show-locs",
                   "--out-file", str(BASELINE), str(lib)])
        if res.returncode != 0:
            sys.stderr.write(res.stdout + res.stderr)
            die("abidw failed")
        BASELINE_VER.write_text(lib_version(lib) + "\n")

    so = soversion_of(baseline_soname())
    ver = BASELINE_VER.read_text().strip()
    print(f">> baseline refreshed: version {ver}, soname libmgclient.so.{so}")
    print(f">> commit {BASELINE.relative_to(ROOT)}, "
          f"{BASELINE_VER.relative_to(ROOT)} and CMakeLists.txt together.")


def main() -> None:
    p = argparse.ArgumentParser(description="mgclient ABI/version consistency gate")
    sub = p.add_subparsers(dest="cmd")
    sub.add_parser("check", help="check working tree vs committed baseline (default)")
    up = sub.add_parser("update", help="rebuild and refresh the baseline (+ optionally set CMake vars)")
    up.add_argument("--version", help="set project VERSION (X.Y.Z) in CMakeLists.txt")
    up.add_argument("--soversion", type=int, help="set mgclient_SOVERSION in CMakeLists.txt")
    args = p.parse_args()
    (cmd_update if args.cmd == "update" else cmd_check)(args)


if __name__ == "__main__":
    main()
