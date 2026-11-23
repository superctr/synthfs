#!/usr/bin/env python3
# Copyright 2026 Ian Karlsson
# maps/*.json made src/maps.cpp: each model's blocks, parameters and addresses as tables (docs/maps.md)
import json
import pathlib
import re
import sys

root = pathlib.Path(__file__).resolve().parent.parent
FLAGS = (("signed", "SIGNED"), ("receive_only", "RECEIVE_ONLY"))


def param(p, where):
    known = {"offset", "size", "bits", "name", "min", "max", "display", "signed", "field", "order", "frac", "receive_only"}
    if set(p) - known:
        sys.exit(f"{where}: {p['name']}: {sorted(set(p) - known)} not known")
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


out = [
    "// Made by tools/maps.py from maps/*.json: not edited by hand.",
    '#include "maps.hpp"',
    "",
    "namespace syfs::map {",
    "",
]
tables, maps = {}, []


def table(kind, rows):
    """a table's name, one table for every list of the same rows"""
    body = "".join(f"\t{r},\n" for r in rows)
    if (kind, body) not in tables:
        name = f"{kind.lower()}s{len(tables)}"
        tables[kind, body] = name
        out.append(f"static const {kind} {name}[] = {{\n{body}}};\n")
    return tables[kind, body]


for path in sorted((root / "maps").glob("*.json")):
    m = json.loads(path.read_text())
    ids = [k for k in m["blocks"] if k != "top"]
    blocks = []
    for k in ids:
        b = m["blocks"][k]
        for p in b["params"]:
            if p["offset"] + p["size"] > b["size"]:
                sys.exit(f"{path.name}: {k}: {p['name']} past the block's {b['size']}")
        params = table("Param", [param(p, f"{path.name}: {k}") for p in b["params"]])
        # the name: the parameters "... Name 1" on, a character each
        chars = [p for p in b["params"] if re.search(r"Name \d+$", p["name"])]
        first = chars[0] if chars and chars[0]["name"].endswith("Name 1") else None
        if first and any(p["size"] != first["size"] or p["offset"] != first["offset"] + n * first["size"]
                         for n, p in enumerate(chars)):
            sys.exit(f"{path.name}: {k}: a name not in a row")
        name = f'{first["offset"]}, {len(chars)}, {first["size"]}' if first else "0, 0, 0"
        blocks.append(f'{{ "{k}", {b["size"]}, {params}, {len(b["params"])}, {name} }}')
    entries, sizes, last = [], set(), -1
    for e in m["blocks"]["top"]["entries"]:
        addr = 0
        for byte in e["addr"]:
            addr = addr * 128 + byte
        sizes.add(len(e["addr"]))
        if entries and addr <= last:
            sys.exit(f"{path.name}: {e['name']} not after the entry before it")
        last = addr
        entries.append(f"{{ 0x{addr:07x}, {ids.index(e['block'])} }}")
    if len(sizes) != 1 or len(m["model_id"]) > 4:
        sys.exit(f"{path.name}: addresses of {sorted(sizes)} bytes, a model ID of {len(m['model_id'])}")
    maps.append(f'{{ "{path.stem}", "{m["model"]}", {{ {", ".join(f"0x{b:02x}" for b in m["model_id"])} }}, '
                f'{len(m["model_id"])}, {sizes.pop()}, {table("Block", blocks)}, {len(blocks)}, '
                f'{table("Entry", entries)}, {len(entries)} }}')

out.append("static const Map maps[] = {\n" + "".join(f"\t{r},\n" for r in maps) + "};\n")
out.append("""const Block *Map::block(std::string_view id) const
{
	for (uint32_t n = 0; n < block_count; n++)
		if (id == blocks[n].id)
			return &blocks[n];
	return nullptr;
}

const Entry *Map::at(const Block *b, uint32_t n) const
{
	for (uint32_t k = 0; k < entry_count; k++)
		if (&blocks[entries[k].block] == b && !n--)
			return &entries[k];
	return nullptr;
}

uint32_t Map::places(const Block *b) const
{
	uint32_t n = 0;
	for (uint32_t k = 0; k < entry_count; k++)
		n += &blocks[entries[k].block] == b;
	return n;
}

// the entries are in the order of their addresses
const Entry *Map::entry_of(uint32_t addr) const
{
	uint32_t lo = 0, hi = entry_count;
	while (lo < hi) {
		const uint32_t mid = (lo + hi) / 2;
		if (entries[mid].addr <= addr)
			lo = mid + 1;
		else
			hi = mid;
	}
	return lo && addr - entries[lo - 1].addr < blocks[entries[lo - 1].block].size ? &entries[lo - 1] : nullptr;
}

const Map *find(std::string_view id)
{
	for (const Map &m : maps)
		if (id == m.id)
			return &m;
	return nullptr;
}

const Map *all(size_t &count)
{
	count = sizeof maps / sizeof *maps;
	return maps;
}

}
""")
(root / "src/maps.cpp").write_text("\n".join(out))
