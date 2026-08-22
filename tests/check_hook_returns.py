#!/usr/bin/env python3
"""
Guard against returning into a hook's own patched bytes.

Syringe implements DEFINE_HOOK(addr, name, size) by writing a JMP over
[addr, addr+size). Returning an address inside that range lands control in the
middle of Syringe's own JMP, which executes a fragment of the patch as an
instruction and jumps somewhere arbitrary.

This is not theoretical. FreeUnitExt shipped exactly that bug at 0x740801:

    DEFINE_HOOK(0x740801, ..., 0x5)   ; patches 0x740801..0x740805
    ...
    return 0x740805;                  ; the LAST BYTE of the patch

which crashed as `Exception code: C0000005 at 00005280`. The wild EIP made it
look like memory corruption; it was a one-byte addressing mistake.

The failure is silent at compile time and only reproduces when that exact code
path runs, so it belongs in CI rather than in a reviewer's head.

Run: python3 tests/check_hook_returns.py
"""
import re
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parent.parent / "src"

HOOK = re.compile(
    r"DEFINE_HOOK(?:_AGAIN)?\s*\(\s*(0x[0-9A-Fa-f]+)\s*,\s*(\w+)\s*,\s*(0x[0-9A-Fa-f]+|\d+)\s*\)"
)
# Literal addresses that could be returned, whether written inline or via an
# enum/constant defined in the same function.
ADDR = re.compile(r"0x4[0-9A-Fa-f]{5}|0x7[0-9A-Fa-f]{5}")


def main() -> int:
    failures = []
    checked = 0

    for path in sorted(SRC.rglob("*.cpp")):
        text = path.read_text(encoding="utf-8")
        hooks = list(HOOK.finditer(text))

        for i, match in enumerate(hooks):
            addr = int(match.group(1), 16)
            name = match.group(2)
            size = int(match.group(3), 16 if match.group(3).startswith("0x") else 10)
            checked += 1

            # Body runs to the next hook (or EOF) — good enough, and it errs
            # toward checking too much rather than too little.
            end = hooks[i + 1].start() if i + 1 < len(hooks) else len(text)
            body = text[match.end():end]

            # Strip comments so prose about the bug does not trip the check.
            body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
            body = re.sub(r"//[^\n]*", "", body)

            for found in ADDR.finditer(body):
                target = int(found.group(0), 16)
                if addr <= target < addr + size:
                    line = text[:match.end() + found.start()].count("\n") + 1
                    failures.append(
                        f"{path.relative_to(SRC.parent)}:{line}: {name} "
                        f"@ {hex(addr)} size {hex(size)} references {hex(target)}, "
                        f"which is INSIDE its own patched range "
                        f"[{hex(addr)}, {hex(addr + size)})"
                    )

    if failures:
        print("FAIL — hook returns into its own stolen bytes:\n")
        for f in failures:
            print("  " + f)
        print(f"\n{len(failures)} problem(s) across {checked} hook(s).")
        print("Return 0 (re-executes the stolen bytes) or an address >= addr+size.")
        return 1

    print(f"ok — {checked} hooks checked, none return into their own patched range")
    return 0


if __name__ == "__main__":
    sys.exit(main())
