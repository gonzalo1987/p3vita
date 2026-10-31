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
    (0x5289B0, 0x528CB2),  # the candidates for mayor
    (0x5458D0, 0x545B11), (0x5462F0, 0x5463BC),  # a new town
    (0x531D50, 0x532286),  # found a town
    (0x4ECDF4, 0x4ECF5E),  # event 0x39
    (0x4EA0D4, 0x4EA590),  # event 0x2F
    (0x535FD6, 0x536050),  # operation 0x46
    (0x510C50, 0x510F98), (0x51D410, 0x51D4B9), (0x51D4C0, 0x51D5E6),  # the militia
    (0x51FF30, 0x520507),  # the works advance
    (0x510AA0, 0x510C4B),  # what the weaponsmith makes
    (0x4D4400, 0x4D4985),  # an office business's average costs
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


# --- new_town.cpp ---

MALE_NAMES, FAMILY_NAMES = 0x6DDB70, 0x6DDB74  # u16: the numbers of names of the lists (0x6DDAA0 + 0xD0, + 0xD4)


def candidate_wraps(o, t, seed, step):
    """Which wraps of 0x5289B0's series (names, families, [0x6EC]) a call with
    seed and step would make in town t (the same steps as the game)."""
    a = o.town(t)
    nm, nf = o.u16(MALE_NAMES), o.u16(FAMILY_NAMES)
    v, hit = seed & 0xFFFFFFFF, set()
    lists = {}
    for base, n, tag in ((0x6D4, nm, 'names'), (0x6D8, nf, 'families')):
        got = []
        for i in range(4):
            v %= n
            c = 0
            while c < i:
                if got[c] == v:
                    v += 1
                    if v >= n:
                        v = 0
                        hit.add(tag)
                    c = 0
                    continue
                c += 1
            got.append(v)
            v = (v + i + step) & 0xFFFFFFFF
        lists[base] = got
    # (The names may change against the other towns: ignored, it only moves the series' start below.)
    old = [o.u8(a + 0x6E4 + c) for c in range(4)]
    e8 = []
    for i in range(4):
        v %= nf
        e8.append(v)
        c = 0
        while c < i:
            if old[c] == v:
                v += 1
                if v >= nm:
                    v = 0
                c = 1
                continue
            c += 1
        v = (v + i + step) & 0xFFFFFFFF
    ec = []
    for i in range(4):
        v %= nf
        c = 0
        while c < i + 4:
            other = lists[0x6D8][c] if c < 4 else (e8 + ec)[c]
            if other == v:
                v += 1
                if v >= nf:
                    v = 0
                    hit.add('6ec')
                c = 1
                continue
            c += 1
        ec.append(v)
        v = (v + i + step) & 0xFFFFFFFF
    return hit


def mayor_names(o):
    """Direct calls to 0x5289B0 (the candidates for mayor) in town 0 with
    series whose names, families and [0x6EC] wrap round the end of their
    lists; then town 1 gets town 0's candidates and town 0 draws again with
    the same series: they meet another town's (the name or the family moves
    on, in turns)."""
    nm, nf = o.u16(MALE_NAMES), o.u16(FAMILY_NAMES)
    calls = []
    for tag in ('names', 'families', '6ec'):
        for step in (0, 1, 2, nm, nf):
            seed = next((s for s in range(4 * max(nm, nf)) if tag in candidate_wraps(o, 0, s, step)), None)
            if seed is not None:
                calls.append((seed, step))
                break
    print(f'{nm} names, {nf} families; series {calls}')

    def during(o):
        for seed, step in calls:
            direct(o, 0x5289B0, [0, seed, step], ecx=o.town(0))
        seed, step = 12345, 7
        direct(o, 0x5289B0, [1, seed, step], ecx=o.town(0))
        o.uc.mem_write(o.town(1) + 0x6D4, bytes(o.uc.mem_read(o.town(0) + 0x6D4, 8)))
        direct(o, 0x5289B0, [1, seed, step], ecx=o.town(0))
    return during


def new_town_bits(o):
    """Direct calls to 0x5458D0 (what is left of a new town; without its flag)
    on town 0 with the bits of `wares` the founding never gives (0x20000,
    0x80000, 0x100000, 0x200000) and with 0x20000 in `other`; and to 0x5462F0
    with facilities out of 4..20."""
    a = o.town(0)

    def during(o):
        direct(o, 0x5458D0, [0, 0x3A0000 | 0x7, 0x18], ecx=a)
        direct(o, 0x5458D0, [0, 0x10, 0x20000], ecx=a)
        for k in (3, 0x15):
            direct(o, 0x5462F0, [k], ecx=a)
    return during


def river_town(o):
    """retouch.fundar with the number of the new town changed to a free one
    whose StadtDaten.ini entry is on a river ([+0x2F]): 0x531D50 sets the bit
    1 of [0x2C8]. The world runs until 0x4EBF54 founds it."""
    used = {o.u8(TOWN_IDS + i) for i in range(o.counts()[0])} | {o.u8(p + 0xE) for _, p in pirates(o)}
    river = [n for n in range(40) if o.u8(to.TOWN_TABLE + 0x34 * n + 0x2F)]
    print(f'numbers on a river: {river}; taken {sorted(used)}')
    i, a = rt.fundar(o)
    n = o.u8(a + 0x14)
    if n not in river:
        w8(o, to.TOWN_TABLE + 0x34 * n + 0x2F, 1)
        print(f'number {n} (the task\'s) put on a river')


