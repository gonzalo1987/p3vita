"""Retouched states for the pirates (docs/cloud/battles-2.md): records like
town_oracle.py --barcos --pirates starting from the save changed after the
load by a scenario, for the branches the saves don't reach. tick_probe
(with --pirates) repeats each call from the game's state just before it
(without --continuo: p3vita doesn't know the retouch).

usage: python3 retouch_pirates.py <Patrician3.exe> <save.raw> <out.probe> <days> <scenario>
       --barcos DATA --cpr p2arch0_eng.cpr [--functions F,F...] [--blocks]
By default it records 514b80,515da0,513bf0,505c90 (lost, alone and guide:
use --functions 506720,517170,5062f0,5017f0). With --blocks it prints
the blocks of the recorded functions that ran.

Scenarios:
  disband      every pirate convoy at sea in state 0x12 is put at its pirate's
               nest without a route and with cargo (so 0x505C90 sends it home
               and 0x514B80 disbands it): its guide gets captain 0, its last
               ship 1/3 of its hull, the levels of its ships are 0, 1, 2...;
               the pirate gets 0x3000 of [+0x16] and a full pool (0x513BF0
               grows it)
  disband_map  the same, and each pirate gets a map for its nest (a copy of
               town 0's town_class1 object and grid), entry 0 of its pool at
               the anchor [+0x5C], [+0x5D]
  plan         direct calls of 0x515DA0 on each pirate, at an even and at an
               odd tick: with a convoy in [+0x10] (it returns), with no money,
               with 0x3000 and no map, with a map (entry 0 at the anchor, at
               (0, 0) and at (0xF0, 0xF0), outside the grid; with a town hall
               site, class 0x1B, where the search starts and 3 rings away),
               and with the pool full
  lost         state 0x11 for a human player's ship and an AI merchant's ship
               in port (0x506AF2, 0x517170) and at sea (0x507022; if no human
               player's ship is alone at sea, an AI one becomes the player's)
  alone        a pirate alone at sea in state 0x12 (0x507099): a ship of each
               pirate convoy leaves it; one with hull (0x5062F0 makes it a
               convoy), one without (deleted)
  guide        direct calls of 0x5017F0 on the pirate convoys: guide without
               hull, no captains, no hull at all, guide without sailors
  daily        direct calls of 0x514000 on each pirate: a privateer's ship at
               the nest, [+0x14] = 4, no captains, no ships at the nest; then
               the countdowns of the pool, an empty pool, and every pirate
               ship in one pirate's list (many away)
  leave        direct calls of 0x514D40 (operation 0xB5): ships with a captain
               accepted and refused, up to 3; a privateer's ship that goes home
               and one that leaves with the pirates
  retire       each pirate goes away ([+0x15] = 1; 0x514000, 0x513EC0)
  attack       direct calls of 0x5063C0 (the attack of event 0x1E): kinds 0,
               1, 2, 5, a target in the shallows, no target; with 0 days
  attack_one   one call of 0x5063C0 (kind 0), then the days
  attack_new   the same and then no pirate at all (one is made: 0x513D80);
               with 0 days
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import town_oracle as to  # noqa: E402
from town_oracle import WORLD  # noqa: E402

SORT_X = 0x50D0C0  # thiscall on the ships object (index): the ship back in its place in the list by x
PLAN = 0x515DA0
FUNCTIONS = [0x514B80, 0x515DA0, 0x513BF0, 0x505C90]
RANGES = {0x5063C0: 0x5066E1, 0x513D80: 0x513EBF, 0x515EA0: 0x515F00, 0x514000: 0x514492, 0x514D40: 0x5152EE, 0x513EC0: 0x513FF3, 0x514B80: 0x514D36, 0x515DA0: 0x515E9D, 0x513BF0: 0x513D11, 0x505C90: 0x506270}
TOWN_TABLE = 0x6DDB90
TICK = 0x6DE4B4
# A call from Python returns to SENTINEL, where the emulation stops before the
# Recorder can close the sample: the direct calls go through a trampoline
# (one each 0x20 bytes from here).
TRAMPOLINES = to.SENTINEL + 0x180
CHECK_GUIDE = 0x5017F0  # thiscall on a convoy
LEAVE_CONVOY = 0x509F80  # thiscall on the ships object (ship): out of its convoy
_trampolines = {}


def direct(o, target, args, ecx):
    """Calls target(args) on ecx through a trampoline, so that the Recorder
    records it."""
    at = _trampolines.get(target)
    if at is None:
        at = _trampolines[target] = TRAMPOLINES + 0x20 * len(_trampolines)
        _, build = to.trampoline(target, len(args))
        o.uc.mem_write(at, build(at))
    return o.call(at, args, ecx=ecx)


def w8(o, a, v):
    o.uc.mem_write(a, bytes([v & 0xFF]))


def w16(o, a, v):
    o.uc.mem_write(a, struct.pack('<H', v & 0xFFFF))


def w32(o, a, v):
    o.uc.mem_write(a, struct.pack('<I', v & 0xFFFFFFFF))


def pirates(o):
    """[(k, address of the pirate object)] of the pirates that exist."""
    return [(k, o.u32(to.SHIPS + 0xC + 4 * k)) for k in range(5) if o.u32(to.SHIPS + 0xC + 4 * k)]


def pirate_convoys(o):
    """[(convoy, guide, members)] of the pirate convoys in the list in state 0x12."""
    base = o.u32(to.SHIPS + 0x8)
    ships = o.counts()[3]
    out = []
    c = o.u16(to.SHIPS + 0xEC)
    while c < o.u16(to.SHIPS + 0xF6):
        p = base + 0x3C * c
        if o.u8(p) >= o.counts()[2] and o.u16(p + 0x12) == 0x12:
            members = []
            m = o.u16(p + 0xA)
            while m < ships:
                members.append(m)
                m = o.u16(o.ship(m) + 0x6)
            out.append((c, o.u16(p + 0x10), members))
        c = o.u16(p + 0x8)
    return out


def full_pool(o, pirate):
    w16(o, pirate + 0xA, o.u16(pirate + 0x8))


def give_map(o, pirate):
    """A map for the nest: a copy of town 0's town_class1 object and grid; entry
    0 of the pool at its anchor [+0x5C], [+0x5D]."""
    town = o.u32(WORLD + 0x68)
    obj = bytes(o.uc.mem_read(town + 0x7A4, 0x6C))
    width, height = struct.unpack_from('<ii', obj, 4)
    cells = o.alloc(width * height)
    o.uc.mem_write(cells, bytes(o.uc.mem_read(o.u32(town + 0x80C), width * height)))
    m = o.alloc(0x6C)
    o.uc.mem_write(m, obj[:0x68] + struct.pack('<I', cells))
    w32(o, pirate, m)
    pool = o.u32(pirate + 0x4)
    w8(o, pool, obj[0x5C])
    w8(o, pool + 1, obj[0x5D])
    print(f'pirate at {pirate:#x}: map {width} x {height}, entry 0 at ({obj[0x5C]}, {obj[0x5D]})')
    return m


def disband(o, with_map=False):
    captains = o.u16(to.SHIPS + 0xF2)
    for c, guide, members in pirate_convoys(o):
        k = o.u8(o.ship(guide) + 0x158)
        pirate = o.u32(to.SHIPS + 0xC + 4 * k) if k < 5 else 0
        if not pirate:
            continue
        nest = o.u8(pirate + 0xE)
        x = o.u16(TOWN_TABLE + 0x34 * nest + 0x20)
        y = o.u16(TOWN_TABLE + 0x34 * nest + 0x24)
        g = o.ship(guide)
        w32(o, g + 0x1C, x << 16)
        w32(o, g + 0x20, y << 16)
        w32(o, g + 0x24, 0)
        o.call(SORT_X, [guide], ecx=to.SHIPS)
        if captains and o.u16(g + 0x42) >= captains:
            w16(o, g + 0x42, 0)
        for n, m in enumerate(members):
            s = o.ship(m)
            w8(o, s + 0x136, o.u8(s + 0x136) & 0x7F)
            w8(o, s + 0xF, n % 3)
            for ware, amount in ((0, 1001), (9, 333), (20, 7), (23, 0x7FFF)):
                w32(o, s + 0x54 + 4 * ware, amount + n)
        last = o.ship(members[-1])
        w32(o, last + 0x18, o.s32(last + 0x14) // 3)
        w16(o, pirate + 0x16, 0x3000)
        full_pool(o, pirate)
        if with_map and not o.u32(pirate):
            give_map(o, pirate)
        print(f'convoy {c}: guide {guide} at the nest {nest} of pirate {k} ({x}, {y}), ships {members}')


def disband_map(o):
    disband(o, True)


def plan(o):
    convoys = o.u16(to.SHIPS + 0xF6)
    for tick in (0, 1):
        w32(o, TICK, (o.u32(TICK) & ~3) | tick)
        for k, pirate in pirates(o):
            w16(o, pirate + 0x10, 0 if convoys else 0xFFFF)
            direct(o, PLAN, [], pirate)  # with a convoy (or none, if there are no convoys)
            w16(o, pirate + 0x10, 0xFFFF)
            w16(o, pirate + 0x16, 0)
            direct(o, PLAN, [], pirate)  # no money
            w16(o, pirate + 0x16, 0x3000)
            direct(o, PLAN, [], pirate)  # no map
            m = o.u32(pirate) or give_map(o, pirate)
            pool = o.u32(pirate + 0x4)
            anchor = bytes(o.uc.mem_read(m + 0x5C, 2))
            for at in (anchor, b'\0\0', b'\xf0\xf0'):
                o.uc.mem_write(pool, at)
                direct(o, PLAN, [], pirate)
            # A town hall site (class 0x1B, what the type 0x20 looks for) where
            # the search starts, and then one 3 rings away.
            o.uc.mem_write(pool, anchor)
            width, height = struct.unpack('<ii', bytes(o.uc.mem_read(m + 4, 8)))
            cells = o.u32(m + 0x68)
            x0 = (anchor[0] - (o.u32(TICK) & 3) + 0xF) & 0xFF
            y0 = (anchor[1] - (o.u8(WORLD + 4) & 3) + 0xF) & 0xFF
            for dx in (0, 3):
                at = cells + (x0 + dx) * height + y0
                keep = o.u8(at)
                w8(o, at, 0x1B)
                direct(o, PLAN, [], pirate)
                w8(o, at, keep)
            full_pool(o, pirate)
            direct(o, PLAN, [], pirate)
            print(f'pirate {k}: 0x515DA0 9 times at tick & 3 = {tick}')


def walk(o, head):
    """The ships of the list that starts at the global head (linked by [+0x6])."""
    out = []
    i = o.u16(head)
    while i < o.counts()[3] and len(out) < 10000:
        out.append(i)
        i = o.u16(o.ship(i) + 0x6)
    return out


def human(o, i):
    owner = o.u8(o.ship(i))
    return owner < o.counts()[2] and o.u16(o.merchant(owner) + 8) == 0


def lost(o):
    """State 0x11 for two ships in port and two at sea: a human player's and
    an AI merchant's in each list (the first ones found)."""
    for head, where in ((to.SHIPS + 0xE8, 'port'), (to.SHIPS + 0xEA, 'sea')):
        chosen = {}
        for i in walk(o, head):
            s = o.ship(i)
            if o.u8(s) >= o.counts()[2] or o.u16(s + 0x8) < o.u16(to.SHIPS + 0xF6):
                continue
            if human(o, i):
                chosen.setdefault(True, i)
                continue
            if not human(o, i) and False not in chosen:
                chosen[False] = i
            elif not human(o, i) and True not in chosen:
                # No human player's ship alone in the list: an AI one becomes
                # the player's.
                w8(o, o.ship(i), o.u32(0x6DFC14))
                chosen[True] = i
        for is_human, i in chosen.items():
            w16(o, o.ship(i) + 0x134, 0x11)
            print(f'{where}: ship {i} ({"human" if is_human else "AI"}) in state 0x11')


