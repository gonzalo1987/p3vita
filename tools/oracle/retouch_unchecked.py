"""Retouched states for the branches no save reaches (task
cloud/unchecked-branches, docs/cloud/unchecked-branches.md): records like
town_oracle.py --barcos starting from the save changed after the load by a
scenario, and tells which of the scenario's target branches the game ran.
tick_probe repeats each recorded call from the game's state just before it
(without --continuo: p3vita doesn't know the retouch).

usage: python3 retouch_unchecked.py <Patrician3.exe> <save.raw> <out.probe> <scenario>
       --barcos DATA --cpr p2arch0_eng.cpr [--days N] [--functions F,F...] [--seed S]
       python3 retouch_unchecked.py --list

A scenario changes the state after the load and may make direct calls to game
functions or run operations through the executor (0x535760) while the
recorder is on (each through a trampoline, so the recorder sees the call and
tick_probe repeats it); then the world runs its days. Its targets are blocks
("51a48e") or edges between blocks ("53275c>532c1f": the block at 0x532C1F
right after the one at 0x53275C) of the game's code: block hooks follow the
functions of RANGES and the script prints each target as hit or MISSING.
Block addresses are the emulator's translation blocks: a block starts at a
jump target, after a conditional jump or after a call.

Unless a scenario says otherwise it uses the first AI merchant with an
office and records the functions in its FUNCTIONS.
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import retouch as rt  # noqa: E402
import town_oracle as to  # noqa: E402
from unicorn import UC_HOOK_BLOCK  # noqa: E402

WORLD = to.WORLD
PIRATES = 0x6DD7AC  # 5 pointers to the pirate objects (0x18 bytes)
TOWN_IDS = 0x6DE4B8  # u8 per town: its number in StadtDaten.ini
HANSE_TOWN = 0x6DE52E  # u8: the town of the head of the Hanse
TRAMPOLINES = to.SENTINEL + 0x400  # free part of the sentinel page: 0x40 bytes per trampoline

# The game's functions followed by the block hooks: [begin, end].
RANGES = [
    (0x515A60, 0x515ADA),  # free town number
    (0x51A470, 0x51A4B6),  # a ship's pirate
    (0x5326E0, 0x532C38),  # the candidacy
    (0x53FD10, 0x53FE01),  # operation 0x85
    (0x4EAFE4, 0x4EBF23),  # event 0x32
    (0x4EBF54, 0x4EC32F),  # a task starts
    (0x4EC354, 0x4EC5DE),  # a task done
    (0x51A4E0, 0x51A741),  # artillery
    (0x51A750, 0x51A808),  # a level more
    (0x516170, 0x51627B),  # a new pirate ship
    (0x5144A0, 0x514528),  # the pirates reinforce their lair
    (0x53A3E0, 0x53A61D),  # operation 0x3B
    (0x53B440, 0x53B4CE),  # operation 0x4B
    (0x4F9C80, 0x4FA12E),  # a site for a building
    (0x51FAB0, 0x51FE88),  # the town's works: a building
    (0x5250D0, 0x525212),  # where to look for a well
    (0x5254C0, 0x52555F),  # into the works
    (0x521900, 0x522626),  # build
    (0x523830, 0x523D6D),  # a tower
    (0x523D70, 0x52409A),  # a gate
    (0x522C70, 0x523829),  # the next wall piece
    (0x51F3A0, 0x51FA2D),  # what building costs
    (0x4F8A50, 0x4F8AE9), (0x4F8C70, 0x4F8C92), (0x4F8560, 0x4F8684),  # reputation for buildings
    (0x535B05, 0x535C30),  # the executor's cases 0x22..0x27
    (0x53CE30, 0x53CF44),  # operation 0x22
    (0x537025, 0x53702F),  # the executor's end
]


# --- Helpers ---

def w8(o, a, v):
    o.uc.mem_write(a, bytes([v & 0xFF]))


def w16(o, a, v):
    o.uc.mem_write(a, struct.pack('<H', v & 0xFFFF))


def w32(o, a, v):
    o.uc.mem_write(a, struct.pack('<I', v & 0xFFFFFFFF))


_trampolines = {}


def direct(o, target, args, ecx=0, cdecl=False):
    """Calls the game's `target` through a trampoline (push the arguments;
    call; ret), so the recorder sees the call. Returns eax."""
    key = (target, len(args), cdecl)
    at = _trampolines.get(key)
    if at is None:
        at = _trampolines[key] = TRAMPOLINES + 0x40 * len(_trampolines)
        n = len(args)
        code = bytes([0xFF, 0x74, 0x24, 4 * n]) * n
        code += bytes([0xE8]) + struct.pack('<i', target - (at + len(code) + 5))
        if cdecl and n:
            code += bytes([0x83, 0xC4, 4 * n])
        code += bytes([0xC2, 4 * n, 0x00])
        assert len(code) <= 0x40 and at + 0x40 <= to.SENTINEL + 0x1000
        o.uc.mem_write(at, code)
    return o.call(at, args, ecx=ecx)


def operation(o, *words, fmt=None):
    """Runs an operation (0x14 bytes) with the game's executor, as the oracle
    does at the end of a tick (with the mark tick_probe looks for)."""
    op = struct.pack(fmt, *words) if fmt else struct.pack('<5I', *(list(words) + [0] * (5 - len(words))))
    op = op.ljust(to.OPERATION_SIZE, b'\0')
    buffer = o.alloc(to.OPERATION_SIZE)
    o.uc.mem_write(buffer, op)
    o.tick_operations = []
    o.call(to.EXECUTOR_TRAMPOLINE, list(struct.unpack('<5I', op)) + [to.EXECUTOR_MAGIC], ecx=buffer)
    print(f'operation {op.hex()}')


def pirates(o):
    """[(index, address)] of the pirate objects."""
    return [(k, o.u32(PIRATES + 4 * k)) for k in range(5) if o.u32(PIRATES + 4 * k)]


def pirate_ships(o, k):
    """The ships of pirate k (list [+0xC], next [+0x4])."""
    out, s, ships = [], o.u16(o.u32(PIRATES + 4 * k) + 0xC), o.counts()[3]
    while s < ships:
        out.append(s)
        s = o.u16(o.ship(s) + 0x4)
    return out


def merchant_ships(o, m):
    out, s, ships = [], o.u16(o.merchant(m) + 0xE), o.counts()[3]
    while s < ships:
        out.append(s)
        s = o.u16(o.ship(s) + 0x4)
    return out


def task_events(o, merchant=None):
    """[(index, address)] of the events 0x32 (the Hanse's tasks), with the
    given merchant if any."""
    return [(i, a) for i, a in rt.events(o)
            if o.u16(a + 6) == 0x32 and (merchant is None or o.u8(a + 0xD) == merchant)]


def show_task(o, i, a, what=''):
    raw = bytes(o.uc.mem_read(a, 0x18))
    print(f'task {i}{what}: tick {o.u32(a):#x}, kind {raw[0xC]}, merchant {raw[0xD]}, started {raw[0xF]}, '
          f'deadline {o.u32(a + 8):#x}, data {raw[0x10:].hex()}')


def set_task(o, task, kind=None, merchant=None, started=None, deadline=None, at=None, **fields):
    """Changes a task (index, address): its kind, merchant, [+0xF], deadline
    (ticks from now), bytes (f12=1 → [+0x12]; w14=… a u16) and, with `at`
    (ticks from now), moves it in the queue to that tick."""
    i, a = task
    if kind is not None:
        w8(o, a + 0xC, kind)
    if merchant is not None:
        w8(o, a + 0xD, merchant)
    if started is not None:
        w8(o, a + 0xF, started)
    if deadline is not None:
        w32(o, a + 8, o.u32(WORLD + 0x14) + deadline)
    for name, value in fields.items():
        (w16 if name[0] == 'w' else w8)(o, a + int(name[1:], 16), value)
    if at is not None:
        rt.move_event(o, i, a, o.u32(WORLD + 0x14) + at)
    show_task(o, i, a)


def candidacy_tasks(o, m):
    """The game's candidacy for merchant m (retouch.run_candidacy): the
    tasks it creates, {kind: (index, address)}."""
    tasks = rt.run_candidacy(o, m)
    print('tasks: ' + ', '.join(f'{k}: {i}' for k, (i, _) in sorted(tasks.items())))
    return tasks


def inspect(o):
    towns, offices, merchants, ships = o.counts()
    player = o.u32(to.PLAYER)
    settings = o.u32(to.SETTINGS_POINTER)
    hanse = o.u8(HANSE_TOWN)
    print(f'{towns} towns, {offices} offices, {merchants} merchants, {ships} ships, player {player}, '
          f'difficulty [0x13] {o.u8(settings + 0x13)}, Hanse town {hanse} ([0x810] {o.u32(o.town(hanse) + 0x810)})')
    for k, p in pirates(o):
        s = pirate_ships(o, k)
        print(f'pirate {k}: nest {o.u8(p + 0xE)}, [+0x0] {o.u32(p):#x}, [+0x14] {o.u8(p + 0x14)}, ships {s}, '
              f'captains {[o.u16(o.ship(x) + 0x42) for x in s]}')
    print(f'AI merchants with an office: {rt.ai_with_office(o)}')


# --- alderman.cpp: the candidacy (0x5326E0) ---

def candidate(o, retouch=None):
    """The first AI merchant with an office, mayor with [+0x37] = 0 and
    reputation to stand (retouch.make_candidate): while recording, `retouch`
    changes the state and 0x4F75F0 runs on his town (the candidacy, 0x5326E0,
    queues its operations, which run at the end of the next tick)."""
    m = rt.ai_with_office(o)[0]
    town = rt.make_candidate(o, m)

    def during(o):
        if retouch:
            retouch(o)
        o.tick_operations = []
        direct(o, 0x4F75F0, [town], ecx=o.merchant(m))
    return during


def candidacy_pirate_index(o):
    """The candidacy with every pirate ship's [+0x158] = 5: 0x51A470 doesn't
    find the ship's pirate and takes the first that exists."""
    def retouch(o):
        for k, _ in pirates(o):
            for s in pirate_ships(o, k):
                w8(o, o.ship(s) + 0x158, 5)
    return candidate(o, retouch)


def candidacy_no_targets(o):
    """The candidacy with no pirate ship with a captain (no task 2) and every
    town with a market at 2001 inhabitants or more (no task 4)."""
    def retouch(o):
        for k, _ in pirates(o):
            for s in pirate_ships(o, k):
                w16(o, o.ship(s) + 0x42, 0xFFFF)
        for i in range(o.counts()[0]):
            t = o.town(i)
            if o.u32(t + 0x76C) & 0x100 and o.u32(t + 0x2D4) < 2001:
                w32(o, t + 0x2D4, 2001)
    return candidate(o, retouch)


def found_towns(o, n):
    """Founds towns with the game's code (a task 0 started by the event
    queue, retouch.found_town) until there are n."""
    while o.counts()[0] < n:
        rt.found_town(o)


def candidacy_26_towns(o):
    """26 towns (founded with the game's code before recording): the
    candidacy doesn't create the task of founding one. Save 3 (in save 1 the
    second founding task isn't created: the game finds it equal to the
    first, the same mask of wares)."""
    found_towns(o, 26)
    return candidate(o)


# --- alderman.cpp: the operations 0x85, 0x3B and 0x4B ---

ROAD_WARES = (0x0B, 0x13, 0x0C, 0x11)


def hanse_ops(o):
    """Operations 0x85, 0x3B and 0x4B with what the interface or a network
    game could send, run with the executor: 0x85 of kind 5; 0x3B with an
    event out of the queue, a town out of the world, an event that isn't a
    task, a task that isn't a road, another merchant, another town, no
    segments left and a merchant out of the world (on a task not given:
    the game takes the event's address as the amount missing); 0x4B with a
    merchant and an event out of range, an event that isn't a task, a task
    already given, a task of kind 5 and a task not given but started
    ([+0xF] = 1: 0x4EBF54 does nothing)."""
    towns, _, merchants, _ = o.counts()
    m = rt.ai_with_office(o)[0]
    player = o.u32(to.PLAYER)
    tasks = candidacy_tasks(o, m)
    queue = rt.events(o)
    count = o.u16(to.EVENTS + 0xC)
    other = next((i, a) for i, a in queue if o.u16(a + 6) != 0x32)
    road = tasks[1]
    a, b = o.u8(road[1] + 0x10), o.u8(road[1] + 0x11)
    elsewhere = next(t for t in range(towns) if t not in (a, b))

    def during(o):
        operation(o, 0x85, o.u32(WORLD + 0x14) + 0x100, 5)
        # 0x3B (u16 event, u16 amount, u8 town, u8 merchant, u8 material) on the road task not given.
        fmt = '<IHHBBBBQ'
        operation(o, 0x3B, count, 1, a, 0xFF, 0, 0, 0, fmt=fmt)
        operation(o, 0x3B, road[0], 1, towns, 0xFF, 0, 0, 0, fmt=fmt)
        operation(o, 0x3B, other[0], 1, a, 0xFF, 0, 0, 0, fmt=fmt)
        if 4 in tasks:
            operation(o, 0x3B, tasks[4][0], 1, a, 0xFF, 0, 0, 0, fmt=fmt)
        operation(o, 0x3B, road[0], 1, a, m, 0, 0, 0, fmt=fmt)
        operation(o, 0x3B, road[0], 1, elsewhere, 0xFF, 0, 0, 0, fmt=fmt)
        for k, amount, material in ((0, 3, 0), (1, 300, 0x13), (2, 5, 0x0C), (3, 7, 0x11), (3, 2, 0x22)):
            operation(o, 0x3B, road[0], amount, b, 0xFF, material, 0, 0, fmt=fmt)
        days = o.u8(road[1] + 0x12)
        w8(o, road[1] + 0x12, 0)
        operation(o, 0x3B, road[0], 1, a, 0xFF, 0, 0, 0, fmt=fmt)
        w8(o, road[1] + 0x12, days)
        # 0x4B ({0x4B, merchant, event}): the player, candidate without a task.
        w8(o, o.merchant(player) + 0x37, 1)
        operation(o, 0x4B, merchants, road[0])
        operation(o, 0x4B, player, count)
        operation(o, 0x4B, player, other[0])
        if 0 in tasks:
            w8(o, tasks[0][1] + 0xD, m)
            operation(o, 0x4B, player, tasks[0][0])
        k5 = tasks.get(2) or tasks.get(3)
        if k5:
            w8(o, k5[1] + 0xC, 5)
            operation(o, 0x4B, player, k5[0])
        w8(o, road[1] + 0xF, 1)
        operation(o, 0x4B, player, road[0])
        show_task(o, *road)
    return during


# --- alderman.cpp: a task with a merchant (event 0x32 from 0x4EB0EB) ---

def task_branches(o):
    """Tasks moved to the next ticks, each for an odd branch of the event
    0x32 with a merchant: a road between two towns that aren't a pair of
    0x672BF8 (the default deadline); a task of kind 5 given and not started
    (0x4EBF54's default); a pirate ship hunted ([+0x16] = 1: done); a road
    started with its first town out of the world and another with its
    second (removed)."""
    towns, _, merchants, _ = o.counts()
    ais = rt.ai_with_office(o)
    tasks = candidacy_tasks(o, ais[0])
    # Not a road of the table: towns 0 and 1 unless they are one.
    ids = {(o.u8(TOWN_IDS + i), o.u8(TOWN_IDS + j)): (i, j) for i in range(towns) for j in range(towns) if i != j}
    pairs = {(0x03, 0x04), (0x09, 0x0A), (0x0B, 0x0D), (0x0C, 0x0E), (0x1C, 0x1F), (0x24, 0x25), (0x26, 0x27)}
    i, j = next(v for k, v in sorted(ids.items(), key=lambda x: x[1]) if k not in pairs and k[::-1] not in pairs)
    set_task(o, tasks[1], merchant=ais[0], started=0, f10=i, f11=j, at=1)
    k5 = tasks.get(4)
    set_task(o, k5, kind=5, merchant=ais[1], started=0, at=2)
    if 2 in tasks:
        set_task(o, tasks[2], merchant=ais[2], started=1, deadline=0x3000, f16=1, at=3)
    if 0 in tasks:
        set_task(o, tasks[0], kind=1, merchant=ais[3], started=1, deadline=0x3000, f10=0xFF, f11=1, f12=3, at=4)
    if 3 in tasks:
        set_task(o, tasks[3], kind=1, merchant=ais[4], started=1, deadline=0x3000, f10=0, f11=0xFF, f12=3, at=5)


def new_town_market(o):
    """A town founded with the game's code (retouch.found_town) gets its
    market (bit 0x100 of [0x76C]) and its task is looked at in the next
    tick: done, and the merchant's [+0x37] goes up 2."""
    town = rt.found_town(o)
    w32(o, o.town(town) + 0x76C, o.u32(o.town(town) + 0x76C) | 0x100)
    task = next((i, a) for i, a in task_events(o) if o.u8(a + 0xC) == 0 and o.u8(a + 0x16) == town)
    set_task(o, task, at=1)


# --- alderman.cpp: the pirate lair (kind 3) ---

def nest_branches(o):
    """Tasks of the lair (kind 3) given to AI merchants, each looked at in
    the next ticks: in phase 0 with a pirate that doesn't exist; in phase 1
    with a lair that isn't the pirate's; in phase 1 with the attacker
    ([+0x14]) at the lair and with one away from it."""
    ais = rt.ai_with_office(o)
    tasks = candidacy_tasks(o, ais[0])
    ps = pirates(o)
    k, p = ps[0]
    nest = o.u8(p + 0xE)
    ships = [s for x, _ in ps for s in pirate_ships(o, x)]
    at_nest, away = ships[0], ships[1]
    x = o.s32(to.TOWN_TABLE + 0x34 * nest + 0x20)
    y = o.s32(to.TOWN_TABLE + 0x34 * nest + 0x24)
    w16(o, o.ship(at_nest) + 0x1E, x)
    w16(o, o.ship(at_nest) + 0x22, y)
    w16(o, o.ship(away) + 0x1E, x + 5)
    print(f'pirate {k}, lair {nest} at ({x}, {y}); ship {at_nest} there, ship {away} at ({x + 5}, {y})')
    order = [t for t in (tasks.get(0), tasks.get(1), tasks.get(2), tasks.get(4)) if t]
    setups = [dict(f10=7, f11=nest, f12=0), dict(f10=k, f11=(nest + 1) & 0xFF, f12=1),
              dict(f10=k, f11=nest, f12=1, w14=at_nest), dict(f10=k, f11=nest, f12=1, w14=away)]
    for n, (task, fields) in enumerate(zip(order, setups)):
        set_task(o, task, kind=3, merchant=ais[n + 1], started=1, deadline=0x3000, at=n + 1, **fields)


def nest_reinforce(o, away=False):
    """retouch.guarida_cerca (a ship of the merchant near the lair: phase 1
    and the pirates reinforce it, 0x5144A0 for each pirate: the others have
    another lair) with the town of the head of the Hanse at the top level
    (0x52BB00 = 5: 0x51A4E0 takes 4). With away, the lair's pirate is out
    ([+0x0] = 1): no reinforcement."""
    hanse = o.u8(HANSE_TOWN)
    w32(o, o.town(hanse) + 0x810, 4000000)
    task = rt.nest_task(o, near=(3, 4))
    if away:
        w32(o, o.u32(PIRATES + 4 * o.u8(task[1] + 0x10)), 1)
    show_task(o, *task)


def nest_reinforce_away(o):
    nest_reinforce(o, away=True)


# --- alderman.cpp: artillery and levels of a ship (0x51A4E0, 0x51A750) ---

GUN_WEIGHT = (1000, 1000, 2000, 2000, 2000, 1000)  # 0x672CB4
GUN_POWER = (9, 10, 22, 24, 30, 18)  # 0x672CC8


def arm(o, s, slots):
    """Sets the ship's 24 artillery slots ([+0x13C]) to `slots` (0xFF free,
    7 unusable, 6 the second half of a big piece) and its weight [+0x11C]
    and strength [+0x120] to theirs."""
    a = o.ship(s)
    slots = list(slots) + [0xFF] * (24 - len(slots))
    o.uc.mem_write(a + 0x13C, bytes(slots))
    pieces = [x for x in slots if x < 6]
    w32(o, a + 0x11C, sum(GUN_WEIGHT[x] for x in pieces))
    w32(o, a + 0x120, sum(GUN_POWER[x] for x in pieces))


def ship_artillery(o):
    """Direct calls to 0x51A4E0 and 0x51A750 on AI ships (thiscall on the
    ship): strength already reached; no room; level 1; level 6 (it takes 4);
    big pieces that don't fit; small pieces and pairs of small pieces
    replaced by big ones (with and without room for a small one); a level
    more for a ship at level 2."""
    ships = [s for m in rt.ai_with_office(o) for s in merchant_ships(o, m)]
    big = sorted(ships, key=lambda s: -o.s32(o.ship(s) + 0x10))

    def during(o):
        for n, (s, strength, level, slots, hold) in enumerate((
                (big[0], 5, 3, [1], None),
                (big[1], 500, 3, [], 0x11C),
                (big[2], 500, 1, [], None),
                (big[3], 500, 6, [], None),
                (big[4], 5000, 4, [], 5000),
                (big[5], 400, 3, [1, 0xFF, 1, 1, 0, 1, 7, 7, 1, 8, 0xFF, 0xFF], None),
                (big[6], 400, 2, [1, 1, 0, 0, 1, 7, 1, 1, 9, 1], 1500),
                (big[7], 400, 4, [2, 6, 1, 1, 1, 0xFF, 0, 1], 0))):
            a = o.ship(s)
            arm(o, s, slots)
            if hold == 0x11C:
                w32(o, a + 0x118, o.s32(a + 0x10) - o.s32(a + 0x11C) - 999)
            elif hold is not None:
                w32(o, a + 0x118, o.s32(a + 0x10) - o.s32(a + 0x11C) - hold)
            print(f'ship {s}: hold {o.s32(a + 0x10)}, cargo {o.s32(a + 0x118)}, guns {o.s32(a + 0x11C)}, '
                  f'strength {o.u32(a + 0x120)} → {strength}, level {level}, slots {bytes(o.uc.mem_read(a + 0x13C, 24)).hex()}')
            direct(o, 0x51A4E0, [strength, level], ecx=a)
            print(f'  → strength {o.u32(a + 0x120)}, slots {bytes(o.uc.mem_read(a + 0x13C, 24)).hex()}')
        s = big[8]
        w8(o, o.ship(s) + 0xF, 2)
        direct(o, 0x51A750, [1], ecx=o.ship(s))
        w8(o, o.ship(s) + 0xF, 1)
        direct(o, 0x51A750, [3], ecx=o.ship(s))
    return during


# --- construction.cpp ---

SCRATCH = 0x6DFF00  # 0x100 bytes of the globals the game doesn't use: pointer arguments of direct calls
SCRATCH_X, SCRATCH_Y = 0x6DFFF0, 0x6DFFF4
NORTH_WEST, NORTH_EAST = 0xE0, 0x20  # 0x67792F, 0x677929: the houses' orientations
GRID = 0x80C  # town_class1 [+0x68]: the grid, height [0x7AC] bytes per column


def store(o, t, n=1000000):
    """The town's store with n of bricks, timber, tools and hemp."""
    a = o.town(t)
    for off in (0x50, 0x30, 0x34, 0x48):
        w32(o, a + off, n)


def grid_at(o, t, x, y):
    a = o.town(t)
    return o.u32(a + GRID) + o.s32(a + 0x7AC) * x + y


def set_cell(o, t, x, y, v):
    w8(o, grid_at(o, t, x, y), v)


def cells(o, t, kind):
    """The (x, y) of the grid cells with (cell & 0x3F) == kind."""
    a = o.town(t)
    w, h = o.s32(a + 0x7A8), o.s32(a + 0x7AC)
    data = bytes(o.uc.mem_read(o.u32(a + GRID), w * h))
    return [(i // h, i % h) for i, c in enumerate(data) if c & 0x3F == kind]


def records(o, t, head):
    """[(index, x, y, owner, type, pending)] of the town's list at [head]."""
    a = o.town(t)
    n, base, out, r = o.u16(a + 0x776), o.u32(a + 0x75C), [], o.u16(a + head)
    while r < n:
        rec = bytes(o.uc.mem_read(base + 8 * r, 8))
        out.append((r, rec[0], rec[1], rec[2], rec[3], rec[6]))
        r = struct.unpack_from('<H', rec, 4)[0]
    return out


def find_site(o, t, kind, x, y, param=NORTH_EAST, flag=0):
    """Direct call to 0x4F9C80 (with x and y in the scratch globals)."""
    w8(o, SCRATCH_X, x)
    w8(o, SCRATCH_Y, y)
    found = direct(o, 0x4F9C80, [SCRATCH_X, SCRATCH_Y, kind, param, flag], ecx=o.town(t) + 0x7A4) & 0xFF
    return found, o.u8(SCRATCH_X), o.u8(SCRATCH_Y)


def build_op(o, owner, t, x, y, kind, param=0):
    operation(o, 0x22, owner, t, x, y, kind, param, 0, 0, fmt='<IBBBBBBBB')


def office_town(o, m):
    """The town of merchant m's first office."""
    first = o.u16(o.merchant(m) + 0xC)
    return o.u8(o.office(first) + 0x2C6)


def site_search(o):
    """Direct calls in town 0: 0x4F9C80 for every type from the statue (but
    0x34 and 0x35, whose mask the game leaves unset: see construction.cpp),
    both orientations and both values of the flag; from out of the grid; from
    a cell that already is a site. 0x51FAB0 for a type < 0x15 without a site,
    the houses (next to one of their type in the list), the bath, the
    shipyard and, building at once, a tower and a house. 0x5250D0 with the
    statue near the left edge and the wells counted so that x wraps."""
    t = 0
    a = o.town(t)
    sx, sy = o.u8(a + 0x800), o.u8(a + 0x801)

    def during(o):
        for kind in range(0x3A):
            if kind in (0x34, 0x35):
                continue
            for param, flag in ((NORTH_EAST, 0), (NORTH_WEST, 1)):
                found = find_site(o, t, kind, sx, sy, param, flag)
                print(f'0x4F9C80 type {kind:#x} param {param:#x} flag {flag}: {found}')
        print(f'from out of the grid: {find_site(o, t, 0x10, 0xFF, 0xFF)}')
        ok, x, y = find_site(o, t, 0x10, sx, sy)
        print(f'from a site: {find_site(o, t, 0x10, x, y)}')
        store(o, t)
        for kind in (0x02, 0x26, 0x2E) + tuple(range(0x15, 0x1E)):
            print(f'0x51FAB0 type {kind:#x}: {direct(o, 0x51FAB0, [kind, 0], ecx=a)}')
        for kind in (0x30, 0x15):
            print(f'0x51FAB0 type {kind:#x} at once: {direct(o, 0x51FAB0, [kind, 1], ecx=a)}')
        w8(o, a + 0x800, 10)
        for wells in (4, 6, 12):
            w8(o, a + 0x789, wells)
            w8(o, SCRATCH_X, 0)
            w8(o, SCRATCH_Y, 0)
            direct(o, 0x5250D0, [SCRATCH_X, SCRATCH_Y], ecx=a)
            print(f'0x5250D0, statue x 10, {wells} wells: ({o.u8(SCRATCH_X)}, {o.u8(SCRATCH_Y)})')
    return during


def construction_merchant(o):
    """An AI merchant with an office and the town of that office."""
    m = rt.ai_with_office(o)[0]
    return m, office_town(o, m)


def build_ops(o):
    """Operations 0x22 (0x53CE30 → 0x521900) in the town of an AI merchant's
    office, on grid cells set to the site of each type (to the right of the
    statue): the town builds a fisherman with [0x2CC] = 0 (the hemp halved),
    a town hall it already has, a business outside the walls in a siege, an
    arsenal (bit 0x8 cleared: the wall from point 0, 0x522B20), a bath it
    already has, a well with too many, the 0x2A and 0x2B without their bits;
    the merchant builds a store (0x4FF2D0 on his office), a 0x29 and a 0x2C
    (0x4F8A50), a 0x2A and a 0x2B (0x4F8C70) and a 0x22 (0x4F8560); another
    merchant without an office there builds from his docked ship (0x4F5450).
    A direct call to 0x521900 with type 0x30. Operations 0x37 (0x4F8560):
    town out of the world, not enough money, already paid, 0x6DE52A set.
    Operations 0x27 (out of the world, with the hemp bit), 0x23, 0x24 and
    0x22 (out of the world) and 0x22 of types 0x36..0x39 (a wall, 0x522B20)."""
    m, t = construction_merchant(o)
    a = o.town(t)
    towns, _, merchants, _ = o.counts()
    other = next(x for x in rt.ai_with_office(o) if t not in [o.u8(o.office(f) + 0x2C6) for f in rt.merchant_offices(o, x)]
                 and merchant_ships(o, x))
    ship = merchant_ships(o, other)[0]
    sx, sy = o.u8(a + 0x800), o.u8(a + 0x801)
    print(f'town {t}, merchant {m} (office), merchant {other} (no office, ship {ship}), statue ({sx}, {sy})')

    def during(o):
        store(o, t)
        w32(o, o.merchant(m), 50000000)
        w32(o, o.merchant(other), 50000000)
        w, h = o.s32(a + 0x7A8), o.s32(a + 0x7AC)
        spot = iter([(x, y) for y in range(max(sy - 8, 2), min(sy + 9, h - 2), 4)
                     for x in range(max(sx - 10, 2), min(sx + 11, w - 2), 2) if (x, y) != (sx, sy)])

        def at(value):
            x, y = next(spot)
            set_cell(o, t, x, y, value)
            print(f'cell ({x}, {y}) = {value:#x}; works {len(records(o, t, 0x762))}')
            return x, y
        bits = o.u32(a + 0x76C)
        w32(o, a + 0x2CC, 0)
        build_op(o, 0xFF, t, *at(0x0E), 0x05)
        w32(o, a + 0x76C, bits | 0x3)
        build_op(o, 0xFF, t, *at(0x1B), 0x20)
        w32(o, a + 0x2C8, o.u32(a + 0x2C8) | 0x10)
        build_op(o, 0xFF, t, *at(0x30), 0x04)
        w32(o, a + 0x2C8, o.u32(a + 0x2C8) & ~0x10)
        w32(o, a + 0x76C, (bits | 0x4) & ~0x8)
        point = bytes(o.uc.mem_read(a + 0x7B0, 2))
        w8(o, a + 0x7B0, sx)
        w8(o, a + 0x7B1, sy)
        build_op(o, 0xFF, t, *at(0x14), 0x21)
        o.uc.mem_write(a + 0x7B0, point)
        w32(o, a + 0x76C, bits | 0x4010)
        build_op(o, 0xFF, t, *at(0xFF), 0x26)
        w8(o, a + 0x789, 200)
        build_op(o, 0xFF, t, *at(0x84), 0x28)
        w32(o, a + 0x76C, bits | 0x600)
        build_op(o, 0xFF, t, *at(0xFF), 0x2A)
        w32(o, a + 0x76C, (bits | 0x1000) & ~0x200)
        build_op(o, 0xFF, t, *at(0xFF), 0x2B)
        w32(o, a + 0x76C, bits)
        build_op(o, m, t, *at(0xFF), 0x1E, NORTH_EAST)
        w32(o, a + 0x2D4, 100000)
        w8(o, a + 0x773, 0)
        w8(o, a + 0x771, 0)
        build_op(o, m, t, *at(0xFF), 0x29)
        build_op(o, m, t, *at(0xFF), 0x2C)
        w32(o, a + 0x76C, (bits & ~0x1400) | 0x200)
        build_op(o, m, t, *at(0xFF), 0x2A)
        build_op(o, m, t, *at(0xFF), 0x2B)
        w32(o, a + 0x76C, (bits | 0x10) & ~0x2000)
        build_op(o, m, t, *at(0x1A), 0x22)
        s = o.ship(ship)
        w8(o, s + 0x39, t)
        w16(o, s + 0x134, 1)
        w32(o, s + 0x54 + 4 * 0x13, 40 * 2000)
        build_op(o, other, t, *at(0x30), 0x04)
        x, y = at(0xFF)
        direct(o, 0x521900, [0xFF, x, y, 0x30, 0], ecx=a)
        for town, amount, money, paid, flag in ((towns, 10, None, 0, 0), (t, 10, 5, 0, 0), (t, 10, None, 1, 0),
                                                (t, 10, None, 0, 1), (t, 10, None, 0, 0)):
            ma = o.merchant(m)
            w32(o, ma + 0x468, (o.u32(ma + 0x468) | (1 << t)) if paid else (o.u32(ma + 0x468) & ~(1 << t)))
            w32(o, ma, money if money is not None else 50000000)
            w8(o, 0x6DE52A, flag)
            operation(o, 0x37, m, town, amount)
        w8(o, 0x6DE52A, 0)
        operation(o, 0x27, towns, 1, 1, 1)
        operation(o, 0x27, t | 0x80000000, 2000, 2000, 200)
        operation(o, 0x23, 0, towns)
        operation(o, 0x24, m, towns, 0)
        build_op(o, m, towns, sx, sy, 0x04)
        for kind in (0x36, 0x37, 0x38, 0x39):
            build_op(o, 0xFF, t, sx, sy, kind)
    return during


def tower_ops(o, pick=None):
    """Operations 0x22 of towers and gates (0x523830, 0x523D70) in the town
    of an AI merchant's office: a new tower on a tower cell (0x1C: into the
    works at the end, 0x5254C0); a tower where there is no piece; in a siege,
    on a wall piece (0x36) and on a 0x32; a gate (0x34) on a finished piece
    by the merchant, who has an office there."""
    m, t = (pick or construction_merchant)(o)
    a = o.town(t)
    walls = records(o, t, 0x768)
    piece = next(r for r in walls if r[4] == 0x36)
    finished = next(r for r in walls if r[5] == 0 and r[0] != piece[0])
    big = next((r for r in walls if r[4] == 0x32), None)
    towers = cells(o, t, 0x1C)
    print(f'town {t}: tower cells {towers}, wall piece {piece}, finished {finished}, 0x32 {big}')

    def during(o):
        store(o, t)
        w32(o, a + 0x700, 50000000)
        if towers:
            build_op(o, 0xFF, t, *towers[0], 0x30)
        build_op(o, 0xFF, t, o.u8(a + 0x800), o.u8(a + 0x801), 0x30)
        w32(o, a + 0x2C8, o.u32(a + 0x2C8) | 0x10)
        build_op(o, m, t, piece[1], piece[2], 0x30)
        if big:
            build_op(o, m, t, big[1], big[2], 0x32)
        w32(o, a + 0x2C8, o.u32(a + 0x2C8) & ~0x10)
        build_op(o, m, t, finished[1], finished[2], 0x34)
    return during


def town_with_big(o):
    """The town of an AI merchant's office with a 0x32 piece in [0x768]."""
    for m in rt.ai_with_office(o):
        for f in rt.merchant_offices(o, m):
            t = o.u8(o.office(f) + 0x2C6)
            if any(r[4] == 0x32 for r in records(o, t, 0x768)):
                return m, t
    return construction_merchant(o)


def tower_ops_big(o):
    """tower_ops in a town where there is a 0x32 piece."""
    return tower_ops(o, town_with_big)


def wall_ops(o):
    """Operations 0x24 (0x522C70) in the town of an AI merchant's office:
    in a siege (nothing); and with a single pending piece ([0x766] cut after
    the first one that is not a wall, or the first) that the merchant can
    pay: the wall is closed (the pieces of that side out, the counts again,
    the debts of the offices...)."""
    m, t = construction_merchant(o)
    a = o.town(t)
    pending = records(o, t, 0x766)
    print(f'town {t}: {len(pending)} pending pieces, first {pending[:3]}')

    def during(o):
        store(o, t)
        w32(o, a + 0x700, 50000000)
        w32(o, a + 0x2C8, o.u32(a + 0x2C8) | 0x10)
        operation(o, 0x24, m, t, 0)
        w32(o, a + 0x2C8, o.u32(a + 0x2C8) & ~0x10)
        first = pending[0]
        w16(o, o.u32(a + 0x75C) + 8 * first[0] + 4, 0xFFFF)
        operation(o, 0x24, m, t, 0 if first[4] == 0x36 else 1)
        print(f'pending now {records(o, t, 0x766)}, [0x76C] {o.u32(a + 0x76C):#x}')
    return during


def cost_fisherman(o):
    """Direct calls to 0x51F3A0 (its 0x44-byte structure in the scratch
    globals) for a merchant with an office in the town: the fisherman (type
    5) with [0x2CC] = 0 and with 0x400, and a tower."""
    m, t = construction_merchant(o)
    a = o.town(t)

    def during(o):
        for kind, hemp in ((5, 0), (5, 0x400), (0x30, 0)):
            w32(o, a + 0x2CC, hemp)
            o.uc.mem_write(SCRATCH, bytes(0x44))
            w16(o, SCRATCH + 0x3C, kind)
            w16(o, SCRATCH + 0x3E, m)
            print(f'0x51F3A0 type {kind:#x}, [0x2CC] {hemp:#x}: {direct(o, 0x51F3A0, [SCRATCH], ecx=a) & 0xFF}')
    return during


def chapel_reputation(o):
    """Direct calls to 0x4F8A50 and 0x4F8C70 on an AI merchant: a town
    without inhabitants; without `count` (only operation 0x25 calls with 0);
    with few 0x29 and 0x2C for the inhabitants and with enough."""
    m, t = construction_merchant(o)
    a, ma = o.town(t), o.merchant(m)

    def during(o):
        people = o.s32(a + 0x2D4)
        w32(o, a + 0x2D4, 0)
        direct(o, 0x4F8A50, [t, 1], ecx=ma)
        w32(o, a + 0x2D4, people)
        direct(o, 0x4F8A50, [t, 0], ecx=ma)
        w8(o, a + 0x773, 0)
        w8(o, a + 0x771, 0)
        direct(o, 0x4F8A50, [t, 1], ecx=ma)
        w8(o, a + 0x773, 50)
        direct(o, 0x4F8A50, [t, 1], ecx=ma)
        direct(o, 0x4F8C70, [t], ecx=ma)
    return during


class Scenario:
    def __init__(self, setup, functions, days, targets):
        self.setup, self.functions, self.days, self.targets = setup, functions, days, targets


EVENTS_FUNCTIONS = [0x4D85C0, 0x4EBF54]
BUILD_FUNCTIONS = [0x535760, 0x521900, 0x523830, 0x522C70, 0x4F8A50, 0x4F8C70, 0x4F8560, 0x5254C0]
SCENARIOS = {
    'candidacy_pirate_index': Scenario(candidacy_pirate_index, [0x5326E0, 0x535760], 1, ['51a48e']),
    'candidacy_no_targets': Scenario(candidacy_no_targets, [0x5326E0, 0x535760], 1,
                                     ['5329dc>532c1f', '532ba2>532c1f']),
    'candidacy_26_towns': Scenario(candidacy_26_towns, [0x5326E0, 0x535760], 1, ['53275c>532c1f']),
    'hanse_ops': Scenario(hanse_ops, [0x535760, 0x4EBF54], 0, [
        '53fd10>53fe00', '53a3e0>53a613', '53a3fc>53a613', '53a41f>53a613', '53a430>53a613', '53a43a>53a613',
        '53a452>53a613', '53a45b>53a613', '53a619', '53b440>53b4cc', '53b453>53b4cc', '53b471>53b4cc',
        '53b47a>53b4cc', '53b488>53b4cc', '4ebf89>4ec30c']),
    'task_branches': Scenario(task_branches, EVENTS_FUNCTIONS, 1, [
        '4ec108', '4ebf9a>4ec30c', '4eb7aa', '4eb44a>4ebf0f', '4eb45d>4ebf0f']),
    'new_town_market': Scenario(new_town_market, EVENTS_FUNCTIONS, 1, ['4eb41e', '4ec481']),
    'nest_branches': Scenario(nest_branches, EVENTS_FUNCTIONS, 1, ['4ebd06', '4eb972', '4eb99e', '4eb9d9']),
    'nest_reinforce': Scenario(nest_reinforce, EVENTS_FUNCTIONS + [0x5144A0, 0x516170, 0x51A4E0, 0x51A750], 1,
                               ['5144a0>514526', '51a530']),
    'nest_reinforce_away': Scenario(nest_reinforce_away, EVENTS_FUNCTIONS + [0x5144A0], 1, ['5144b0>514526']),
    'ship_artillery': Scenario(ship_artillery, [0x51A4E0, 0x51A750], 0, [
        '51a4e0>51a73c', '51a50a>51a73c', '51a51e>51a73c', '51a530', '51a566', '51a5f0', '51a637', '51a76d>51a806']),
    # construction.cpp
    'site_search': Scenario(site_search, [0x4F9C80, 0x51FAB0, 0x5250D0, 0x4FA7F0, 0x521900, 0x523830], 0, [
        '4f9c80>4fa10d', '4f9cc9>4fa10d', '4f9ca6', '4f9cbb', '4f9cef', '4f9d2e', '4f9d3d', '4f9d4c', '4f9d99',
        '4f9dae', '4f9e02', '4f9e17', '4f9e30', '4f9e47', '4f9e59', '4f9e68', '4f9e91', '4f9e9b', '4f9eab>4f9ee2',
        '51fae0>51fe7f', '51fbbc', '51fbc0', '51fc05', '51fc17', '51fc4d', '51fdb3', '51fdd9', '525177']),
    'build_ops': Scenario(build_ops, BUILD_FUNCTIONS, 0, [
        '521a08', '521da3', '521ae2', '521e12', '52207f', '5220e5', '522169', '5221c1', '522533', '52255a',
        '5225a2', '522592', '4f8aa8', '522415', '521919', '4f8560>4f8672', '4f858f>4f8672', '4f85af>4f8661',
        '4f85c5>4f8661', '4f85d3',
        '535bbc>537025', '535be8', '535b12>537025', '535b44>537025', '53ce30>53cef2', '53cf25']),
    'tower_ops': Scenario(tower_ops, BUILD_FUNCTIONS, 0, [
        '52552b', '523927>523d66', '523990', '523e3d']),
    'tower_ops_big': Scenario(tower_ops_big, BUILD_FUNCTIONS, 0, ['5239c0>523d66']),
    'wall_ops': Scenario(wall_ops, BUILD_FUNCTIONS, 0, ['522ca3>523803', '5230dc']),
    'cost_fisherman': Scenario(cost_fisherman, [0x51F3A0], 0, ['51f44c']),
    'chapel_reputation': Scenario(chapel_reputation, [0x4F8A50, 0x4F8C70], 0, [
        '4f8a50>4f8ae6', '4f8ac7', '4f8aa8']),
}


class Blocks:
    """The blocks run in RANGES and the edges between consecutive ones of
    the same emulator run."""

    def __init__(self, o):
        self.blocks, self.edges, self.last = set(), set(), None
        for lo, hi in RANGES:
            o.uc.hook_add(UC_HOOK_BLOCK, self._hit, begin=lo, end=hi)
        start = o.uc.emu_start

        def emu_start(*args, **kwargs):
            self.last = None
            return start(*args, **kwargs)
        o.uc.emu_start = emu_start

    def _hit(self, uc, address, size, data):
        self.blocks.add(address)
        if self.last is not None:
            self.edges.add((self.last, address))
        self.last = address

    def report(self, targets):
        missing = 0
        for t in targets:
            if '>' in t:
                a, b = (int(x, 16) for x in t.split('>'))
                hit = (a, b) in self.edges
            else:
                a = int(t, 16)
                hit = a in self.blocks
            missing += not hit
            print(f'target {t}: {"hit" if hit else "MISSING"}')
            if not hit:
                near = sorted(e for e in self.edges if e[0] == a or e[1] == a)
                print('  edges there: ' + ' '.join(f'{x:x}>{y:x}' for x, y in near[:12]))
        print(f'targets: {len(targets) - missing} of {len(targets)} hit')


def main():
    if '--list' in sys.argv:
        for name, s in SCENARIOS.items():
            print(f'{name}: {(s.setup.__doc__ or "").strip().splitlines()[0]}')
        return
    p = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    p.add_argument('exe')
    p.add_argument('save')
    p.add_argument('out')
    p.add_argument('scenario', choices=sorted(SCENARIOS) + ['inspect'])
    p.add_argument('--barcos', metavar='DATA', required=True)
    p.add_argument('--cpr', required=True)
    p.add_argument('--days', type=int)
    p.add_argument('--functions')
    p.add_argument('--seed', type=int, default=1)
    a = p.parse_args()
    o = to.TownOracle(a.exe, open(a.save, 'rb').read(), a.seed, a.barcos, rest=list(to.REST), cpr=a.cpr)
    if a.scenario == 'inspect':
        o.load()
        inspect(o)
        return
    s = SCENARIOS[a.scenario]
    blocks = Blocks(o)
    o.load()
    during = s.setup(o)
    functions = [int(x, 16) for x in a.functions.split(',')] if a.functions else s.functions
    days = s.days if a.days is None else a.days
    with open(a.out, 'wb') as f:
        f.write(b'P3PROBE1')
        rec = to.Recorder(o, functions, f, 1 << 30)
        rec.active = True
        if callable(during):
            during(o)
        for _ in range(days * 256):
            o.advance_tick()
        f.write(b'E')
    print(f'{rec.count} samples in {a.out}')
    blocks.report(s.targets)


if __name__ == '__main__':
    main()