def new_event(o, tick, kind, **fields):
    """A new event (0x4D8CF0) at `tick` (from now) with its kind and bytes
    (b8=… a u8 at [+0x8], d10=… a u32)."""
    a = o.call(0x4D8CF0, [o.u32(WORLD + 0x14) + tick], ecx=to.EVENTS)  # returns the event
    i = (a - o.u32(to.EVENTS)) // 0x18
    w16(o, a + 6, kind)
    for name, value in fields.items():
        {'b': w8, 'w': w16, 'd': w32}[name[0]](o, a + int(name[1:], 16), value)
    print(f'event {i}: {kind:#x} at +{tick:#x}, {bytes(o.uc.mem_read(a + 8, 0x10)).hex()}')
    return i, a


def new_town_events(o):
    """Events 0x39 (0x4ECDF4: the new town's wall) and 0x2F (0x4EA0D4) in
    the next ticks, on existing towns: 0x39 of a town out of the world; of
    one with [0x2C8] & 0xE0; of one with pending pieces; of one without
    them and without the point 2 of the wall (0x5252F0 = 2); of one without
    the point 1 (= 1, with walls in the works and without); 0x2F of a town
    out of the world."""
    towns = o.counts()[0]
    for t in range(1, 6):
        a = o.town(t)
        w32(o, a + 0x2C8, o.u32(a + 0x2C8) & ~0xE0)
    for t in (3, 4, 5):
        a = o.town(t)
        w16(o, a + 0x766, o.u16(a + 0x776))
    a = o.town(3)
    w8(o, a + 0x7B4, 0xFF)  # point 2 out of the grid
    for t in (4, 5):
        w8(o, o.town(t) + 0x7B2, 0xFF)  # point 1 out of the grid
    a = o.town(5)
    for r in records(o, 5, 0x762):
        w8(o, o.u32(a + 0x75C) + 8 * r[0] + 3, 0x2F)  # no wall pieces in the works
    new_event(o, 1, 0x39, b8=towns)
    new_event(o, 2, 0x39, b8=0)
    new_event(o, 3, 0x39, b8=1)
    new_event(o, 4, 0x39, b8=3)
    new_event(o, 5, 0x39, b8=4)
    new_event(o, 6, 0x39, b8=5)
    new_event(o, 7, 0x2F, dC=towns)


def mayor_ops(o):
    """Operations 0x46 (0x535FD6): a merchant and a town out of the world; a
    town that has a mayor; an AI merchant without rank 5 at home and without
    the mark (nothing) and with it (mayor)."""
    towns, _, merchants, _ = o.counts()
    free = next(t for t in range(towns) if o.u8(o.town(t) + 0x6F1) >= merchants)
    taken = next(t for t in range(towns) if t != free)
    m = rt.ai_with_office(o)[0]
    w8(o, o.town(taken) + 0x6F1, rt.ai_with_office(o)[1])
    ma = o.merchant(m)
    w8(o, ma + 0x39C + o.u8(ma + 0x19), 2)
    print(f'town {free} without a mayor, town {taken} with one; merchant {m}')

    def during(o):
        operation(o, 0x46, merchants, free, 0, 0)
        operation(o, 0x46, m, towns, 0, 0)
        operation(o, 0x46, m, taken, 0, 0)
        operation(o, 0x46, m, free, 0, 0)
        operation(o, 0x46, m, free, 0, 0xBADEAFFE)
    return during


# --- facilities.cpp ---

WORLD_RECORDS = 0x6DE510  # pointer to the world records (0x14 bytes)
RECORD_COUNT, RECORD_FREE = WORLD + 0x6, WORLD + 0x7C  # u16


def plain_town(o):
    """A town without a merchant mayor and with [0x2C8] & 0xF10 = 0."""
    towns, _, merchants, _ = o.counts()
    return next(t for t in range(towns) if o.u8(o.town(t) + 0x6F1) >= merchants and not o.u32(o.town(t) + 0x2C8) & 0xF10)


def militia(o):
    """Direct calls to 0x510C50 (the militia, facility 0) of a town without
    a merchant mayor: with its employees not 5 per soldier; with guards to
    spare for its inhabitants (one dismissed, 0x51D410) and soldiers of the
    other types to spare for its walls (types 1, 2 and 3 in turn); with the
    human player as mayor. 0x51D410 directly: a type without soldiers nor
    recruits, and the third class with fewer than 20 people. 0x51D4C0 with
    the human player as mayor, with and without an office there."""
    t = plain_town(o)
    a = o.town(t)
    f = a + 0x840
    player = o.u32(to.PLAYER)

    def soldiers(guards, others):
        for i, n in enumerate([guards] + list(others)):
            w8(o, a + 0x998 + i, n)
            w8(o, a + 0x99C + i, 0)
        w16(o, a + 0x844, 5 * (guards + sum(others)))
        w16(o, f + 0x4, 5 * (guards + sum(others)))

    def during(o):
        w32(o, a + 0x700, 10000000)
        soldiers(3, (1, 1, 1))
        w16(o, f + 0x4, 0)
        direct(o, 0x510C50, [], ecx=f)
        soldiers(8, (0, 0, 0))
        w32(o, a + 0x2D4, 300)
        direct(o, 0x510C50, [], ecx=f)
        w32(o, a + 0x2D4, 400)
        w16(o, a + 0x786, 0)
        for others in ((2, 0, 0), (0, 2, 0), (0, 0, 2)):
            soldiers(5, others)
            direct(o, 0x510C50, [], ecx=f)
        w8(o, a + 0x6F1, player)
        direct(o, 0x510C50, [], ecx=f)
        soldiers(2, (0, 0, 0))
        direct(o, 0x51D410, [3], ecx=a)
        w32(o, a + 0x2E0, 5)
        direct(o, 0x51D410, [0], ecx=a)
        w32(o, a + 0x2E4, 1000)
        w32(o, a + 0x54, 1000)
        direct(o, 0x51D4C0, [0], ecx=a)
        office = facilities_office(o, player, t)
        if office is not None:
            w32(o, o.office(office) + 0x54, 1000)
            direct(o, 0x51D4C0, [0], ecx=a)
            print(f'player {player}: office {office} in town {t}')
        for town in range(o.counts()[0]):
            first = facilities_office(o, player, town)
            if first is not None and town != t:
                w8(o, o.town(town) + 0x6F1, player)
                w32(o, o.town(town) + 0x2E4, 1000)
                w32(o, o.office(first) + 0x54, 1000)
                direct(o, 0x51D4C0, [0], ecx=o.town(town))
                print(f'player mayor of town {town}, where he has office {first}')
                break
    return during


