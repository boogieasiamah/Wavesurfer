#!/usr/bin/env python3
"""Fail if a macOS binary needs a system symbol that an older macOS doesn't export.

Usage: check_macos_imports.py <Mach-O binary> <MacOSX10.13.sdk dir> [arch]

What counts is what dyld actually resolves: the symbols named in the binary's bind and
lazy-bind tables (LC_DYLD_INFO). Each one that is not a weak import must be exported by
the SDK's .tbd stubs, or the plugin fails to load (non-lazy) or aborts on first call (lazy).

Undefined symbols that appear in the symbol table but in no bind table are ignored: no
fixup refers to them, so dyld never looks them up. Apple's linker leaves such entries
behind after LTO dead-strips the code that used them (JUCE 8: kIOMainPortDefault,
os_workgroup_*). Flux 512 v0.2.3 has the same four entries and loads on macOS 10.13.

Binaries with chained fixups (deployment target 12+) fall back to the strict nm check.
Exit code 1 lists the missing symbols.
"""
import glob, os, re, shutil, subprocess, sys

binary, sdk = sys.argv[1], sys.argv[2]
arch = sys.argv[3] if len(sys.argv) > 3 else "x86_64"


def tool(*names):
    for n in names:
        if shutil.which(n):
            return n
    sys.exit("missing tool: " + " or ".join(names))


def run(cmd):
    return subprocess.run(cmd, capture_output=True, text=True, check=True).stdout


NM = tool("llvm-nm", "nm")
OBJDUMP = tool("llvm-objdump", "objdump")
arch_flag = ["--arch=" + arch]

# --- SDK exports -------------------------------------------------------------------------
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

# --- undefined symbols and their weak flag (nm -m) ----------------------------------------
undefined = {}  # sym -> (is_weak, lib)
for line in run([NM, "-m", "-u"] + arch_flag + [binary]).splitlines():
    m = re.search(r"\(undefined\)\s+(weak\s+)?external\s+(\S+)(?:\s+\(from\s+(\S+)\))?", line)
    if m:
        undefined[m.group(2)] = (bool(m.group(1)), m.group(3) or "?")

# --- symbols dyld actually binds -----------------------------------------------------------
headers = run([OBJDUMP, "--macho", "--private-headers"] + arch_flag + [binary])
chained = "LC_DYLD_CHAINED_FIXUPS" in headers
if chained:
    print("note: chained fixups; checking every undefined symbol (strict)")
    bound = set(undefined)
else:
    bound = set()
    for opt in ("--bind", "--lazy-bind"):
        for line in run([OBJDUMP, "--macho", opt] + arch_flag + [binary]).splitlines():
            f = line.split()
            if len(f) >= 5 and f[0].startswith("__") and f[-1].startswith("_"):
                bound.add(f[-1])
            elif len(f) >= 6 and f[0].startswith("__") and f[-2].startswith("_") and "weak" in f[-1]:
                bound.add(f[-2])

strong, weak, unreferenced, missing = 0, 0, [], []
for sym, (is_weak, lib) in sorted(undefined.items()):
    if sym not in bound:
        if not is_weak and sym not in exports:
            unreferenced.append((lib, sym))
        continue
    if is_weak:
        weak += 1
        continue
    strong += 1
    if sym not in exports:
        missing.append((lib, sym))

print(f"{os.path.basename(sdk)}: {len(exports)} exports; dyld binds {strong} required + {weak} optional "
      f"({arch}); {len(undefined) - strong - weak} undefined entries have no fixup")
if unreferenced:
    print("Not on the target macOS but never bound by dyld (harmless):")
    for lib, sym in unreferenced:
        print(f"  {sym}  (from {lib})")
if missing:
    print("REQUIRED symbols missing on the target macOS (would crash or fail to load):")
    for lib, sym in missing:
        print(f"  {sym}  (from {lib})")
    sys.exit(1)
print("OK: every symbol dyld binds exists on the target macOS")