def alone(o):
    """A pirate ship alone at sea in state 0x12: the last ship of each pirate
    convoy in state 0x12 leaves it (0x509F80) and is put at the head of the
    sea list; the one of the second convoy with no hull; for the first, no free
    convoy is left (0x5062F0 makes more with 0x509AE0)."""
    for n, (c, guide, members) in enumerate(pirate_convoys(o)):
        if len(members) < 2:
            continue
        ship = members[-1] if members[-1] != guide else members[0]
        o.call(LEAVE_CONVOY, [ship], ecx=to.SHIPS)
        s = o.ship(ship)
        if n == 1:
            w32(o, s + 0x18, 0)
        if n == 0:
            w16(o, to.SHIPS + 0xE4, o.u16(to.SHIPS + 0xF6))  # no free convoy: 0x509AE0 makes more
        if ship not in walk(o, to.SHIPS + 0xEA):  # at the head of the sea list
            w16(o, s + 0x6, o.u16(to.SHIPS + 0xEA))
            w16(o, to.SHIPS + 0xEA, ship)
        print(f'convoy {c}: ship {ship} alone, state {o.u16(s + 0x134):#x}, hull {o.s32(s + 0x18)}, '
              f'in the sea list: {ship in walk(o, to.SHIPS + 0xEA)}')