def facilities_office(o, m, t):
    """Merchant m's office in town t, or None."""
    return next((f for f in rt.merchant_offices(o, m) if o.u8(o.office(f) + 0x2C6) == t), None)


def works_completion(o):
    """Direct calls to 0x51FF30 (the town's works advance) on a works list
    made of new records (0x51F100) with 1 day left: owned by a merchant with
    an office there, the types of each way to finish (shipyard, to the town,
    wall, tower, big tower, the gate that replaces a piece, free) and by the
    town (free, shipyard, list, flag, replace); first with [0x2C8] & 0x800
    (the 0x30 waits), then without it."""
    m = rt.ai_with_office(o)[0]
    t = office_town(o, m)
    a = o.town(t)
    walls = records(o, t, 0x768)
    gate37 = next((r for r in walls if r[4] == 0x37), walls[0])
    gate38 = next((r for r in walls if r[4] == 0x38), walls[0])
    plan = [(m, 1, None), (m, 3, None), (m, 0x30, None), (m, 0x36, None), (m, 0x37, None), (m, 0x34, gate37),
            (m, 2, None), (0xFF, 2, None), (0xFF, 1, None), (0xFF, 0x1F, None), (0xFF, 0x2E, None), (0xFF, 0x35, gate38)]
    base = o.u32(a + 0x75C)
    link = a + 0x762
    while o.u16(link) < o.u16(a + 0x776):
        link = base + 8 * o.u16(link) + 4
    for owner, kind, at in plan:
        r = o.call(0x51F100, [], ecx=a) & 0xFFFF
        base = o.u32(a + 0x75C)
        x, y = (at[1], at[2]) if at else (o.u8(a + 0x800), o.u8(a + 0x801))
        o.uc.mem_write(base + 8 * r, bytes([x, y, owner, kind]) + struct.pack('<H', 0xFFFF) + bytes([1, 0]))
        w16(o, link, r)
        link = base + 8 * r + 4
        print(f'record {r}: owner {owner}, type {kind:#x} at ({x}, {y})')

    def during(o):
        w32(o, a + 0x2C8, o.u32(a + 0x2C8) | 0x800)
        direct(o, 0x51FF30, [200], ecx=a)
        w32(o, a + 0x2C8, o.u32(a + 0x2C8) & ~0x800)
        direct(o, 0x51FF30, [200], ecx=a)
    return during


def world_records(o):
    """Direct calls on the world records: 0x4D36E0 (an office's business)
    with 6 and 9 of the type; 0x5306B0 with no free record left (the table
    grows by 0x40); 0x530780 on a record already free and on a business with
    employees (with fewer poor than twice them and with more)."""
    count = o.u16(RECORD_COUNT)
    table = o.u32(WORLD_RECORDS)
    business = next(i for i in range(count) if o.u8(table + 0x14 * i + 6) < 0x15 and o.u8(table + 0x14 * i + 7) < o.counts()[0]
                    and o.u16(table + 0x14 * i + 0x8) == 0xFFFF and i != o.u16(RECORD_FREE))
    print(f'{count} world records, free head {o.u16(RECORD_FREE)}, business {business}')

    def during(o):
        rec = o.u32(WORLD_RECORDS) + 0x14 * business
        for n in (6, 9):
            direct(o, 0x4D36E0, [n], ecx=rec)
        free = o.u16(RECORD_FREE)
        if free < o.u16(RECORD_COUNT):
            direct(o, 0x530780, [free], ecx=WORLD)
        town = o.town(o.u8(rec + 7))
        w16(o, rec + 4, 30)
        w32(o, town + 0x2E0, 20)
        direct(o, 0x530780, [business], ecx=WORLD)
        w16(o, RECORD_FREE, o.u16(RECORD_COUNT))
        print(f'0x5306B0 → {direct(o, 0x5306B0, [], ecx=WORLD) & 0xFFFF}, count {o.u16(RECORD_COUNT)}')
    return during


def wall_level(o):
    """Direct calls to 0x5252F0 (the wall's level): a town whose first
    pending piece is not the town's and no piece is 0xFE's (0); one without
    pending pieces and without the point 2 (2)."""
    towns = o.counts()[0]
    t1, t2 = 0, 1

    def during(o):
        a = o.town(t1)
        pending = records(o, t1, 0x766)
        w8(o, o.u32(a + 0x75C) + 8 * pending[0][0] + 2, 3)
        for r in records(o, t1, 0x768):
            if r[3] == 0xFE:
                w8(o, o.u32(a + 0x75C) + 8 * r[0] + 2, 0xFD)
        direct(o, 0x5252F0, [], ecx=a)
        a = o.town(t2)
        w16(o, a + 0x766, o.u16(a + 0x776))
        w8(o, a + 0x7B4, 0xFF)
        direct(o, 0x5252F0, [], ecx=a)
    return during


def shipyard_repair(o):
    """Direct calls to 0x508AA0 (the shipyard repairs; thiscall on the ships
    object, with the town and the work) with two AI ships put in the town's
    repair list [0x81E]: one that ends with orders 2 and one that doesn't
    with orders 1."""
    m = rt.ai_with_office(o)[0]
    t = office_town(o, m)
    a = o.town(t)
    s1, s2 = merchant_ships(o, m)[:2]

    def during(o):
        for s, health, orders in ((s1, -10, 2), (s2, -100000, 1)):
            sa = o.ship(s)
            w32(o, sa + 0x18, o.s32(sa + 0x14) + health)
            w8(o, sa + 0x136, orders)
        w16(o, o.ship(s1) + 0x6, s2)
        w16(o, o.ship(s2) + 0x6, 0xFFFF)
        w16(o, a + 0x81E, s1)
        direct(o, 0x508AA0, [t, 1000], ecx=to.SHIPS)
    return during


