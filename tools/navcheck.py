#!/usr/bin/env python3
"""Nav mesh coverage check for HL2MP maps (offline: reads <map>.bsp entities and <map>.nav).

  python -I navcheck.py <maps-dir> [map ...]        (default: every <map>.bsp that has a .nav)
  python -I navcheck.py <maps-dir> --json out.json [map ...]

For every spawn point (info_player_deathmatch/rebel/combine/start) and pickup (weapon_*, item_*)
it reports whether the spot sits on a nav area ("covered") and whether that area is reachable over
the mesh from the spawn areas ("reachable": directed walk/ladder links, BFS from every spawn area).
"""
import json, os, struct, sys
from collections import deque

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bspents import entities  # noqa: E402

SPAWNS = ("info_player_deathmatch", "info_player_rebel", "info_player_combine", "info_player_start")


class Reader:
    def __init__(self, data):
        self.d, self.o = data, 0

    def u8(self):
        v = self.d[self.o]; self.o += 1; return v

    def u16(self):
        v = struct.unpack_from("<H", self.d, self.o)[0]; self.o += 2; return v

    def u32(self):
        v = struct.unpack_from("<I", self.d, self.o)[0]; self.o += 4; return v

    def f(self, n=1):
        v = struct.unpack_from("<%df" % n, self.d, self.o); self.o += 4 * n; return v if n > 1 else v[0]

    def skip(self, n):
        self.o += n


def load_nav(path):
    r = Reader(open(path, "rb").read())
    if r.u32() != 0xFEEDFACE:
        raise ValueError("bad magic")
    ver = r.u32()
    if ver != 16:
        raise ValueError("unsupported nav version %d" % ver)
    r.u32(); r.u32(); r.u8()                  # subversion, bsp size, analyzed
    for _ in range(r.u16()):                  # place directory
        r.skip(r.u16())
    r.u8()                                    # has unnamed areas
    areas = {}
    for _ in range(r.u32()):
        aid = r.u32(); r.u32()                # id, attributes
        nw = r.f(3); se = r.f(3); nez = r.f(); swz = r.f()
        links = []
        for _d in range(4):
            links += [r.u32() for _ in range(r.u32())]
        for _h in range(r.u8()):              # hiding spots: id, pos, flags
            r.skip(4 + 12 + 1)
        for _e in range(r.u32()):             # encounter paths
            r.skip(4 + 1 + 4 + 1)
            r.skip(r.u8() * 5)
        r.u16()                               # place
        ladders = []
        for _d in range(2):
            ladders += [r.u32() for _ in range(r.u32())]
        r.skip(4 * 2 + 4 * 4)                 # earliest occupy (2 teams), light intensity
        r.skip(r.u32() * 5)                   # potentially visible areas
        r.u32()                               # inherit visibility from
        areas[aid] = {"nw": nw, "se": se, "nez": nez, "swz": swz, "links": links, "ladders": ladders}
    ladders = {}
    for _ in range(r.u32()):
        lid = r.u32(); r.f(); r.f(3); r.f(3); r.f(); r.u32()
        ladders[lid] = [r.u32() for _ in range(5)]   # top fwd/left/right/behind, bottom
    return areas, ladders


def area_z(a, x, y):
    nw, se = a["nw"], a["se"]
    dx = se[0] - nw[0] or 1.0
    dy = se[1] - nw[1] or 1.0
    u = min(max((x - nw[0]) / dx, 0.0), 1.0)
    v = min(max((y - nw[1]) / dy, 0.0), 1.0)
    north = nw[2] + u * (a["nez"] - nw[2])
    south = a["swz"] + u * (se[2] - a["swz"])
    return north + v * (south - north)


def find_area(areas, pos, horiz, zbelow, zabove):
    """Best area under pos: xy within the (grown) extent, floor between pos.z-zbelow and pos.z+zabove."""
    best, bestd = None, 1e9
    x, y, z = pos
    for aid, a in areas.items():
        nw, se = a["nw"], a["se"]
        if not (nw[0] - horiz <= x <= se[0] + horiz and nw[1] - horiz <= y <= se[1] + horiz):
            continue
        az = area_z(a, x, y)
        if z - zbelow <= az <= z + zabove:
            ox = max(nw[0] - x, 0, x - se[0])
            oy = max(nw[1] - y, 0, y - se[1])
            d = ox + oy + abs(z - az) * 0.1
            if d < bestd:
                best, bestd = aid, d
    return best


