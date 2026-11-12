#!/usr/bin/env python3
"""Fail if a macOS binary requires a system symbol that an older macOS doesn't export.

Usage: check_macos_imports.py <Mach-O binary> <MacOSX10.13.sdk dir> [arch]

Reads the binary's undefined symbols (nm -m) and checks every non-weak one against the
exports listed in the SDK's .tbd stubs. Weak imports are allowed (they are optional at
load time). Exit code 1 lists the missing symbols.
"""
import glob, os, re, subprocess, sys

binary, sdk = sys.argv[1], sys.argv[2]
arch = sys.argv[3] if len(sys.argv) > 3 else "x86_64"

exports = set()
for tbd in glob.glob(os.path.join(sdk, "**", "*.tbd"), recursive=True):
    if not os.path.isfile(tbd):
        continue
    text = open(tbd, errors="ignore").read()
    for m in re.finditer(r"(symbols|objc-classes|objc-ivars|weak-def-symbols|thread-local-symbols|objc-eh-types)\s*:\s*\[([^\]]*)\]", text, re.S):
        kind = m.group(1)
        for t in re.split(r"[\s,]+", m.group(2)):
            t = t.strip().strip("'\"")
            if not t:
                continue
            if kind == "objc-classes":
                base = t if t.startswith("_") else "_" + t
                exports.add("_OBJC_CLASS_$" + base)
                exports.add("_OBJC_METACLASS_$" + base)
            else:
                exports.add(t)

out = subprocess.run(["nm", "-m", "-u", "-arch", arch, binary], capture_output=True, text=True, check=True).stdout
strong, weak, missing = 0, 0, []
for line in out.splitlines():
    m = re.search(r"\(undefined\)\s+(weak\s+)?external\s+(\S+)(?:\s+\(from\s+(\S+)\))?", line)
    if not m:
        continue
    is_weak, sym, lib = bool(m.group(1)), m.group(2), m.group(3) or "?"
    if is_weak:
        weak += 1
        continue
    strong += 1
    if sym not in exports:
        missing.append((lib, sym))

print(f"{os.path.basename(sdk)}: {len(exports)} exports; binary imports {strong} required + {weak} optional ({arch})")
if missing:
    print("REQUIRED symbols missing on the target macOS (would crash or fail to load):")
    for lib, sym in missing:
        print(f"  {sym}  (from {lib})")
    sys.exit(1)
print("OK: every required import exists on the target macOS")