def weaponsmith(o):
    """Direct calls on the weaponsmith (facility 3) of town 0: 0x510AA0 with
    the weapon it makes ([0x6D0] = 0x15) still in its range (kept); 0x50F6E0
    with its [+0x0] at 0 (it becomes 0x400)."""
    a = o.town(0)
    f = a + 0x840 + 16 * 3

    def during(o):
        w32(o, a + 0x6D0, 0x15)
        direct(o, 0x510AA0, [], ecx=f)
        w32(o, f, 0)
        direct(o, 0x50F6E0, [a, 0x15], ecx=f)
    return during


def office_recount(o):
    """Direct calls to 0x4FF2D0 (an office recounts its buildings) on an AI
    merchant's office: with [0x2F0] > 0; with a store (0x1E) still pending
    in its list; with one of its businesses (world records, [0x2CC]) whose
    buildings are gone (it is removed: 0x50E380 and 0x530780)."""
    def buildings(f):
        oa = o.office(f)
        a = o.town(o.u8(oa + 0x2C6))
        n, r, k = o.u16(a + 0x776), o.u16(oa + 0x2CE), 0
        while r < n:
            k += 1
            r = o.u16(o.u32(a + 0x75C) + 8 * r + 4)
        return k
    office, m = max(((f, m) for m in rt.ai_with_office(o) for f in rt.merchant_offices(o, m)), key=lambda x: buildings(x[0]))
    oa = o.office(office)
    t = o.u8(oa + 0x2C6)
    a = o.town(t)
    print(f'merchant {m}, office {office} in town {t} ({buildings(office)} buildings)')

    def during(o):
        w16(o, oa + 0x2F0, 1)
        direct(o, 0x4FF2D0, [], ecx=oa)
        w16(o, oa + 0x2F0, 0)
        base = o.u32(a + 0x75C)
        n = o.u16(a + 0x776)
        r, kinds = o.u16(oa + 0x2CE), []
        while r < n:
            kinds.append((r, o.u8(base + 8 * r + 3)))
            r = o.u16(base + 8 * r + 4)
        print(f'its buildings: {kinds}')
        if kinds:
            first, kind = kinds[0]
            w8(o, base + 8 * first + 3, 0x1E)
            w8(o, base + 8 * first + 6, 3)
            direct(o, 0x4FF2D0, [], ecx=oa)
            gone = [r for r, k in kinds if k == kind]
            for r in gone:
                w8(o, base + 8 * r + 3, 0x1E)
            direct(o, 0x4FF2D0, [], ecx=oa)
    return during


# --- offices.cpp ---

PRICES = 0x6DE3D8  # the prices object


def first_business(o, ai=True):
    """(merchant, office, world record) of the first office with a business."""
    merchants = o.counts()[2]
    for m in (rt.ai_with_office(o) if ai else [o.u32(to.PLAYER)]):
        for f in rt.merchant_offices(o, m):
            r = o.u16(o.office(f) + 0x2CC)
            if r < o.u16(RECORD_COUNT):
                return m, f, r
    raise RuntimeError('no office with a business')


def office_orders(o):
    """Direct calls on AI and human offices: 0x4FFF20 (the office's day) of
    the human player with the orders 0x10 (cleared) and of an AI merchant
    with the orders 2 (0x4FFC20: buys what its businesses need); 0x4FF780 (the
    administrator) without an administrator, selling what passes its stock
    (0x52EA80) and buying more than the town has, for 0x1000000 or more, and
    with a merchant without money."""
    player = o.u32(to.PLAYER)
    human = next((f for f in rt.merchant_offices(o, player)), None)
    m, f, _ = first_business(o)
    oa = o.office(f)
    t = o.u8(oa + 0x2C6)
    a = o.town(t)
    print(f'player office {human}; merchant {m}, office {f} in town {t}')

    def during(o):
        day = o.u16(WORLD + 4)
        if human is not None:
            w16(o, o.office(human) + 0x2D6, 0x10)
            direct(o, 0x4FFF20, [day], ecx=o.office(human))
        w32(o, o.merchant(m), 100000000)
        w16(o, oa + 0x2D6, 2)
        for w in range(24):
            w32(o, oa + 0x64 + 4 * w, o.s32(oa + 4 + 4 * w) + 2000)
        direct(o, 0x4FFF20, [day], ecx=oa)
        direct(o, 0x4FFC20, [], ecx=oa)
        w16(o, oa + 0x2F2, 0xFFFF)
        for w in range(24):
            w32(o, oa + 0x2F4 + 4 * w, 0)
        w32(o, oa + 4 + 4 * 2, 40000)
        w32(o, oa + 0x354 + 4 * 2, 1000)
        w32(o, oa + 0x2F4 + 4 * 2, 30)  # sell fish over its stock for 30 or more
        w32(o, oa + 0x354 + 4 * 0xB, 10000000)
        w32(o, oa + 0x2F4 + 4 * 0xB, -1000)  # buy timber up to a lot, for up to 1000
        w32(o, oa + 0x354 + 4 * 0x13, 1000000000)
        w32(o, oa + 0x2F4 + 4 * 0x13, -1000000000)  # bricks at any price: all the town has, 0x1000000 or more
        w32(o, a + 4 + 4 * 0x13, 1000000000)
        w32(o, oa + 0x354 + 4 * 0x11, 10000000)
        w32(o, oa + 0x2F4 + 4 * 0x11, -1000)  # hemp, with negative thresholds: more than the town has
        for i in range(4):
            w32(o, a + 0x4F0 + 16 * 0x11 + 4 * i, -100000 * (i + 1))
        direct(o, 0x4FF780, [], ecx=oa)
        w32(o, o.merchant(m), 10)
        w32(o, a + 4 + 4 * 0x13, 1000000000)
        w32(o, oa + 4 + 4 * 0x13, 0)
        direct(o, 0x4FF780, [], ecx=oa)
    return during


