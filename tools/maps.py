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
KNOWN = {"offset", "size", "bits", "name", "min", "max", "display", "signed", "field", "order", "frac", "receive_only", "absent"}


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
tables, maps = {}, []


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
    if r.get("packing") != "bits" or r.get("first", "low") not in ("low", "high") or set(r) - {"map", "packing", "first", "blocks", "lead"}:
        sys.exit(f"{path.name}: packing {r.get('packing')!r}, first {r.get('first')!r}, keys {sorted(r)}")
    records[r["map"]] = r


def centre(p):
    """what a packed parameter's units are above its stored bits: a value about 64 kept about the middle of fewer
    bits than hold it"""
    return 64 - (1 << (p["bits"] - 1)) if p["size"] == 1 and p["bits"] < 7 and p["max"] >= 1 << p["bits"] else 0


for path in sorted((root / "maps").glob("*.json")):
    m = json.loads(path.read_text())
    record = records.get(path.stem, {})
    packed, lead = dict(record.get("blocks", {})), dict(record.get("lead", {}))
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
        bytes_packed, before = packed.pop(k, 0), lead.pop(k, 0)
        if bytes_packed and before + sum(p["bits"] * p["size"] for p in b["params"]) > 8 * bytes_packed:
            sys.exit(f"{path.name}: {k}: its parameters' bits are more than {bytes_packed} bytes")
        for p in b["params"] if bytes_packed else []:
            if p["size"] == 1 and not 0 <= p["min"] - centre(p) <= p["max"] - centre(p) < 1 << p["bits"]:
                sys.exit(f"{path.name}: {k}: {p['name']}: {p['min']} to {p['max']} not in {p['bits']} bits")
        blocks.append(f'{{ "{k}", {b["size"]}, {params}, {len(b["params"])}, {name}, {bytes_packed}, {before} }}')
    if packed or lead:
        sys.exit(f"maps/records/{path.name}: no blocks {sorted(packed) + sorted(lead)}")

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
    # number its place among the bank's.  An entry whose group holds groups alone is an area for each of them (a
    # part's temporary patch and its temporary rhythm set), named as the group names them.  The areas alike one
    # after another, a constant step apart, are written once as a run
    runs, sizes, last, counts = [], set(), -1, {}
    for e in m["blocks"]["top"]["entries"]:
        sizes.add(len(e["addr"]))
        top = m["blocks"][e["block"]]
        inner = top.get("entries", [])
        if inner and all("entries" in m["blocks"][i["block"]] for i in inner):
            parts = [(i["name"].split(" (")[0], i["block"], number(e["addr"]) + number(i["addr"])) for i in inner]
        else:
            parts = [(e["name"].split(" (")[0], e["block"], number(e["addr"]))]
        for bank, group, addr in parts:
            own = leaves(group, addr)
            for at, block in own:
                if at <= last:
                    sys.exit(f"{path.name}: {e['name']}: {block} not after the block before it")
                last = at + m["blocks"][block]["size"] - 1
            members = table("Member", [f"{{ 0x{at - addr:07x}, {ids.index(block)} }}" for at, block in own])
            n = counts.get(bank, 0)
            counts[bank] = n + 1
            r = runs[-1] if runs else None
            if r and (r["group"], r["bank"], r["members"]) == (group, bank, members) and r["number"] + r["count"] == n \
                    and (r["count"] == 1 or addr - r["last"] == r["stride"]):
                r["stride"] = addr - r["last"]
                r["last"] = addr
                r["count"] += 1
            else:
                runs.append(dict(group=group, bank=bank, members=members, number=n, addr=addr, last=addr, stride=0,
                                 count=1, size=len(own)))
    if len(sizes) != 1 or len(m["model_id"]) > 4:
        sys.exit(f"{path.name}: addresses of {sorted(sizes)} bytes, a model ID of {len(m['model_id'])}")
    rows = [f'{{ "{r["group"]}", "{r["bank"]}", {r["number"]}, 0x{r["addr"]:07x}, 0x{r["stride"]:07x}, {r["count"]}, '
            f'{r["members"]}, {r["size"]} }}' for r in runs]
    maps.append(f'{{ "{path.stem}", "{m["model"]}", {{ {", ".join(f"0x{b:02x}" for b in m["model_id"])} }}, '
                f'{len(m["model_id"])}, {sizes.pop()}, {"true" if record.get("first") == "high" else "false"}, '
                f'{table("Block", blocks)}, {len(blocks)}, {table("Run", rows)}, {len(rows)} }}')

out.append("static const Source maps[] = {\n" + "".join(f"\t{r},\n" for r in maps) + "};\n")
out.append("""const Source *sources(size_t &count)
{
	count = sizeof maps / sizeof *maps;
	return maps;
}

}
""")
(root / "src/maps.cpp").write_text("\n".join(out))