def guide(o):
    """Direct calls of 0x5017F0 on each pirate convoy in state 0x12: the guide
    without hull (another with hull and captain guides); no captain in the
    convoy (the first with hull gets a new one, 0x5097C0); no ship with hull;
    the guide without sailors and in the list by x (it goes back to it)."""
    captains = o.u16(to.SHIPS + 0xF2)
    base = o.u32(to.SHIPS + 0x8)
    for c, lead, members in pirate_convoys(o):
        convoy = base + 0x3C * c
        keep = {i: bytes(o.uc.mem_read(o.ship(i), 0x180)) for i in members}
        w32(o, o.ship(lead) + 0x18, 0)
        direct(o, CHECK_GUIDE, [], convoy)
        for i in members:
            w16(o, o.ship(i) + 0x42, 0xFFFF)
        direct(o, CHECK_GUIDE, [], convoy)
        for i in members:
            w32(o, o.ship(i) + 0x18, 0)
        direct(o, CHECK_GUIDE, [], convoy)
        for i, data in keep.items():
            o.uc.mem_write(o.ship(i), data)
        w16(o, convoy + 0x10, lead)
        w16(o, o.ship(lead) + 0x40, 0)
        direct(o, CHECK_GUIDE, [], convoy)
        print(f'convoy {c}: guide {lead}, ships {members}, captains {captains}: 0x5017F0 4 times')