def graph(areas, ladders):
    g = {aid: set(a["links"]) for aid, a in areas.items()}
    for aid, a in areas.items():
        for lid in a["ladders"]:
            for other in ladders.get(lid, ()):
                if other and other != aid:
                    g[aid].add(other)
    return g


def check(maps_dir, name):
    bsp = os.path.join(maps_dir, name + ".bsp")
    nav = os.path.join(maps_dir, name + ".nav")
    areas, ladders = load_nav(nav)
    g = graph(areas, ladders)
    spawns, items = [], []
    ents = entities(bsp)
    # entities named by a point_template are template sources (spawned elsewhere/later): skip them
    templated = set()
    for e in ents:
        if e.get("classname", "").lower() == "point_template":
            templated.update(v.lower() for k, v in e.items() if k.lower().startswith("template"))
    for e in ents:
        c = e.get("classname", "").lower()
        if "origin" not in e or e.get("targetname", "").lower() in templated:
            continue
        try:
            pos = tuple(float(v) for v in e["origin"].split()[:3])
        except ValueError:
            continue
        if c in SPAWNS:
            spawns.append((c, pos))
        elif c.startswith("weapon_") or c.startswith("item_"):
            items.append((c, pos))
    res = {"map": name, "areas": len(areas), "ladders": len(ladders), "spawns": [], "items": []}
    spawn_areas = []
    for c, pos in spawns:
        aid = find_area(areas, pos, 24, 80, 24)
        res["spawns"].append({"class": c, "pos": pos, "area": aid})
        if aid is not None:
            spawn_areas.append(aid)
    for c, pos in items:
        wall = c in ("item_healthcharger", "item_suitcharger")
        aid = find_area(areas, pos, 56 if wall else 32, 112 if wall else 72, 32)
        res["items"].append({"class": c, "pos": pos, "area": aid})
    seen = set(spawn_areas)
    q = deque(spawn_areas)
    while q:
        a = q.popleft()
        for b in g.get(a, ()):
            if b in areas and b not in seen:
                seen.add(b); q.append(b)
    res["reachable_areas"] = len(seen)
    for lst in (res["spawns"], res["items"]):
        for t in lst:
            t["reachable"] = t["area"] is not None and t["area"] in seen
    return res


def summary(r):
    def frac(lst, key):
        return "%d/%d" % (sum(1 for t in lst if t[key]), len(lst))
    sp = [dict(t, covered=t["area"] is not None) for t in r["spawns"]]
    it = [dict(t, covered=t["area"] is not None) for t in r["items"]]
    return ("%-24s areas %5d  reach %5d  spawns cov %-7s  items cov %-7s reach %-7s"
            % (r["map"], r["areas"], r["reachable_areas"], frac(sp, "covered"), frac(it, "covered"),
               frac(it, "reachable")))


if __name__ == "__main__":
    args = sys.argv[1:]
    out = None
    if "--json" in args:
        i = args.index("--json"); out = args[i + 1]; del args[i:i + 2]
    maps_dir, names = args[0], args[1:]
    if not names:
        names = sorted(f[:-4] for f in os.listdir(maps_dir)
                       if f.endswith(".nav") and os.path.exists(os.path.join(maps_dir, f[:-4] + ".bsp")))
    results = []
    for n in names:
        try:
            r = check(maps_dir, n)
        except Exception as e:  # noqa: BLE001
            print("%-24s ERROR %s" % (n, e)); continue
        results.append(r)
        print(summary(r))
        for t in r["spawns"] + r["items"]:
            if not t["reachable"]:
                print("    %-8s %-28s %s" % ("MISSING" if t["area"] is None else "UNREACH", t["class"],
                                            " ".join("%.0f" % v for v in t["pos"])))
    if out:
        json.dump(results, open(out, "w"), indent=1)
