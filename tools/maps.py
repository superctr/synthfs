#!/usr/bin/env python3
# Copyright 2026 Ian Karlsson
# maps/*.json and maps/records/*.json made src/maps.cpp: each model's blocks, parameters, addresses and areas as
# tables (docs/maps.md)
import json
import pathlib
import re
import sys

root = pathlib.Path(__file__).resolve().parent.parent
FLAGS = (("signed", "SIGNED"), ("receive_only", "RECEIVE_ONLY"))
KNOWN = {"offset", "size", "bits", "name", "min", "max", "display", "signed", "field", "order", "frac", "receive_only"}


def param(p, where):
    if set(p) - KNOWN:
        sys.exit(f"{where}: {p['name']}: {sorted(set(p) - KNOWN)} not known")
    flags = [c for k, c in FLAGS if p.get(k)]
    if p.get("order", "be") not in ("be", "le"):
        sys.exit(f"{where}: {p['name']}: order {p['order']!r}")
    if p.get("order") == "le":
        flags.append("LOW_FIRST")
    bit, width = p.get("field", (0, 0))
    if "field" in p:
        flags.append("FIELD")
    return (f"{{ {p['offset']}, {p['size']}, {p['bits']}, {' | '.join(flags) or '0'}, {bit}, {width}, "
            f"{p.get('frac', 0)}, {p['min']}, {p['max']} }}")


def number(addr):
    n = 0
    for byte in addr:
        n = n * 128 + byte
    return n


out = [
    "// Made by tools/maps.py from maps/*.json and maps/records/*.json: not edited by hand.",
    '#include "maps.hpp"',
    "",
    "namespace syfs::map {",
    "",
]
tables, maps, strings = {}, [], {}


def table(kind, rows):
    """a table's name, one table for every list of the same rows"""
    body = "".join(f"\t{r},\n" for r in rows)
    if (kind, body) not in tables:
        name = f"{kind.lower()}s{len(tables)}"
        tables[kind, body] = name
        out.append(f"static const {kind} {name}[] = {{\n{body}}};\n")
    return tables[kind, body]


records = {}
for path in sorted((root / "maps/records").glob("*.json")):
    r = json.loads(path.read_text())
    if r.get("packing") != "bits":
        sys.exit(f"{path.name}: packing {r.get('packing')!r}")
    records[r["map"]] = r["blocks"]

for path in sorted((root / "maps").glob("*.json")):
    m = json.loads(path.read_text())
    packed = dict(records.get(path.stem, {}))
    # the blocks of parameters; a block of entries is a group, its blocks at their places within it
    ids = [k for k, b in m["blocks"].items() if "params" in b]
    blocks = []
    for k in ids:
        b = m["blocks"][k]
        for p in b["params"]:
            if p["offset"] + p["size"] > b["size"]:
                sys.exit(f"{path.name}: {k}: {p['name']} past the block's {b['size']}")
        params = table("Param", [param(p, f"{path.name}: {k}") for p in b["params"]])
        # the name: the parameters "... Name 1" on, a character each
        chars = [p for p in b["params"] if re.search(r"name \d+$", p["name"], re.I)]
        first = chars[0] if chars and chars[0]["name"].lower().endswith("name 1") else None
        if first and any(p["size"] != first["size"] or p["offset"] != first["offset"] + n * first["size"]
                         for n, p in enumerate(chars)):
            sys.exit(f"{path.name}: {k}: a name not in a row")
        name = f'{first["offset"]}, {len(chars)}, {first["size"]}' if first else "0, 0, 0"
        bytes_packed = packed.pop(k, 0)
        if bytes_packed and sum(p["bits"] * p["size"] for p in b["params"]) > 8 * bytes_packed:
            sys.exit(f"{path.name}: {k}: its parameters' bits are more than {bytes_packed} bytes")
        blocks.append(f'{{ "{k}", {b["size"]}, {params}, {len(b["params"])}, {name}, {bytes_packed} }}')
    if packed:
        sys.exit(f"maps/records/{path.name}: no blocks {sorted(packed)}")

    def leaves(block, addr):
        """the blocks of parameters under a block, each with its address"""
        b = m["blocks"][block]
        if "params" in b:
            return [(addr, block)]
        found = []
        for e in b["entries"]:
            found += leaves(e["block"], addr + number(e["addr"]))
        return found

    # an area is an entry of the top: a block, or a group's blocks; its bank the entry's name before a bracket, its
    # number its place among the bank's
    entries, areas, sizes, last, counts = [], [], set(), -1, {}
    for e in m["blocks"]["top"]["entries"]:
        sizes.add(len(e["addr"]))
        bank = e["name"].split(" (")[0]
        own = leaves(e["block"], number(e["addr"]))
        areas.append(f'{{ "{e["block"]}", "{bank}", {counts.get(bank, 0)}, 0x{number(e["addr"]):07x}, '
                     f"{len(entries)}, {len(own)} }}")
        counts[bank] = counts.get(bank, 0) + 1
        for addr, block in own:
            if addr <= last:
                sys.exit(f"{path.name}: {e['name']}: {block} not after the block before it")
            last = addr + m["blocks"][block]["size"] - 1
            entries.append(f"{{ 0x{addr:07x}, {ids.index(block)}, {len(areas) - 1} }}")
    if len(sizes) != 1 or len(m["model_id"]) > 4:
        sys.exit(f"{path.name}: addresses of {sorted(sizes)} bytes, a model ID of {len(m['model_id'])}")
    maps.append(f'{{ "{path.stem}", "{m["model"]}", {{ {", ".join(f"0x{b:02x}" for b in m["model_id"])} }}, '
                f'{len(m["model_id"])}, {sizes.pop()}, {table("Block", blocks)}, {len(blocks)}, '
                f'{table("Entry", entries)}, {len(entries)}, {table("Area", areas)}, {len(areas)} }}')

out.append("static const Map maps[] = {\n" + "".join(f"\t{r},\n" for r in maps) + "};\n")
out.append("""const Map *all(size_t &count)
{
	count = sizeof maps / sizeof *maps;
	return maps;
}

}
""")
(root / "src/maps.cpp").write_text("\n".join(out))