DAILY = 0x514000  # thiscall on a pirate object (its number)
LEAVE_NEST = 0x514D40  # thiscall on a pirate object (most ships, strength): operation 0xB5
GO_AWAY = 0x513EC0  # thiscall on a pirate object
INSERT_X = 0x50CE50  # thiscall on the ships object (ship, after): into the list by x


def nest_ships(o, pirate):
    """The ships at the pirate's nest ([+0x12], linked by [+0x6])."""
    out = []
    i = o.u16(pirate + 0x12)
    while i < o.counts()[3] and len(out) < 1000:
        out.append(i)
        i = o.u16(o.ship(i) + 0x6)
    return out


def captain(o, c):
    return o.u32(to.SHIPS) + 0x10 * c


def daily(o):
    """Direct calls of 0x514000 on each pirate, with [+0x10] cleared: as it is;
    every ship at the nest repaired and the first a privateer's (merchant 0)
    whose time has passed (it is chosen: operation 0xB5 with 1); [+0x14] = 4
    (0x673658 past its 4 entries: a ship at the nest without captain is
    deleted); no captains at all (the first at the nest gets one); no ships
    at the nest at a day & 3 = 0 (0x516170 makes one) and at day & 3 = 1."""
    captains = o.u16(to.SHIPS + 0xF2)
    for k, pirate in pirates(o):
        w16(o, pirate + 0x10, 0xFFFF)
        direct(o, DAILY, [k], pirate)
        nest = nest_ships(o, pirate)
        for i in nest:
            s = o.ship(i)
            w32(o, s + 0x18, o.s32(s + 0x14))
        if nest:
            s = o.ship(nest[0])
            w8(o, s + 0x15D, 0)
            w32(o, s + 0x4C, 0)
        direct(o, DAILY, [k], pirate)
        keep = o.u8(pirate + 0x14)
        w8(o, pirate + 0x14, 4)
        for i in nest_ships(o, pirate)[1:]:
            w16(o, o.ship(i) + 0x42, 0xFFFF)
        direct(o, DAILY, [k], pirate)
        w8(o, pirate + 0x14, keep)
        i = o.u16(pirate + 0xC)
        while i < o.counts()[3]:
            w16(o, o.ship(i) + 0x42, 0xFFFF)
            i = o.u16(o.ship(i) + 0x4)
        direct(o, DAILY, [k], pirate)
        for day in (0, 1):
            w16(o, pirate + 0x12, 0xFFFF)
            w8(o, WORLD + 4, (o.u8(WORLD + 4) & ~3) | day)
            direct(o, DAILY, [k], pirate)
        print(f'pirate {k}: nest {nest}, captains {captains}: 0x514000 6 times')
    # The countdowns of the pool ([+0x6] 0x10, 2), an empty pool, and a
    # pirate with every pirate ship in its list (many away: a ship at the nest
    # without captain is deleted) and [+0x14] = 4.
    found = pirates(o)
    if not found:
        return
    k, pirate = found[0]
    pool = o.u32(pirate + 0x4)
    for e, value in ((0, 0x10), (1, 2), (2, 0x10)):
        if e < o.u16(pirate + 0x8):
            w8(o, pool + 8 * e + 6, value)
    direct(o, DAILY, [k], pirate)
    keep = o.u16(pirate + 0x8)
    w16(o, pirate + 0x8, 0)
    direct(o, DAILY, [k], pirate)
    w16(o, pirate + 0x8, keep)
    ships = o.counts()[3]
    pirate_ships = [i for i in range(ships) if o.u8(o.ship(i)) == 0xFF and o.u8(o.ship(i) + 0x15D) >= o.counts()[2]
                    and o.u16(o.ship(i) + 0x134) != 0x15]
    nest = [i for i in pirate_ships if o.u16(o.ship(i) + 0x134) not in (0x12,)][:1] or pirate_ships[:1]
    if nest:
        # One of them at the nest without captain, after another with captain
        # 0; all in [+0xC].
        first = [i for i in pirate_ships if i != nest[0]][:1]
        w16(o, pirate + 0x12, (first or nest)[0])
        if first:
            w16(o, o.ship(first[0]) + 0x6, nest[0])
            w16(o, o.ship(first[0]) + 0x42, 0)
        w16(o, o.ship(nest[0]) + 0x6, 0xFFFF)
        w16(o, o.ship(nest[0]) + 0x42, 0xFFFF)
        for a, b in zip(pirate_ships, pirate_ships[1:] + [0xFFFF]):
            w16(o, o.ship(a) + 0x4, b)
        w16(o, pirate + 0xC, pirate_ships[0])
        w8(o, pirate + 0x14, 4)
        direct(o, DAILY, [k], pirate)
        print(f'pirate {k}: {len(pirate_ships)} ships in its list, {nest[0]} at the nest')
    w16(o, pirate + 0xC, 0xFFFF)  # no ships at all
    direct(o, DAILY, [k], pirate)