def reverse_prices(o):
    """Direct calls to 0x52EC50 (the stock below which buying costs more than
    a price) and 0x52EA80 (above which selling pays less) for a ware of town
    0 with prices from far below to far above its base: every interval of
    the price curve; with quality; the town's taxed ware; a town without a
    market."""
    a = o.town(0)

    def during(o):
        for ware in (0, 0xB):
            for price in (1, 5, 10, 20, 30, 40, 50, 60, 80, 100, 150, 200, 300, 500, 1000, 3000, 100000):
                direct(o, 0x52EC50, [ware, 0, price, 0], ecx=PRICES)
                direct(o, 0x52EA80, [ware, 0, price], ecx=PRICES)
        direct(o, 0x52EC50, [0xB, 0, 50, 0x80], ecx=PRICES)
        taxed = o.u8(a + 0x6F0)
        w8(o, a + 0x6F0, 0xB)
        direct(o, 0x52EC50, [0xB, 0, 50, 0], ecx=PRICES)
        w8(o, a + 0x6F0, taxed)
        w32(o, a + 0x76C, o.u32(a + 0x76C) & ~0x100)
        direct(o, 0x52EC50, [0xB, 0, 50, 0], ecx=PRICES)
        direct(o, 0x52EA80, [0xB, 0, 50], ecx=PRICES)
        direct(o, 0x52EA80, [0xB, 0, -50], ecx=PRICES)
    return during


def office_business(o):
    """Direct calls on an AI office's business (a world record): 0x4D4400
    (the average costs) as a type out of 4..20, as fishing (5) without fish
    and as cattle (10) without leather and without meat; 0x4D3670 with more
    workers than it wants (dismissed, 0x50E380); 0x4D3560 (wages) as fishing
    in a town with [0x2CC] for the human player. 0x4F8690 and 0x4F8840 (the
    merchant's sales and purchases) without `flag`, with totals that
    saturate, and 0x4F8690 with a ware ≥ 0x14."""
    m, f, rec_index = first_business(o)
    oa, ma = o.office(f), o.merchant(m)
    player = o.u32(to.PLAYER)

    def during(o):
        rec = o.u32(WORLD_RECORDS) + 0x14 * rec_index
        kind = o.u8(rec + 6)
        w8(o, rec + 6, 3)  # (with 0x15 the game divides by 0 first: see offices.cpp)
        direct(o, 0x4D4400, [oa, 10, 0], ecx=rec)
        w32(o, o.town(o.u8(rec + 7)) + 0x2CC, 0x400)
        for k, stock in ((5, (0xC, 0x2C)), (10, (0x38, 0x8))):
            w8(o, rec + 6, k)
            for off in stock:
                w32(o, oa + off, 0)
            direct(o, 0x4D4400, [oa, 0, 0], ecx=rec)
            direct(o, 0x4D4400, [oa, 0, 5], ecx=rec)
        w8(o, rec + 6, kind)
        w16(o, rec + 4, 500)
        direct(o, 0x4D3670, [0x1234], ecx=rec)
        w8(o, rec + 6, 5)
        t = o.u8(rec + 7)
        w32(o, o.town(t) + 0x2CC, 0x400)
        direct(o, 0x4D3560, [o.merchant(player), oa], ecx=rec)
        w8(o, rec + 6, kind)
        t = o.u8(oa + 0x2C6)
        for fn, field in ((0x4F8690, 0x49C), (0x4F8840, 0x4A0)):
            direct(o, fn, [t, 2, 100, 10, 0], ecx=ma)
            w32(o, ma + field, 0x7FFFFF00)
            direct(o, fn, [t, 2, 0x1000, 10, 0], ecx=ma)
            w32(o, ma + field + 0x14, 0x7FFFFF00)
            direct(o, fn, [t, 2, 0x1000, 10, 1], ecx=ma)
        direct(o, 0x4F8690, [t, 0x14, 100, 10, 1], ecx=ma)
    return during


# --- businesses.cpp, population.cpp, merchants.cpp ---

JOB_OFFERS = 0x6DD628  # u32 count, s16 who at +4 + 2·k, u16 wanted at +0x82 + 2·k


def business_staff(o):
    """Direct calls on town 0's businesses: 0x50E380 (dismiss) with more than
    its workers, with and without `all`; 0x5109B0 with more than its workers;
    0x50FBC0 (the workshop) with pig iron below zero and the timber at the
    top (the subtraction overflows)."""
    a = o.town(0)
    k = next(k for k in range(4, 21) if o.u16(a + 0x840 + 16 * k + 4) > 2)
    f = a + 0x840 + 16 * k
    workshop = a + 0x840 + 16 * 7
    print(f'facility {k} with {o.u16(f + 4)} workers')

    def during(o):
        n = o.u16(f + 4)
        direct(o, 0x50E380, [n + 5, 1], ecx=f)
        w16(o, f + 4, n)
        direct(o, 0x50E380, [n + 5, 0], ecx=f)
        w16(o, f + 4, n)
        direct(o, 0x5109B0, [n + 5], ecx=f)
        w16(o, workshop + 4, 50)
        w32(o, a + 4 + 4 * 0xB, 0x7FFFFFF0)
        w32(o, a + 4 + 4 * 0x10, 0xFFFFFF00)
        direct(o, 0x50FBC0, [a, 60], ecx=workshop)
    return during


