#!/usr/bin/env python3
"""Synthetic HLE names for compiling the real mixer without proprietary assets.

Only addresses are synthetic. Xbox 3936/3948 mix-bin ABI constants match PGR1.
Tests call production C++ functions and own guest memory, never these addresses.
"""
import re
import sys
from pathlib import Path

source = Path(sys.argv[1]).read_text(encoding="utf-8")
names = set(re.findall(r"(?:SYM|GUEST)\((\w+)\)", source))
names.update(re.findall(r"HLE_(?:STDCALL|CDECL|FASTCALL|WRAP)\((\w+)\s*,", source))
names.update("IDirectSoundBuffer_" + n for n in re.findall(r"\bBUFFER\((\w+)\s*,", source))
names.update("CDirectSoundStream_" + n for n in re.findall(r"\bSTREAM\((\w+)\s*,", source))
names.discard("name")
header = ["#pragma once", "// Synthetic test addresses; no XBE or generated game code required."]
for i, name in enumerate(sorted(names)):
    header += [f"#define SYM_{name} {0x100000 + i * 16:08X}", f"#define HAS_{name} 1"]
header += ["#define XDK_DSound_MixBinHeadroom 1", "#define XDK_DSound_MixBinMask 1",
           "#define XDK_DSound_LockFlagsSwapped 1"]
destination = Path(sys.argv[2])
destination.parent.mkdir(parents=True, exist_ok=True)
destination.write_text("\n".join(header) + "\n", encoding="utf-8")
