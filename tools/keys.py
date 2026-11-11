#!/usr/bin/env python3
# Copyright 2026 Ian Karlsson
# src/keys.def made include/synthfs/syfs_keys.h: each name's constant, the low 31 bits of its string's FNV-1a hash
import pathlib
import shlex
import sys

root = pathlib.Path(__file__).resolve().parent.parent


def key_of(name):
    h = 0x811C9DC5
    for b in name.encode():
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h & 0x7FFFFFFF


rows, seen = [], {}
for n, line in enumerate((root / "src/keys.def").read_text().splitlines(), 1):
    f = shlex.split(line, comments=True)
    if not f:
        continue
    if len(f) not in (3, 4):
        sys.exit(f"keys.def:{n}: class, constant, string and perhaps a title")
    cls, ident, name = f[:3]
    title = f[3] if len(f) == 4 else name
    k = key_of(name)
    if k == 0 or seen.get(k, name) != name:
        sys.exit(f"keys.def:{n}: {name!r} hashes as {seen.get(k, 'none')!r} does")
    seen[k] = name
    rows.append((cls, f"SYFS_{cls.upper()}_{ident}", name, title, k))

out = [
    "// Made by tools/keys.py from src/keys.def: not edited by hand.",
    "",
    "//! \\file syfs_keys.h",
    "//! Every key the library knows, by its constant.",
    "/*!",
    " * A key is the low 31 bits of its name's 32-bit FNV-1a hash, so a name's key never changes and a host may hash a",
    " * name itself (syfs_key_of does).  Names of two classes that read the same share their key.",
    " */",
    "#ifndef SYFS_KEYS_H",
    "#define SYFS_KEYS_H",
    "",
    "//! The keys: SYFS_<CLASS>_<NAME>, each with its string.",
    "enum syfs_keys {",
    "\tSYFS_NONE = 0,                            //!< None or unknown.",
]
cls = None
for c, ident, name, title, k in rows:
    if c != cls:
        out.append("")
        cls = c
    out.append(f"\t{ident} = 0x{k:08x},{' ' * max(1, 28 - len(ident))}//!< \"{name}\"")
out += ["};", "", "//! Every key once: X(constant, class, name, title).", "#define SYFS_KEYS(X) \\"]
out += [f'\tX({ident}, "{c}", "{name}", "{title}") \\' for c, ident, name, title, k in rows]
out[-1] = out[-1][:-2]
out += ["", "#endif", ""]
(root / "include/synthfs/syfs_keys.h").write_text("\n".join(out))