def housing(o):
    """Direct calls to 0x520840 (the occupancy of the houses) in every town
    with each class 1, 2 and 3 places short of full: float rounding can make
    the step negative there (exactly it is (places − occupancy)² / ...)."""
    towns = o.counts()[0]

    def during(o):
        for t in range(towns):
            a = o.town(t)
            places = [o.u16(a + 0x77E + 2 * k) for k in range(3)]
            for less in (1, 2, 3):
                for k in range(3):
                    w16(o, a + 0x778 + 2 * k, max(places[k] - less, 0))
                direct(o, 0x520840, [a + 0x778, o.u16(a + 0x764)], ecx=a)
    return during


def job_offers(o):
    """A direct call to 0x51CBE0 (the beggars take jobs) with two offers of an
    office's business that can't be: a world record out of the table and one
    of another town."""
    m, f, rec_index = first_business(o)
    t = o.u8(o.office(f) + 0x2C6)
    other = (t + 1) % o.counts()[0]
    a = o.town(t)

    def during(o):
        w32(o, a + 0x2E4, 1000)
        w16(o, a + 0x2FC, o.s32(a + 0x2E0) + 500)
        w32(o, a + 0x76C, o.u32(a + 0x76C) | 0x200)
        w32(o, JOB_OFFERS, 2)
        w16(o, JOB_OFFERS + 4, ~o.u16(RECORD_COUNT) & 0xFFFF)
        w16(o, JOB_OFFERS + 0x82, 20)
        w16(o, JOB_OFFERS + 6, ~rec_index & 0xFFFF)
        w16(o, JOB_OFFERS + 0x84, 20)
        rec = o.u32(WORLD_RECORDS) + 0x14 * rec_index
        w8(o, rec + 7, other)
        direct(o, 0x51CBE0, [], ecx=a)
    return during


def bankruptcy(o):
    """Operations 0x65 (0x4F5D80, a merchant goes bankrupt): the human
    player (nothing); an AI merchant who is the mayor of his home town, with
    the surnames set so that the search for a free one passes the divisor
    (the last merchant's surname) and goes back to 0. A direct call to
    0x4F68D0 (a new life) with a3 = 1."""
    towns, _, merchants, _ = o.counts()
    m = rt.ai_with_office(o)[0]
    ma = o.merchant(m)
    home = o.u8(ma + 0x19)
    day = o.u16(WORLD + 4)
    g = o.u16(0x6DD888)
    surnames = o.u16(FAMILY_NAMES)
    last = merchants - 1
    # The bankrupt merchant's own surname is 0 by then (0x4F68D0), so a value
    # in 1..s0 − 1 must be free or the game never stops looking.
    divisor = next(d for d in range(3, 40) if (g + day) % d >= 2)
    s0 = (g + day) % divisor
    held = {x: o.u8(o.merchant(x) + 0x1D) for x in range(merchants)}
    used = {v for v in held.values() if v < surnames}
    pool = [v for v in range(divisor + 1, surnames) if v not in used]
    # Valid surnames only (an index past the list makes the game build a bad name).
    for x, v in held.items():
        if v < s0 and x != last:
            held[x] = pool.pop()
    w8(o, ma + 0x1D, pool.pop())
    spare = [x for x, v in held.items() if v > divisor and x not in (m, last)]
    for v in range(s0, divisor):
        if v not in held.values():
            held[spare.pop()] = v
    held[last] = divisor
    for x, v in held.items():
        w8(o, o.merchant(x) + 0x1D, v)
    w8(o, o.town(home) + 0x6F1, m)
    print(f'merchant {m}, mayor of {home}; surnames {s0}..{divisor - 1} taken, the last one has {divisor}, none below {s0}'
          f' (but his own, 0 after his new life)')

    def during(o):
        operation(o, 0x65, o.u32(to.PLAYER))
        operation(o, 0x65, m)
        direct(o, 0x4F68D0, [o.u32(WORLD + 0x14), 40, m, 1, 0], ecx=o.merchant(rt.ai_with_office(o)[1]))
    return during


def bankruptcy_zero(o):
    """Operation 0x65 with the last merchant's surname 0: the game divides by
    zero (an exception in the emulator; p3vita returns). Nothing to repeat."""
    merchants = o.counts()[2]
    w8(o, o.merchant(merchants - 1) + 0x1D, 0)
    m = rt.ai_with_office(o)[0]

    def during(o):
        try:
            operation(o, 0x65, m)
            print('no exception')
        except RuntimeError as e:
            print(f'the game fails: {e}')
    return during


# --- diet.cpp ---

CONVOYS = 0x6DD7A8  # pointer to the convoys (0x3C bytes)
CONVOY_COUNT, FREE_CONVOY, CONVOY_HEAD = to.SHIPS + 0xF6, to.SHIPS + 0xE4, to.SHIPS + 0xEC


def convoy_list(o, head):
    out, c = [], o.u16(head)
    while c < o.u16(CONVOY_COUNT) and len(out) < 1000:
        out.append(c)
        c = o.u16(o.u32(CONVOYS) + 0x3C * c + 0x8)
    return out