def leave(o):
    """Direct calls of 0x514D40 (operation 0xB5) on each pirate: the ships at
    the nest with full hull and captain 0 (accepted, [+0x8] > 0x20), the
    second one's captain 1 refused ([+0x8] = 0x10, 0x5098B0), up to 3 ships;
    then a privateer's ship (merchant 0) whose time has passed (it goes home:
    message, maybe event 5, deleted), and one whose time has not."""
    w8(o, captain(o, 0) + 8, 0x40)
    w8(o, captain(o, 1) + 8, 0x10)
    for k, pirate in pirates(o):
        w16(o, pirate + 0x10, 0xFFFF)
        nest = nest_ships(o, pirate)
        for n, i in enumerate(nest):
            s = o.ship(i)
            w32(o, s + 0x18, o.s32(s + 0x14))
            w16(o, s + 0x42, 1 if n == 0 else 0xFFFF)
        if len(nest) > 1:
            w16(o, o.ship(nest[1]) + 0x42, 0)
            o.call(INSERT_X, [nest[1], 0xFFFF], ecx=to.SHIPS)
        if len(nest) > 2:
            w16(o, o.ship(nest[2]) + 0x42, 0)
        direct(o, LEAVE_NEST, [3, 0x1F4], pirate)
        for when in (0, 0x7FFFFFFF):
            nest = nest_ships(o, pirate)
            if not nest:
                break
            s = o.ship(nest[0])
            w16(o, s + 0x42, 0)
            w8(o, s + 0x15D, 0)
            w32(o, s + 0x4C, when)
            direct(o, LEAVE_NEST, [1, 0x1F4], pirate)
        w16(o, pirate + 0x12, 0xFFFF)
        direct(o, LEAVE_NEST, [1, 0x1F4], pirate)
        print(f'pirate {k}: 0x514D40 4 times')


def retire(o):
    """Each pirate goes away ([+0x15] = 1): 0x514000 deletes the ships at its
    nest and sinks its other ships (those in the list of convoys, or alone);
    for the second pirate its list [+0xC] is emptied first, so 0x513EC0 runs
    and the object is deleted (it has a map); for the third, a direct call of
    0x513EC0 with its lists and a map. Run it with 0 days: a convoy of a
    pirate that went away is left without a guide (see «Game bugs»)."""
    for n, (k, pirate) in enumerate(pirates(o)):
        if n == 2:
            if not o.u32(pirate):
                give_map(o, pirate)
            direct(o, GO_AWAY, [], pirate)
            print(f'pirate {k}: 0x513EC0 with its lists')
            continue
        w8(o, pirate + 0x15, 1)
        if n == 1:
            w16(o, pirate + 0xC, 0xFFFF)
            if not o.u32(pirate):
                give_map(o, pirate)
        direct(o, DAILY, [k], pirate)
        if n == 0 and o.u32(to.SHIPS + 0xC + 4 * k):
            # Again without ships, map or pool: 0x513EC0 and the object deleted.
            w16(o, pirate + 0xC, 0xFFFF)
            w16(o, pirate + 0x8, 0)
            w32(o, pirate + 0x4, 0)
            direct(o, DAILY, [k], pirate)
        print(f'pirate {k} goes away')


