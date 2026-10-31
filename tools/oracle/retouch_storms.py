"""Retouched states for the storms (0x5053C0, 0x505290, 0x447580; see
docs/cloud/battles.md §6): records like town_oracle.py --barcos --storms
starting from the save changed after the load by a scenario, for the branches
the saves don't reach. tick_probe repeats each call from the game's state just
before it (without --continuo: p3vita doesn't know the retouch).

usage: python3 retouch_storms.py <Patrician3.exe> <save.raw> <out.probe> <days> <scenario>
       --barcos DATA --cpr p2arch0_eng.cpr [--blocks]
It records 5053c0,505290,447580. With --blocks it prints the blocks of those
three functions that did not run.

The storms are the pseudo-ships of the 16 slots of the ships object
([0x6DD7A0 + 0x20 + 0xC·k]: u16 ship, u16 strength, u16 x, u16 y, u16 kind,
u16 countdown). A scenario moves existing storms (the saves have 11) next to
chosen ships and sets their kind, strength and countdown; 0x50D0C0 puts the
storm back in its place in the list by x.

Scenarios:
  convoys     two convoys at sea, each with a storm of kind 2 and one of kind 0,
              one storm a bit to the right of the guide and one to the left (both
              walks); one ship of each convoy with 2 of hull (the minimum of 1)
  countdown   storms with the countdown of the sea map at 6 and 3, and two that
              end (strength 4) with the countdown at 2 and at 10 (the sea map is told)
  vertical    ships sailing with a vertical course only ([+0x28] = 0) next to a
              storm of kind 1 on each side (the push back that tests [+0x2C])
  reroute     ships with a captain and a route: a storm at 8 places round each one
              (20 px away in the 8 directions), so the corners of 0x447580 vary
  coast       the same with ships that have just left a town (land near the storm)
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import town_oracle as to  # noqa: E402
from town_oracle import WORLD  # noqa: E402

SLOTS = to.SHIPS + 0x20
SORT_X = 0x50D0C0  # thiscall on the ships object (index): the ship back in its place in the list by x
FUNCTIONS = [0x5053C0, 0x505290, 0x447580]
RANGES = [(0x5053C0, 0x505A02), (0x505290, 0x5053BD), (0x447580, 0x448030)]


def w16(o, a, v):
    o.uc.mem_write(a, struct.pack('<H', v & 0xFFFF))


def w32(o, a, v):
    o.uc.mem_write(a, struct.pack('<I', v & 0xFFFFFFFF))


def storms(o):
    """The slots with a storm: [(slot address, storm ship)]."""
    out = []
    for k in range(16):
        slot = SLOTS + 0xC * k
        ship = o.u16(slot)
        if ship < o.counts()[3]:
            out.append((slot, ship))
    return out


def place(o, slot, storm, x, y, kind, strength=200, countdown=0):
    s = o.ship(storm)
    w32(o, s + 0x1C, x << 16)
    w32(o, s + 0x20, y << 16)
    o.call(SORT_X, [storm], ecx=to.SHIPS)
    w16(o, slot + 0x2, strength)
    w16(o, slot + 0x8, kind)
    w16(o, slot + 0xA, countdown)
    print(f'storm {storm} (slot {(slot - SLOTS) // 0xC}) at ({x}, {y}) kind {kind} strength {strength} '
          f'countdown {countdown}')


def pos(o, i):
    s = o.ship(i)
    return struct.unpack('<hh', bytes(o.uc.mem_read(s + 0x1E, 2)) + bytes(o.uc.mem_read(s + 0x22, 2)))


def sailing(o, captain=True, route=True):
    """Ships of a merchant at sea (state 0x0F): with route=True, the ones with a
    route (alone or guiding a convoy); with route=False, the ones alone."""
    towns, offices, merchants, ships = o.counts()
    captains = o.u16(to.SHIPS + 0xF2)
    out = []
    for i in range(ships):
        s = o.ship(i)
        if o.u16(s + 0x134) != 0x0F or o.u8(s) >= merchants:
            continue
        if not route and o.u16(s + 0x8) < o.u16(to.SHIPS + 0xF6):
            continue
        if captain and o.u16(s + 0x42) >= captains:
            continue
        if route and not o.u32(s + 0x24):
            continue
        out.append(i)
    return out


def convoys(o):
    s = storms(o)
    base = o.u32(to.SHIPS + 0x8)
    found = []
    for c in range(o.u16(to.SHIPS + 0xF6)):
        p = base + 0x3C * c
        if o.u16(p + 0x12) != 0x0F or o.u8(p) >= o.counts()[2]:
            continue
        members = []
        m = o.u16(p + 0xA)
        while m < o.counts()[3]:
            members.append(m)
            m = o.u16(o.ship(m) + 0x6)
        if len(members) >= 2:
            found.append((c, o.u16(p + 0x10), members))
    for (c, guide, members), (kind, side) in zip(found, [(2, 8), (0, -8), (2, -8), (0, 8)]):
        if not s:
            break
        slot, storm = s.pop()
        x, y = pos(o, guide)
        w32(o, o.ship(members[-1]) + 0x18, 2)
        print(f'convoy {c}: guide {guide}, ships {members}; ship {members[-1]} with 2 of hull')
        place(o, slot, storm, x + side, y, kind)


def countdown(o):
    s = storms(o)
    for (slot, storm), (cd, strength) in zip(s, [(6, 200), (3, 200), (2, 4), (10, 4)]):
        x, y = pos(o, storm)
        place(o, slot, storm, x, y, o.u16(slot + 0x8), strength, cd)


def vertical(o):
    s = storms(o)
    for i, side in zip(sailing(o, captain=False, route=False), [8, -8, 8, -8]):
        if not s:
            break
        w32(o, o.ship(i) + 0x28, 0)
        if o.s32(o.ship(i) + 0x2C) == 0:
            w32(o, o.ship(i) + 0x2C, 0x4000)
        slot, storm = s.pop()
        x, y = pos(o, i)
        print(f'ship {i}: course (0, {o.s32(o.ship(i) + 0x2C):#x})')
        place(o, slot, storm, x + side, y, 1)


AROUND = [(20, 0), (-20, 0), (0, 20), (0, -20), (14, 14), (-14, 14), (14, -14), (-14, -14)]


def reroute(o, ships=None):
    s = storms(o)
    ships = ships if ships is not None else sailing(o)
    for i, (dx, dy) in zip(ships, AROUND):
        if not s:
            break
        slot, storm = s.pop()
        x, y = pos(o, i)
        print(f'ship {i} at ({x}, {y}), route point {o.u16(o.ship(i) + 0x34)}')
        place(o, slot, storm, x + dx, y + dy, 1)


def coast(o):
    """Ships with a route less than 50 px from a ship in port (that is, near a
    town: land near the storm)."""
    towns, offices, merchants, ships = o.counts()
    docked = [pos(o, i) for i in range(ships) if o.u16(o.ship(i) + 0x134) in (0, 1, 2, 3)]
    near = [i for i in sailing(o)
            if any((x - pos(o, i)[0]) ** 2 + (y - pos(o, i)[1]) ** 2 < 2500 for x, y in docked)]
    reroute(o, near)


SCENARIOS = {f.__name__: f for f in (convoys, countdown, vertical, reroute, coast)}


def main():
    p = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    p.add_argument('exe')
    p.add_argument('save')
    p.add_argument('out')
    p.add_argument('days', type=int)
    p.add_argument('scenario', choices=sorted(SCENARIOS))
    p.add_argument('--barcos', metavar='DATA', required=True)
    p.add_argument('--cpr', required=True)
    p.add_argument('--blocks', action='store_true')
    a = p.parse_args()
    o = to.TownOracle(a.exe, open(a.save, 'rb').read(), 1, a.barcos, rest=list(to.REST), cpr=a.cpr, storms=True)
    hits = set()
    if a.blocks:
        from unicorn import UC_HOOK_BLOCK
        for lo, hi in RANGES:
            o.uc.hook_add(UC_HOOK_BLOCK, lambda uc, address, size, data: hits.add(address), begin=lo, end=hi)
    o.load()
    SCENARIOS[a.scenario](o)
    with open(a.out, 'wb') as f:
        f.write(b'P3PROBE1')
        rec = to.Recorder(o, FUNCTIONS, f, 1 << 30)
        rec.active = True
        for _ in range(a.days * 256):
            o.advance_tick()
        f.write(b'E')
    print(f'{rec.count} samples in {a.out}')
    if a.blocks:
        print('blocks run: ' + ' '.join(f'{x:#x}' for x in sorted(hits)))


if __name__ == '__main__':
    main()