def diet_branches(o):
    """Missions of the Hanse's diet (events 0x34 and 0x24, 0x4E09D4) put at
    the current tick and the event queue (0x4D85C0) called directly, so that
    no other part of the world runs: a town out of the world; a phase past
    10; disbanding (phase 5) a convoy out of the list and one out of the
    table; phase 7 and 8 without a convoy; phase 9 without a convoy, with
    the convoy fighting (state 0x14) and with a convoy out of the list;
    phase 10 without a convoy, out of the list and with the convoy not
    sailing back (state ≠ 0x16). 0x4F8CA0 with a town (only the battles
    pass one)."""
    towns = o.counts()[0]
    listed = convoy_list(o, CONVOY_HEAD)
    free = convoy_list(o, FREE_CONVOY)
    sea = next(c for c in listed if o.u16(o.u32(CONVOYS) + 0x3C * c + 0x12) != 0x16)
    print(f'{len(listed)} convoys listed, {len(free)} free; listed convoy {sea}, free convoy {free[0]}')
    m = rt.ai_with_office(o)[0]

    def during(o):
        # [+0x8] of a 0x34 from phase 2 on is the game's list of notices: none (0).
        new_event(o, 0, 0x34, d8=0, b14=towns, b15=0)
        new_event(o, 0, 0x34, d8=0, b14=1, b15=11)
        new_event(o, 0, 0x34, d8=0, b14=1, b15=5, w16=free[0])
        new_event(o, 0, 0x24, d8=0xFFFFFFFF, dC=0xFFFFFFFF, b14=1, b15=5, w16=0xFFFF)
        new_event(o, 0, 0x34, d8=0, b14=1, b15=7, w16=0xFFFF)
        new_event(o, 0, 0x34, d8=0, b14=1, b15=8, w16=0xFFFF)
        later = o.u32(WORLD + 0x14) + 0x1000
        new_event(o, 0, 0x34, d8=0, b14=1, b15=9, w16=0xFFFF, d10=later)
        new_event(o, 0, 0x34, d8=0, b14=1, b15=9, w16=free[0], d10=later)
        new_event(o, 0, 0x34, d8=0, b14=1, b15=10, w16=0xFFFF)
        new_event(o, 0, 0x34, d8=0, b14=1, b15=10, w16=free[0])
        new_event(o, 0, 0x34, d8=0, b14=1, b15=10, w16=sea)
        direct(o, 0x4D85C0, [o.u32(WORLD + 0x14)], ecx=to.EVENTS)
        state = o.u32(CONVOYS) + 0x3C * sea + 0x12
        old = o.u16(state)
        w16(o, state, 0x14)
        new_event(o, 0, 0x34, d8=0, b14=1, b15=9, w16=sea, d10=later)
        direct(o, 0x4D85C0, [o.u32(WORLD + 0x14)], ecx=to.EVENTS)
        w16(o, state, old)
        direct(o, 0x4F8CA0, [2], ecx=o.merchant(m))
    return during


# --- grid.cpp ---

def grid_branches(o):
    """Direct calls on town 0's grid (thiscall on town_class1): 0x4FACD0 (a
    building leaves the grid) of a fisherman (5) and a pier (0x2F); 0x4FD680
    (inside the walls) with the statue out of the grid; 0x4FD830 after a gate
    0x37 in a cell outside the town, so that 0x4FDA10 looks for the walls:
    with no wall cell (0x91) at all, with the statue inside and with it out
    of the grid; and with a single wall cell, after the statue, whose four
    seeds aren't 0x11."""
    a = o.town(0)
    c1 = a + 0x7A4
    w, h = o.s32(a + 0x7A8), o.s32(a + 0x7AC)
    base = o.u32(a + GRID)
    data = bytearray(o.uc.mem_read(base, w * h))
    sx, sy = o.u8(a + 0x800), o.u8(a + 0x801)
    # A gate cell outside the town: neither (x + 1, y) nor (x, y − 4) inside.
    gx, gy = next((x, y) for x in range(4, w - 4) for y in range(8, h - 8)
                  if not data[h * (x + 1) + y] & 0x80 and not data[h * x + y - 4] & 0x80)
    walls = [i for i, c in enumerate(data) if c == 0x91]
    print(f'grid {w}×{h}, statue ({sx}, {sy}), {len(walls)} wall cells, gate cell ({gx}, {gy})')

    def during(o):
        direct(o, 0x4FACD0, [sx + 3, sy + 4, 5, 0], ecx=c1)
        direct(o, 0x4FACD0, [sx + 6, sy + 8, 0x2F, 0], ecx=c1)
        w8(o, a + 0x800, 0xFF)
        direct(o, 0x4FD680, [], ecx=c1)
        for i in walls:
            w8(o, base + i, 0x11)
        w8(o, a + 0x800, sx)
        direct(o, 0x4FD830, [gx, gy, 0x37], ecx=c1)
        w8(o, a + 0x800, w + 2)
        direct(o, 0x4FD830, [gx, gy, 0x37], ecx=c1)
        w8(o, a + 0x800, sx)
        start = sx * h + sy
        cell = start + 4 * h + 1
        w8(o, base + cell, 0x91)
        for d in (2 * h + 2, -2 * h - 2, -2 * h + 2, 2 * h - 2):
            w8(o, base + cell + d, 0x05)
        direct(o, 0x4FD830, [gx, gy, 0x37], ecx=c1)
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
    # new_town.cpp
    'mayor_names': Scenario(mayor_names, [0x5289B0], 0, ['528a0d', '528a55', '528c1d', '528adb', '528ae3', '528afb']),
    'new_town_bits': Scenario(new_town_bits, [0x5458D0, 0x5462F0], 0, [
        '545958', '54597b', '5459be', '5459d5', '5459ec', '545aab', '5462f0>546387', '546313>546387']),
    'river_town': Scenario(river_town, EVENTS_FUNCTIONS, 2, ['531ff7']),
    'new_town_events': Scenario(new_town_events, [0x4D85C0], 1, [
        '4ecdf4>4ecf58', '4ece19>4ecf58', '4ece48', '4ece9c>4ecf58', '4ecea4', '4ea0f9']),
    'mayor_ops': Scenario(mayor_ops, [0x535760], 0, [
        '535fd6>537025', '535fed>537025', '535ffe>537025', '536039>537025']),
    # facilities.cpp
    'militia': Scenario(militia, [0x510C50, 0x51D410, 0x51D4C0], 0, [
        '510cfb', '@510ebd', '@510f30', '@510f53', '@510f5c', '510e67>510f75', '51d43f>51d4b7', '51d497', '@51d587']),
    'works_completion': Scenario(works_completion, [0x51FF30], 0, [
        '@520022', '520127', '52010c', '5201ac', '5201c7', '5201e9', '52020c', '520298', '520324', '520316', '520352',
        '520369', '5203dd']),
    'world_records': Scenario(world_records, [0x4D36E0, 0x5306B0, 0x530780], 0, ['@4d373e', '@5306d3', '@53079e']),
    'wall_level': Scenario(wall_level, [0x5252F0], 0, ['@525343', '@52535a']),
    'shipyard_repair': Scenario(shipyard_repair, [0x508AA0], 0, ['@508bf6', '@508c20']),
    'weaponsmith': Scenario(weaponsmith, [0x510AA0, 0x50F6E0], 0, ['510b29>510c45', '@50f724']),
    'office_recount': Scenario(office_recount, [0x4FF2D0], 0, ['@4ff31b', '@4ff3d1', '@4ff64e']),
    # offices.cpp
    'office_orders': Scenario(office_orders, [0x4FFF20, 0x4FFC20, 0x4FF780, 0x52EA80], 0, [
        '@5001a8', '@5001c0', '@4ff817', '@4ff83e', '@4ff932', '@4ff958', '@4ff98a>4ffa15']),
    'reverse_prices': Scenario(reverse_prices, [0x52EC50, 0x52EA80], 0, [
        '@52ec80', '@52ecdf', '@52ed25', '@52ed44', '@52ed61', '@52edb0', '@52edff', '@52ee4d', '@52eab5', '@52eb25',
        '@52eb3f', '@52eb8c', '@52ebd9', '@52ec22']),
    'office_business': Scenario(office_business, [0x4D4400, 0x4D3670, 0x4D3560, 0x4F8690, 0x4F8840], 0, [
        '@4d449e>4d497c', '@4d467e', '@4d47c2', '@4d4846', '@4d36d3', '@4d3602', '@4f86df', '@4f888f', '@4f871a',
        '@4f872b>4f8831']),
    # businesses.cpp, population.cpp, merchants.cpp
    'business_staff': Scenario(business_staff, [0x50E380, 0x5109B0, 0x50FBC0], 0, [
        '@50e3b3', '@50e3bd', '@50e3cb', '@5109ca', '@50fc92']),
    'housing': Scenario(housing, [0x520840], 0, ['@520a28']),
    'job_offers': Scenario(job_offers, [0x51CBE0], 0, ['@51ccda>51cd3f', '@51ccf4>51cd3f']),
    'bankruptcy': Scenario(bankruptcy, [0x535760, 0x4F68D0], 0, ['@4f5d99>4f5fbf', '@4f5f79', '@4f5fbc', '@4f68e2']),
    'bankruptcy_zero': Scenario(bankruptcy_zero, [], 0, ['@4f5f57']),
    # diet.cpp
    'diet_branches': Scenario(diet_branches, [0x4D85C0, 0x4F8CA0], 0, [
        '@4e0a10>4e1b36', '@4e0a2e>4e1b36', '@4e1272>4e1830', '@4e13a7>4e1b36', '@4e147d>4e1b18', '@4e1547>4e1b18',
        '@4e16af>4e1b18', '@4e16e7', '@4e170d>4e168f', '@4e18fd>4e1b18', '@4e193d>4e16e9', '@4e195d>4e1b18', '@4f8caf']),
    # grid.cpp
    'grid_branches': Scenario(grid_branches, [0x4FACD0, 0x4FD680, 0x4FD830], 0, [
        '@4fb161', '@4fb1f7', '@4fd693>4fd816', '@4fdb0d>4fdc95', '@4fdb21', '@4fdbb4>4fdc91']),
}