ATTACK = 0x5063C0  # thiscall on the ships object (target ship, kind)
SHALLOWS = (442, 1521)  # a corner inside the first box of 0x673564


def attack_one(o):
    """One call of 0x5063C0 (kind 0) on a ship of a human player at sea."""
    target = [i for i in walk(o, to.SHIPS + 0xEA) if human(o, i)][:1] or walk(o, to.SHIPS + 0xEA)[:1]
    direct(o, ATTACK, [target[0], 0], to.SHIPS)


def attack(o):
    """Direct calls of 0x5063C0 (the pirate of event 0x1E): on a ship of a
    human player at sea with the kinds 0, 1, 2 and 5; kind 2 with the target
    in the shallows; no target (0xFFFF). Run it with 0 days: six new pirates
    on one place tangle the list by x (0x50D0C0 never ends)."""
    target = None
    for i in walk(o, to.SHIPS + 0xEA):
        if human(o, i):
            target = i
            break
    if target is None:
        target = walk(o, to.SHIPS + 0xEA)[0]
    for kind in (0, 1, 2, 5):
        direct(o, ATTACK, [target, kind], to.SHIPS)
    s = o.ship(target)
    keep = bytes(o.uc.mem_read(s + 0x1C, 8))
    w32(o, s + 0x1C, SHALLOWS[0] << 16)
    w32(o, s + 0x20, SHALLOWS[1] << 16)
    direct(o, ATTACK, [target, 2], to.SHIPS)
    o.uc.mem_write(s + 0x1C, keep)
    direct(o, ATTACK, [0xFFFF, 1], to.SHIPS)
    print(f'target {target}: 0x5063C0 6 times')
    return target


def attack_new(o):
    """attack, then with no pirate at all (the pointers cleared; pirate 4 is
    made: 0x513720, 0x513D80) twice. Run it with 0 days: the convoys of the
    pirates whose pointers were cleared are left without a pirate."""
    target = attack(o)
    for k in range(5):
        w32(o, to.SHIPS + 0xC + 4 * k, 0)
    direct(o, ATTACK, [target, 0], to.SHIPS)
    direct(o, ATTACK, [target, 4], to.SHIPS)


SCENARIOS = {f.__name__: f for f in (disband, disband_map, plan, lost, alone, guide, daily, leave, retire, attack, attack_new, attack_one)}


def main():
    p = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    p.add_argument('exe')
    p.add_argument('save')
    p.add_argument('out')
    p.add_argument('days', type=int)
    p.add_argument('scenario', choices=sorted(SCENARIOS))
    p.add_argument('--barcos', metavar='DATA', required=True)
    p.add_argument('--cpr', required=True)
    p.add_argument('--functions', default=','.join(f'{x:x}' for x in FUNCTIONS))
    p.add_argument('--blocks', action='store_true')
    a = p.parse_args()
    functions = [int(x, 16) for x in a.functions.split(',') if x]
    o = to.TownOracle(a.exe, open(a.save, 'rb').read(), 1, a.barcos, rest=list(to.REST), cpr=a.cpr, pirates=True)
    hits = set()
    if a.blocks:
        from unicorn import UC_HOOK_BLOCK
        for lo in functions:
            if lo in RANGES:
                o.uc.hook_add(UC_HOOK_BLOCK, lambda uc, address, size, data: hits.add(address), begin=lo,
                              end=RANGES[lo])
    o.load()
    with open(a.out, 'wb') as f:
        f.write(b'P3PROBE1')
        rec = to.Recorder(o, functions, f, 1 << 30)
        rec.active = True
        SCENARIOS[a.scenario](o)
        for _ in range(a.days * 256):
            o.advance_tick()
        f.write(b'E')
    print(f'{rec.count} samples in {a.out}')
    if a.blocks:
        print('blocks run: ' + ' '.join(f'{x:#x}' for x in sorted(hits)))


if __name__ == '__main__':
    main()