class Blocks:
    """The blocks run in RANGES and the edges between consecutive ones of
    the same emulator run; and the instructions of the '@' targets run."""

    def __init__(self, o, targets=()):
        self.blocks, self.edges, self.last = set(), set(), None
        self.ran = set()
        from unicorn import UC_HOOK_CODE
        self.taken, self.pending = set(), {}
        for t in targets:
            if t.startswith('@') and '>' in t:
                # A jump at A taken to B: B runs right after A (and not A's next instruction).
                a, b = (int(x, 16) for x in t[1:].split('>'))
                o.uc.hook_add(UC_HOOK_CODE, self._jump_from, begin=a, end=a + 15, user_data=a)
                o.uc.hook_add(UC_HOOK_CODE, self._jump_to, begin=b, end=b, user_data=(a, b))
            elif t.startswith('@'):
                a = int(t[1:], 16)
                o.uc.hook_add(UC_HOOK_CODE, lambda uc, address, size, data: self.ran.add(address), begin=a, end=a)
        for lo, hi in RANGES:
            o.uc.hook_add(UC_HOOK_BLOCK, self._hit, begin=lo, end=hi)
        start = o.uc.emu_start

        def emu_start(*args, **kwargs):
            self.last = None
            return start(*args, **kwargs)
        o.uc.emu_start = emu_start

    def _jump_from(self, uc, address, size, a):
        if address == a:
            self.pending[a] = a + size
        elif self.pending.get(a) == address:
            self.pending[a] = None

    def _jump_to(self, uc, address, size, ab):
        if self.pending.get(ab[0]):
            self.taken.add(ab)
        self.pending[ab[0]] = None

    def _hit(self, uc, address, size, data):
        self.blocks.add(address)
        if self.last is not None:
            self.edges.add((self.last, address))
        self.last = address

    def report(self, targets):
        missing = 0
        for t in targets:
            if t.startswith('@') and '>' in t:
                a, b = (int(x, 16) for x in t[1:].split('>'))
                hit = (a, b) in self.taken
            elif t.startswith('@'):
                a = int(t[1:], 16)
                hit = a in self.ran
            elif '>' in t:
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
    blocks = Blocks(o, s.targets)
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
