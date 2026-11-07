"""Retouched states for the branches no save reaches (task
cloud/unchecked-branches, docs/cloud/unchecked-branches.md): records like
town_oracle.py --barcos starting from the save changed after the load by a
scenario, and tells which of the scenario's target branches the game ran.
tick_probe repeats each recorded call from the game's state just before it
(without --continuo: p3vita doesn't know the retouch).

usage: python3 retouch_unchecked.py <Patrician3.exe> <save.raw> <out.probe> <scenario>
       --barcos DATA --cpr p2arch0_eng.cpr [--days N] [--functions F,F...] [--seed S] [--write OUT]
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
from unicorn import UC_HOOK_BLOCK, UC_HOOK_CODE  # noqa: E402

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
    (0x4E9A94, 0x4EA0A8),  # the council's vote (event 0x2E)
    (0x526AA0, 0x526AF3),  # a captain out of a town's list
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


def pirate_into_x_list(o, avoid=()):
    """For a save where no pirate sails (save 1): a pirate ship out of the list by x (not in `avoid`) put in it at
    sea in the state 0x12, with the game's own insertion by x (0x50CE50 from the sea's first, as in 0x506E60).
    Returns it, or None without such a ship."""
    ships = o.counts()[3]
    for k, _ in pirates(o):
        for s in pirate_ships(o, k):
            if s in avoid or o.u16(o.ship(s) + 0xA) < ships or o.u16(o.ship(s) + 0xC) < ships:
                continue
            w16(o, o.ship(s) + 0x134, 0x12)
            o.call(0x50CE50, [s, o.u16(to.SHIPS + 0xEA)], ecx=to.SHIPS)
            print(f'pirate ship {s} put in the list by x: [+0xA] {o.u16(o.ship(s) + 0xA)}, [+0xC] {o.u16(o.ship(s) + 0xC)}')
            return s
    return None


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


def road_expired(o):
    """The human player's road task expires in the next tick (0x4EB25F):
    the game makes an equal task, already started, whose merchant [+0xD] is
    the town [+0x10] (a game bug). The town is under the number of
    merchants, so the copy is that merchant's; the two towns have the bit
    0x8000 of [0x76C], as when the task started. For the fix
    (tick_probe --fix-game-bugs): the copy has no merchant and is removed."""
    towns, _, merchants, _ = o.counts()
    player = o.u32(to.PLAYER)
    tasks = candidacy_tasks(o, rt.ai_with_office(o)[0])
    i, j = 1, 2
    assert i < merchants
    for t in (i, j):
        w32(o, o.town(t) + 0x76C, o.u32(o.town(t) + 0x76C) | 0x8000)
    set_task(o, tasks[1], merchant=player, started=1, deadline=0, f10=i, f11=j, f12=3, at=1)


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


def office_ops(o):
    """Operations 0x5D (0x5362AE) and 0x21 (0x53CE30 with `request`), which
    only the interface queues; both can make a merchant open an office
    (0x51B890). In the town of an AI merchant's office: another AI merchant
    without one there opens one with 0x5D, with a ship docked there carrying
    some bricks and timber (0x4F5450 takes from it what the town doesn't
    give); 0x5D again (he has one now: nothing); 0x5D with the merchant and
    with the town out of the world. A third merchant with 0x21 and the type
    0x2C: first with the town's market empty (0x51F3A0 refuses: nothing), then
    from the market alone. 0x21 with the types 0x57 (the fisherman, 5, on a
    site cell), 0x29 (a tower, 0x33), 0x2F (0x35) and 0x70 (not in the table
    0x672FC0: nothing)."""
    m, t = construction_merchant(o)
    a = o.town(t)
    towns, _, merchants, _ = o.counts()
    others = [x for x in rt.ai_with_office(o) if facilities_office(o, x, t) is None and merchant_ships(o, x)]
    other, third = others[0], others[1]
    ship = merchant_ships(o, other)[0]
    sx, sy = o.u8(a + 0x800), o.u8(a + 0x801)
    print(f'town {t}: merchant {other} (ship {ship}) and merchant {third} without an office there; statue ({sx}, {sy})')

    def request(owner, x, y, kind, param=0):
        operation(o, 0x21, owner, t, x, y, kind, param, 0, 0, fmt='<IBBBBBBBB')

    def offices():
        return f'offices: {other} {facilities_office(o, other, t)}, {third} {facilities_office(o, third, t)}'

    def during(o):
        w32(o, o.merchant(other), 50000000)
        w32(o, o.merchant(third), 50000000)
        s = o.ship(ship)
        w8(o, s + 0x39, t)
        w16(o, s + 0x134, 1)
        w32(o, s + 0x54 + 4 * 0x13, 3 * 2000)
        w32(o, s + 0x54 + 4 * 0xB, 2 * 2000)
        store(o, t)
        operation(o, 0x5D, other, t)
        print(f'{offices()}; ship bricks {o.u32(s + 0x54 + 4 * 0x13)}, timber {o.u32(s + 0x54 + 4 * 0xB)}')
        operation(o, 0x5D, other, t)
        operation(o, 0x5D, merchants, t)
        operation(o, 0x5D, third, towns)
        store(o, t, 0)
        request(third, 0, 0, 0x2C)
        print(offices())
        store(o, t)
        request(third, 0, 0, 0x2C)
        print(offices())
        x, y = sx + 4, sy
        set_cell(o, t, x, y, 0x0E)
        request(0xFF, x, y, 0x57)
        request(0xFF, sx - 4, sy, 0x29)
        request(0xFF, sx, sy + 4, 0x2F)
        request(0xFF, sx, sy - 4, 0x70)
        print(f'works {len(records(o, t, 0x762))}')
    return during


def captain_at(o, c):
    return o.u32(to.SHIPS) + 0x10 * c


def player_pirate_setup(o):
    """The player's first ship in port (state 0, not in a convoy) left without
    its captain, who goes to the head of the town's list of captains
    ([0x82E], next [+0x0]): (ship, town, captain)."""
    towns, _, _, ships = o.counts()
    convoys = o.u16(to.SHIPS + 0xF6)
    s = o.u16(o.merchant(o.u32(to.PLAYER)) + 0xE)
    while s < ships:
        a = o.ship(s)
        if o.u16(a + 0x134) == 0 and o.u8(a + 0x39) < towns and o.u16(a + 8) >= convoys:
            break
        s = o.u16(a + 4)
    else:
        raise RuntimeError('the player has no ship in port')
    a = o.ship(s)
    t, c = o.u8(a + 0x39), o.u16(a + 0x42)
    captains = o.u16(to.SHIPS + 0xF2)
    if c < captains:
        w16(o, a + 0x42, 0xFFFF)
        w16(o, captain_at(o, c), o.u16(o.town(t) + 0x82E))
        w16(o, o.town(t) + 0x82E, c)
    elif o.u16(o.town(t) + 0x82E) < captains:
        c = o.u16(o.town(t) + 0x82E)  # the ship has none: one already in the town's list
    else:
        # The head of another town's list, moved to this one.
        u = next(u for u in range(towns) if o.u16(o.town(u) + 0x82E) < captains)
        c = o.u16(o.town(u) + 0x82E)
        w16(o, o.town(u) + 0x82E, o.u16(captain_at(o, c)))
        w16(o, captain_at(o, c), 0xFFFF)
        w16(o, o.town(t) + 0x82E, c)
    print(f'ship {s} of the player {o.u32(to.PLAYER)} in town {t}: its captain {c} to the town\'s list; '
          f'pirates {[k for k, _ in pirates(o)]}')
    return s, t, c


def player_pirate(o):
    """Operation 0x4E (0x53B850, which only the interface queues): a player's
    ship turns pirate. The player's first ship in port without its captain,
    who is put in the town's list: 0x4E with him (the ship joins the pirate
    nearest to the town: 0x509250 with no winner, 0x4F5900, 0x515EA0, 0x5172A0
    to the town's first waypoint); the same again (the ship is no longer a
    merchant's: refused); before them, with a captain not in the town's list
    (0x526AA0 refuses). Then a day with the ships, the new pirate among them."""
    s, t, c = player_pirate_setup(o)
    towns, _, _, _ = o.counts()

    def during(o):
        operation(o, 0x4E, (c + 1) % o.u16(to.SHIPS + 0xF2), s, 30, t)
        operation(o, 0x4E, c, s, 30, (t + 1) % towns)
        a = o.ship(s)
        print(f'ship {s}: owner {o.u8(a):#x}, state {o.u16(a + 0x134)}, captain {o.u16(a + 0x42)}, '
              f'sailors {o.u16(a + 0x40)}, [+0x159] {o.u8(a + 0x159)}, [+0x4C] {o.u32(a + 0x4C):#x}')
        operation(o, 0x4E, c, s, 30, t)
    return during


def player_pirate_new(o):
    """Operation 0x4E as in player_pirate, with no pirate object (the five
    pointers set to 0): the game makes one in slot 0 (0x513720, 0x513D80 with
    its nest and ships) and the ship joins it. Only the operation."""
    s, t, c = player_pirate_setup(o)
    for k in range(5):
        w32(o, PIRATES + 4 * k, 0)

    def during(o):
        operation(o, 0x4E, c, s, 30, t)
        print(f'pirates now {[k for k, _ in pirates(o)]}; ship {s} owner {o.u8(o.ship(s)):#x}')
    return during


def merchant_lists(o, m):
    """{list: (length word, [u16])} of merchant m's ship lists not empty."""
    a, out = o.merchant(m), {}
    for k in range(11):
        word, pointer = o.u16(a + 0x60C + 2 * k), o.u32(a + 0x624 + 4 * k)
        n = word & 0x7FFF
        out[k] = (word, list(struct.unpack(f'<{n}H', bytes(o.uc.mem_read(pointer, 2 * n)))) if n and pointer else [])
    return {k: v for k, v in out.items() if v[0]}


def player_pirate_lists(o):
    """player_pirate with the player's ship lists filled (the u16 arrays at
    [+0x624 + 4·k], lengths in [+0x60C + 2·k]; empty in every save): list 0
    with the ship, list 3 with an AI ship (not his: out), the ship and another
    of his (kept), with the bit 0x8000; list 9 with the ship twice. Operation
    0x4E (0x509250 → 0x4F5900), then 0x0A on his other ship (0x507FB0 →
    0x4F5900)."""
    s, t, c = player_pirate_setup(o)
    _, _, merchants, ships = o.counts()
    p = o.u32(to.PLAYER)
    pm = o.merchant(p)
    second = next(x for x in merchant_ships(o, p) if x != s)
    ai = next(x for x in range(ships) if o.u8(o.ship(x)) < merchants and o.u8(o.ship(x)) != p)

    def put(k, values, high=0):
        a = o.alloc(2 * len(values) + 4)
        o.uc.mem_write(a, struct.pack(f'<{len(values)}H', *values))
        w32(o, pm + 0x624 + 4 * k, a)
        w16(o, pm + 0x60C + 2 * k, len(values) | high)
    put(0, [s])
    put(3, [ai, s, second], 0x8000)
    put(9, [s, s])
    print(f'lists of the player {p}: {merchant_lists(o, p)} (ship {s}, his other ship {second}, AI ship {ai})')

    def during(o):
        operation(o, 0x4E, c, s, 30, t)
        print(f'after 0x4E: {merchant_lists(o, p)}')
        operation(o, 0x0A, second, p)
        print(f'after 0x0A: {merchant_lists(o, p)}; ship {second} state {o.u16(o.ship(second) + 0x134):#x}')
    return during


def battle_join(o):
    """Operation 0x9F (0x536CD5 → 0x611309; queued by the chase 0x50B250,
    scenario chase_battle): ships join a sea battle and its music
    level [+0x1E94] goes down once per bit of their number. On the battle
    the save starts with (saves 6, 7, tut4): 1 ship, 0x2D, 0x7FFF (the 15
    bits; with bit 15 the game's loop never ends) and 0; a slot without a
    battle; the slot 0xFF; and the battle with [+0xAD2] = 3 (nothing)."""
    slots = [k for k in range(0xFF) if o.u32(to.BATTLES + 4 * k)]
    if not slots:
        raise RuntimeError('no sea battle in this save')
    k = slots[0]
    b = o.u32(to.BATTLES + 4 * k)
    empty = next(x for x in range(0xFF) if not o.u32(to.BATTLES + 4 * x))
    print(f'battle of slot {k} at {b:#x}: [+0xAD2] {o.u8(b + 0xAD2)}, [+0x1E94] {o.u16(b + 0x1E94):#x}')

    def during(o):
        for ships in (1, 0x2D, 0x7FFF, 0):
            operation(o, 0x9F, k, ships)
            print(f'{ships:#x} ships: [+0x1E94] {o.u16(b + 0x1E94):#x}')
        operation(o, 0x9F, empty, 3)
        operation(o, 0x9F, 0xFF, 3)
        side = o.u8(b + 0xAD2)
        w8(o, b + 0xAD2, 3)
        operation(o, 0x9F, k, 0x2D)
        w8(o, b + 0xAD2, side)
        print(f'with [+0xAD2] = 3: [+0x1E94] {o.u16(b + 0x1E94):#x}')
    return during


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


def pier_edge(o):
    """0x524920 (a new pier) in a town whose only pier cell (0x0F) is in the
    last column with y = 1 (the others become 0x0B), with the bit 0x1000000
    of [0x2C8] cleared, timber and tools in the store and many workers: the
    fourth cell is past the grid (the game writes after it) and the third
    (y - 2) is the last row of the previous column. For the fix
    (tick_probe --fix-game-bugs): that one is not written."""
    t = construction_merchant(o)[1]
    a = o.town(t)
    w = o.s32(a + 0x7A8)
    for x, y in cells(o, t, 0x0F):
        if o.u8(grid_at(o, t, x, y)) == 0x0F:
            set_cell(o, t, x, y, 0x0B)
    set_cell(o, t, w - 1, 1, 0x0F)
    w32(o, a + 0x2C8, o.u32(a + 0x2C8) & ~0x1000000)
    store(o, t)
    print(f'town {t}: width {w}, pier cell ({w - 1}, 1)')

    def during(o):
        direct(o, 0x524920, [100000], ecx=a)
    return during


def gate_costs(o):
    """Operation 0x22 of a gate (0x34, 0x523D70) on a finished piece by an AI
    merchant whose office there has some of each material (less than the
    gate needs of bricks and hemp, more of tools than of timber): the game
    takes the tools with the timber's amount, pays 0 for them and nothing to
    the merchant. For the fix (tick_probe --fix-game-bugs): as the towers."""
    m, t = construction_merchant(o)
    a = o.town(t)
    f = facilities_office(o, m, t)
    walls = records(o, t, 0x768)
    finished = next(r for r in walls if r[5] == 0)
    for ware, n in ((0x13, 3 * 2000), (0xB, 2 * 2000), (0xC, 9 * 200), (0x11, 1 * 2000)):
        w32(o, o.office(f) + 4 + 4 * ware, n)
    print(f'town {t}: office {f} of merchant {m}, finished piece {finished}')

    def during(o):
        store(o, t)
        w32(o, a + 0x700, 50000000)
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


def town_offices(o, t):
    """The offices of town t, in its list ([0x784], next [+0x2CA])."""
    offices, out, f = o.counts()[1], [], o.u16(o.town(t) + 0x784)
    while f < offices:
        out.append(f)
        f = o.u16(o.office(f) + 0x2CA)
    return out


def town_tick_fixes(o):
    """Direct calls for three game bugs of the town tick (for
    tick_probe --fix-game-bugs): 0x51C360 (people leaving poor) in a town
    where an office after the first has businesses with staff, without
    workers starting in the facilities (the game adds only the first
    office's businesses); 0x5109B0 on a business with a reserve and a float
    [+0xC] near 1, so that one leaves (the game takes it from the float
    twice: negative); 0x5101D0 on the grain farm (facility 9) with 1 worker
    and no reserve (the game compares the room the other way round)."""
    towns, _, _, _ = o.counts()
    count = o.u16(RECORD_COUNT)

    base = o.u32(WORLD + 0x70)  # the world records, 0x14 bytes each

    def staffed(f):
        r = o.u16(o.office(f) + 0x2CC)
        while r < count:
            if o.u16(base + 0x14 * r + 4):
                return True
            r = o.u16(base + 0x14 * r + 8)
        return False
    t = next(t for t in range(towns) if any(staffed(f) for f in town_offices(o, t)[1:]))
    a = o.town(t)
    for k in range(1, 21):
        w16(o, a + 0x840 + 16 * k + 0xA, 0)
    print(f'town {t}: offices {town_offices(o, t)}')
    b = next(k for k in range(4, 21) if k != 9 and o.u16(o.town(0) + 0x840 + 16 * k + 4) > 4)
    f = o.town(0) + 0x840 + 16 * b
    farm = o.town(0) + 0x840 + 16 * 9
    print(f'business {b} of town 0 with {o.u16(f + 4)} workers; the farm\'s type {o.u8(farm + 6)}')

    def during(o):
        direct(o, 0x51C360, [20], ecx=a)
        w16(o, f + 4, 5)
        w16(o, f + 0xA, 10)
        o.uc.mem_write(f + 0xC, struct.pack('<f', 0.9))
        direct(o, 0x5109B0, [2], ecx=f)
        w16(o, farm + 4, 1)
        w16(o, farm + 0xA, 0)
        direct(o, 0x5101D0, [o.town(0)], ecx=farm)
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


# --- sieges.cpp ---

def siege_object(o, t):
    return o.u32(to.SIEGES) + 0x1024 * t


def siege_ops(o):
    """Operations 0x8E (a siege, 0x633AF0): a town out of the world; a town
    of the region of the human player's home town, where he has rank 5 (he is
    asked for help: operation 0x8B)."""
    towns = o.counts()[0]
    m = o.u32(to.PLAYER)
    home = o.u8(o.merchant(m) + 0x19)
    ready = [i for i in range(towns) if rt.siege_ready(o, i)]
    b = next(i for i in ready if i != home and rt.region(o, i) == rt.region(o, home))
    w8(o, o.merchant(m) + 0x39C + home, 5)
    print(f'town {b}, of the region of {home}, home town of the player {m}')

    def during(o):
        operation(o, 0x8E, towns, 1, 0x80, 1)
        operation(o, 0x8E, b, 2, 0x31, 1)
    return during


def siege_branches(o):
    """A siege made with the game's code (operation 0x8E, without the flag, on
    a town that can have one and where an AI merchant has an office) and
    direct calls on it: 0x629C60 again on its army, not new ([+0x3A] set:
    nothing); 0x62B2B0 (the loader's targets) with [+0x242] set; 0x62E810
    with the first target's record not finished; 0x6281F0 (the army's day)
    attacking a gate 0x34 with ammunition, without pitch and with it;
    0x51D5F0 (a defender lost) with few people in the classes; 0x633DB0 (the
    ransom) with nothing to pay, and with money just under the limit and
    wares to fill it; 0x635220 (what the town lacks) with only one ware in
    stock (over 0x800000) and no wares chosen yet; 0x635440 (supplies) with
    a node that pays past the 0x4FFF cap."""
    towns, _, merchants, _ = o.counts()
    ready = [i for i in range(towns) if rt.siege_ready(o, i)]
    t, m = next((i, x) for i in ready for x in rt.ai_with_office(o) if facilities_office(o, x, i) is not None)
    a = o.town(t)
    s = siege_object(o, t)
    print(f'siege of town {t} (object {s:#x}); merchant {m} has an office there')

    def during(o):
        operation(o, 0x8E, t, 1, 0x80, 0)
        army = o.u32(s + 8)
        print(f'army at {army:#x}: [+0x3A] {o.u16(army + 0x3A)}, targets [+0x242] {o.u16(army + 0x242)}')
        soldiers, other = o.u16(army + 0x3A), o.u16(army + 0x6A)
        w16(o, army + 0x3A, 5)
        w16(o, army + 0x6A, 0)
        direct(o, 0x629C60, [1, 0x80, 0], ecx=army)
        w16(o, army + 0x3A, soldiers)
        w16(o, army + 0x6A, other)
        base = o.u32(a + 0x75C)
        for r in records(o, t, 0x768):
            if r[4] == 0x36 and r[5] == 0:
                w8(o, base + 8 * r[0] + 3, 0x32)  # finished pieces the army can aim at
        # As many as it will find (with fewer the game writes past its block: see sieges.cpp).
        pieces = [r for r in records(o, t, 0x768) if r[4] in (0x32, 0x33) and r[5] == 0]
        w16(o, army + 0x242, len(pieces))
        direct(o, 0x62B2B0, [], ecx=army)
        targets = o.u32(army + 0x250)
        print(f'targets now {o.u16(army + 0x242)} at {targets:#x}')
        if targets and o.u16(army + 0x242):
            w8(o, base + 8 * o.u32(targets + 8) + 6, 3)
        direct(o, 0x62E810, [], ecx=army)
        gate = o.u32(army + 0x244 + 4 * o.u8(army + 0x6D))
        if gate < o.u16(a + 0x776):
            w8(o, base + 8 * gate + 3, 0x34)
        for pitch in (0, 1000000):
            w32(o, army + 0x4, 0x30)
            w8(o, army + 0x78, 0)
            w16(o, army + 0x42, 0)
            w32(o, a + 0x40, pitch)
            direct(o, 0x6281F0, [], ecx=army)
        w8(o, a + 0x999, 2)
        w32(o, a + 0x2E0, 5)
        direct(o, 0x51D5F0, [1], ecx=a)
        offices = [f for f in range(o.u16(WORLD + 8)) if o.u8(o.office(f) + 0x2C6) == t]
        w32(o, s, 0)
        w32(o, a + 0x700, 0)
        for w in range(24):
            w32(o, a + 4 + 4 * w, 0)
            for f in offices:
                w32(o, o.office(f) + 4 + 4 * w, 0)
        direct(o, 0x633DB0, [SCRATCH, SCRATCH + 4, SCRATCH + 8], ecx=s)
        limit = (o.u32(a + 0x2E0) + (o.u32(a + 0x2DC) + o.u32(a + 0x2D8) * 2) * 2) << 4
        w32(o, s, 0)
        # Ware 23 is worth 14.0 a unit: 200 units pay 2800 exactly, so after it
        # the ransom is at the limit and ware 22 is not taken.
        w32(o, a + 0x700, max(limit - 2800, 1))
        w32(o, a + 4 + 4 * 23, 1000)
        w32(o, a + 4 + 4 * 22, 1000)
        print(f'ransom limit {limit}')
        direct(o, 0x633DB0, [SCRATCH, SCRATCH + 4, SCRATCH + 8], ecx=s)
        print(f'ransom: amount {o.u32(SCRATCH)}, ware 23 left {o.u32(a + 4 + 4 * 23)}, ware 22 left {o.u32(a + 4 + 4 * 22)}')
        for x in range(towns):
            for w in range(1, 24):
                w32(o, o.town(x) + 4 + 4 * w, 0)
        for f in range(o.u16(WORLD + 8)):
            for w in range(1, 24):
                w32(o, o.office(f) + 4 + 4 * w, 0)
        w32(o, o.town(0) + 4, 9000000)
        o.uc.mem_write(s + 0x1010, bytes([0xFF] * 4))
        direct(o, 0x635220, [], ecx=s)
        office = facilities_office(o, m, t)
        w32(o, o.office(office) + 4 + 4 * 3, 100000)  # beer, in barrels
        node = o.alloc(12)
        o.uc.mem_write(node, bytes([3, 0, 0, m]) + struct.pack('<HHI', 10, 5, 0))
        w32(o, s + 0x101C, node)
        w16(o, s + 0x1018, 0x4FF0)
        direct(o, 0x635440, [], ecx=s)
    return during


def siege_gates(o):
    """A siege made with the game's code (operation 0x8E) on a town retouched
    so that 0x629C60 finds only two gates on the side it attacks, worth the
    same (no towers 0x32 or 0x33 left near them): the tie is broken by the
    inhabitants, and the third gate, not found, is read from the record of
    address 0 when sorting the office buildings outside the walls."""
    towns = o.counts()[0]
    ready = [i for i in range(towns) if rt.siege_ready(o, i)]
    t = ready[-1]
    a = o.town(t)
    base = o.u32(a + 0x75C)
    for r in records(o, t, 0x768):
        if r[4] in (0x32, 0x33):
            w8(o, base + 8 * r[0] + 3, 0x36)
    side = o.call(0x5252F0, [], ecx=a) & 0xFF
    gates = [r for r in records(o, t, 0x768) + records(o, t, 0x762)
             if r[4] in (0x34, 0x35, 0x37, 0x38) and r[3] == (0xFF - side) & 0xFF]
    for r in gates[2:]:
        w8(o, base + 8 * r[0] + 2, ((0xFF - side) ^ 1) & 0xFF)
    print(f'town {t}: side {side}, gates of that side {[(r[0], hex(r[4])) for r in gates]}; two are left')

    def during(o):
        operation(o, 0x8E, t, 1, 0x80, 0)
        army = o.u32(siege_object(o, t) + 8)
        print(f'gates chosen {[o.u32(army + 0x244 + 4 * k) for k in range(3)]} of {o.u16(a + 0x776)} records')
    return during


def siege_pitch(o):
    """A siege made with the game's code (operation 0x8E) retouched so that
    its army has arrived ([+0x42] = 0), has ammunition and attacks a gate
    0x34, against a town with defenders of types 1..3 ([0x999..0x99B]): while
    the town is besieged (bit 0x10 of [+0x2C8]) its price thresholds
    (0x528070) add 7 and 14 times the pitch of 0x636920 (0x632690 and
    0x6326E0) to pitch's first two."""
    towns = o.counts()[0]
    ready = [i for i in range(towns) if rt.siege_ready(o, i)]
    t = ready[0]
    a = o.town(t)
    s = siege_object(o, t)
    base = o.u32(a + 0x75C)

    def during(o):
        operation(o, 0x8E, t, 1, 0x80, 0)
        army = o.u32(s + 8)
        w16(o, army + 0x42, 0)
        w8(o, army + 0x78, 0)
        w32(o, army + 0x4, 0x30000)
        for k, n in ((1, 3), (2, 2), (3, 1)):
            w8(o, a + 0x998 + k, n)
        gate = o.u32(army + 0x244 + 4 * o.u8(army + 0x6D))
        if gate < o.u16(a + 0x776):
            w8(o, base + 8 * gate + 3, 0x34)
        print(f'siege of town {t}: army {army:#x}, gate {gate}, besiegers of type 0 {o.u16(army + 0x21E)}, '
              f'flags {o.u32(a + 0x2C8):#x}')
    return during


def siege_sale(o):
    """A building's sale price (0x4DAF10, direct calls) in a town besieged
    with the game's code (operation 0x8E): halved while the siege goes on
    (0x633AE0 → 0x629C30 on its army). With the army besieging; without
    besiegers of type 0; retreating ([+0x3A]) but not yet down to [+0x6A]
    (besieging) and past it (not); with the flag (60 %, no siege check);
    and a building of a town without a siege."""
    towns = o.counts()[0]
    ready = [i for i in range(towns) if rt.siege_ready(o, i)]
    t = ready[0]
    other = next(i for i in range(towns) if i != t and records(o, i, 0x764))
    s = siege_object(o, t)
    r = records(o, t, 0x764)[0][0]
    r2 = records(o, other, 0x764)[0][0]

    def during(o):
        operation(o, 0x8E, t, 1, 0x80, 0)
        army = o.u32(s + 8)
        price = lambda town, rec, flag=0: direct(o, 0x4DAF10, [town, rec, flag], ecx=to.EVENTS)
        print(f'siege of town {t}, army {army:#x}: [+0x3A] {o.u16(army + 0x3A)}, [+0x6A] {o.u16(army + 0x6A)}, '
              f'[+0x21E] {o.u16(army + 0x21E)}')
        print(f'besieged: {price(t, r)}')
        troops = o.u16(army + 0x21E)
        w16(o, army + 0x21E, 0)
        print(f'no besiegers of type 0: {price(t, r)}')
        w16(o, army + 0x21E, troops)
        retreat, low = o.u16(army + 0x3A), o.u16(army + 0x6A)
        w16(o, army + 0x3A, 5)
        w16(o, army + 0x6A, 2)
        print(f'retreating, still besieging: {price(t, r)}')
        w16(o, army + 0x6A, 5)
        print(f'retreating, done: {price(t, r)}')
        w16(o, army + 0x3A, retreat)
        w16(o, army + 0x6A, low)
        print(f'with the flag: {price(t, r, 1)}; town {other} without a siege: {price(other, r2)}')
    return during


def side_gates(o, t):
    """The first three gates of the side the army attacks in town t (as
    0x629C60 finds them) and the town's wall pieces (0x36) of [0x768]."""
    side = o.call(0x5252F0, [], ecx=o.town(t)) & 0xFF
    gates = [r for r in records(o, t, 0x768) + records(o, t, 0x762)
             if r[4] in (0x34, 0x35, 0x37, 0x38) and r[3] == (0xFF - side) & 0xFF][:3]
    pieces = [r for r in records(o, t, 0x768) if r[4] == 0x36]
    return gates, pieces


def tower_next_to(o, t, piece, gate, pending):
    """The wall piece `piece` becomes a tower 0x32 next to `gate` (distance
    0 for 0x629C60), with `pending` days of works."""
    at = o.u32(o.town(t) + 0x75C) + 8 * piece[0]
    o.uc.mem_write(at, bytes([gate[1], (gate[2] - 2) & 0xFF]))
    w8(o, at + 3, 0x32)
    w8(o, at + 6, pending)


def siege_fixes(o):
    """Two sieges made with the game's code (operation 0x8E) for game bugs of
    0x629C60 (for tick_probe --fix-game-bugs). In one town a tower in the
    works (1 day left) is put next to each of the first three gates of the
    side it attacks, so whichever gate it takes has towers near that
    0x62B2B0 doesn't make targets: the strength adds entries it didn't fill.
    In another one finished towers next to the second and third gates make
    it take the first, the records outside the wall lists are chained into
    an office's buildings ([+0x2CE]; the other offices without any) at that
    gate's place, and no grid cell has the bit 0x80 (inside the walls): more
    than 0x7F groups come: from the 101st on they run over the army's own
    fields and past it, and the game works on what it has overwritten, so
    with the setting off that call differs from p3vita (see sieges.cpp).
    And a third one for the gate not found (gate_missing)."""
    towns = o.counts()[0]
    ready = [i for i in range(towns) if rt.siege_ready(o, i)]
    t1, t2 = ready[0], ready[1]
    gates, pieces = side_gates(o, t1)
    for g, r in zip(gates, pieces):
        tower_next_to(o, t1, r, g, 1)
    print(f'town {t1}: gates {[g[0] for g in gates]}, towers in the works next to them '
          f'{[r[0] for r in pieces[:len(gates)]]}; record 0 {records_at(o, t1, 0)}')
    a2 = o.town(t2)
    gates, pieces = side_gates(o, t2)
    for g, r in zip(gates[1:], pieces):
        tower_next_to(o, t2, r, g, 0)
    n, base = o.u16(a2 + 0x776), o.u32(a2 + 0x75C)
    walls = {r[0] for r in records(o, t2, 0x768) + records(o, t2, 0x762)}
    chain = [r for r in range(n) if r not in walls and o.u8(base + 8 * r + 3) != 5]
    for r, nxt in zip(chain, chain[1:] + [0xFFFF]):
        o.uc.mem_write(base + 8 * r, bytes([gates[0][1], gates[0][2]]))
        w16(o, base + 8 * r + 4, nxt)
    offices = town_offices(o, t2)
    for f in offices:
        w16(o, o.office(f) + 0x2CE, 0xFFFF)
    w16(o, o.office(offices[0]) + 0x2CE, chain[0])
    w, h = o.s32(a2 + 0x7A8), o.s32(a2 + 0x7AC)
    grid = o.u32(a2 + GRID)
    o.uc.mem_write(grid, bytes(c & 0x7F for c in o.uc.mem_read(grid, w * h)))
    print(f'town {t2}: gates {[g[0] for g in gates]}; office {offices[0]} with {len(chain)} records')
    t3 = gate_missing(o, ready[-1])
    seen = set()

    def read_at_0(uc, address, size, data):
        value = bytes(uc.mem_read(0, 4)).hex()
        if value not in seen:
            seen.add(value)
            print(f'address 0 when 0x629C60 reads a gate (0x62A4F8): {value}')
    o.uc.hook_add(UC_HOOK_CODE, read_at_0, begin=0x62A4F8, end=0x62A4F8)

    def during(o):
        operation(o, 0x8E, t1, 1, 0x80, 0)
        operation(o, 0x8E, t2, 1, 0x80, 0)
        operation(o, 0x8E, t3, 1, 0x80, 0)
        for t in (t1, t2, t3):
            army = o.u32(siege_object(o, t) + 8)
            print(f'town {t}: gate {o.u8(army + 0x6D)}, targets {o.u16(army + 0x242)}, '
                  f'groups {o.u8(army + 0x6E)}, strength {o.u32(army + 0x580)}')
    return during


def gate_missing(o, t):
    """Town t retouched as in siege_gates (two gates on the side, worth the
    same), with an office building outside the walls nearer (0x30, 0xEF)
    than the chosen gate, and nearer it than the other one (the two gates
    moved for that). The game reads the gate not found from address 0 (on
    Windows it fails; in the oracle that place), so it leaves the building
    out of the groups; with the fix it is a group."""
    a = o.town(t)
    base, n = o.u32(a + 0x75C), o.u16(a + 0x776)
    for r in records(o, t, 0x768):
        if r[4] in (0x32, 0x33):
            w8(o, base + 8 * r[0] + 3, 0x36)
    side = o.call(0x5252F0, [], ecx=a) & 0xFF
    gates = [r for r in records(o, t, 0x768) + records(o, t, 0x762)
             if r[4] in (0x34, 0x35, 0x37, 0x38) and r[3] == (0xFF - side) & 0xFF]
    for r in gates[2:]:
        w8(o, base + 8 * r[0] + 2, ((0xFF - side) ^ 1) & 0xFF)
    g0, g1 = gates[0], gates[1]
    v0, v1 = (int(g[4] in (0x34, 0x35)) for g in (g0, g1))
    chosen, other = (g1, g0) if v1 < v0 or (v1 == v0 and o.s32(a + 0x2D4) % 2 == 0) else (g0, g1)
    w, h = o.s32(a + 0x7A8), o.s32(a + 0x7AC)
    # The gates moved (the choice depends on their types and the inhabitants,
    # not on where they are), so that there is room for such a building.
    for g, (gx, gy) in ((chosen, (20, 30)), (other, (2, 2))):
        o.uc.mem_write(base + 8 * g[0], bytes([gx, gy]))
    chosen, other = (chosen[0], 20, 30), (other[0], 2, 2)

    def d2(x, y, g):
        return (x - g[1]) ** 2 + (y - g[2]) ** 2
    # The game reads the missing gate from address 0 (see sieges.cpp): in
    # the oracle, during the operation, the stack address 0x010FEF30, so
    # (0x30, 0xEF) (printed below). A place nearer that than the chosen
    # gate, and nearer the chosen gate than the other one.
    missing = (None, 0x30, 0xEF)
    x, y = min(((x, y) for x in range(w) for y in range(h)
                if d2(x, y, missing) < d2(x, y, chosen) < d2(x, y, other)),
               key=lambda p: d2(p[0], p[1], chosen))
    f, r = next((f, o.u16(o.office(f) + 0x2CE)) for f in town_offices(o, t)
                if o.u16(o.office(f) + 0x2CE) < n and o.u8(base + 8 * o.u16(o.office(f) + 0x2CE) + 3) != 5)
    o.uc.mem_write(base + 8 * r, bytes([x, y]))
    cell = grid_at(o, t, x, y)
    w8(o, cell, o.u8(cell) & 0x7F)
    print(f'town {t}: gates {chosen} (chosen) and {other}; record {r} of office {f} at ({x}, {y}); '
          f'at address 0: {bytes(o.uc.mem_read(0, 8)).hex()}')
    return t


def records_at(o, t, r):
    """The record r of town t: (x, y, owner, type, pending)."""
    rec = bytes(o.uc.mem_read(o.u32(o.town(t) + 0x75C) + 8 * r, 8))
    return rec[0], rec[1], rec[2], rec[3], rec[6]


# --- operations.cpp: the small operations of the executor ---

CONVOY_TABLE = 0x6DD7A8  # pointer to the convoys (0x3C bytes each)
CONVOY_COUNT = to.SHIPS + 0xF6  # u16
SPECIAL_SHIPS = 0x6DD7C0  # 16 entries of 12 bytes: u16 ship first
EVENT_COUNT = to.EVENTS + 0xC  # u16: records of the event queue


def convoy_at(o, c):
    return o.u32(CONVOY_TABLE) + 0x3C * c


def operation_guards(o):
    """The checks of the small operations that the simulation always queues
    with valid values, run with the executor (the interface or a network
    game could send others): 0x7B with a town out of the world, with the
    slot 6 and with the slot 4 (the jump for 3..5); 0x7A with each town out
    and with a town whose [0x83F] is already a merchant; 0x37, 0x87, 0x61
    and 0x81 with a merchant out of the world; 0x61 with a price over the
    merchant's money; 0x2C with a town out; 0x38 less than 40 days ahead;
    0x7C on a day already past (the plague at the start of the next day);
    0x7F and 0x60 with a tick that isn't to come; 0x80 with a queue of 0x80
    records (save 5's; in the others the count is lowered for the call);
    0xB8 of kind 5 (nothing) and of kind 0 (with the pirates' ships'
    places); 0x78 with the 16 entries of [0x6DD7C0] in use; and last 0x7E
    with 18 towns without the bit 0x40 (the list of towns goes past [+0x17],
    into the next record)."""
    towns, _, merchants, ships = o.counts()
    m = rt.ai_with_office(o)[0]
    now, day = o.u32(WORLD + 0x14), o.u16(WORLD + 4)
    t = office_town(o, m)
    print(f'merchant {m} (town {t}), day {day}, event records {o.u16(EVENT_COUNT):#x}, '
          f'special ships {[o.u16(SPECIAL_SHIPS + 12 * k) for k in range(16)]}')

    def during(o):
        operation(o, 0x7B, 0x44332211, 0x6655, 0, towns)
        operation(o, 0x7B, 0x44332211, 0x6655, 0, t | (6 << 8))
        operation(o, 0x7B, 0x44332211, 0x6655, 0, t | (4 << 8))
        operation(o, 0x7A, towns, 1, 0x1234, 0x55)
        operation(o, 0x7A, 1, towns, 0x1234, 0x55)
        town = o.town(2)
        old = o.u8(town + 0x83F)
        w8(o, town + 0x83F, m or 1)
        operation(o, 0x7A, 2, 3, 0x1234, 0x55)
        w8(o, town + 0x83F, old)
        operation(o, 0x37, merchants, t, 100)
        operation(o, 0x2C, 1, m, towns, 1)
        operation(o, 0x38, now + 0x100, 1)
        operation(o, 0x7C, 0x50454031, now + 0x900, o.u32(o.town(4) + 0x2D4) // 40 << 16, (day - 1) | (4 << 16),
                  fmt='<IIIII')
        operation(o, 0x7F, 0, 1, 2, now)
        count = o.u16(EVENT_COUNT)
        w16(o, EVENT_COUNT, min(count, 0x80))
        operation(o, 0x80, 0, 0x1234, 0x5678)
        w16(o, EVENT_COUNT, count)
        operation(o, 0x87, merchants, 0x0403020F)
        operation(o, 0x60, now, 0, 0, 0)
        operation(o, 0x61, merchants, now + 0x100, t, 0)
        operation(o, 0x61, m, now + 0x100, t, (o.u32(o.merchant(m)) + 1) & 0xFFFFFFFF)
        operation(o, 0x81, merchants, 1, 0x100, 0)
        operation(o, 0xB8, 5, 0)
        operation(o, 0xB8, 0, 0)
        entries = [o.u16(SPECIAL_SHIPS + 12 * k) for k in range(16)]
        for k in range(16):
            if entries[k] >= ships:
                w16(o, SPECIAL_SHIPS + 12 * k, 0)
        operation(o, 0x78, 1, 2, 300, 400)
        for k in range(16):
            w16(o, SPECIAL_SHIPS + 12 * k, entries[k])
        flags = [o.u8(o.town(i) + 0x2C8) for i in range(towns)]
        cleared = 0
        for i in range(towns):
            if flags[i] & 0x40 and cleared < 18 - sum(1 for f in flags if not f & 0x40):
                w8(o, o.town(i) + 0x2C8, flags[i] & ~0x40)
                cleared += 1
        listed = sum(1 for i in range(towns) if not o.u8(o.town(i) + 0x2C8) & 0x40)
        assert listed > 16, listed
        seasons = {i for i, a in rt.events(o) if o.u16(a + 6) == 0x1F}
        operation(o, 0x7E, 0x12340008, 0x56780010, 0x9ABC0018, 0xDEF00020)
        index = next(i for i, a in rt.events(o) if o.u16(a + 6) == 0x1F and i not in seasons)
        a = o.u32(to.EVENTS) + 0x18 * index
        print(f'0x7E with {listed} towns: event {index} of {o.u16(EVENT_COUNT)}: '
              f'{bytes(o.uc.mem_read(a, 0x18)).hex()} | next {bytes(o.uc.mem_read(a + 0x18, 4)).hex()}')
        for i in range(towns):
            w8(o, o.town(i) + 0x2C8, flags[i])
    return during


def operation_convoys(o):
    """Operations 0x1E (0x539B50, a convoy leaves on an expedition) and 0x1F
    (0x539C60, a point of its route), which the AI's convoys queue for
    convoys that can sail: each with a convoy out of the table, with the
    convoy's leading ship out of the world, and in port with a leading ship
    that can't sail (0x519BA0: its [+0x3F] at 0); then 0x1E asking for more
    money than the owner has (1000: he gives it all) and again with the
    owner in debt (−500: nothing), and a 0x1F."""
    _, _, merchants, ships = o.counts()
    player = o.u32(to.PLAYER)
    n = o.u16(CONVOY_COUNT)

    def can_sail(c):
        lead = o.u16(convoy_at(o, c) + 0x10)
        return lead < ships and o.call(0x519BA0, [], ecx=o.ship(lead)) & 0xFF

    c = next(c for c in range(n) if o.u16(convoy_at(o, c) + 0x12) < 0xF and o.u8(convoy_at(o, c)) < merchants
             and o.u8(convoy_at(o, c)) != player and can_sail(c))
    a = convoy_at(o, c)
    lead, owner = o.u16(a + 0x10), o.u8(a)
    print(f'{n} convoys; convoy {c}: owner {owner}, lead {lead}, state {o.u16(a + 0x12):#x}; '
          f'[0x6DE52B] {o.u8(0x6DE52B):#x}')

    def ops(convoy):
        operation(o, 0x1E, 5000, convoy, 0x10, fmt='<IIHH')
        operation(o, 0x1F, convoy, 1, 100, 200, 3, fmt='<IHHHHB')

    def during(o):
        ops(n)
        w16(o, a + 0x10, 0xFFFF)
        ops(c)
        w16(o, a + 0x10, lead)
        ship = o.ship(lead)
        condition = o.u8(ship + 0x3F)
        w8(o, ship + 0x3F, 0)
        ops(c)
        w8(o, ship + 0x3F, condition)
        w32(o, o.merchant(owner), 1000)
        operation(o, 0x1E, 5000, c, 0x10, fmt='<IIHH')
        print(f'owner {o.s32(o.merchant(owner))}, convoy [+0x20] {o.u32(a + 0x20)}, state {o.u16(a + 0x12):#x}')
        w32(o, o.merchant(owner), -500)
        operation(o, 0x1E, 5000, c, 0x20, fmt='<IIHH')
        print(f'owner {o.s32(o.merchant(owner))}, convoy [+0x20] {o.u32(a + 0x20)}')
        operation(o, 0x1F, c, 2, 300, 400, 5, fmt='<IHHHHB')
    return during


def operation_loans(o):
    """Operation 0x29 (0x539DC0, a loan: event 0x01), which the merchants'
    tick queues with the town's money-lender as the lender: with a town out
    of the world; with an AI merchant as the lender without the amount; and
    with him having it (0x4F8FE0: he pays it and his reputation in the town
    goes up): the tavern's slot 1 (< 3) with the kind 2, and the slot 4 (the
    jump for 3..5) with the kind 9 (taken as 4)."""
    towns, _, merchants, _ = o.counts()
    ais = rt.ai_with_office(o)
    lender, borrower = ais[0], ais[1]
    t = office_town(o, lender)
    fmt = '<IIB3sHBBBBBB'

    def loan(town, amount, kind, slot, who=borrower):
        operation(o, 0x29, 0x04030201, 0x05, b'\x06\x07\x08', amount, 4, 0x09, lender, who, town,
                  (slot << 4) | kind, fmt=fmt)

    def show():
        m = o.merchant(lender)
        print(f'lender {lender}: money {o.s32(m)}, reputation in {t} '
              f'{struct.unpack("<f", o.uc.mem_read(m + 0x120 + 12 * t, 4))[0]}; '
              f'borrower {borrower}: {o.s32(o.merchant(borrower))}; tavern [0x9CA..] '
              f'{[o.u16(o.town(t) + 0x9CA + 6 * k) for k in range(6)]}')

    def during(o):
        loan(towns, 1000, 2, 7)
        w32(o, o.merchant(lender), 100)
        loan(t, 1000, 2, 7)
        w32(o, o.merchant(lender), 1000000)
        for k in range(6):
            w16(o, o.town(t) + 0x9CA + 6 * k, 0x100 + k)
        show()
        loan(t, 1000, 2, 1)
        show()
        loan(t, 2500, 9, 4, who=0xFF)
        show()
    return during


def office_records(o, t, f):
    """(record, x, y) of the buildings in the lists [+0x2D0] and [+0x2CE] of
    office f in town t."""
    return rt.records_of(o, t, o.u16(o.office(f) + 0x2D0)) + rt.records_of(o, t, o.u16(o.office(f) + 0x2CE))


def operation_sale(o):
    """Operation 0x28 (0x53D190, a merchant puts a building on sale), which
    the AI of a merchant in debt queues for a building of his: with a town
    and with a merchant out of the world; for a merchant without an office in
    the town; in a town with the bit 0x10 of [0x2C8], the building's cell
    without the bit 0x80 (nothing) and with it (the auction, 0x4DAB00); and
    a cell with no building of his nor of the town (x = 0x100: the office is
    recounted, 0x4FF2D0). Saves 3, 5 and 7 (in 1 no AI office has a
    building in those lists)."""
    towns, offices, merchants, _ = o.counts()
    for f in range(offices):
        m, t = o.u16(o.office(f) + 0x2C4), o.u8(o.office(f) + 0x2C6)
        if m < merchants and o.u16(o.merchant(m) + 8) and office_records(o, t, f):
            break
    else:
        raise RuntimeError('no AI office with a building in its lists')
    r, x, y = office_records(o, t, f)[0]
    other = next(k for k in rt.ai_with_office(o) if facilities_office(o, k, t) is None)
    a = o.town(t)
    cell = grid_at(o, t, x, y)
    print(f'merchant {m}, office {f} in town {t} ([0x2C8] {o.u32(a + 0x2C8):#x}): record {r} at ({x}, {y}), '
          f'cell {o.u8(cell):#x}; merchant {other} without an office there')

    def during(o):
        operation(o, 0x28, towns, m, x, y)
        operation(o, 0x28, t, merchants, x, y)
        operation(o, 0x28, t, other, x, y)
        flags, value = o.u8(a + 0x2C8), o.u8(cell)
        w8(o, a + 0x2C8, flags | 0x10)
        w8(o, cell, value & 0x7F)
        operation(o, 0x28, t, m, x, y)
        w8(o, cell, value | 0x80)
        operation(o, 0x28, t, m, x, y)
        w8(o, cell, value)
        w8(o, a + 0x2C8, flags)
        operation(o, 0x28, t, m, 0x100, y)
        print(f'auctions: {[i for i, e in rt.events(o) if o.u16(e + 6) == 0x36]}')
    return during


# --- events.cpp: the event queue and what hangs from it ---

DAY = WORLD + 4  # u16: the day of the year
WORLD_FLAG_8A = 0x6DE52A  # u8: with it the AI doesn't buy nor build
ALDERMAN_BUSY = 0x6DF31C  # u32
NETWORK = 0x6DF2F0  # u32: the operations object's mode ([+0x0] & 0xE: a network game)
EVENT_FREE = to.EVENTS + 0xA  # u16: the first free record
SCRATCH_PRICE, SCRATCH_TICK = SCRATCH + 0xE0, SCRATCH + 0xE4


def events_of(o, kind):
    return [(i, a) for i, a in rt.events(o) if o.u16(a + 6) == kind]


def event_queue_branches(o):
    """The event queue (0x4D85C0, called directly) and the grain event
    (0x4DB330): an event 0x37 (a merchant may trade again in a town: the bit
    of [+0x608] cleared; 0x4E62F6 makes it); an event 0x04 of a merchant out
    of the world (0x4DFC94: removed); a grain event 0x00 with [0x6DF31C]
    (0x4DB330 gives 0: removed) and in a network game ([0x6DF2F0] & 0xC: the
    operation 0x83 instead); and last the queue without a free record
    (0x4D8CF0 reserves 0x80 more) when the operation 0x83 adds an event."""
    towns, _, merchants, _ = o.counts()
    m = rt.ai_with_office(o)[0]
    t = 3
    w32(o, o.merchant(m) + 0x608, o.u32(o.merchant(m) + 0x608) | (1 << t))

    def during(o):
        now = o.u32(WORLD + 0x14)
        new_event(o, 0, 0x37, d8=t, dC=m)
        new_event(o, 0, 0x04, w8=merchants)
        direct(o, 0x4D85C0, [now], ecx=to.EVENTS)
        print(f'[+0x608] of merchant {m}: {o.u32(o.merchant(m) + 0x608):#x}')
        _, grain = new_event(o, 0x1000, 0x00, d8=now + 0x1000, dC=5000, b10=2)
        for address, value in ((ALDERMAN_BUSY, 1), (NETWORK, o.u32(NETWORK) | 4)):
            old = o.u32(address)
            w32(o, address, value)
            print(f'0x4DB330 with [{address:#x}] = {value:#x}: {direct(o, 0x4DB330, [grain], ecx=to.EVENTS)}')
            w32(o, address, old)
        count = o.u16(EVENT_COUNT)
        w16(o, EVENT_FREE, count)
        operation(o, 0x83, now + 0x2000, 1000, 2)
        print(f'event records {count:#x} → {o.u16(EVENT_COUNT):#x}, free {o.u16(EVENT_FREE):#x}')
    return during


def ai_debt_branches(o):
    """The AI of a merchant (event 0x04), direct calls: 0x4F42B0 in a network
    game ([0x6DF2F0] & 0xE: the best human merchant's reputation); 0x4F9580
    with a town out of the world and with a reputation over 32758; 0x4F4A20,
    0x4F4B40 and 0x4F4E50 with [0x6DE52A] (the last gives 15000); 0x4F4A20
    for a merchant with the bit 4 of [+0x8]; 0x4F4E50 for a merchant under
    −50000 with houses in an office: 0x4F5A20 looks at them (nothing chosen:
    the rent it compares with starts at 0, see events.cpp) and, with nothing
    of his on sale (0x4DB220), he goes bankrupt (operation 0x65 queued); and
    again with one of his buildings in an auction: the loan of 50000
    (operation 0x29). Saves 3, 5 and 7 (in 1 no AI merchant has houses)."""
    towns, _, merchants, _ = o.counts()
    ais = rt.ai_with_office(o)
    for m in ais:
        houses = [(f, rt.records_of(o, office_town_of(o, f), o.u16(o.office(f) + 0x2D0)))
                  for f in rt.merchant_offices(o, m)]
        houses = [(f, h) for f, h in houses if h]
        if houses:
            break
    else:
        raise RuntimeError('no AI merchant with houses')
    f, h = houses[0]
    t = office_town_of(o, f)
    other = next(x for x in ais if x != m)
    on_sale = o.call(0x4DB220, [m], ecx=to.EVENTS) & 0xFF
    print(f'merchant {m}: houses {h[:3]} of office {f} in town {t}; anything on sale: {on_sale}')
    a = o.merchant(m)
    rep = a + 0x2FC + 4 * t

    def during(o):
        old = o.u32(NETWORK)
        w32(o, NETWORK, old | 2)
        direct(o, 0x4F42B0, [], ecx=a)
        w32(o, NETWORK, old)
        direct(o, 0x4F9580, [towns], ecx=a)
        value = o.u32(rep)
        o.uc.mem_write(rep, struct.pack('<f', 40000.0))
        print(f'0x4F9580 at 40000: {direct(o, 0x4F9580, [t], ecx=a) & 0xFFFF:#x}')
        w32(o, rep, value)
        w8(o, WORLD_FLAG_8A, 1)
        for target in (0x4F4A20, 0x4F4B40, 0x4F4E50):
            direct(o, target, [m], ecx=a)
        w8(o, WORLD_FLAG_8A, 0)
        flags = o.u8(o.merchant(other) + 8)
        w8(o, o.merchant(other) + 8, flags | 4)
        direct(o, 0x4F4A20, [other], ecx=o.merchant(other))
        w8(o, o.merchant(other) + 8, flags)
        w32(o, a, -60000)
        o.tick_operations = []
        direct(o, 0x4F4E50, [m], ecx=a)
        print(f'queued: {[op[:4].hex() for op in o.tick_operations]}')
        new_event(o, 0x1000, 0x36, w8=h[0][0], wA=0xFFFF, wC=0xFFFF, wE=0x1000, d10=0, b14=1, b15=t, b16=0, b17=0xFF)
        o.tick_operations = []
        direct(o, 0x4F4E50, [m], ecx=a)
        print(f'queued: {[op[:8].hex() for op in o.tick_operations]}')
    return during


def office_town_of(o, f):
    return o.u8(o.office(f) + 0x2C6)


def sale_branches(o):
    """Putting a building on sale (0x4DAB00, operation 0x28) and the
    queue's 0x4DAFE0 and 0x4DAF10: direct calls with a town and with a
    record out of the world (the interface's operation 0x3C calls 0x4DAB00
    with no check); the operation 0x28 on a merchant's building (a new
    auction, phase 1) and again on it (on sale already: 0x4DAFE0 with the
    phase-1 wait); with the byte [+0xD] of the settings at 0, on a second
    building (added to that auction); on a building of a town with an
    auction in phase 2 (added to it); on one whose owner lacks the town's
    bit in [+0x468]; and 0x4DAB00 on a record of the town that isn't a
    merchant's. Saves 3, 5 and 7."""
    towns, offices, merchants, _ = o.counts()
    found = []
    for f in range(offices):
        m, t = o.u16(o.office(f) + 0x2C4), o.u8(o.office(f) + 0x2C6)
        if m >= merchants or not o.u16(o.merchant(m) + 8) or t in [x[1] for x in found]:
            continue
        if not o.u32(o.merchant(m) + 0x468) & (1 << (t & 31)):
            continue
        recs = office_records(o, t, f)
        if len(recs) >= (3 if not found else 1):
            found.append((m, t, recs))
        if len(found) == 2:
            break
    else:
        raise RuntimeError('no two AI offices with buildings')
    (m, t, recs), (m2, t2, recs2) = found
    n = o.u16(o.town(t) + 0x776)
    base = o.u32(o.town(t) + 0x75C)
    town_building = next(r for r in range(n) if o.u8(base + 8 * r + 2) >= merchants)
    settings = o.u32(to.SETTINGS_POINTER)
    print(f'merchant {m} in town {t}: {recs[:3]}; merchant {m2} in town {t2}: {recs2[:1]}; '
          f'town building {town_building}; settings [+0xD] {o.u8(settings + 0xD)}')

    def sale(town, merchant, record):
        operation(o, 0x28, town, merchant, record[1], record[2])

    def during(o):
        now = o.u32(WORLD + 0x14)
        direct(o, 0x4DAF10, [towns, 0, 0], ecx=to.EVENTS)
        direct(o, 0x4DAF10, [t, n, 0], ecx=to.EVENTS)
        direct(o, 0x4DAFE0, [towns, 0, SCRATCH_PRICE, SCRATCH_TICK], ecx=to.EVENTS)
        direct(o, 0x4DAFE0, [t, n, SCRATCH_PRICE, SCRATCH_TICK], ecx=to.EVENTS)
        direct(o, 0x4DAB00, [towns, 0], ecx=to.EVENTS)
        direct(o, 0x4DAB00, [t, n], ecx=to.EVENTS)
        sale(t, m, recs[0])
        sale(t, m, recs[0])
        direct(o, 0x4DAFE0, [t, recs[0][0], SCRATCH_PRICE, SCRATCH_TICK], ecx=to.EVENTS)
        print(f'record {recs[0][0]} on sale: price {o.s32(SCRATCH_PRICE)}, tick {o.u32(SCRATCH_TICK):#x}')
        value = o.u8(settings + 0xD)
        w8(o, settings + 0xD, 0)
        sale(t, m, recs[1])
        w8(o, settings + 0xD, value)
        new_event(o, 0x1000, 0x36, w8=0xFFFF, wA=0xFFFF, wC=0xFFFF, wE=0x1000, d10=0, b14=2, b15=t2, b16=0, b17=0xFF)
        sale(t2, m2, recs2[0])
        direct(o, 0x4DAB00, [t, town_building], ecx=to.EVENTS)
        bits = o.u32(o.merchant(m) + 0x468)
        w32(o, o.merchant(m) + 0x468, bits & ~(1 << (t & 31)))
        sale(t, m, recs[2])
        w32(o, o.merchant(m) + 0x468, bits)
        for i, e in events_of(o, 0x36):
            print(f'auction {i}: {bytes(o.uc.mem_read(e, 0x18)).hex()}')
    return during


def ice_storm_branches(o):
    """The ice and the winter storms (event 0x0D), direct calls with the day
    of the year changed for each: 0x4E45C4 on day 0x3A (the ice of every town
    cleared); on day 0x30 with town 0's ice at 1 (it melts below 0: 0); on
    day 0x10 with the game's speed 3 and 4 ([+0x4] of the settings) and town
    0's level going up (the days closed scaled by 0x66DE7C). 0x4E43E4 on day
    10 with one of the 16 ships of [0x6DD7C0] where its storm would go
    (found first, not recorded, with the same rand state): it looks for
    another place."""
    _, _, _, ships = o.counts()
    day = o.u16(DAY)
    settings = o.u32(to.SETTINGS_POINTER)
    town = o.town(0)
    w16(o, DAY, 10)
    point = None
    for seed in range(1, 20000):
        o.rand_state = seed
        queued, recorded = len(o.tick_operations), len(o.queued)
        direct(o, 0x4E43E4, [])
        storms = [op for op in o.tick_operations[queued:] if struct.unpack_from('<II', op) == (0x78, 2)]
        del o.tick_operations[queued:]
        del o.queued[recorded:]
        if storms:
            point = struct.unpack_from('<ii', storms[0], 0xC)
            break
    if point is None:
        raise RuntimeError('no winter storm')
    w16(o, DAY, day)
    slot = next(k for k in range(16) if o.u16(SPECIAL_SHIPS + 12 * k) >= ships)
    ship = 0
    print(f'day {day}, speed {o.s32(settings + 4)}; the storm of seed {seed} at {point}: ship {ship} in slot {slot}')

    def during(o):
        w16(o, DAY, 0x3A)
        direct(o, 0x4E45C4, [])
        w16(o, DAY, 0x30)
        w32(o, town + 0x9B8, 1)
        direct(o, 0x4E45C4, [])
        speed = o.s32(settings + 4)
        for value in (3, 4):
            w16(o, DAY, 0x10)
            w32(o, town + 0x9B8, 0x5FF)
            w8(o, town + 0x9BD, 4)
            w32(o, settings + 4, value)
            direct(o, 0x4E45C4, [])
            print(f'speed {value}: [0x9C0] {o.u32(town + 0x9C0):#x}, [0x9BD] {o.u8(town + 0x9BD):#x}')
        w32(o, settings + 4, speed)
        w16(o, DAY, 10)
        w16(o, SPECIAL_SHIPS + 12 * slot, ship)
        w16(o, o.ship(ship) + 0x1E, point[0])
        w16(o, o.ship(ship) + 0x22, point[1])
        o.rand_state = seed
        queued = len(o.tick_operations)
        direct(o, 0x4E43E4, [])
        print(f'with the ship there: {[op[:0x14].hex() for op in o.tick_operations[queued:]]}')
        w16(o, DAY, day)
    return during


def jobs_captains(o):
    """The people looking for work in the taverns (event 0x19, 0x4E8E24) and
    the captains (event 0x08, 0x4E2634), direct calls on their events:
    0x4E8E24 with 70 people (61..90: the next look in people + 0x40 ticks),
    with [0x6DF31C], with [0x6DE52A] and with no more merchants than towns
    (the count lowered for the call); 0x4E2634 with every tavern empty but
    town 0's (fewer than 3 towns with a good captain: a new one with 0x526A50
    and 1, so 0x4FDF50 not young) and [+0xC] of the event at 0x1A (→ 3)."""
    towns = o.counts()[0]
    (_, jobs), = events_of(o, 0x19)
    (_, taverns), = events_of(o, 0x08)
    slots = [o.town(t) + base + 6 * k for t in range(towns) for base in (0x9CA, 0x9DC) for k in range(3)]
    for k, a in enumerate(slots):
        w16(o, a, 0x100 + k if k < 70 else 0)
    print(f'{towns} towns: 70 people of {len(slots)} slots')

    def during(o):
        direct(o, 0x4E8E24, [jobs], ecx=to.EVENTS)
        for address, value, size in ((ALDERMAN_BUSY, 1, 4), (WORLD_FLAG_8A, 1, 1), (WORLD + 0xA, towns, 2)):
            old = (o.u32, o.u8, o.u16)[(4, 1, 2).index(size)](address)
            (w32, w8, w16)[(4, 1, 2).index(size)](o, address, value)
            direct(o, 0x4E8E24, [jobs], ecx=to.EVENTS)
            (w32, w8, w16)[(4, 1, 2).index(size)](o, address, old)
        for t in range(1, towns):
            w16(o, o.town(t) + 0x82E, 0xFFFF)
        w32(o, taverns + 0xC, 0x1A)
        direct(o, 0x4E2634, [taverns], ecx=to.EVENTS)
        print(f'event 0x08: {bytes(o.uc.mem_read(taverns, 0x18)).hex()}')
    return during


# --- events_towns.cpp: the towns' events ---

MAILBOX = 0x6DD730
OFFICE_FREE = WORLD + 0x7E  # u16: the first free office (linked by [+0x2C8])
SCRATCH_TYPE = SCRATCH + 0xE8


def rand_step(state):
    """MSVC's rand() (0x639C03): (new state, value)."""
    state = (state * 0x343FD + 0x269EC3) & 0xFFFFFFFF
    return state, (state >> 16) & 0x7FFF


def new_event_at(o, tick, kind, **fields):
    """new_event at an absolute tick."""
    return new_event(o, (tick - o.u32(WORLD + 0x14)) & 0xFFFFFFFF, kind, **fields)


def record_at(o, t, r):
    return o.u32(o.town(t) + 0x75C) + 8 * r


def unlink_record(o, t, r):
    """Takes record r out of whichever list of town t holds it (the town's
    heads [0x762..0x76A] and its offices' [+0x2CE] and [+0x2D0])."""
    a = o.town(t)
    n = o.u16(a + 0x776)
    heads = [a + h for h in range(0x762, 0x76C, 2)]
    for f in range(o.counts()[1]):
        if o.u8(o.office(f) + 0x2C6) == t and o.u16(o.office(f) + 0x2C4) < o.counts()[2]:
            heads += [o.office(f) + 0x2CE, o.office(f) + 0x2D0]
    for link in heads:
        steps = n
        while o.u16(link) < n and steps:
            if o.u16(link) == r:
                w16(o, link, o.u16(record_at(o, t, r) + 4))
                w16(o, record_at(o, t, r) + 4, 0xFFFF)
                return link
            link = record_at(o, t, o.u16(link)) + 4
            steps -= 1
    raise RuntimeError(f'record {r} of town {t} in no list')


FLAMMABLE_TYPES = set(range(4, 0x1F)) | {0x29, 0x2C}


def debt_mail_branches(o):
    """Debts (event 0x01, 0x4DB4E0, called directly on new events), the
    mailbox (0x4D6780) and operation 0x12: a debt of an AI merchant without
    money nor an office in the town, with a ship there carrying 20 loads of
    each of four wares (0x4F6570 sells the cargo while the total stays under
    65000: the rest is skipped); a debt whose debtor is out of the world
    (a merchant as the creditor); 0x4D6780 for a merchant out of the world
    and for the player with an old letter of type 0x90 in his chain (the
    chain is cut there and, removed, the free list is lost); the operation
    0x12 for a ship out of the world and for one without a captain."""
    towns, _, merchants, ships = o.counts()
    player = o.u32(to.PLAYER)
    debtor = ship = town = None
    for m in rt.ai_with_office(o):
        for s in merchant_ships(o, m):
            t = o.u8(o.ship(s) + 0x39)
            if t < towns and o.u16(o.ship(s) + 0x134) < 0xC and facilities_office(o, m, t) is None:
                debtor, ship, town = m, s, t
                break
        if debtor is not None:
            break
    a = o.ship(ship)
    for w in (9, 10, 12, 13):
        w32(o, a + 0x54 + 4 * w, 40000)
    w32(o, o.merchant(debtor), 0)
    chain, i = [], o.u16(o.merchant(player) + 0xA)
    while i < o.u16(MAILBOX + 6) and len(chain) < 3:
        chain.append(i)
        i = o.u16(o.u32(MAILBOX) + 0x10 * i + 6)
    letter = o.u32(MAILBOX) + 0x10 * chain[1]
    w8(o, letter + 4, 0x90)
    w16(o, letter + 2, o.u16(letter + 2) - 2)  # two years older
    bare = next(s for s in range(ships) if o.u16(o.ship(s) + 0x42) >= o.u16(to.SHIPS + 0xF2))
    print(f'debtor {debtor}, ship {ship} in town {town}; the player\'s letters {chain}; ship {bare} has no captain')

    def during(o):
        now = o.u32(WORLD + 0x14)
        _, e = new_event(o, 0x1000, 0x01, d8=1000000, dC=now & ~0xFF, b10=0xFF, b11=debtor, b12=0, b13=0, b14=town,
                         b15=0, b16=0, b17=0)
        direct(o, 0x4DB4E0, [e], ecx=to.EVENTS)
        _, e = new_event(o, 0x1000, 0x01, d8=1000, dC=now & ~0xFF, b10=0, b11=merchants, b12=0, b13=0, b14=town,
                         b15=0, b16=0, b17=0)
        direct(o, 0x4DB4E0, [e], ecx=to.EVENTS)
        direct(o, 0x4D6780, [merchants], ecx=MAILBOX)
        direct(o, 0x4D6780, [player], ecx=MAILBOX)
        print(f'mail free head {o.u16(MAILBOX + 4):#x}, the player\'s head {o.u16(o.merchant(player) + 0xA):#x}')
        operation(o, 0x12, ships, 8, 0, 8)
        operation(o, 0x12, bare, 8, 0, 8)
    return during


def fire_branches(o):
    """Fires (event 0x1D, 0x4E9564; the queue called directly), each in a
    town: the nearest building, of an office's list, with the record's owner
    a merchant without an office there (the fire stops); the same with the
    owner 0xFF (looked for in the town's list [0x764]: not there, it stops);
    the player's store (0x1E) burns (0x500C30); and a town whose burning list
    [0x76A] has a tower 0x32 (skipped) and a building with [+0x6] 0x40
    (− 0x33). Saves 3, 5 and 7 (in 1 the player has no store)."""
    towns, offices, merchants, _ = o.counts()
    player = o.u32(to.PLAYER)
    store = None
    for f in range(offices):
        if o.u16(o.office(f) + 0x2C4) != player:
            continue
        t = o.u8(o.office(f) + 0x2C6)
        r = next((r for r, _, _ in rt.records_of(o, t, o.u16(o.office(f) + 0x2CE))
                  if o.u8(record_at(o, t, r) + 3) == 0x1E), None)
        if r is not None:
            store = (t, r)
            break
    if store is None:
        raise RuntimeError('the player has no store')
    used = {store[0]}
    burning = []  # (town, record, office)
    for f in range(offices):
        m, t = o.u16(o.office(f) + 0x2C4), o.u8(o.office(f) + 0x2C6)
        if m >= merchants or m == player or t in used:
            continue
        r = next((r for r, _, _ in rt.records_of(o, t, o.u16(o.office(f) + 0x2CE))
                  if o.u8(record_at(o, t, r) + 3) in FLAMMABLE_TYPES), None)
        if r is not None:
            burning.append((t, r, m))
            used.add(t)
        if len(burning) == 2:
            break
    (ta, ra, ma), (tb, rb, _) = burning
    other = next(x for x in rt.ai_with_office(o) if facilities_office(o, x, ta) is None)
    w8(o, record_at(o, ta, ra) + 2, other)
    w8(o, record_at(o, tb, rb) + 2, 0xFF)
    td = next(t for t in range(towns) if t not in used and any(
        o.u8(record_at(o, t, r) + 3) == 0x32 for r in range(o.u16(o.town(t) + 0x776))))
    tower = next(r for r in range(o.u16(o.town(td) + 0x776)) if o.u8(record_at(o, td, r) + 3) == 0x32)
    plain = next(r for r, _, _ in rt.records_of(o, td, o.u16(o.town(td) + 0x764))
                 if o.u8(record_at(o, td, r) + 3) not in (0x32, 0x33))
    for r in (tower, plain):
        unlink_record(o, td, r)
    w16(o, record_at(o, td, tower) + 4, plain)
    w8(o, record_at(o, td, plain) + 6, 0x40)
    w16(o, o.town(td) + 0x76A, tower)
    print(f'fires: town {ta} record {ra} (owner now {other}), town {tb} record {rb} (owner 0xFF), the store {store}; '
          f'town {td}: burning {tower} (tower) and {plain}')

    def during(o):
        now = o.u32(WORLD + 0x14)
        tick = now & ~0x38
        for t, r in ((ta, ra), (tb, rb), store):
            rec = record_at(o, t, r)
            new_event_at(o, tick, 0x1D, d8=tick, wC=0, wE=5, b10=o.u8(rec), b11=o.u8(rec + 1), b12=t, b13=0,
                         d14=0)
        new_event_at(o, tick, 0x1D, d8=tick, wC=0, wE=0, b10=0, b11=0, b12=td, b13=0, d14=0)
        direct(o, 0x4D85C0, [now], ecx=to.EVENTS)
        print(f'town {td}: [0x76A] {o.u16(o.town(td) + 0x76A)}, record {plain} [+0x6] {o.u8(record_at(o, td, plain) + 6):#x}')
    return during


def wedding_feast_branches(o):
    """Weddings (event 0x15, 0x4E7C74) and feasts (event 0x07, 0x4E23A4) put
    at the current tick and the queue called directly: a wedding of a
    merchant out of the world; of one whose [+0x28] has passed; of one whose
    [+0x13] is over his fleet (0); of the AI merchant with the biggest fleet
    (a hold 480000 bigger) in a town with [0x2C8] & 1, in a network game
    ([0x6DF2F0] & 2: [+0x138] = 0x15): his cog becomes a crayer (0x50E000).
    The player's feasts: in a town without his office; in one with it, with
    his reputation there −100 and 100 inhabitants (k below 0 and fewer than 5
    guests). Then 0x50E000 with a strength (the interface's and the new
    game's: artillery and levels) and 0x531470: on a town out of the world,
    and (through 0x5316E0, the town's feast) with an office of no merchant in
    the town's list."""
    towns, offices, merchants, _ = o.counts()
    player = o.u32(to.PLAYER)
    now = o.u32(WORLD + 0x14)

    def fleet(m):
        return sum(o.s32(o.ship(s) + 0x10) // 40000 for s in merchant_ships(o, m))
    ai = [m for m in range(merchants) if o.u16(o.merchant(m) + 8) and not o.u8(o.merchant(m) + 8) & 4]
    big = max(ai, key=fleet)
    other, third = [m for m in ai if m != big][:2]
    river = next(t for t in range(towns) if o.u32(o.town(t) + 0x2C8) & 1)
    for m in (big, third):
        w32(o, o.merchant(m) + 0x28, 0xFFFFFFFF)
    w32(o, o.merchant(other) + 0x28, now)
    first = o.u16(o.merchant(big) + 0xE)
    w32(o, o.ship(first) + 0x10, o.u32(o.ship(first) + 0x10) + 480000)
    homes = {o.u8(o.merchant(m) + 0x19) for m in (big, third, player)}
    mine = [office_town_of(o, f) for f in rt.merchant_offices(o, player)]
    feast_town = next((t for t in mine if t not in homes and t != river), next(t for t in mine if t != river))
    away = next(t for t in range(towns) if t not in mine)
    rep = o.merchant(player) + 0x2FC + 4 * feast_town
    o.uc.mem_write(rep, struct.pack('<f', -100.0))
    t5 = next(t for t in range(towns) if t not in homes and o.u16(o.town(t) + 0x784) < offices and t != feast_town)
    office5 = o.u16(o.town(t5) + 0x784)
    print(f'big {big} (fleet {fleet(big)}) in {river}, other {other}, third {third}; feasts in {away} and {feast_town}; '
          f'town {t5} with office {office5}')

    def wedding(m, town, age=5500, wares=0xFF, b13=1):
        new_event(o, 0, 0x15, d8=now, wC=age, bE=wares, bF=7, b10=0x85, b11=3, b12=town, b13=b13, b14=0, b15=m,
                  w16=0)

    def during(o):
        wedding(merchants, 0)
        wedding(other, 0)
        wedding(third, o.u8(o.merchant(third) + 0x19), b13=200)
        wedding(big, river)
        new_event(o, 0, 0x07, d8=player, dC=away, d10=2)
        new_event(o, 0, 0x07, d8=player, dC=feast_town, d10=2)
        people = o.u32(o.town(feast_town) + 0x2D4)
        w32(o, o.town(feast_town) + 0x2D4, 100)
        network = o.u32(NETWORK)
        w32(o, NETWORK, network | 2)
        direct(o, 0x4D85C0, [now], ecx=to.EVENTS)
        w32(o, NETWORK, network)
        w32(o, o.town(feast_town) + 0x2D4, people)
        s = o.u16(o.merchant(big) + 0xE)
        print(f'big\'s new ship {s}: type {o.u8(o.ship(s) + 0xE)}, [+0x138] {o.u16(o.ship(s) + 0x138):#x}')
        for kind, strength, town in ((1, 0x50, o.u8(o.merchant(player) + 0x19)), (2, 400, feast_town)):
            w16(o, SCRATCH_TYPE, kind)
            direct(o, 0x50E000, [SCRATCH_TYPE, strength, town, player], ecx=to.SHIPS)
        direct(o, 0x531470, [towns], ecx=WORLD)
        owner = o.u16(o.office(office5) + 0x2C4)
        w16(o, o.office(office5) + 0x2C4, 0xFFFF)
        w8(o, WORLD + 0x88, t5)
        w32(o, WORLD + 0x84, now - 1)
        o.tick_operations = []
        direct(o, 0x5316E0, [], ecx=WORLD)
        print(f'the feast of town {t5}: {[op.hex() for op in o.tick_operations]}')
        w16(o, o.office(office5) + 0x2C4, owner)
    return during


def free_office(o, f):
    """Office f leaves its merchant's and its town's lists and goes to the
    head of the free list (as when a merchant goes bankrupt)."""
    m, t = o.u16(o.office(f) + 0x2C4), o.u8(o.office(f) + 0x2C6)
    for link, next_field in ((o.merchant(m) + 0xC, 0x2C8), (o.town(t) + 0x784, 0x2CA)):
        while o.u16(link) != f:
            link = o.office(o.u16(link)) + next_field
        w16(o, link, o.u16(o.office(f) + next_field))
    w16(o, o.office(f) + 0x2C4, 0xFFFF)
    w16(o, o.office(f) + 0x2C8, o.u16(OFFICE_FREE))
    w16(o, OFFICE_FREE, f)


def event03_branches(o):
    """The event 0x03's pieces, direct calls with the event at the head of
    the queue: 0x4DDA40 with [0x6DF31C], with no more merchants than towns
    (the count lowered) and with [0x6DE52A]; 0x4DCEA0 with the player's
    administrators at skill 0xF0 (+0x2B overflows: no operation 0x67; his
    ships' captains young and none of the phase, and the rand state chosen
    so that the first draw lets it through); 0x4DCBF0 with every town on
    fire ([0x2C8] & 0x8000); 0x4DE790 (event 0x2B at the head) with
    [0x6DF31C] and a town of 10 million; and an office whose index is under
    the number of merchants freed and opened again (operation 0x5D,
    0x5308A0: the news 0x0D isn't written)."""
    towns, offices, merchants, ships = o.counts()
    player = o.u32(to.PLAYER)
    now = o.u32(WORLD + 0x14)
    (i03, e03), = events_of(o, 0x03)
    (i2b, e2b), = events_of(o, 0x2B)
    captains = o.u16(to.SHIPS + 0xF2)
    used = set()
    for s in merchant_ships(o, player):
        c = o.u16(o.ship(s) + 0x42)
        if c < captains:
            used.add(c & 7)
            w32(o, o.u32(to.SHIPS) + 0x10 * c + 4, now - 365 * 256)
    phase = next(p for p in range(8) if p not in used)
    w32(o, e03 + 0x10, (phase - 1) & 7 if phase else 7)
    admin = next(c for c in range(phase, captains, 8))
    w8(o, o.u32(to.SHIPS) + 0x10 * admin + 0xA, 0xF0)
    for f in rt.merchant_offices(o, player):
        w16(o, o.office(f) + 0x2F2, admin)
    seed = next(s for s in range(1, 100000) if rand_step(s)[1] & 0x3FF < 0x1B3)
    low = next(f for f in range(merchants) if o.u16(o.office(f) + 0x2C4) < merchants
               and o.u16(o.merchant(o.u16(o.office(f) + 0x2C4)) + 8))
    m, t = rt.ai_with_office(o)[0], None
    t = next(x for x in range(towns) if facilities_office(o, m, x) is None)
    print(f'phase {phase}, administrator {admin}, seed {seed}; office {low} freed; merchant {m} opens one in {t}')

    def head(i, a):
        rt.move_event(o, i, a, now - 0x100)

    def during(o):
        head(i2b, e2b)
        busy = o.u32(ALDERMAN_BUSY)
        people = o.u32(o.town(0) + 0x2D4)
        w32(o, o.town(0) + 0x2D4, 10000000)
        w32(o, ALDERMAN_BUSY, 1)
        direct(o, 0x4DE790, [], ecx=to.EVENTS)
        w32(o, ALDERMAN_BUSY, busy)
        w32(o, o.town(0) + 0x2D4, people)
        head(i03, e03)
        o.rand_state = seed
        direct(o, 0x4DCEA0, [], ecx=to.EVENTS)
        for address, value, size in ((ALDERMAN_BUSY, 1, 4), (WORLD + 0xA, towns, 2), (WORLD_FLAG_8A, 1, 1)):
            k = (4, 2, 1).index(size)
            old = (o.u32, o.u16, o.u8)[k](address)
            (w32, w16, w8)[k](o, address, value)
            direct(o, 0x4DDA40, [], ecx=to.EVENTS)
            (w32, w16, w8)[k](o, address, old)
        flags = [o.u32(o.town(x) + 0x2C8) for x in range(towns)]
        for x in range(towns):
            w32(o, o.town(x) + 0x2C8, flags[x] | 0x8000)
        direct(o, 0x4DCBF0, [], ecx=to.EVENTS)
        for x in range(towns):
            w32(o, o.town(x) + 0x2C8, flags[x])
        free_office(o, low)
        w32(o, o.merchant(m), 50000000)
        store(o, t)
        operation(o, 0x5D, m, t)
        print(f'merchant {m}: office {facilities_office(o, m, t)} in town {t}')
    return during


def child_names(o):
    """A child of a married AI merchant (0x4F69B0, direct calls), who may
    have (length of his first name % 3) + 2 children: with 2 and with 3
    already (so that the length decides) and his first name (0x512A60) one
    written by the players (the player's own and the index 85: 80 men's
    names, then the players' 20) or one past them (110: the list's first
    name); then with a sister born a year ago whose name the first draw of
    rand() gives (> 0x7F, a woman's: the next one is looked for among the
    women's names, 0x4F6B22; the rand state chosen for it)."""
    now = o.u32(WORLD + 0x14)
    m = rt.ai_with_office(o)[0]
    a = o.merchant(m)
    w8(o, a + 0x31, o.u8(a + 0x19))
    w32(o, a + 0x28, now - 18 * 365 * 256)
    w32(o, a + 0x2C, now - 300 * 256)
    w8(o, a + 0x36, 0)
    w16(o, a + 0x34, 0xFFFF)
    men = o.u16(0x6DDB70)
    seed = next(s for s in range(1, 100000) if rand_step(s)[1] & 0xFF > 0x7F)
    name = rand_step(seed)[1] & 0xFF
    sister = o.call(0x5097C0, [1], ecx=to.SHIPS) & 0xFFFF
    c = o.u32(to.SHIPS) + 0x10 * sister
    w16(o, c, 0xFFFF)
    w8(o, c + 2, name)
    w32(o, c + 4, now - 365 * 256)
    print(f'merchant {m}: {men} men\'s names; seed {seed} draws {name:#x}; the sister {sister}')

    def during(o):
        own = o.u8(o.merchant(o.u32(to.PLAYER)) + 0x1C)
        for first in (own, men + 5, men + 30):
            for children in (2, 3):
                w8(o, a + 0x1C, first)
                w8(o, a + 0x36, children)
                o.tick_operations = []
                print(f'first name {first}, {children} children: {direct(o, 0x4F69B0, [m], ecx=a) & 0xFF}')
        w16(o, a + 0x34, sister)
        w8(o, a + 0x36, 1)
        o.rand_state = seed
        o.tick_operations = []
        print(f'with the sister: {direct(o, 0x4F69B0, [m], ecx=a) & 0xFF}, {[op[:10].hex() for op in o.tick_operations]}')
    return during


def yard_branches(o):
    """Ships ordered at a shipyard (operation 0x2C, 0x52A360), in a town with
    one and materials: an AI merchant's order with the yard's level for the
    type below 0; a cog (type 2) with the town's [0x2C8] & 1; with the
    shipyard's bit 0x20000 of [0x76C] off (0x52B2C0 refuses); without money
    and not forced; and the player's order (the default name of the type)."""
    towns, _, merchants, _ = o.counts()
    player = o.u32(to.PLAYER)
    t = next(x for x in range(towns) if o.u32(o.town(x) + 0x76C) & 0x20000 and not o.u32(o.town(x) + 0x2C8) & 1
             and all(o.u8(o.town(x) + 0x824 + k) < 0x80 for k in (1, 2)))
    a = o.town(t)
    rt.yard_materials(o, t, 150)
    m = rt.ai_with_office(o)[0]
    print(f'town {t}: yard levels {[o.u8(a + 0x824 + k) for k in range(4)]}; merchant {m}, the player {player}')

    def order(kind, who, force):
        operation(o, 0x2C, kind, who, t, force)

    def during(o):
        level = o.u8(a + 0x825)
        w8(o, a + 0x825, 0xFF)
        order(1, m, 1)
        w8(o, a + 0x825, level)
        flags = o.u32(a + 0x2C8)
        w32(o, a + 0x2C8, flags | 1)
        order(2, m, 1)
        w32(o, a + 0x2C8, flags)
        bits = o.u32(a + 0x76C)
        w32(o, a + 0x76C, bits & ~0x20000)
        order(1, m, 1)
        w32(o, a + 0x76C, bits)
        money = o.u32(o.merchant(m))
        w32(o, o.merchant(m), 0)
        order(1, m, 0)
        w32(o, o.merchant(m), money)
        w32(o, o.merchant(player), 10000000)
        order(1, player, 0)
        s = o.u16(o.merchant(player) + 0xE)
        print(f'the player\'s new ship {s}: [+0x15E] {o.u16(o.ship(s) + 0x15E):#x}, '
              f'{bytes(o.uc.mem_read(o.ship(s) + 0x160, 8))}')
    return during


def captain_retire_branches(o):
    """AI captains retiring (event 0x27, 0x4DDC00; the queue called directly
    with the events at the current tick), by the ship's state: 0 (leaves it),
    4 leading a convoy (waits 0x20), 5 (leaves it), 7 with the captain
    already in the free list (0x5098B0 gives 0xFFFF), 12, 8 (the shipyard
    freed), 9 (waits 0x40), 10 and 0x15 (nothing); a captain out of the
    world; and a ship whose captain isn't the event's: the game looks at the
    first ship of each merchant from the last, and finds him in the
    player's (the last merchant; if his first ship has no captain it gets
    one, as otherwise the game loops for ever). The AI ships in port get a
    captain made by the game (0x5097C0) if they have none."""
    towns, _, merchants, ships = o.counts()
    player = o.u32(to.PLAYER)
    captains = o.u16(to.SHIPS + 0xF2)
    convoys = o.u16(CONVOY_COUNT)

    def leads(s):
        c = o.u16(o.ship(s) + 8)
        return c < convoys and o.u16(convoy_at(o, c) + 0x10) == s
    ai = [m for m in range(merchants) if o.u16(o.merchant(m) + 8)]
    pool = [s for m in ai for s in merchant_ships(o, m) if o.u8(o.ship(s) + 0x39) < towns and not leads(s)
            and o.u16(o.ship(s) + 0x134) not in (0x11, 0x12)][:10]
    for s in pool:
        if o.u16(o.ship(s) + 0x42) >= o.u16(to.SHIPS + 0xF2):
            w16(o, o.ship(s) + 0x42, o.call(0x5097C0, [0], ecx=to.SHIPS) & 0xFFFF)  # a captain of the game's
    captains = o.u16(to.SHIPS + 0xF2)
    lead = next(convoy_at(o, c) for c in range(convoys) if o.u8(convoy_at(o, c)) < merchants
                and o.u8(convoy_at(o, c)) != player and o.u16(convoy_at(o, c) + 0x10) < ships
                and o.u16(o.ship(o.u16(convoy_at(o, c) + 0x10)) + 0x42) < captains)
    lead = o.u16(lead + 0x10)
    states = [0, 5, 7, 12, 8, 9, 10, 0x15]
    chosen = pool[:len(states)]
    first = o.u16(o.merchant(player) + 0xE)
    if o.u16(o.ship(first) + 0x42) >= captains:
        w16(o, o.ship(first) + 0x42, o.call(0x5097C0, [0], ecx=to.SHIPS) & 0xFFFF)
        captains = o.u16(to.SHIPS + 0xF2)
    wanted = o.u16(o.ship(first) + 0x42)
    print(f'ships {list(zip(chosen, states))}, convoy lead {lead}; the player\'s first ship {first}, captain {wanted}')

    def during(o):
        now = o.u32(WORLD + 0x14)
        for s, state in zip(chosen, states):
            w16(o, o.ship(s) + 0x134, state)
            new_event(o, 0, 0x27, d8=s, dC=o.u16(o.ship(s) + 0x42))
        seven = chosen[states.index(7)]
        c = o.u16(o.ship(seven) + 0x42)
        w16(o, o.u32(to.SHIPS) + 0x10 * c, o.u16(to.SHIPS + 0xE0))
        w16(o, to.SHIPS + 0xE0, c)
        w16(o, o.ship(lead) + 0x134, 4)
        new_event(o, 0, 0x27, d8=lead, dC=o.u16(o.ship(lead) + 0x42))
        new_event(o, 0, 0x27, d8=chosen[0], dC=captains)
        new_event(o, 0, 0x27, d8=chosen[1], dC=wanted)
        direct(o, 0x4D85C0, [now], ecx=to.EVENTS)
        print(f'captains now: {[(s, o.u16(o.ship(s) + 0x42)) for s in chosen + [lead, first]]}')
    return during


# --- events_mayor.cpp: elections, the Hanse's votes and the auctions ---

ALDERMAN, ALDERMAN_TOWN = 0x6DE52D, HANSE_TOWN
FREE_SHIP = to.SHIPS + 0xE2  # u16: the first free ship slot


def hanse_towns(o):
    return [t for t in range(o.counts()[0]) if o.u32(o.town(t) + 0x2C8) & 0x40]


def alderman_hanse_votes(o):
    """The Alderman's election (event 0x20, 0x4DBBD0) in its phase 0, with
    the event at the head of the queue, every Hanse town's mayor a merchant
    but one's and two merchants with [+0x37] = 4 (so the candidates are the
    Alderman's town, that town and the two merchants), the town with 3000
    inhabitants (the least as big as the most: half of it − 1); then no
    merchant and three citizens' towns of 17, 20 and 21 (the most under
    0x40: 0x40; the least under 0x20: 0x10, which gives the town of 17 a
    weight of 4 where 0x11 would give 0). The Hanse's vote (event 0x21, 0x4DC1F0) with the
    player's rank at home 6 (his vote weighs 2) and, in phase 5, with
    [+0x15] = 2 (the abstentions: phase 0 leaves it at 0)."""
    towns, _, merchants, _ = o.counts()
    now = o.u32(WORLD + 0x14)
    player = o.u32(to.PLAYER)
    (i20, e20), = events_of(o, 0x20)
    hanse = hanse_towns(o)
    mayors = {t: o.u8(o.town(t) + 0x6F1) for t in hanse}
    people = {t: o.u32(o.town(t) + 0x2D4) for t in hanse}
    ais = [m for m in range(merchants) if o.u16(o.merchant(m) + 8)]
    progress = {m: o.u8(o.merchant(m) + 0x37) for m in ais}
    hanse_ais = [m for m in ais if o.u8(o.merchant(m) + 0x19) in hanse]
    alderman_town = o.u8(ALDERMAN_TOWN)
    print(f'Hanse towns {hanse}, Alderman {o.u8(ALDERMAN)} of town {alderman_town}; merchants of Hanse towns '
          f'{hanse_ais[:6]}')
    day = (now & ~0xFF) - 0x100
    rt.move_event(o, i20, e20, day)

    def during(o):
        for t in hanse:
            w8(o, o.town(t) + 0x6F1, ais[0])
        for m in ais:
            w8(o, o.merchant(m) + 0x37, 0)
        for m in hanse_ais[:2]:
            w8(o, o.merchant(m) + 0x37, 4)
        lone = next(t for t in hanse if t != alderman_town)
        w8(o, o.town(lone) + 0x6F1, 0xFC)
        w32(o, o.town(lone) + 0x2D4, 3000)
        w32(o, e20, day)
        direct(o, 0x4DBBD0, [], ecx=to.EVENTS)
        print(f'one town of 3000: {bytes(o.uc.mem_read(e20, 0x18)).hex()}')
        w8(o, o.town(lone) + 0x6F1, ais[0])
        for m in ais:
            w8(o, o.merchant(m) + 0x37, 0)
        citizens = [t for t in hanse if t != alderman_town][:4 if alderman_town >= towns else 3]
        for t, n in zip(citizens, (17, 20, 21, 22)):
            w8(o, o.town(t) + 0x6F1, 0xFC)
            w32(o, o.town(t) + 0x2D4, n)
        w32(o, e20, day)
        direct(o, 0x4DBBD0, [], ecx=to.EVENTS)
        print(f'case B ({citizens}): {bytes(o.uc.mem_read(e20, 0x18)).hex()}')
        for t in hanse:
            w8(o, o.town(t) + 0x6F1, mayors[t])
            w32(o, o.town(t) + 0x2D4, people[t])
        for m in ais:
            w8(o, o.merchant(m) + 0x37, progress[m])
        home = o.u8(o.merchant(player) + 0x19)
        w8(o, o.merchant(player) + 0x39C + home, 6)
        outside = next(t for t in range(towns) if t not in hanse)
        i21, e21 = new_event_at(o, day - 0x100, 0x21, b8=o.u8(HANSE_TOWN), b9=outside, bA=2, bB=1, bC=0, bD=0, bE=4,
                                bF=0, d10=0, d14=0)
        direct(o, 0x4DC1F0, [i21], ecx=to.EVENTS)
        w32(o, e21, day - 0x100 + 5)
        w8(o, e21 + 0x15, 2)
        direct(o, 0x4DC1F0, [i21], ecx=to.EVENTS)
        print(f'the vote: {bytes(o.uc.mem_read(e21, 0x18)).hex()}')
    return during


def ship_making_branches(o):
    """New ships and their sale, direct calls: 0x507D60 in the modes 0 (at
    the shipyard), 1 (in port), 3 and 4 (out of the lists), and with no free
    slot (0x40 more); 0x52A8D0 with the yard's level below 0 (a hulk where
    the town can't make them) and with [0x828 + type] past the type's most;
    0x4F4400 (an AI bid for a ship) with the merchant's [+0x470] at 0; and
    0x509250 selling a big ship to a merchant whose home town has [0x2C8] &
    1 (the next town without it is his ship's base)."""
    towns, _, merchants, ships = o.counts()
    player = o.u32(to.PLAYER)
    ais = rt.ai_with_office(o)
    m, w = ais[0], ais[1]
    home_w = o.u8(o.merchant(w) + 0x19)
    big = next(s for x in ais[2:] for s in merchant_ships(o, x) if o.u8(o.ship(s) + 0xE) & 3 >= 2
               and o.u16(o.ship(s) + 8) >= o.u16(CONVOY_COUNT) and o.u16(o.ship(s) + 0x134) < 0xC)
    small = next(s for s in merchant_ships(o, m) if o.u8(o.ship(s) + 0xE) & 2 == 0)
    town = o.u8(o.merchant(m) + 0x19)
    hulkless = next(t for t in range(towns) if o.u8(o.town(t) + 0x827) >= 0x80)
    _, holder = new_event(o, 0x100000, 0x37, d8=0, dC=0)
    print(f'merchants {m} and {w} (home {home_w}); big ship {big}, small {small}; town {town}; '
          f'no hulks in {hulkless}; out in the event at {holder:#x}')

    def during(o):
        made = []
        for mode in (0, 1, 3, 4):
            direct(o, 0x507D60, [holder + 8, town, mode], ecx=to.SHIPS)
            made.append(o.u16(holder + 8))
        w16(o, FREE_SHIP, o.u16(to.SHIPS + 0xF4))
        direct(o, 0x507D60, [holder + 8, town, 1], ecx=to.SHIPS)
        made.append(o.u16(holder + 8))
        print(f'new ships {made}; now {o.u16(to.SHIPS + 0xF4)}')
        direct(o, 0x52A8D0, [o.ship(made[0]), 3], ecx=o.town(hulkless))
        extra = o.u8(o.town(town) + 0x829)
        w8(o, o.town(town) + 0x829, 100)
        direct(o, 0x52A8D0, [o.ship(made[1]), 1], ecx=o.town(town))
        w8(o, o.town(town) + 0x829, extra)
        a = o.merchant(m)
        capacity, wanted = o.u32(a + 0x470), o.u32(a + 0x53C)
        w32(o, a + 0x470, 0)
        w32(o, a + 0x53C, 1000)
        print(f'bid {direct(o, 0x4F4400, [small, town], ecx=a)}')
        w32(o, a + 0x470, capacity)
        w32(o, a + 0x53C, wanted)
        flags = o.u32(o.town(home_w) + 0x2C8)
        w32(o, o.town(home_w) + 0x2C8, flags | 1)
        direct(o, 0x509250, [big, w, 9000, 10000, town], ecx=to.SHIPS)
        w32(o, o.town(home_w) + 0x2C8, flags)
        print(f'ship {big}: owner {o.u8(o.ship(big))}, base {o.u8(o.ship(big) + 0x13A)}')
    return during


def auction_phases(o):
    """Auctions (events 0x0A and 0x36, 0x4E2CD4) put at the current tick and
    the queue called directly: a ship auction in phase 0 with a town out of
    the world (the event's tick % towns); in phase 2 with only [+0xC] (a
    ship; a building) and with none (a building: removed); in phase 7 with a
    town out of the world, sold to the player (the ship has no owner: the
    seller's name in his letter 0x5D from the tick, 0x4E32FF), unsold, and a building's
    auction with a second building in [+0xA] (it goes on with it); and a
    phase 9 (removed). The auctions' ships are made first by the game's
    phase 0 (not recorded). Saves 3, 5 and 7 (in 1 no AI office has two
    buildings)."""
    towns, _, merchants, ships = o.counts()
    now = o.u32(WORLD + 0x14)
    ais = rt.ai_with_office(o)
    made = []
    for k in range(2):
        _, e = new_event(o, 0x100000 + k, 0x0A, d8=0xFFFF, wA=0xFFFF, wC=0xFFFF, wE=0, d10=0, b14=0, b15=k, b16=0,
                         b17=0xFF)
        o.call(0x4E2CD4, [e], ecx=to.EVENTS)
        made.append(o.u16(e + 8))
    for f in range(o.counts()[1]):
        owner, t = o.u16(o.office(f) + 0x2C4), o.u8(o.office(f) + 0x2C6)
        if owner in ais and len(office_records(o, t, f)) >= 2:
            seller, sale_town, recs = owner, t, office_records(o, t, f)
            break
    else:
        raise RuntimeError('no AI office with two buildings')
    buyer = next(x for x in ais if x != seller)
    other_ship = merchant_ships(o, ais[0])[0]
    print(f'auction ships {made}; {seller}\'s buildings {recs[:2]} in town {sale_town}, buyer {buyer}')

    def auction(kind, phase, town, items, winner=0xFF, price=1000):
        i, e = new_event(o, 0, kind, w8=items[0], wA=items[1], wC=items[2], wE=price, w10=100, w12=0, b14=phase,
                         b15=town, b16=0, b17=winner)
        return e

    def during(o):
        auction(0x0A, 0, towns + 3, (0xFFFF, 0xFFFF, 0xFFFF))
        auction(0x0A, 2, 0, (0xFFFF, 0xFFFF, other_ship))
        auction(0x36, 2, sale_town, (0xFFFF, 0xFFFF, recs[0][0]))
        auction(0x36, 2, sale_town, (0xFFFF, 0xFFFF, 0xFFFF))
        auction(0x0A, 7, towns + 2, (made[0], 0xFFFF, 0xFFFF), winner=o.u32(to.PLAYER))
        auction(0x0A, 7, 1, (made[1], 0xFFFF, 0xFFFF))
        auction(0x36, 7, sale_town, (recs[0][0], recs[1][0], 0xFFFF), winner=buyer)
        auction(0x0A, 9, 0, (0xFFFF, 0xFFFF, 0xFFFF))
        direct(o, 0x4D85C0, [now], ecx=to.EVENTS)
        print(f'ships {made}: owners {[o.u8(o.ship(s)) for s in made]}; buildings {recs[:2]}: owners '
              f'{[o.u8(record_at(o, sale_town, r) + 2) for r, _, _ in recs[:2]]}')
    return during


def building_sale_branches(o):
    """The buildings' auctions' pieces, direct calls: 0x4F4550 (an AI bid
    for a building) for houses of classes 1 and 2 with one of the class in
    the town's works list [0x762], and for a fisherman (category 2) with the
    town's [0x2CC] at 2000, with its offices' list and without (the bid as
    it is); 0x5256F0 (the sale) of a building whose owner has no office in
    the town (nothing), of one that is the second of its office's list, to
    a buyer with an office there and to one without (0x5308A0 makes it).
    The buildings: three businesses of an office's list [+0x2CE] (the third
    goes first, so the unlinking walks more than a step). Saves 3, 5 and 7."""
    towns, _, merchants, _ = o.counts()
    m = rt.ai_with_office(o)[0]
    t = next(x for x in range(towns) if o.u16(o.town(x) + 0x762) < o.u16(o.town(x) + 0x776)
             and (o.u8(o.merchant(m) + 0x19) == x or facilities_office(o, m, x) is not None))
    a = o.town(t)
    works = o.u16(a + 0x762)
    target = next(r for r in range(o.u16(a + 0x776)) if o.u8(record_at(o, t, r) + 2) != m
                  and o.u8(record_at(o, t, r) + 6) == 0 and r != works)
    ais = rt.ai_with_office(o)
    for f in range(o.counts()[1]):
        owner, x = o.u16(o.office(f) + 0x2C4), o.u8(o.office(f) + 0x2C6)
        firms = rt.records_of(o, x, o.u16(o.office(f) + 0x2CE))
        buyers = [k for k in ais if k != owner and facilities_office(o, k, x) is not None]
        if owner < merchants and owner != m and len(firms) >= 3 and buyers:
            seller, st, recs, buyer = owner, x, firms, buyers[0]
            break
    else:
        raise RuntimeError('no office with three businesses and another AI office in its town')
    newcomer, stranger = [k for k in ais if k != seller and facilities_office(o, k, st) is None][:2]
    print(f'merchant {m}, town {t}: works {works}, building {target}; seller {seller} in {st} {recs[:3]}, '
          f'buyer {buyer}, newcomer {newcomer}, stranger {stranger}')

    def during(o):
        rec = record_at(o, t, target)
        kind = o.u8(rec + 3)
        work_kind = o.u8(record_at(o, t, works) + 3)
        for house in (0x18, 0x1B):
            w8(o, rec + 3, house)
            w8(o, record_at(o, t, works) + 3, house + 1)
            print(f'class {(house - 0x15) // 3}: {direct(o, 0x4F4550, [target, t, m], ecx=o.merchant(m))}')
        w8(o, record_at(o, t, works) + 3, work_kind)
        w8(o, rec + 3, 5)
        value, offices = o.u32(a + 0x2CC), o.u16(a + 0x784)
        w32(o, a + 0x2CC, 2000)
        print(f'fisherman: {direct(o, 0x4F4550, [target, t, m], ecx=o.merchant(m))}')
        w16(o, a + 0x784, 0xFFFF)
        print(f'fisherman, no offices: {direct(o, 0x4F4550, [target, t, m], ecx=o.merchant(m))}')
        w16(o, a + 0x784, offices)
        w32(o, a + 0x2CC, value)
        w8(o, rec + 3, kind)
        first = record_at(o, st, recs[0][0])
        w8(o, first + 2, stranger)
        direct(o, 0x5256F0, [recs[0][0], buyer, 1000, 900], ecx=o.town(st))
        w8(o, first + 2, seller)
        direct(o, 0x5256F0, [recs[2][0], buyer, 1000, 900], ecx=o.town(st))
        direct(o, 0x5256F0, [recs[1][0], newcomer, 1000, 900], ecx=o.town(st))
        print(f'newcomer {newcomer}: office {facilities_office(o, newcomer, st)}')
    return during


# events_rare.cpp
GOAL_LIMIT = (0x16D00, 0x2DA00, 0x44700, 0x5B400, 0x72100, 0x88E00, 0x9FB00, 0xB6800, 0xCD500, 0xE4200, 0x156300,
              0x1C8400, 0x2AC600, 0x474A00, 0, 0, 0x11170, 0x186A0, 0x30D40)  # the goals' limits (0x4E4A7C)
MONTH_DAYS = (31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31)  # 0x672D94
START_TICK = WORLD + 0xC  # u32: the game's first tick
GAME_OVER = 0x6DFC04  # u32: the end of the game (0x4E4A44) sets it to 0
MONTH = WORLD + 1  # u8


def first_event_of(o, kind):
    """The first record of the event array (not the queue's order) of the kind."""
    base = o.u32(to.EVENTS)
    return next(k for k in range(o.u16(EVENT_COUNT)) if o.u16(base + 0x18 * k + 6) == kind)


def game_end_branches(o):
    """The end of the game (event 0x0E, 0x4E4A44, called directly on one
    event record rewritten for each call: the goal [+0x8], the limit's index
    [+0xA], its tick and the game's start [0x6DE4AC] chosen for the branch).
    The goal 1 (rank 4 in one's home town) with the limit passed, the
    player at rank 4, an AI merchant at rank 4 with the settings' [+0xD] at 0
    (he doesn't count: a month later, the tick of the event record of index
    the number of merchants), the same clamped to the limit, with 20
    merchants (the record 0x14) and without a limit (index 14: the game's
    uninitialized local, 0 here as in p3vita: the frame zeroed); the goal 2
    (a mayor) with the limit passed, the player a mayor, an AI mayor that
    doesn't count (a tick after the first event 2 of the queue), clamped,
    without a limit (index 15), with 20 merchants and with no event 2; the
    goal 3 (the Hanse's head) with the limit passed, the player its head, an
    AI head that doesn't count (a tick after the first event 0x20 of the
    array, clamped), without a limit (index 0x20) and with 20 merchants; the
    goals 4 and 5 (the limit) before and at it; the goal 6 (the towns'
    people) under and over it (the saves have less than 70000: town 0 gets
    70000 more); and a goal 7 (none)."""
    towns, _, merchants, _ = o.counts()
    player = o.u32(to.PLAYER)
    settings = o.u32(to.SETTINGS_POINTER)
    ai = next(m for m in range(0x14, merchants) if o.u16(o.merchant(m) + 8))
    people = sum(o.u32(o.town(t) + 0x2D4) for t in range(towns))
    o.uc.hook_add(UC_HOOK_CODE, o._zero_frame, begin=0x4E4A44, end=0x4E4A44, user_data=0x30)
    o.uc.ctl_remove_cache(0x4E4A44, 0x4E4A45)
    print(f'player {player}, AI merchant {ai}, settings [+0xD] {o.u8(settings + 0xD)}, people {people}')

    def during(o):
        now = o.u32(WORLD + 0x14)
        _, e = new_event(o, 0x10000, 0x0E)
        events = o.u32(to.EVENTS)
        old = {a: o.u8(a) for a in (settings + 0xD, ALDERMAN, o.town(0) + 0x6F1)}
        old_start, old_over = o.u32(START_TICK), o.u32(GAME_OVER)
        ranks = [o.merchant(m) + 0x39C + o.u8(o.merchant(m) + 0x19) for m in (player, ai)]
        old_ranks = [o.u8(a) for a in ranks]
        for a in ranks:
            w8(o, a, 3)
        L = GOAL_LIMIT

        def goal(what, kind, index, tick, first):
            w16(o, e + 8, kind)
            w16(o, e + 0xA, index)
            w32(o, e, tick)
            w32(o, START_TICK, first)
            w32(o, GAME_OVER, 1)
            r = direct(o, 0x4E4A44, [e], ecx=to.EVENTS)
            print(f'{what}: goal {kind}, limit {index}: {r}, tick {o.u32(e):#x}, [0x6DFC04] {o.u32(GAME_OVER)}')

        def count20(what, kind):
            w16(o, WORLD + 0xA, 0x14)
            goal(what, kind, 13, now, now)
            w16(o, WORLD + 0xA, merchants)

        # Goal 1: rank 4.
        goal('passed', 1, 0, now, now - L[0])
        w8(o, ranks[0], 4)
        goal('the player at rank 4', 1, 13, now, now)
        w8(o, ranks[0], 3)
        w8(o, ranks[1], 4)
        w8(o, settings + 0xD, 0)
        goal('an AI merchant at rank 4', 1, 13, now, now)
        new = (MONTH_DAYS[o.u8(MONTH)] << 8) + o.u32(events + 0x18 * merchants) & 0xFFFFFFFF
        goal('clamped', 1, 0, new - 2, new - 1 - L[0])
        count20('20 merchants', 1)
        goal('no limit', 1, 14, now, now - 0x100)
        # Goal 2: a mayor.
        goal('passed', 2, 0, now, now - L[0])
        w8(o, o.town(0) + 0x6F1, ai)
        goal('an AI mayor', 2, 13, now, now)
        w8(o, settings + 0xD, old[settings + 0xD])
        w8(o, o.town(0) + 0x6F1, player)
        goal('the player a mayor', 2, 13, now, now)
        w8(o, o.town(0) + 0x6F1, old[o.town(0) + 0x6F1])
        tick2 = next(o.u32(a) for _, a in rt.events(o) if o.u16(a + 6) == 2)
        goal('clamped', 2, 0, tick2 - 1, tick2 - L[0])
        goal('no limit', 2, 15, now, now - 0x100000)
        count20('20 merchants', 2)
        twos = [a for _, a in rt.events(o) if o.u16(a + 6) == 2]
        for a in twos:
            w16(o, a + 6, 0x7F)
        goal('no event 2', 2, 13, now, now)
        for a in twos:
            w16(o, a + 6, 2)
        # Goal 3: the Hanse's head.
        goal('passed', 3, 0, now, now - L[0])
        w8(o, ALDERMAN, player)
        goal('the player the head', 3, 13, now, now)
        w8(o, ALDERMAN, ai)
        w8(o, settings + 0xD, 0)
        tick20 = o.u32(events + 0x18 * first_event_of(o, 0x20))
        goal('an AI head, clamped', 3, 0, tick20 - 1, tick20 - L[0])
        w8(o, settings + 0xD, old[settings + 0xD])
        w8(o, ALDERMAN, old[ALDERMAN])
        goal('no limit', 3, 0x20, now, now - 0x100000)
        count20('20 merchants', 3)
        # Goals 4 and 5: the limit; 6: the towns' people; 7: none.
        goal('before', 4, 13, now, now)
        goal('passed', 4, 0, now, now - L[0])
        goal('before', 5, 13, now, now)
        goal('passed', 5, 0, now, now - L[0])
        goal('fewer people', 6, 16, now, now)
        w32(o, o.town(0) + 0x2D4, o.u32(o.town(0) + 0x2D4) + GOAL_LIMIT[16])
        goal('more people', 6, 16, now, now)
        w32(o, o.town(0) + 0x2D4, o.u32(o.town(0) + 0x2D4) - GOAL_LIMIT[16])
        goal('no goal', 7, 13, now, now)
        for a, v in zip(ranks, old_ranks):
            w8(o, a, v)
        w32(o, START_TICK, old_start)
        w32(o, GAME_OVER, old_over)
    return during


BUILD_DAYS = 0x672BB4  # u8 per building type: the days of works


def free_captain(o):
    """Takes the first captain out of the free list ([0x6DD880], linked by
    [+0x0]) and returns him."""
    c = o.u16(to.SHIPS + 0xE0)
    w16(o, to.SHIPS + 0xE0, o.u16(o.u32(to.SHIPS) + 0x10 * c))
    return c


def siege_fire_branches(o):
    """The rare events that go through the queue (0x4D85C0, called directly
    with new events at the tick), then 0x4DB100 and the plague (0x4E9094)
    called directly. The siege's fires 0x25: the first time on a wall
    piece in works (100 less what is left of them), on a cell with no wall
    piece, and burnt out with an empty burning list [0x76A] and with one
    that doesn't hold it. The fires 0x26: a town out; buildings of the types
    0x28, 0x32 and 0x33 (nothing); a merchant's without an office in the town
    (0x5308A0 gives 0) and with one but not in its lists; a town's
    building in works; one of type 0x29 with a merchant's owner (looked for
    in the town's list [0x764]); one of no merchant not in [0x764] (looked
    for twice); a town with [0x2C8] & 0x8000; and burnt out, not in the
    burning list. The tax 0x31 of a town out. The monuments 0x38 of a town
    out, of a merchant out, of a mayor with another event 0x38 later in the
    queue (in a town with a monument), and in a town whose first building 0x2D in [0x764] is a
    merchant's (no monument). Then 0x4DB100 with a town out, a record out,
    and a building in an auction 0x36 with another lot (kept) and alone
    (the auction removed). And the plague 0x4E9094 (its event at the head)
    of a town out, and in a town whose first office has an administrator
    (the first free captain, if it has none) when the tick & 0x7F is the
    next office's: the administrator leaves (0x5098B0)."""
    towns, offices, merchants, _ = o.counts()
    now = o.u32(WORLD + 0x14)
    days = lambda kind: o.u8(BUILD_DAYS + kind)
    rec = lambda t, r: record_at(o, t, r)
    kind = lambda t, r: o.u8(rec(t, r) + 3)
    city = lambda t: [r for r, _, _ in rt.records_of(o, t, o.u16(o.town(t) + 0x764))]
    outside = lambda t: [r for r in range(o.u16(o.town(t) + 0x776)) if r not in set(city(t))]
    plain = lambda t, r: kind(t, r) not in (0x28, 0x29, 0x32, 0x33)
    used = set()

    def pick(test):
        t = next(x for x in range(towns) if x not in used and test(x))
        used.add(t)
        return t
    # T1: a wall piece and two buildings of the town with days of works.
    t1 = pick(lambda t: any(days(kind(t, r)) for r, _, _ in rt.records_of(o, t, o.u16(o.town(t) + 0x768)))
              and len([r for r in city(t) if plain(t, r) and days(kind(t, r))]) >= 2)
    wall = next(r for r, _, _ in rt.records_of(o, t1, o.u16(o.town(t1) + 0x768)) if days(kind(t1, r)))
    works, twenty_nine = [r for r in city(t1) if plain(t1, r) and days(kind(t1, r))][:2]
    # T2: six buildings of the town, one out of its list [0x764], a merchant with an office and one without.
    m_in = lambda t: next((m for m in rt.ai_with_office(o) if facilities_office(o, m, t) is not None), None)
    m_out = lambda t: next((m for m in rt.ai_with_office(o) if facilities_office(o, m, t) is None), None)
    t2 = pick(lambda t: len([r for r in city(t) if plain(t, r)]) >= 6 and o.u16(o.town(t) + 0x76A) == 0xFFFF
              and any(plain(t, r) for r in outside(t)) and m_in(t) is not None and m_out(t) is not None)
    c2 = [r for r in city(t2) if plain(t2, r)][:6]
    held = next(r for r in outside(t2) if plain(t2, r))
    # T3: a burning list of one building; T4: the auctions; T5, T6: the monuments; TP: the plague.
    t3 = pick(lambda t: len([r for r in city(t) if plain(t, r)]) >= 2)
    burning, other = [r for r in city(t3) if plain(t3, r)][:2]
    t4 = pick(lambda t: len(city(t)) >= 3)
    lots = city(t4)[:3]
    monuments = lambda t: [r for r in city(t) if kind(t, r) == 0x2D]
    t5 = pick(lambda t: monuments(t) and o.u8(rec(t, monuments(t)[0]) + 2) >= merchants)
    t6 = pick(lambda t: monuments(t))
    tp = pick(lambda t: len(town_offices(o, t)) >= 2)
    ais = rt.ai_with_office(o)
    m5, m6 = ais[0], ais[1]
    print(f'T1 {t1} (wall {wall}, works {works}, 0x29 {twenty_nine}), T2 {t2} ({c2}, held {held}, merchants with '
          f'an office {m_in(t2)}, without {m_out(t2)}), T3 {t3} ({burning}, {other}), T4 {t4} {lots}, T5 {t5}, '
          f'T6 {t6}, TP {tp}; mayors {m5}, {m6}')
    # The retouches.
    w8(o, rec(t1, wall) + 6, 10)
    w8(o, rec(t1, works) + 6, 10)
    w8(o, rec(t1, twenty_nine) + 2, m5)
    w8(o, rec(t1, twenty_nine) + 3, 0x29)
    w8(o, rec(t1, twenty_nine) + 6, 0)  # 0x29 has no days of works: the game would divide by 0
    for r, k in zip(c2, (0x28, 0x32, 0x33)):
        w8(o, rec(t2, r) + 3, k)
    w8(o, rec(t2, c2[3]) + 2, m_out(t2))
    w8(o, rec(t2, c2[4]) + 2, m_in(t2))
    w8(o, rec(t2, held) + 2, 0xFF)
    unlink_record(o, t3, burning)
    w16(o, o.town(t3) + 0x76A, burning)
    w32(o, o.town(t3) + 0x2C8, o.u32(o.town(t3) + 0x2C8) | 0x8000)
    w8(o, o.town(t5) + 0x6F1, m5)
    w8(o, o.town(t6) + 0x6F1, m6)
    w8(o, rec(t6, monuments(t6)[0]) + 2, m6)
    first = o.u16(o.town(tp) + 0x784)
    after = o.u16(o.office(first) + 0x2CA)
    if o.u16(o.office(first) + 0x2F2) >= o.u16(to.SHIPS + 0xF2):
        w16(o, o.office(first) + 0x2F2, free_captain(o))
    print(f'TP office {first} (next {after}): administrator {o.u16(o.office(first) + 0x2F2)}')

    def during(o):
        def fire(k, t, r, first, life=0, x=None, y=0):
            if x is None:
                x, y = (o.u8(rec(t, r)), o.u8(rec(t, r) + 1)) if t < towns else (0, 0)
            new_event_at(o, now, k, w8=r, wA=life, wC=x, wE=y, b10=t, b11=first)
        fire(0x25, t1, wall, 1)
        fire(0x25, t2, c2[5], 1, x=0x100)
        fire(0x25, t2, c2[5], 0)
        fire(0x25, t3, other, 0)
        fire(0x26, towns, 0, 1)
        for r in c2[:5]:
            fire(0x26, t2, r, 1)
        fire(0x26, t2, held, 1)
        fire(0x26, t1, works, 1)
        fire(0x26, t1, twenty_nine, 1)
        fire(0x26, t3, other, 0, life=50)
        fire(0x26, t2, c2[5], 0)
        new_event_at(o, now, 0x31, d8=10000, bC=towns)
        new_event_at(o, now, 0x38, b8=towns, b9=0)
        new_event_at(o, now, 0x38, b8=t5, b9=merchants)
        new_event_at(o, now, 0x38, b8=t5, b9=m5)
        new_event_at(o, now + 0x10000, 0x38, b8=t5, b9=m5)
        new_event_at(o, now, 0x38, b8=t6, b9=m6)
        direct(o, 0x4D85C0, [now], ecx=to.EVENTS)
        print(f'T1 burning list {[r for r, _, _ in rt.records_of(o, t1, o.u16(o.town(t1) + 0x76A))]}')
        # 0x4DB100.
        new_event_at(o, now + 0x10000, 0x36, w8=lots[0] | 0x8000, wA=lots[1] | 0x8000, wC=0xFFFF, b15=t4)
        _, alone = new_event_at(o, now + 0x10000, 0x36, w8=lots[2] | 0x8000, wA=0xFFFF, wC=0xFFFF, b15=t4)
        direct(o, 0x4DB100, [towns, lots[0]], ecx=to.EVENTS)
        direct(o, 0x4DB100, [t4, o.u16(o.town(t4) + 0x776)], ecx=to.EVENTS)
        direct(o, 0x4DB100, [t4, lots[0]], ecx=to.EVENTS)
        direct(o, 0x4DB100, [t4, lots[2]], ecx=to.EVENTS)
        print(f'the lone auction: kind {o.u16(alone + 6):#x}')
        # The plague.
        for k, (t, at) in enumerate(((towns, 0x100), (tp, 0x200))):
            i, a = new_event(o, 0x30000, 0x1C, b8=1, b9=1, bA=1, bB=1, dC=now + 0x30000, w10=0, w12=0x7FF0, b16=t)
            rt.move_event(o, i, a, (now & ~0xFF) - at + 0x10)
            tick = (now & ~0x7F) | (after & 0x7F)
            w32(o, WORLD + 0x14, tick)
            print(f'plague in {t} at {tick:#x}: {direct(o, 0x4E9094, [], ecx=to.EVENTS)}')
            w32(o, WORLD + 0x14, now)
        print(f'TP office {first}: administrator {o.u16(o.office(first) + 0x2F2)}')
    return during


SEASON_WARES = (1, 5, 7, 9, 0xC, 0xE, 0x12)  # the wares the pirates' season looks at (0x4E0334)


def pirate_event_branches(o):
    """The pirates' events, direct calls. 0x4DFD44 (event 0x1E, on one event
    record rewritten for each call): the state 0 over (the town's [0x6F0] to
    0xFF) without [0x2C8] & 0x600 and with it and the other classes' moods
    over −10 (the state 4); the state 1 going on, and over with [0x2C8] &
    0x600 and a mood of −20 (the town's [0x2C6] back, the bit 0x2000000
    off); the state 2 over; and a state 5. 0x4E0334 (event 0x1F, the save's,
    with its first slot the town): before March (day 30); a town of six very
    low moods (kind 4); one of high moods with a pirate object of 600 (kind
    2); one with a business of more than 950 ([0x848 + 16·k]) and 200 of its
    workers (kind 1) when every town with [0x2C8] & 0x40 but the last has the
    player for mayor and that one an AI merchant (the operation 0x84 skips
    the human mayors and takes the AI one's names). The day 100 and, but for the
    kind 1, no business over 950 and no ware short ([0x430 + 4·w]), so that
    the kind stays. 0x4EA594 (event 0x30 at the head): with no task tick
    [+0x8] and, with an AI merchant the Hanse's head, of kind 5. Saves 3, 5
    and 7 (save 1 has no world records)."""
    towns, _, merchants, _ = o.counts()
    player = o.u32(to.PLAYER)
    now = o.u32(WORLD + 0x14)
    (i1f, e1f), = events_of(o, 0x1F)
    plain = [t for t in range(towns) if not o.u32(o.town(t) + 0x2C8) & 0x40]
    hanse = [t for t in range(towns) if o.u32(o.town(t) + 0x2C8) & 0x40]
    ta, tb, tc = plain[:3]
    ai = next(m for m in range(merchants) if o.u16(o.merchant(m) + 8))
    pirate = next(o.u32(PIRATES + 4 * k) for k in range(5) if o.u32(PIRATES + 4 * k))
    records = o.u16(RECORD_COUNT)
    if not records:
        raise RuntimeError('no world records (the businesses): the kind 1 needs one')
    record = lambda r: o.u32(WORLD_RECORDS) + 0x14 * r
    others = sum(o.u16(record(r) + 4) for r in range(1, records) if o.u8(record(r) + 6) == 4)
    workers = max(200, others // 19 + 1)
    print(f'towns {ta}, {tb}, {tc}; with [0x2C8] & 0x40 {hanse}; AI merchant {ai}; the event 0x1F {i1f}; '
          f'businesses 4 elsewhere {others}, here {workers}')

    def keep(fields, saved):
        """Writes (address, size, value) and returns the old ones to put back."""
        for a, size, v in fields:
            saved.append((a, size, (o.u8, o.u16, o.u32)[(1, 2, 4).index(size)](a)))
            (w8, w16, w32)[(1, 2, 4).index(size)](o, a, v)
        return saved

    def restore(saved):
        for a, size, v in reversed(saved):
            (w8, w16, w32)[(1, 2, 4).index(size)](o, a, v)

    def during(o):
        # Event 0x1E.
        _, e = new_event(o, 0x10000, 0x1E)
        ta_flags = o.u32(o.town(ta) + 0x2C8)

        def pirates(what, state, over, flags=0, moods=(0, 0, 0)):
            saved = keep([(o.town(ta) + 0x2C8, 4, (ta_flags & ~0x600) | flags), (o.town(ta) + 0x6F0, 1, 3)] +
                         [(o.town(ta) + 0x308 + 2 * k, 2, v & 0xFFFF) for k, v in enumerate(moods)], [])
            tick = now - 1 if over else now + 0x1000
            for a, v in ((e + 8, tick), (e + 0xC, tick)):
                w32(o, a, v)
            for a, v in ((e + 0x10, state), (e + 0x11, ta), (e + 0x14, 5), (e + 0x15, 2), (e + 0x16, 0xFF),
                         (e + 0x17, 0)):
                w8(o, a, v)
            w16(o, e + 0x12, 0x1234)
            r = direct(o, 0x4DFD44, [e], ecx=to.EVENTS)
            print(f'{what}: state {state}: {r}, now state {o.u8(e + 0x10)}, town [0x6F0] {o.u8(o.town(ta) + 0x6F0):#x}, '
                  f'[0x2C6] {o.u16(o.town(ta) + 0x2C6):#x}, [0x2C8] {o.u32(o.town(ta) + 0x2C8):#x}')
            restore(saved)
        pirates('over, no 0x600', 0, True)
        pirates('over, the pirates win', 0, True, 0x200)
        pirates('going on', 1, False)
        saved = keep([(o.town(ta) + 0x2C6, 2, o.u16(o.town(ta) + 0x2C6))], [])
        pirates('over, a mood of -20', 1, True, 0x2000200, (0, 0, -20))
        restore(saved)
        pirates('over', 2, True)
        pirates('no state', 5, False)
        # Event 0x1F.

        def season(what, town, day=100):
            saved = keep([(DAY, 2, day)] + [(e1f + 8 + k, 1, 0xFF) for k in range(8)] +
                         [(e1f + 0x10 + k, 1, 0x11 + k) for k in range(8)], [])
            w8(o, e1f + 8, town)
            r = direct(o, 0x4E0334, [i1f], ecx=to.EVENTS)
            print(f'{what}: {r}, tick {o.u32(e1f):#x}, last event {[hex(o.u16(a + 6)) for _, a in events_of(o, 0x1E)]}')
            w16(o, DAY, saved[0][2])

        def quiet(t, keep_business=None):
            a = o.town(t)
            fields = [(a + 0x430 + 4 * w, 4, 0) for w in SEASON_WARES]
            fields += [(a + 0x848 + 16 * k, 2, 0) for k in range(4, 0x15) if k != keep_business]
            return keep(fields, [])
        season('before March', ta, day=30)
        saved = quiet(tb)
        keep([(o.town(tb) + 0x300 + 2 * k, 2, 0xFFE0) for k in (0, 1, 2, 4, 5, 6)], saved)
        season('kind 4', tb)
        restore(saved)
        saved = quiet(tb)
        keep([(o.town(tb) + 0x300 + 2 * k, 2, 30) for k in range(3)] + [(pirate + 0x16, 2, 600)], saved)
        season('kind 2', tb)
        restore(saved)
        saved = quiet(tc, 4)
        keep([(o.town(tc) + 0x848 + 16 * 4, 2, 1000), (record(0) + 4, 2, workers), (record(0) + 6, 1, 4),
              (record(0) + 7, 1, o.u8(o.town(tc) + 0x2C0))] + [(o.town(t) + 0x6F1, 1, player) for t in hanse[:-1]] +
             [(o.town(hanse[-1]) + 0x6F1, 1, ai)], saved)
        season('kind 1, human mayors but one', tc)
        restore(saved)
        # Event 0x30.
        for k, (task, head) in enumerate(((0, 0xFC), (now, ai))):
            i, a = new_event(o, 0x20000, 0x30, d8=task, bC=0, b12=0, b13=5, b14=0, b15=0)
            rt.move_event(o, i, a, now - 0x100 * (k + 1))
            old = o.u8(ALDERMAN)
            w8(o, ALDERMAN, head)
            print(f'0x30 with task {task:#x}, head {head}: {direct(o, 0x4EA594, [], ecx=to.EVENTS)}')
            w8(o, ALDERMAN, old)
    return during


def set_fields(o, fields):
    """Writes (address, size, value) and returns the old values (restore_fields puts them back)."""
    saved = []
    for a, size, v in fields:
        k = (1, 2, 4).index(size)
        saved.append((a, size, (o.u8, o.u16, o.u32)[k](a)))
        (w8, w16, w32)[k](o, a, v)
    return saved


def restore_fields(o, saved):
    for a, size, v in reversed(saved):
        (w8, w16, w32)[(1, 2, 4).index(size)](o, a, v)


def council_task_branches(o):
    """Direct calls. The council's vote 0x4E9A94 (its event 0x2E at the head;
    the world's tick with its low byte 0, the votes shared, or 0xFF with the
    event's, the count): of a town out; the extra tax (proposal 0) with a
    recent one ([0x6F8]) and the share between 10 and 90, over 90 (nothing
    paid) and under 10 (a million); the wall (1) with [+0x15], with works
    pending, and with none and [0x78C] = [0x78E] (0x4FAC70); [0x9A0] (2)
    with [0x2C8] & 0x10 and the weight between, under 100 and over 0x398;
    the tax per head (3) under and over the town's rate; a proposal 4. Then
    the count, the proposals passed: the extra tax (an event 0x31 a day
    later), the wall with the point 1 out of the grid (the side 2, 0x522B20),
    [0x9A0] under 0x8000 and at it, the tax per head (0x52A150). Whether a
    Hanse task still stands, 0x532C50 (on a new event): past the queue, an
    event of another kind, a task with the merchant [+0xD] the number of
    merchants; the kind 1 with a town out and one with [0x76C] & 0x8000; the
    kind 2 with a ship out, not a pirate's (0x12), without a captain, with
    the captain's names and with another first or family name; the kind 3
    with the object 5 and a missing one; the kind 4 with a town out; a kind
    5. And 0x4EAFE4 (the event 0x32 at the head) of no merchant, with [+0xE]
    at 0x3F: given to the AI merchants with [+0x37] 1..3 (two of them set
    to 2), and with none."""
    towns, _, merchants, ships = o.counts()
    now = o.u32(WORLD + 0x14)
    captains = o.u16(to.SHIPS + 0xF2)
    inside = lambda t, i: (o.u8(o.town(t) + 0x7B0 + 2 * i) < o.u32(o.town(t) + 0x7A8)
                           and o.u8(o.town(t) + 0x7B1 + 2 * i) < o.u32(o.town(t) + 0x7AC))
    tc = next(t for t in range(towns) if inside(t, 2))
    a = o.town(tc)
    people = o.u32(a + 0x2D4)
    ais = [m for m in range(merchants) if o.u16(o.merchant(m) + 8)]
    s = merchant_ships(o, o.u32(to.PLAYER))[0]
    c = next(k for k in range(captains) if o.u8(o.u32(to.SHIPS) + 0x10 * k + 2) != 0)
    first, family = o.u8(o.u32(to.SHIPS) + 0x10 * c + 2), o.u8(o.u32(to.SHIPS) + 0x10 * c + 3)
    missing = next(k for k in range(5) if not o.u32(PIRATES + 4 * k))
    print(f'council in {tc} ({people} people, [0x766] {o.u16(a + 0x766)} of {o.u16(a + 0x776)}, [0x78C] '
          f'{o.u16(a + 0x78C)}, [0x78E] {o.u16(a + 0x78E)}, rate {o.u8(a + 0x6F8)}); ship {s}, captain {c} '
          f'({first}, {family}); no pirate object {missing}')

    def during(o):
        i, e = new_event(o, 0x20000, 0x2E)
        rt.move_event(o, i, e, now - 0x100)
        day = now & ~0xFF

        def vote(what, proposal, amount=0, flag=0, rate=0, result=False, town=tc, fields=()):
            saved = set_fields(o, [(WORLD + 0x14, 4, day - 1 if result else day)] + list(fields))
            w32(o, e, day - 1 if result else day)
            w32(o, e + 8, amount)
            for k, v in ((0xC, town), (0xD, proposal), (0xE, 0), (0xF, 0), (0x10, 5 if result else 0),
                         (0x11, 1 if result else 0), (0x12, 0), (0x13, 0), (0x14, rate), (0x15, flag), (0x16, 0xFF)):
                w8(o, e + k, v)
            r = direct(o, 0x4E9A94, [], ecx=to.EVENTS)
            print(f'{what}: {r}, for {o.u8(e + 0xE)}, against {o.u8(e + 0xF)}; [0x9A0] {o.u16(a + 0x9A0):#x}, '
                  f'[0x766] {o.u16(a + 0x766)}, rate {o.u8(a + 0x6F8)}')
            restore_fields(o, saved[:1])
            return saved[1:]
        vote('a town out', 0, town=towns)
        recent = (day & ~0xFF) | o.u8(a + 0x6F8)
        restore_fields(o, vote('the tax, recent', 0, 30000, fields=[(a + 0x6F8, 4, recent), (a + 0x6F4, 4, 20000)]))
        old = o.u8(a + 0x6F8)
        restore_fields(o, vote('the tax, nothing', 0, 0, fields=[(a + 0x6F8, 4, old)]))
        restore_fields(o, vote('the tax, a million', 0, 1000000, fields=[(a + 0x6F8, 4, old)]))
        vote('the wall, [+0x15]', 1, flag=1)
        restore_fields(o, vote('the wall, works', 1, fields=[(a + 0x766, 2, 0)]))
        restore_fields(o, vote('the wall, nothing pending', 1, fields=[
            (a + 0x766, 2, 0xFFFF), (a + 0x78C, 2, o.u16(a + 0x78E))]))
        between = (2 * people + 0x200 - 500) // 203
        restore_fields(o, vote('[0x9A0], 0x10', 2, fields=[(a + 0x2C8, 4, o.u32(a + 0x2C8) | 0x10), (a + 0x9A0, 2, between)]))
        restore_fields(o, vote('[0x9A0], under 100', 2, fields=[(a + 0x2C8, 4, o.u32(a + 0x2C8) & ~0x10),
                                                                 (a + 0x9A0, 2, (people + 0x200) // 203 + 2)]))
        restore_fields(o, vote('[0x9A0], over 0x398', 2, fields=[(a + 0x2C8, 4, o.u32(a + 0x2C8) & ~0x10),
                                                                  (a + 0x9A0, 2, 0)]))
        restore_fields(o, vote('per head, under', 3, rate=5, fields=[(a + 0x6F8, 1, 10)]))
        restore_fields(o, vote('per head, over', 3, rate=12, fields=[(a + 0x6F8, 1, 10)]))
        vote('a proposal 4', 4)
        vote('passed: the tax', 0, 50000, result=True)
        print(f'events 0x31: {[(i, hex(o.u32(x))) for i, x in events_of(o, 0x31)]}')
        restore_fields(o, vote('passed: the wall', 1, result=True, fields=[(a + 0x7B2, 1, 0xFF)]))
        vote('passed: [0x9A0]', 2, result=True, fields=[(a + 0x9A0, 2, 100)])
        vote('passed: [0x9A0] at 0x8000', 2, result=True, fields=[(a + 0x9A0, 2, 0x8000)])
        vote('passed: per head', 3, rate=7, result=True)
        # 0x532C50 on a new event.
        j, t = new_event(o, 0x30000, 0x32, bD=0xFF)

        def stands(what, fields=(), index=None):
            saved = set_fields(o, list(fields))
            r = direct(o, 0x532C50, [j if index is None else index], ecx=WORLD) & 0xFF
            print(f'stands, {what}: {r}')
            restore_fields(o, saved)
        stands('past the queue', index=o.u16(EVENT_COUNT))
        stands('another kind', [(t + 6, 2, 0x31)])
        stands('the merchant the count', [(t + 0xD, 1, merchants)])
        stands('kind 1, a town out', [(t + 0xC, 1, 1), (t + 0x10, 1, towns)])
        stands('kind 1, 0x8000', [(t + 0xC, 1, 1), (t + 0x10, 1, tc), (a + 0x76C, 4, o.u32(a + 0x76C) | 0x8000)])
        ship = [(o.ship(s) + 0x134, 2, 0x12), (o.ship(s) + 0x42, 2, c)]
        kind2 = [(t + 0xC, 1, 2), (t + 0x10, 2, s), (t + 0x12, 2, first), (t + 0x14, 2, family)]
        stands('kind 2, a ship out', kind2 + [(t + 0x10, 2, ships)])
        stands('kind 2, not a pirate', kind2 + ship + [(o.ship(s) + 0x134, 2, 0)])
        stands('kind 2, no captain', kind2 + ship + [(o.ship(s) + 0x42, 2, captains)])
        stands('kind 2, the names', kind2 + ship)
        stands('kind 2, another first name', kind2 + ship + [(t + 0x12, 2, first ^ 1)])
        stands('kind 2, another family name', kind2 + ship + [(t + 0x14, 2, family ^ 1)])
        stands('kind 3, the object 5', [(t + 0xC, 1, 3), (t + 0x10, 1, 5)])
        stands('kind 3, a missing object', [(t + 0xC, 1, 3), (t + 0x10, 1, missing)])
        stands('kind 4, a town out', [(t + 0xC, 1, 4), (t + 0x12, 1, towns)])
        stands('kind 5', [(t + 0xC, 1, 5)])
        # 0x4EAFE4 with the event 0x32 at the head.
        for k, willing in enumerate((ais[1:3], [])):
            n, h = new_event(o, 0x30000, 0x32, bC=0, bD=0xFF, bE=0x3F, bF=1, b14=0xEE)
            rt.move_event(o, n, h, now - 0x200 * (k + 1))
            saved = set_fields(o, [(o.merchant(m) + 0x37, 1, 2 if m in willing else 0) for m in ais])
            r = direct(o, 0x4EAFE4, [], ecx=to.EVENTS)
            print(f'0x4EAFE4 with {willing}: {r}, merchant {o.u8(h + 0xD)}, tick {o.u32(h):#x}')
            restore_fields(o, saved)
    return during


def expedition_branches(o):
    """The expeditions' event 0x12 (0x4E6A54, called directly): a new one
    with a convoy out of the table (nothing), and the save's expedition
    arriving, the player's (convoy and ships, as retouch_mail.py
    expedition does: the report 0x63 reaches him), without a guide ([+0x10]
    0xFFFF: its name from 0x512B20 with 0xFFFF). Saves with an expedition
    under way: 7 and the tutorials."""
    found = events_of(o, 0x12)
    if not found:
        raise RuntimeError('no expedition under way (event 0x12)')
    (i, a), ships, player = found[0], o.counts()[3], o.u32(to.PLAYER)
    convoy = o.u32(to.SHIPS + 8) + 0x3C * o.u16(a + 8)
    members, s = [], o.u16(convoy + 0xA)
    while s < ships and len(members) < 50:
        members.append(s)
        s = o.u16(o.ship(s) + 6)
    print(f'expedition {i}: convoy {o.u16(a + 8)} of {o.u8(convoy)}, guide {o.u16(convoy + 0x10)}, ships {members}')
    w8(o, convoy, player)
    for s in members:
        w8(o, o.ship(s), player)
    w8(o, o.merchant(player) + 0x3A, 0)  # he keeps the trading information (class 3: 0x63)
    w16(o, convoy + 0x10, 0xFFFF)
    w16(o, a + 0xA, 4)

    def during(o):
        _, e = new_event(o, 0x30000, 0x12, w8=o.u16(CONVOY_COUNT), wA=4)
        print(f'a convoy out: {direct(o, 0x4E6A54, [e], ecx=to.EVENTS)}')
        print(f'the expedition without a guide: {direct(o, 0x4E6A54, [a], ecx=to.EVENTS)}')
    return during


SCRATCH_LETTER = SCRATCH + 0xC0  # a letter's 0x10 bytes (0x4D6530)


def mail_branches(o):
    """Sending a letter, 0x4D6530 (called directly with the letter in
    scratch globals; the oracle lets the call run as it does the connected
    ones): of type 0x86 to the chronicle (which keeps any type: nothing),
    to a merchant past the merchants
    (nothing), to −3 (the owners of the offices of the town [+0x5]: a town
    with an office of the player, who keeps it), and to the chronicle (−2)
    with the free list's head past the entries (0x4D6B40 grows the mailbox
    and brings the head down to the count first)."""
    merchants, player = o.counts()[2], o.u32(to.PLAYER)
    t = next(o.u8(o.office(f) + 0x2C6) for f in rt.merchant_offices(o, player))
    print(f'the player {player} has an office in {t}')

    def during(o):
        at = _trampolines.get((0x4D6530, 2, False), TRAMPOLINES + 0x40 * len(_trampolines))
        to.MAIL_SITES.add(at + 4 * 2 + 5)

        def send(what, merchant, kind, town=0xFF):
            o.uc.mem_write(SCRATCH_LETTER, bytes([0, 0x80, 0xFF, 0xFF, kind, town, 0xFF, 0xFF]) + bytes(range(1, 9)))
            direct(o, 0x4D6530, [merchant & 0xFFFFFFFF, SCRATCH_LETTER], ecx=MAILBOX)
            print(f'{what}: free {o.u16(MAILBOX + 4)}, count {o.u16(MAILBOX + 6)}, chronicle {o.u16(MAILBOX + 8)}, '
                  f'the player\'s first {o.u16(o.merchant(player) + 0xA)}, the letter {o.u32(SCRATCH_LETTER):#x}')
        send('type 0x86 to the chronicle', -2, 0x86)
        send('a merchant out', merchants, 0x06)
        send('to the town', -3, 0x06, t)
        w16(o, MAILBOX + 4, 0xFFFF)
        send('the chronicle, the free head past', -2, 0x06)
    return during


def site_start_houses(o):
    """0x500F50 (where to start looking for a site; called directly on an
    office, x and y in scratch globals) with house types (0x15 on), which
    only the interface asks for (0x41B7B2): the office with the most houses
    of a type in its list [0x2D0] (two or more), n = 0 (the second) and n = 5
    (the last one), and
    a type it doesn't have (x and y as they were)."""
    towns, offices, merchants, _ = o.counts()
    best = None  # (count, office, town, houses, type)
    for f in range(offices):
        t = o.u8(o.office(f) + 0x2C6)
        if o.u16(o.office(f) + 0x2C4) >= merchants or t >= towns:
            continue
        houses = [(r, o.u8(record_at(o, t, r) + 3)) for r, _, _ in rt.records_of(o, t, o.u16(o.office(f) + 0x2D0))]
        kinds = [k for _, k in houses]
        for k in set(kinds):
            if k >= 0x15 and kinds.count(k) >= 2 and (best is None or kinds.count(k) > best[0]):
                best = (kinds.count(k), f, t, houses, k)
    if best is None:
        raise RuntimeError('no office with two houses of a type')
    _, f, t, houses, kind = best
    kinds = [k for _, k in houses]
    other = next(k for k in range(0x15, 0x29) if k not in kinds)
    print(f'office {f} in {t}: houses {houses}; type {kind:#x}, another {other:#x}')

    def during(o):
        for n, k in ((0, kind), (5, kind), (0, other)):
            w8(o, SCRATCH_X, 0x11)
            w8(o, SCRATCH_Y, 0x22)
            direct(o, 0x500F50, [SCRATCH_X, SCRATCH_Y, n, k], ecx=o.office(f))
            print(f'type {k:#x}, n {n}: ({o.u8(SCRATCH_X)}, {o.u8(SCRATCH_Y)})')
    return during


def ship_trade_branches(o):
    """Direct calls. 0x526AA0 (a captain out of a town's list) on a town
    with an empty list and with a captain who isn't in it. 0x519920 (an AI
    ship buys what to take to a town) with the ware 0 of its town at 2000 and
    thresholds below 0 (−400000..−100000), which no game has: 0x52F030 then
    asks for more than the store has, and the purchase is cut to it; the
    other wares' thresholds over their stores (0x52F030 gives 0), and the
    destination with nothing of the ware and its thresholds 0 at 500000 and
    1 at 1000000 (the margin positive)."""
    towns, _, merchants, ships = o.counts()
    captains = o.u16(to.SHIPS + 0xF2)
    empty = next(t for t in range(towns) if o.u16(o.town(t) + 0x82E) >= captains)
    listed = next(t for t in range(towns) if o.u16(o.town(t) + 0x82E) < captains)
    head = o.u16(o.town(listed) + 0x82E)
    s = next(k for k in range(ships) if o.u8(o.ship(k)) < merchants and o.u16(o.merchant(o.u8(o.ship(k))) + 8)
             and o.u16(o.ship(k) + 0x134) in (0, 1, 2, 3) and o.u8(o.ship(k) + 0x39) < towns)
    here = o.u8(o.ship(s) + 0x39)
    there = next(t for t in range(towns) if t != here)
    print(f'captains: town {empty} without, town {listed} with {head} first; ship {s} of '
          f'{o.u8(o.ship(s))} in {here}, to {there}')

    def during(o):
        print(f'0x526AA0, empty: {direct(o, 0x526AA0, [0], ecx=o.town(empty)) & 0xFF}')
        # A captain not in the list: the list walked to its end.
        chain, c = [], head
        while c < captains and len(chain) < 64:
            chain.append(c)
            c = o.u16(o.u32(to.SHIPS) + 0x10 * c)
        stranger = next(k for k in range(captains) if k not in chain)
        print(f'0x526AA0, {stranger} not in {chain[:8]}: {direct(o, 0x526AA0, [stranger], ecx=o.town(listed)) & 0xFF}')
        a, b = o.town(here), o.town(there)
        fields = [(a + 4, 4, 2000)] + [(a + 0x4F0 + 4 * k, 4, (-400000 + 100000 * k) & 0xFFFFFFFF) for k in range(4)]
        for w in range(1, 24):
            fields += [(a + 0x4F0 + 16 * w, 4, 100000000), (a + 0x4F4 + 16 * w, 4, 100000000)]
        fields += [(b + 4, 4, 0), (b + 0x670, 4, 0), (b + 0x310, 4, 0), (b + 0xC4, 4, 0), (b + 0x64, 4, 0),
                   (b + 0x4F0, 4, 500000), (b + 0x4F4, 4, 1000000)]
        set_fields(o, fields)
        direct(o, 0x519920, [there], ecx=o.ship(s))
        print(f'0x519920: the town has {o.u32(a + 4)} of ware 0 left, the ship {o.u32(o.ship(s) + 0x54)}')
    return during


def ship_ai_branches(o):
    """0x519BA0 (may the ship sail?) called directly on an AI ship that may,
    in the states 11 to 15 (12..14 can't); 0x501BE0 (a convoy recalculated)
    called directly with its guide in the state 1 and another ship with a
    [+0x138] over the convoy's [+0x16] (it takes it); and a tick of the
    world with the player's first ship in port in the state 4 (to the repair
    yard: 0x506A19 counts the player's ships, [0x6DD8A0]), recording the
    ships' update 0x506720."""
    towns, _, merchants, ships = o.counts()
    player = o.u32(to.PLAYER)
    docked = lambda k: o.u16(o.ship(k) + 0x134) in (0, 1, 2, 3, 5) and o.u8(o.ship(k) + 0x39) < towns
    able = next(k for k in range(ships) if o.u8(o.ship(k)) < merchants and o.u16(o.merchant(o.u8(o.ship(k))) + 8)
                and docked(k) and direct(o, 0x519BA0, [], ecx=o.ship(k)) & 0xFF)
    for c in range(o.u16(CONVOY_COUNT)):
        a = convoy_at(o, c)
        guide, members, s = o.u16(a + 0x10), [], o.u16(a + 0xA)
        while s < ships and len(members) < 50:
            members.append(s)
            s = o.u16(o.ship(s) + 6)
        if guide < ships and len(members) >= 2:
            break
    else:
        raise RuntimeError('no convoy of two ships with a guide')
    other = next(k for k in members if k != guide)
    mine = next(k for k in merchant_ships(o, player) if docked(k))
    print(f'ship {able} may sail; convoy {c} led by {guide}, ships {members}; the player\'s ship {mine} in '
          f'{o.u8(o.ship(mine) + 0x39)}, state {o.u16(o.ship(mine) + 0x134)}')

    def during(o):
        state = o.u16(o.ship(able) + 0x134)
        for k in range(11, 16):
            w16(o, o.ship(able) + 0x134, k)
            print(f'0x519BA0 in the state {k}: {direct(o, 0x519BA0, [], ecx=o.ship(able)) & 0xFF}')
        w16(o, o.ship(able) + 0x134, state)
        a = convoy_at(o, c)
        w16(o, o.ship(guide) + 0x134, 1)
        w16(o, o.ship(other) + 0x138, o.u16(a + 0x16) + 0x100)
        direct(o, 0x501BE0, [], ecx=a)
        print(f'convoy {c}: state {o.u16(a + 0x12)}, [+0x16] {o.u16(a + 0x16):#x}')
        w16(o, o.ship(mine) + 0x134, 4)
        count = o.u16(to.SHIPS + 0x100)
        o.advance_tick()
        print(f'the player\'s ship {mine}: state {o.u16(o.ship(mine) + 0x134)}; [0x6DD8A0] {count} → '
              f'{o.u16(to.SHIPS + 0x100)}')
    return during


def ship_delete_branches(o):
    """0x507FB0 (a ship deleted; called directly on the ships object): a
    ship out of the table, the first of the free list [0xE2] (nothing more),
    and a pirate's ship (not a merchant's: out of the pirates' lists, the
    yard lists searched from town 5)."""
    ships = o.counts()[3]
    free = o.u16(FREE_SHIP)
    if free >= ships:
        raise RuntimeError('no free ship slot')
    k, pirate_ship = next((k, s) for k, _ in pirates(o) for s in pirate_ships(o, k))
    print(f'free slot {free}; pirate {k}\'s ship {pirate_ship} (convoy {o.u16(o.ship(pirate_ship) + 8)}, state '
          f'{o.u16(o.ship(pirate_ship) + 0x134):#x})')

    def during(o):
        for s in (ships, free, pirate_ship):
            direct(o, 0x507FB0, [s], ecx=to.SHIPS)
            print(f'0x507FB0 on {s}: free {o.u16(FREE_SHIP)}, pirate {k}\'s first {o.u16(o.u32(PIRATES + 4 * k) + 0xC)}')
    return during


def pursue_branches(o):
    """The chase 0x50B030 (called directly on the ships object) of an AI
    ship at sea, its target [+0x3A] and fields set for each call: in the
    state 0x11; raiding ([+0x15C]) a ship in port, one in the state 0x13
    and one at sea far away; not raiding, a merchant's ship at sea, and in
    the state 0x16 a pirate convoy's ship that isn't in the list by x and a
    pirate far away (it keeps it); and near (20, 10 away) a raiding ship at
    sea: a cog at a tick & 7 of 1 and of 0 (the new heading, 0x5172A0), a
    snaikka at 0, and a cog in the shallows (it gives up). The calls near
    come first, so that the ship has a route to lose. Not the battle
    (under 0x190: 0x50B250, scenario chase_battle). In save 1, where no
    pirate sails, one is put in the list by x for its case."""
    towns, _, merchants, ships = o.counts()
    convoys = o.u16(CONVOY_COUNT)
    state = lambda s: o.u16(o.ship(s) + 0x134)
    at_sea = [s for s in range(ships) if state(s) == 0xF and o.u16(o.ship(s) + 8) >= convoys
              and o.u8(o.ship(s)) < merchants and not o.u8(o.ship(s) + 0x15C)]
    chaser, target = at_sea[0], at_sea[1]
    docked = next(s for s in range(ships) if state(s) in (0, 1, 2, 3, 5) and o.u8(o.ship(s) + 0x39) < towns)
    loose = next(s for k, _ in pirates(o) for s in pirate_ships(o, k)
                 if o.u16(o.ship(s) + 0xA) >= ships and o.u16(o.ship(s) + 0xC) >= ships)
    listed = next((s for k, _ in pirates(o) for s in pirate_ships(o, k)
                   if o.u16(o.ship(s) + 0xA) < ships or o.u16(o.ship(s) + 0xC) < ships), None)
    reach = o.u32(to.SHIPS + 0xFC) & 0xFFFF
    if reach < 500:
        raise RuntimeError(f'the chase reaches {reach}')
    print(f'chaser {chaser}, target at sea {target}, in port {docked}, a pirate out of the list {loose} (state '
          f'{state(loose):#x}, at {o.u16(o.ship(loose) + 0x1E)}, {o.u16(o.ship(loose) + 0x22)}), in it '
          f'{listed}; reach {reach}')

    def during(o):
        c, t = o.ship(chaser), o.ship(target)
        now = o.u32(WORLD + 0x14)

        def chase(what, aim, fields=(), tick=None):
            saved = set_fields(o, [(c + 0x3A, 2, aim)] + list(fields) +
                               ([(WORLD + 0x14, 4, (now & ~7) | tick)] if tick is not None else []))
            r = direct(o, 0x50B030, [chaser], ecx=to.SHIPS) & 0xFF
            print(f'{what}: {r}, target {o.u16(c + 0x3A):#x}, route {o.u32(c + 0x24) != 0}')
            restore_fields(o, saved)
        near = [(c + 0x1C, 4, o.u32(t + 0x1C) + (20 << 16)), (c + 0x20, 4, o.u32(t + 0x20) + (10 << 16)),
                (t + 0x15C, 1, 1)]
        shallows = [(c + 0x1C, 4, 500 << 16), (c + 0x20, 4, 1600 << 16), (t + 0x1C, 4, 520 << 16),
                    (t + 0x20, 4, 1610 << 16), (t + 0x15C, 1, 1), (c + 0xE, 1, 2)]
        chase('in the state 0x11', target, [(c + 0x134, 2, 0x11)])
        chase('near, a cog, tick 1', target, near + [(c + 0xE, 1, 2)], tick=1)
        chase('near, a cog, tick 0', target, near + [(c + 0xE, 1, 2)], tick=0)
        chase('raiding a ship in port', docked, [(c + 0x15C, 1, 1)])
        chase('raiding a ship in the state 0x13', target, [(c + 0x15C, 1, 1), (t + 0x134, 2, 0x13),
                                                          (t + 0x39, 1, 0xFF)])
        chase('raiding a ship far away', target, [(c + 0x15C, 1, 1), (t + 0x39, 1, 0xFF),
                                                  (t + 0x1C, 4, o.u32(c + 0x1C) + (1000 << 16))])
        chase('a merchant\'s ship', target)
        chase('in the state 0x16, a pirate out of the list by x, far away', loose, [
            (c + 0x134, 2, 0x16), (c + 0x1C, 4, o.u32(o.ship(loose) + 0x1C) + (1000 << 16)),
            (c + 0x20, 4, o.u32(o.ship(loose) + 0x20))])
        aim = listed if listed is not None else pirate_into_x_list(o, avoid=(loose,))  # save 1: no pirate sails
        chase('in the state 0x16, a pirate far away', aim, [(c + 0x134, 2, 0x16)])
        chase('near, a snaikka, tick 0', target, near + [(c + 0xE, 1, 0)], tick=0)
        chase('a cog in the shallows', target, shallows)
    return during


def chase_battle(o):
    """The chase 0x50B030 (called directly) that catches its target, under 0x190 away: the sea battle 0x50B250 (the
    battles' piece 4), each case on ships of its own (a battle changes the lists): an AI ship raiding ([+0x15C])
    catches an AI merchant's ship, both alone, with the player's first ship targeting the chaser (the operation 0x9F
    with 1); two AI convoys' guides, the chaser raiding, the player's ship targeting it too by [+0x52]; the player's
    ship raiding (his own: the count of his ships' targets near the chaser, one near and one far); an AI ship in the
    state 0x16 (the Hanse's hunt) catching a pirate of the list by x (in save 1, where no pirate sails, one put in it:
    a pirate alone; in the others, one in its convoy)."""
    towns, _, merchants, ships = o.counts()
    convoys = o.u16(CONVOY_COUNT)
    player = o.u32(to.PLAYER)
    state = lambda s: o.u16(o.ship(s) + 0x134)
    at_sea = [s for s in range(ships) if state(s) == 0xF and o.u16(o.ship(s) + 8) >= convoys
              and o.u8(o.ship(s)) < merchants and o.u8(o.ship(s)) != player and not o.u8(o.ship(s) + 0x15C)]
    guides = [c for c in convoy_list(o, CONVOY_HEAD)
              if o.u8(o.u32(CONVOYS) + 0x3C * c) < merchants and o.u8(o.u32(CONVOYS) + 0x3C * c) != player
              and o.u16(o.u32(CONVOYS) + 0x3C * c + 0x10) < ships]
    mine = []
    s = o.u16(o.merchant(player) + 0xE)
    while s < ships:
        mine.append(s)
        s = o.u16(o.ship(s) + 4)
    listed = next((s for k, _ in pirates(o) for s in pirate_ships(o, k)
                   if o.u16(o.ship(s) + 0xA) < ships or o.u16(o.ship(s) + 0xC) < ships), None)
    if len(at_sea) < 6 or len(guides) < 2 or not mine:
        raise RuntimeError(f'at sea {len(at_sea)}, convoys {len(guides)}, the player\'s {mine}')
    print(f'at sea {at_sea[:6]}, convoys {guides[:2]}, the player\'s ships {mine}, a listed pirate {listed}')

    def during(o):
        def caught(what, chaser, target, fields=()):
            c, t = o.ship(chaser), o.ship(target)
            set_fields(o, [(c + 0x3A, 2, target), (c + 0x1C, 4, o.u32(t + 0x1C) + (12 << 16)),
                           (c + 0x20, 4, o.u32(t + 0x20) + (8 << 16))] + list(fields))
            r = direct(o, 0x50B030, [chaser], ecx=to.SHIPS) & 0xFF
            print(f'{what}: {r}, the chaser\'s state {o.u16(c + 0x134):#x}, convoy {o.u16(c + 8)}, the target\'s '
                  f'state {o.u16(t + 0x134):#x}, convoy {o.u16(t + 8)}, queued {[op.hex() for op in o.queued[-1:]]}')
        p = o.ship(mine[0])
        set_fields(o, [(p + 0x50, 2, at_sea[0])])
        caught('a raider catches a merchant, the player\'s ship targeting it', at_sea[0], at_sea[1],
               [(o.ship(at_sea[0]) + 0x15C, 1, 1)])
        g0 = o.u16(o.u32(CONVOYS) + 0x3C * guides[0] + 0x10)
        g1 = o.u16(o.u32(CONVOYS) + 0x3C * guides[1] + 0x10)
        set_fields(o, [(p + 0x52, 2, g0)])
        caught('two convoys, the chaser raiding', g0, g1, [(o.ship(g0) + 0x15C, 1, 1), (o.ship(g0) + 0x134, 2, 0xF),
                                                          (o.ship(g1) + 0x134, 2, 0xF), (o.ship(g1) + 0x39, 1, 0xFF)])
        raider = mine[-1]
        r = o.ship(raider)
        caught('the player\'s ship raiding', raider, at_sea[2], [
            (r + 0x15C, 1, 1), (r + 0x134, 2, 0xF), (r + 0x39, 1, 0xFF), (r + 0x50, 2, at_sea[2]),
            (r + 0x52, 2, at_sea[3]), (o.ship(at_sea[3]) + 0x1C, 4, o.u32(o.ship(at_sea[2]) + 0x1C) + (3000 << 16))])
        pirate = listed if listed is not None else pirate_into_x_list(o)  # save 1: no pirate sails
        caught('the Hanse\'s hunt catches a pirate', at_sea[4], pirate, [(o.ship(at_sea[4]) + 0x134, 2, 0x16)])
    return during


def hanse_fleet(o):
    """0x50BA80 (called directly on a ship of a town): no event 0x24 for its town (false); an event 0x24 of another
    town and then one of its own with no convoy in the state 9 there (false: only the first of its town is looked
    at); with an AI convoy of the list in the state 9 at the town (the Hanse fleet), a ship without a convoy (it gets
    one: a sea battle, true); and a convoy's guide against another fleet (true)."""
    towns, _, merchants, ships = o.counts()
    convoys = o.u16(CONVOY_COUNT)
    player = o.u32(to.PLAYER)
    at_sea = [s for s in range(ships) if o.u16(o.ship(s) + 0x134) == 0xF and o.u16(o.ship(s) + 8) >= convoys
              and o.u8(o.ship(s)) < merchants and o.u8(o.ship(s)) != player]
    guides = [c for c in convoy_list(o, CONVOY_HEAD)
              if o.u8(o.u32(CONVOYS) + 0x3C * c) < merchants and o.u16(o.u32(CONVOYS) + 0x3C * c + 0x10) < ships]
    if len(at_sea) < 2 or len(guides) < 3:
        raise RuntimeError(f'at sea {len(at_sea)}, convoys {len(guides)}')
    town, other = 2, 4
    print(f'ships {at_sea[:2]}, convoys {guides[:3]}, town {town}')

    def during(o):
        def fleet_call(what, ship):
            w8(o, o.ship(ship) + 0x39, town)
            r = direct(o, 0x50BA80, [ship], ecx=to.SHIPS) & 0xFF
            print(f'{what}: {r}, the ship\'s convoy {o.u16(o.ship(ship) + 8)}, state {o.u16(o.ship(ship) + 0x134):#x}')
        fleet_call('no event 0x24', at_sea[0])
        new_event(o, 0x100000, 0x24, b14=other)
        new_event(o, 0x100001, 0x24, b14=town)
        fleet_call('no fleet there', at_sea[0])
        c = o.u32(CONVOYS) + 0x3C * guides[0]
        w16(o, c + 0x12, 9)
        w16(o, c + 0x1C, town)
        fleet_call('a ship alone against the fleet', at_sea[0])
        c = o.u32(CONVOYS) + 0x3C * guides[1]
        w16(o, c + 0x12, 9)
        w16(o, c + 0x1C, town)
        fleet_call('a convoy\'s guide against the fleet', o.u16(o.u32(CONVOYS) + 0x3C * guides[2] + 0x10))
    return during


def raid_fleet(o):
    """raid_arrival with the Hanse fleet guarding the closed port: an event 0x24 for the town and an AI convoy of the
    list in the state 9 there, set just before the raider arrives (0x506E46 → 0x50BA80: a sea battle against the
    fleet instead of going back to the sea's list). As raid_arrival, the port closed and the ship a raider only for
    its arrival's tick when another ship arrived there before it (save 1)."""
    ticks, s, t, late = arrival_of(o)
    merchants, ships = o.counts()[2], o.counts()[3]
    print(f'ship {s} of {o.u8(o.ship(s))} arrives at {t} in {ticks} ticks{" (others there before)" if late else ""}')

    def during(o):
        def close():
            a = o.town(t)
            w32(o, a + 0x2C8, (o.u32(a + 0x2C8) | 0x200) & ~0x4000000)
            w8(o, o.ship(s) + 0x15C, 1)
        if not late:
            close()
        for k in range(ticks - 1):
            o.advance_tick()
        if late:
            close()
        new_event(o, 0x100000, 0x24, b14=t)
        fleet = next(c for c in convoy_list(o, CONVOY_HEAD)
                     if o.u8(o.u32(CONVOYS) + 0x3C * c) < merchants and o.u16(o.u32(CONVOYS) + 0x3C * c + 0x10) < ships)
        c = o.u32(CONVOYS) + 0x3C * fleet
        w16(o, c + 0x12, 9)
        w16(o, c + 0x1C, t)
        print(f'the fleet: convoy {fleet}')
        o.advance_tick()
        print(f'after {ticks} ticks: ship {s} state {o.u16(o.ship(s) + 0x134):#x}, town {o.u8(o.ship(s) + 0x39):#x}, '
              f'convoy {o.u16(o.ship(s) + 8)}, the fleet in the list: {fleet in convoy_list(o, CONVOY_HEAD)}')
    return during


def convoy_states(o):
    """The merchants' convoys in the ships' update (0x506720, a tick of the
    world recorded), each AI convoy put in a state (those in port, waiting in
    the state 1, put in the 0): in port (0) with [+0x14]
    & 0x10 (disbanded, 0x50A580); in port led by a ship given to the player
    with its trade route's bit ([+0x136] & 1, no routes: it is interrupted);
    in port with its guide's destination another town (it sails: 0x519B50
    for each AI ship); 4 with its town [+0x1C] (0) and without one; 9 (0x503930);
    0xA with [+0x16] 5; and 5, a state the switch doesn't take (every ship
    leaves it, 0x509F80, and it is deleted; with any other such state
    0x509F80 takes none out and the game loops forever)."""
    towns, _, merchants, ships = o.counts()
    player = o.u32(to.PLAYER)
    ai = lambda m: m < merchants and o.u16(o.merchant(m) + 8)
    found = []
    for c in range(o.u16(CONVOY_COUNT)):
        a = convoy_at(o, c)
        lead = o.u16(a + 0x10)
        if lead < ships and ai(o.u8(a)) and o.u16(a + 0xA) < ships:
            found.append((o.u16(a + 0x12), c))
    in_port = [c for st, c in found if st in (0, 1)]  # 1: waiting in port; put in the state 0
    others = [c for st, c in found if st in (2, 3, 0xF)] + in_port[3:]  # in the main list (not a yard's)
    if len(in_port) < 3 or len(others) < 5:
        raise RuntimeError(f'AI convoys: {found}')
    print(f'convoys in port {in_port[:3]}, others {others[:5]}')
    c1, c2, c3 = in_port[:3]
    c4, c5, c6, c7 = others[:4]
    c8 = in_port[3] if len(in_port) > 3 else others[4]

    def during(o):
        a = {c: convoy_at(o, c) for c in (c1, c2, c3, c4, c5, c6, c7, c8)}
        for c in (c1, c2, c3):
            w16(o, a[c] + 0x12, 0)
        w8(o, a[c1] + 0x14, o.u8(a[c1] + 0x14) | 0x10)
        lead2 = o.ship(o.u16(a[c2] + 0x10))
        w8(o, lead2, player)
        w8(o, lead2 + 0x136, o.u8(lead2 + 0x136) | 1)
        lead3 = o.ship(o.u16(a[c3] + 0x10))
        members, s = set(), o.u16(a[c3] + 0xA)
        while s < ships:
            members.add(o.u8(o.ship(s) + 0x13B))
            s = o.u16(o.ship(s) + 6)
        there = next(t for t in range(towns) if t != o.u8(lead3 + 0x39) and t not in members)
        w8(o, a[c3] + 0x1F, there)
        w8(o, lead3 + 0x38, there)
        for c, st in ((c4, 4), (c5, 4), (c6, 9), (c7, 0xA), (c8, 5)):
            w16(o, a[c] + 0x12, st)
        w16(o, a[c4] + 0x1C, 0)
        w16(o, a[c5] + 0x1C, 0xFF)
        w16(o, a[c7] + 0x16, 5)
        o.advance_tick()
        print('after: ' + ', '.join(f'{c}: state {o.u16(convoy_at(o, c) + 0x12):#x}, first {o.u16(convoy_at(o, c) + 0xA)}'
                                    for c in (c1, c2, c3, c4, c5, c6, c7, c8)))
    return during


def convoy_ship_branches(o):
    """Direct calls: 0x50A580 (a convoy disbanded) with a convoy out of the
    table; 0x502110 (a convoy's tick at sea) with its guide without hull (a
    new guide, 0x5017F0); and 0x501EB0 (a convoy starts leaving port) with
    one of its ships without sailors (it can't sail: it leaves the convoy,
    0x509F80)."""
    towns, _, merchants, ships = o.counts()
    count = o.u16(CONVOY_COUNT)
    found = {}
    for c in range(count):
        a = convoy_at(o, c)
        lead, s, ms = o.u16(a + 0x10), o.u16(a + 0xA), []
        while s < ships and len(ms) < 50:
            ms.append(s)
            s = o.u16(o.ship(s) + 6)
        if lead < ships and o.u8(a) < merchants and len(ms) >= 2:
            found.setdefault(o.u16(a + 0x12), []).append((c, lead, [m for m in ms if m != lead]))
    at_sea = found.get(0xF, [None])[0]
    able = next((x for x in found.get(1, []) if direct(o, 0x519BA0, [], ecx=o.ship(x[1])) & 0xFF
                 and direct(o, 0x519C40, [], ecx=o.ship(x[1])) & 0xFF), None)
    if at_sea is None or able is None:
        raise RuntimeError(f'convoys by state: {sorted((k, len(v)) for k, v in found.items())}')
    print(f'at sea: convoy {at_sea[0]} led by {at_sea[1]}; in port: convoy {able[0]} led by {able[1]}, '
          f'ship {able[2][0]} without sailors')

    def during(o):
        direct(o, 0x50A580, [count], ecx=to.SHIPS)
        c, lead, _ = at_sea
        w32(o, o.ship(lead) + 0x18, 0)
        print(f'0x502110: {direct(o, 0x502110, [0], ecx=convoy_at(o, c)) & 0xFF}, '
              f'guide {o.u16(convoy_at(o, c) + 0x10)}')
        c, lead, rest = able
        w16(o, o.ship(rest[0]) + 0x40, 0)
        print(f'0x501EB0: {direct(o, 0x501EB0, [], ecx=convoy_at(o, c)) & 0xFF}, '
              f'ship {rest[0]} in convoy {o.u16(o.ship(rest[0]) + 8)}')
    return during


def battle_branches(o):
    """Pieces of the battles' ends, run directly: the operation 0x91 (a ship
    takes another, 0x540520) of a pirate's ship whose convoy's guide is a
    privateer of the player ([+0x15D]) taking an AI merchant's ship at sea,
    out of the sea's list first as in a battle (the player gets its worth and
    the letter 0x30); 0x50BFD0 (a convoy after its
    battle) on a pirate convoy one of whose ships, with a captain, sank,
    beaten by the player (the convoy and its guide out of the lists first, as
    at a battle's start): the ship is deleted (0x507FB0, which frees its
    captain) before its captain is looked at, so the letters 0x54 and 0x2A
    are never sent (a game bug); and 0x509250 (a ship sold) to no merchant (0xFF: it turns
    pirate) with a town out (nothing more)."""
    towns, _, merchants, ships = o.counts()
    player = o.u32(to.PLAYER)
    captains = o.u16(to.SHIPS + 0xF2)
    pirate_convoys = []
    for c in range(o.u16(CONVOY_COUNT)):
        a = convoy_at(o, c)
        guide, s, ms = o.u16(a + 0x10), o.u16(a + 0xA), []
        while s < ships and len(ms) < 50:
            ms.append(s)
            s = o.u16(o.ship(s) + 6)
        if o.u8(a) >= merchants and guide < ships and len(ms) >= 2:
            pirate_convoys.append((c, guide, ms))
    if len(pirate_convoys) < 2:
        raise RuntimeError(f'pirate convoys of two ships: {pirate_convoys}')
    (c1, g1, ms1), (c2, g2, ms2) = pirate_convoys[:2]
    taker = next(s for s in ms1 if s != g1)
    target = next(s for s in range(ships) if o.u8(o.ship(s)) < merchants and o.u16(o.merchant(o.u8(o.ship(s))) + 8)
                  and o.u16(o.ship(s) + 8) >= o.u16(CONVOY_COUNT) and o.u16(o.ship(s) + 0x134) == 0xF)
    sunk = next(s for s in ms2 if s != g2)
    sold = next(s for s in merchant_ships(o, player))
    print(f'pirate convoy {c1} (guide {g1}): {taker} takes {target}; pirate convoy {c2}: {sunk} sinks; the player\'s '
          f'{sold} turns pirate')

    def during(o):
        if o.u16(o.ship(g1) + 0x42) >= captains:
            w16(o, o.ship(g1) + 0x42, free_captain(o))
        w8(o, o.ship(g1) + 0x15D, player)
        # In a battle both ships are out of their lists (0x50BC40): the target out of the sea's.
        link = to.SHIPS + 0xEA
        while o.u16(link) != target:
            link = o.ship(o.u16(link)) + 6
        w16(o, link, o.u16(o.ship(target) + 6))
        w16(o, o.ship(target) + 6, 0xFFFF)
        operation(o, 0x91, taker, target)
        print(f'the player\'s money {o.u32(o.merchant(player))}, ship {target}\'s owner {o.u8(o.ship(target)):#x}')
        if o.u16(o.ship(sunk) + 0x42) >= captains:
            w16(o, o.ship(sunk) + 0x42, free_captain(o))
        w32(o, o.ship(sunk) + 0x18, 0)
        # As at a battle's start (0x50BC40): the convoy out of the list of those at sea ([0xEC], next
        # [+0x8]) and its guide out of the list by x (0x50D040), where 0x50BFD0 puts them back.
        link, steps = to.SHIPS + 0xEC, 0
        while o.u16(link) != c2 and o.u16(link) < o.u16(CONVOY_COUNT) and steps < 200:
            link, steps = convoy_at(o, o.u16(link)) + 8, steps + 1
        if o.u16(link) == c2:
            w16(o, link, o.u16(convoy_at(o, c2) + 8))
            w16(o, convoy_at(o, c2) + 8, 0xFFFF)
        direct(o, 0x50D040, [g2], ecx=to.SHIPS)
        direct(o, 0x50BFD0, [c2, 0, player], ecx=to.SHIPS)
        print(f'convoy {c2}: state {o.u16(convoy_at(o, c2) + 0x12):#x}; ship {sunk} state {o.u16(o.ship(sunk) + 0x134):#x}')
        direct(o, 0x509250, [sold, 0xFF, 0, 0, towns], ecx=to.SHIPS)
        print(f'the player\'s ship {sold}: owner {o.u8(o.ship(sold)):#x}')
    return during


def battle_rocks(o):
    """A sea battle's ship on a rock (kind 2) and on an islet (kind 3): 0x608850
    called directly on the battle the save starts with (saves 6 and 7
    automatic, tut4 with a human side: the shifts by 12 and 6, the turns by 2),
    the obstacle's diamond (0x60B357) put round the ship first: its centre
    inside (the side left as the flags' byte), the diamond's nearest corner 10
    away to the right, left, below and above (the hull crosses a side: the
    side's direction), and far away (no touch). The ship's heading before the
    centre-inside calls makes each turn: away 0x10 one way and the other, 0
    and 0x80 (none); its speed [+0x1C] 0x4000 before each (the rock's damage
    16, 0x61F96F). The saves' battles have no rock nor islet (their saves
    keep none): one of each is made first with the game's 0x619450 (kind,
    number 0, a box far away), the shapes' records 0 sized 4 × 4. The rocks
    got a size only with the town view (2026-11-07), and the saves never put
    a ship on one."""
    slots = [k for k in range(0xFF) if o.u32(to.BATTLES + 4 * k)]
    if not slots:
        raise RuntimeError('no sea battle in this save')
    b = o.u32(to.BATTLES + 4 * slots[0])

    def scene(off):  # the scene's arrays move when it grows (0x61A1D0)
        return o.u32(b + off)

    def entry(item):
        return o.u32(o.u32(b + 4) + 4 * o.u16(scene(0x1C) + 2 * item))

    used = o.u16(b + 0x54)
    ship = next(i for i in range(used) if o.u8(scene(0x18) + i) == 0 and not o.u8(entry(i) + 0x12A) & 0x2)
    rock = direct(o, 0x619450, [2, 0, 30000, 30000, 30100, 30100], ecx=b) & 0xFFFF
    islet = direct(o, 0x619450, [3, 0, 30000, 30000, 30100, 30100], ecx=b) & 0xFFFF
    e = entry(ship)
    print(f'battle at {b:#x} ([+0xAC8] {o.u8(b + 0xAC8)}): ship item {ship} (entry {e:#x}), rock item {rock}, '
          f'islet item {islet}')

    def put(item, x1, y1, x2, y2, size=4):
        kind = o.u8(scene(0x18) + item)
        shape = (0x876 if kind == 2 else 0x676) + 8 * o.u16(scene(0x1C) + 2 * item)
        w16(o, b + shape, size)
        w16(o, b + shape + 2, size)
        w16(o, scene(0x28) + 4 * item, x1)
        w16(o, scene(0x28) + 4 * item + 2, x2)
        w16(o, scene(0x38) + 4 * item, y1)
        w16(o, scene(0x38) + 4 * item + 2, y2)

    def meet(item, heading=None):
        if heading is not None:
            w8(o, e + 0x11, heading)
        w32(o, e + 0x1C, 0x4000)
        direct(o, 0x608850, [ship, item], ecx=b)
        print(f'  heading {o.u8(e + 0x11):#x}, speed {o.u32(e + 0x1C):#x}, x {o.u32(e + 0x8):#x}, '
              f'y {o.u32(e + 0xC):#x}, flags {o.u8(e + 0x12A):#x}')

    def during(o):
        for item in (rock, islet):
            sx, sy = struct.unpack('<hh', bytes(o.uc.mem_read(e + 0xA, 2)) + bytes(o.uc.mem_read(e + 0xE, 2)))
            flags = o.u8(e + 0x12A)
            print(f'item {item} (kind {o.u8(scene(0x18) + item)}): the ship at {sx}, {sy}')
            put(item, sx - 100, sy - 100, sx + 100, sy + 100)
            for away in (0x10, -0x10, 0, 0x80):
                meet(item, (flags + 0x80 + away) & 0xFF if away != 0x80 else flags)
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                sx, sy = struct.unpack('<hh', bytes(o.uc.mem_read(e + 0xA, 2)) + bytes(o.uc.mem_read(e + 0xE, 2)))
                cx, cy = sx + dx * 110, sy + dy * 110  # the nearest corner 10 away
                put(item, cx - 100, cy - 100, cx + 100, cy + 100)
                meet(item)
            put(item, sx + 1000, sy + 1000, sx + 1200, sy + 1200)
            meet(item)
    return during


def town_battle_setup(o):
    """The battles' piece 6: town battles set up directly (0x611D80, then 0x5FBFE0 on the new one) as a raider's
    arrival does (0x506F33): towns 0, 4, 9, 13, 17 and 21 attacked by a pirate convoy's guide (its convoy on side A)
    or by an AI merchant's ship alone at sea; towns 2 and 6 with a guard ship put in [+0x994] (an AI merchant's ship
    in port there: 0x50CC90, side D its new convoy); town 11 by the player's ship (a human side: with no battle
    window, the busy path, operation 0x96); and the nest of the first pirate (town 0xFE − k) with its ships' convoy
    as [+0x10] (the defender) and its pool's entry 1 made a tower (kind 0x30, damage 50: 0x515D30). Not
    reachable: the formation's placing again (0x6003B7: side A stands seaward, out of the town's grid, which the
    map file's numbers move the other way; side D's check reads side A's ship i, 0x6005D2, so on land its loop
    would never end)."""
    towns, _, merchants, ships = o.counts()
    player = o.u32(to.PLAYER)
    convoys = o.u16(CONVOY_COUNT)
    pirate_guides = []
    for c in range(convoys):
        a = convoy_at(o, c)
        guide = o.u16(a + 0x10)
        if o.u8(a) >= merchants and guide < ships and o.u16(a + 0xA) < ships:
            pirate_guides.append(guide)
    alone = [s for s in range(ships) if o.u8(o.ship(s)) < merchants and o.u8(o.ship(s)) != player
             and o.u16(o.ship(s) + 0x134) == 0xF and o.u16(o.ship(s) + 8) >= convoys]
    mine = merchant_ships(o, player)
    if len(alone) < 6 or not mine:
        raise RuntimeError(f'ships alone {alone[:5]}, the player\'s {mine}')
    if not pirate_guides:  # save 1: no pirate convoy at sea
        pirate_guides = alone[4:6]
    plan = [(0, pirate_guides[0]), (4, alone[0]), (9, pirate_guides[-1]), (13, alone[1]), (17, alone[2]),
            (21, pirate_guides[0])]

    def in_port(t):
        return [s for s in range(ships) if o.u8(o.ship(s)) < merchants and o.u8(o.ship(s)) != player
                and o.u16(o.ship(s) + 0x134) == 0 and o.u8(o.ship(s) + 0x39) == t and o.u16(o.ship(s) + 8) >= convoys]
    guards = []
    for t in range(towns):
        if t in (2, 6) or (len(guards) < 2 and t > 6):
            cand = in_port(t)
            if cand and len(guards) < 2:
                guards.append((t, cand[0]))
    k = next(i for i in range(5) if o.u32(PIRATES + 4 * i))
    pirate = o.u32(PIRATES + 4 * k)
    print(f'plan {plan}; guards {guards}; the player\'s ship {mine[0]}; pirate {k}')

    def attack(town, ship):
        slot = direct(o, 0x611D80, [], ecx=to.BATTLES) & 0xFF
        b = o.u32(to.BATTLES + 4 * slot)
        direct(o, 0x5FBFE0, [ship, town], ecx=b)
        print(f'town {town:#x} by ship {ship} (owner {o.u8(o.ship(ship)):#x}): slot {slot}, ships '
              f'{o.u16(b + 0x658)} + {o.u16(b + 0x65A)}, towers {[o.u8(b + 0x1E99 + i) for i in range(4)]}, '
              f'over {o.u8(b + 0x5C)}, automatic {o.u8(b + 0x5D)} {o.u8(b + 0x5E)}, map ends '
              f'{struct.unpack("<ii", bytes(o.uc.mem_read(b + 0x1ED8, 8)))}')
        return b

    def during(o):
        for town, ship in plan:
            attack(town, ship)
        for town, guard in guards:
            w16(o, o.town(town) + 0x994, guard)
            attack(town, alone[3 + len(guards)])
        attack(11, mine[0])
        # The nest: a defender and a tower.
        head = o.u16(pirate + 0xC)
        if head < ships and o.u16(o.ship(head) + 8) < convoys:
            w16(o, pirate + 0x10, o.u16(o.ship(head) + 8))
        pool = o.u32(pirate + 4)
        if o.u16(pirate + 8) > 1:
            w8(o, pool + 8 + 3, 0x30)
            w8(o, pool + 8 + 6, 50)
        attack(0xFE - k, alone[1])
    return during


def town_raids(o):
    """The battles' piece 6, part 3: town battles that start as in the game. The first 16 ships sailing alone
    turn raiders (the operation 0x0D with 1, the viewer's bytes, as pirate_sailing) and the world runs: each one
    that arrives at its town attacks it (0x506F33) and the battle takes its steps (the operation 0x94 → 0x5FD510,
    the ships' 0x621770, the towers' 0x5FE080...) to its end (0x5FE7E0, 0x602990: part 4). Before, the towns
    where they go get a guard ship (a merchant's ship in port there, alone, or else in port anywhere, the AI's
    first, then the player's, in [+0x994], as town_battle_setup), so that side D has ships; the armed ships are
    taken first, and the first eight unarmed get the guns of the save's first armed ship.
    (town_battle_setup's battles can't run to their end: its attackers stay in the sea's lists and some attack
    two towns.)"""
    towns, _, merchants, ships = o.counts()
    player = o.u32(to.PLAYER)
    convoys = o.u16(CONVOY_COUNT)
    alone = [s for s in range(ships) if o.u16(o.ship(s) + 0x134) == 0xF and o.u16(o.ship(s) + 8) >= convoys]
    # The armed ones first (they fire at the towers and the guard ships), then the others.
    free = ([s for s in alone if o.u32(o.ship(s) + 0x120)] + [s for s in alone if not o.u32(o.ship(s) + 0x120)])[:16]
    if not free:
        raise RuntimeError('no ship sailing out of a convoy')
    # The first eight unarmed ones get the guns of the first armed ship of the save ([+0x120] and the slots
    # [+0x13C..]), so that they fire.
    model = next((s for s in range(ships) if o.u32(o.ship(s) + 0x120)), None)
    armed = [s for s in free if not o.u32(o.ship(s) + 0x120)][:8] if model is not None else []
    print(f'arms: ' + ' '.join(f'{s}:{o.u32(o.ship(s) + 0x120):#x}' for s in free) +
          f'; the guns of ship {model} to {armed}; destinations {[o.u8(o.ship(s) + 0x38) for s in free]}')
    guards = []
    # The AI's ships in port alone, then the player's (a human defender: the operation 0x96 hands it to the
    # computer, as when the player doesn't direct the battle).
    docked_all = sorted((x for x in range(ships) if o.u8(o.ship(x)) < merchants and o.u16(o.ship(x) + 0x134) == 0
                         and o.u16(o.ship(x) + 8) >= convoys), key=lambda x: o.u8(o.ship(x)) == player)
    for s in free:
        t = o.u8(o.ship(s) + 0x38)
        docked = [x for x in docked_all if x not in [g for _, g in guards]]
        # One in port there, or else anywhere (0x50CC90 takes it out of the list in port to the battle).
        cand = [x for x in docked if o.u8(o.ship(x) + 0x39) == t] or docked
        if t < towns and cand and o.u16(o.town(t) + 0x994) >= ships and t not in [u for u, _ in guards]:
            guards.append((t, cand[0]))
    print(f'raiders {free}; {len(docked_all)} AI ships in port alone; guard ships {guards}; towns\' guards before '
          f'{[(t, o.u16(o.town(t) + 0x994)) for t in range(towns) if o.u16(o.town(t) + 0x994) < ships]}')

    def during(o):
        for t, g in guards:
            w16(o, o.town(t) + 0x994, g)
        for s in armed:
            w32(o, o.ship(s) + 0x120, o.u32(o.ship(model) + 0x120))
            o.uc.mem_write(o.ship(s) + 0x13C, bytes(o.uc.mem_read(o.ship(model) + 0x13C, 0x18)))
        for s in free:
            run_bytes(o, player_op('pirate', s, 1))
    return during


def pirate_town_attack(o):
    """The battles' piece 6: a pirate convoy at the end of its route at a town attacks it (0x507C0B → 0x507C4C:
    0x611D80, 0x5FBFE0 against the convoy's [+0x1C]). Pirates' routes seldom end at a town (none in the 12 saves
    within 7 days): the first two pirate convoys at sea (state 0x12) whose guide has a route of its own ([+0x36])
    get the last point of that route moved to the approach point of the town nearest the guide, and the guide
    and its convoy's ships put on that point, going to it ([+0x34] the last, still: [+0x28], [+0x2C] 0); the next
    tick's 0x502110 ends the route there and the world runs."""
    towns, _, merchants, ships = o.counts()
    convoys = o.u16(CONVOY_COUNT)
    plan = []
    for c in range(convoys):
        a = convoy_at(o, c)
        guide = o.u16(a + 0x10)
        if (o.u8(a) < merchants or o.u16(a + 0x12) != 0x12 or guide >= ships or len(plan) >= 2
                or not o.u32(o.ship(guide) + 0x24) or not o.u8(o.ship(guide) + 0x36)):
            continue
        gx, gy = o.u16(o.ship(guide) + 0x1E), o.u16(o.ship(guide) + 0x22)

        def d2(t):
            n = o.u8(0x6DE4B8 + t)
            return (o.u32(0x6DDB90 + 0x34 * n + 0x20) - gx) ** 2 + (o.u32(0x6DDB90 + 0x34 * n + 0x24) - gy) ** 2
        town = min((t for t in range(towns) if t != o.u8(a + 0x1E)), key=d2)
        plan.append((c, guide, town))
    if not plan:
        raise RuntimeError('no pirate convoy at sea with a guide on a route of its own')
    print(f'pirate convoys, guides and towns: {plan}')

    def during(o):
        for c, guide, town in plan:
            n = o.u8(0x6DE4B8 + town)
            ax, ay = o.u32(0x6DDB90 + 0x34 * n + 0x20) & 0xFFFF, o.u32(0x6DDB90 + 0x34 * n + 0x24) & 0xFFFF
            g = o.ship(guide)
            route = o.u32(g + 0x24)
            points, count = o.u32(route), o.u32(route + 4)
            w16(o, points + 4 * (count - 1), ax)
            w16(o, points + 4 * (count - 1) + 2, ay)
            w16(o, g + 0x34, count - 1)
            s = o.u16(convoy_at(o, c) + 0xA)
            while s < ships:
                w32(o, o.ship(s) + 0x1C, ax << 16 | 0x7FFF)
                w32(o, o.ship(s) + 0x20, ay << 16 | 0x7FFF)
                s = o.u16(o.ship(s) + 6)
            w32(o, g + 0x28, 0)
            w32(o, g + 0x2C, 0)
            print(f'convoy {c}: guide {guide} at the end of its route ({count} points) on town {town} ({ax}, {ay})')
    return during


def convoy_raid(o):
    """The battles' piece 6: a raiding convoy at its town attacks it (0x502110 puts it in the state 0x14 on
    arriving, then 0x507975: without the Hanse fleet, 0x50797E: 0x535400, the battle of the guide's town and owner,
    0x6130B0 and 0x60234C, or a new one, 0x611D80 and 0x5FBFE0 against the convoy's [+0x1E]). The guides of the
    first four AI convoys at sea (state 0xF) whose ships are all its owner's raid ([+0x15C] 1) and the world runs
    until they arrive. (With another owner's ship in the convoy the game's setup hangs: 0x5FCA7A walks past the
    convoy's list for the attacker's ships.)"""
    _, _, merchants, ships = o.counts()
    convoys = o.u16(CONVOY_COUNT)
    guides = []
    for c in range(convoys):
        a = convoy_at(o, c)
        guide = o.u16(a + 0x10)
        owners, s = set(), o.u16(a + 0xA)
        while s < ships and len(owners) < 3:
            owners.add(o.u8(o.ship(s)))
            s = o.u16(o.ship(s) + 6)
        if (o.u8(a) < merchants and o.u16(a + 0x12) == 0xF and guide < ships and owners == {o.u8(a)}
                and len(guides) < 4):
            guides.append((c, guide, o.u8(o.ship(guide) + 0x38)))
    if not guides:
        raise RuntimeError('no AI convoy at sea')
    print(f'convoys, guides and destinations: {guides}')

    def during(o):
        for _, guide, _ in guides:
            w8(o, o.ship(guide) + 0x15C, 1)
    return during


def town_step_branches(o):
    """The battles' piece 6, part 3: the branches of the town battle's step that the raids don't reach, on the
    first town battle of town_raids (the world runs until it starts), by the game's functions called directly on
    it after placing its first ship:
    - on the land: next to the first land cell of the battle's map of the water that has water round it, at its
      corners and middle (0x600770: the cells round it, the hull on them, 0x601C7B, the push off the land);
    - before each tower standing, 300 below it, heading west (the tower to starboard) and east (to port), loaded:
      its guns (0x624950) at the tower, once as it is and once with its strength at 1 (it falls: 0x60130B, the
      event 0x25);
    - first, the step with [+0x2F1A] set, as a battle read from the save (0x5FD510: the towers' places and items
      again, every slot, and the map of the water);
    - its sub-step (0x621770): near the town area's edges aiming at a tower; each rule of an attacker's decision
      to flee (the sums, its hull and arms retouched) and its hull as at the start; the defender (if any) weak
      and far from its side's centre; sinking, with and without [+0x15C]; and last out of the area."""
    during_raids = town_raids(o)

    def battle_of_town():
        for k in range(o.u8(to.BATTLES + 0x404)):
            b = o.u32(to.BATTLES + 4 * k)
            if b and o.u32(b) == 0x67AAA0 and not o.u8(b + 0x5C) and o.u16(b + 0x10) != 0xFFFF:
                return b
        return None

    def during(o):
        during_raids(o)
        b = None
        for _ in range(400):
            o.advance_tick()
            b = battle_of_town()
            if b:
                break
        if not b:
            raise RuntimeError('no town battle within 400 ticks')
        place = o.u16(b + 0x10)
        e = o.u32(o.u32(b + 4) + 4 * place)
        print(f'town battle {b:#x} of town {o.u8(b + 0x1E98):#x}: ship place {place} (ship {o.u32(e + 4)}, arms '
              f'{o.u32(o.ship(o.u32(e + 4)) + 0x120):#x}), towers {[hex(o.u32(b + 0x1EC4 + 4 * k)) for k in range(4)]}')

        def put(x, y, heading=None):
            w32(o, e + 0x8, (x & 0xFFFF) << 16)
            w32(o, e + 0xC, (y & 0xFFFF) << 16)
            if heading is not None:
                w16(o, e + 0x10, heading << 8)
        # As read from the save (before the towers fall: then the battle would be over, part 4).
        w8(o, b + 0x2F1A, 1)
        direct(o, 0x5FD510, [0x54], ecx=b)
        print(f'  after the reload: tower places {[hex(o.u16(b + 0x1E9E + 2 * k)) for k in range(4)]}')
        # The land: the first land cell with water round it.
        water = bytes(o.uc.mem_read(b + 0x1EE0, 0x1000))
        cell = next(((x, y) for x in range(1, 63) for y in range(1, 63) if water[x * 64 + y] == 0
                     and any(water[(x + dx) * 64 + y + dy] for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))), None)
        if cell:
            x0, y0 = cell[0] * 0x1100 // 64, cell[1] * 3841 // 64
            w = 0x1100 // 64
            h = 3841 // 64
            for dx, dy in ((0, 0), (w, 0), (0, h), (w, h), (w // 2, h // 2), (-w // 2, h // 2), (w // 2, -h // 2)):
                put(x0 + dx, y0 + dy, 0x20)
                w32(o, e + 0x1C, 0x8000)
                direct(o, 0x600770, [place], ecx=b)
                print(f'  land cell {cell} +{dx},{dy}: x {o.u32(e + 0x8):#x} y {o.u32(e + 0xC):#x} heading '
                      f'{o.u16(e + 0x10):#x} speed {o.u32(e + 0x1C):#x}')
        # The ship's sub-step (0x621770), its state set for each branch; the battle's sums as the step left
        # them, retouched for the decision to flee.
        wind = o.u8(b + 0x670)

        def substep(entry, what):
            w8(o, entry + 0x161, 0)
            direct(o, 0x621770, [wind], ecx=entry)
            print(f'  {what}: x {o.u16(entry + 0xA)} y {o.u16(entry + 0xE)} heading {o.u16(entry + 0x10):#x} speed '
                  f'{o.u32(entry + 0x1C):#x} flags {o.u8(entry + 0x12A):#x} target {o.u16(entry + 0x128):#x}')
        # Others of the attacker's owner join it (0x60234C): a ship alone (the battle's first ship put on the
        # entry point of the sea's side first, its circle there: the new ship's place is tried again, 0x6027CC),
        # then the guide of a convoy all of that owner's ships, if any (one without them would leave the scene's
        # lists past their end, a case the game never makes).
        _, _, _, ships = o.counts()
        convoys = o.u16(CONVOY_COUNT)
        owner = o.u16(b + 0x35A)
        inside = {o.u32(o.u32(o.u32(b + 4) + 4 * i) + 4) for i in range(o.u16(b + 0x14))}
        alone = next((s for s in range(ships) if o.u8(o.ship(s)) == owner and s not in inside
                      and o.u16(o.ship(s) + 8) >= convoys and o.u16(o.ship(s) + 0x134) in (0, 0xF)), None)
        guide = None
        for c in range(convoys):
            a = convoy_at(o, c)
            members, s = [], o.u16(a + 0xA)
            while s < ships and len(members) < 20:
                members.append(s)
                s = o.u16(o.ship(s) + 6)
            if (o.u8(a) == owner and members and all(o.u8(o.ship(m)) == owner for m in members)
                    and not inside & set(members)):
                guide = members[0]
                break
        ox, oy = struct.unpack('<ii', bytes(o.uc.mem_read(b + 0x1ED8, 8)))
        if abs(2 * ox) > abs(oy):
            entry_point = (0x10EC if ox < 0 else 0x14, 0x781)
        else:
            entry_point = (0x880, 0xEEE if oy < 0 else 0x14)
        put(*entry_point, 0x40)
        w8(o, e + 0x12A, 0)
        substep(e, f'on the entry point {entry_point}')
        for s in [x for x in (alone, guide) if x is not None]:
            direct(o, 0x60234C, [s], ecx=b)
            n = o.u16(b + 0x14)
            print(f'  ship {s} joins: ships {o.u16(b + 0x658)} + {o.u16(b + 0x65A)}, joined convoys '
                  f'{o.u16(b + 0x2F20)}, the last places '
                  f'{[(o.u16(o.u32(o.u32(b + 4) + 4 * i) + 0xA), o.u16(o.u32(o.u32(b + 4) + 4 * i) + 0xE)) for i in range(max(0, n - 3), n)]}')
        sums = bytes(o.uc.mem_read(b + 0xA90, 0x30))
        hull = o.u32(o.ship(o.u32(e + 4)) + 0x18)
        tower0 = next((k for k in range(4) if o.u32(b + 0x1EC4 + 4 * k)), None)
        # Near the town area's edges (both low, both high), aiming at a tower (the edges' marks).
        for x, y in ((0x150, 0x150), (0xF80, 0xD80)):
            if tower0 is None:
                break
            put(x, y, 0x30)
            w8(o, e + 0x12A, 0)
            w16(o, e + 0x128, 0xFFFE - tower0)
            substep(e, f'near the edge {x:#x}, {y:#x}')
        # The decision to flee (team 0): each rule by the sums (strength [+0xA94] of the enemies, sailors
        # [+0xA9A], fighters [+0xA9C]) and the ship's hull [+0x18] and arms [+0x120], and the hull as at the start.
        s = o.ship(o.u32(e + 4))
        hull_now, arms = o.u32(s + 0x18), o.u32(s + 0x120)
        rules = (('1st', 0x7FFFFFFF, 0x7FFF, None, None, None), ('2nd', 0x7FFFFFFF, 0, None, None, None),
                 ('3rd', 0, 0x7FFF, 0, None, None), ('hull', 0, 0, None, 1, None),
                 ('unarmed', 0, 0x7FFF, 0x7FFF, None, 0), ('none', 0, 0, None, None, None))
        for name, a94, a9a, a9c, hull_set, arms_set in rules:
            o.uc.mem_write(b + 0xA90, sums)
            w32(o, b + 0xA94, a94)
            w16(o, b + 0xA9A, a9a)
            if a9c is not None:
                w16(o, b + 0xA9C, a9c)
            w32(o, s + 0x18, hull_now if hull_set is None else hull_set)
            w32(o, s + 0x120, arms if arms_set is None else arms_set)
            put(0x880, 0x781, 0x10)
            w8(o, e + 0x12A, 0)
            w8(o, b + 0xAC9, 1)
            substep(e, f'the {name} rule to flee')
        w32(o, s + 0x18, hull_now)
        w32(o, s + 0x120, arms)
        o.uc.mem_write(b + 0xA90, sums)
        w8(o, b + 0xAC9, 0)
        w32(o, e + 0x144, hull)
        w8(o, e + 0x12A, 1)
        substep(e, 'its hull as at the start')
        # The defender (team 1), if any: weaker than both teams and far from its side's centre; then with tower
        # 0 fallen.
        d = o.u16(b + 0x65C)
        if d != 0xFFFF:
            f = o.u32(o.u32(b + 4) + 4 * d)
            w32(o, b + 0xA90, 0x7FFFFFFF)
            w32(o, b + 0xA94, 0x7FFFFFFF)
            w32(o, f + 0x8, (o.u32(b + 0xAB8) + 0x300) << 16)
            w8(o, f + 0x12A, 0)
            substep(f, 'the defender, weak and far')
            # The same with tower 0 fallen (the game tests only tower 0): it doesn't flee.
            strength0 = o.u32(b + 0x1EC4)
            w32(o, b + 0x1EC4, 0)
            w32(o, f + 0x8, (o.u32(b + 0xAB8) + 0x300) << 16)
            w8(o, f + 0x12A, 0)
            substep(f, 'the defender, weak and far, tower 0 fallen')
            w32(o, b + 0x1EC4, strength0)
            o.uc.mem_write(b + 0xA90, sums)
        # Sinking (0x4): slowing down only with [+0x15C] (0 in the game: a retouch for the branch).
        flags = o.u8(e + 0x12A)
        for slow in (0, 1):
            put(0x880, 0x781, 0x13)
            w32(o, e + 0x1C, 0x3000)
            w8(o, e + 0x12A, 0x4)
            w32(o, e + 0x15C, slow)
            substep(e, f'sinking, [+0x15C] {slow}')
        w32(o, e + 0x15C, 0)
        w8(o, e + 0x12A, flags)
        # The towers.
        for k in range(4):
            if o.u32(b + 0x1EC4 + 4 * k) == 0:
                continue
            tx, ty = o.u16(b + 0x1E9E + 2 * k), o.u16(b + 0x1EA6 + 2 * k)
            for heading, strength in ((0xC0, None), (0x40, None), (0xC0, 1)):
                if o.u32(b + 0x1EC4 + 4 * k) == 0:
                    break
                if strength is not None:
                    w32(o, b + 0x1EC4 + 4 * k, strength)
                put(tx, ty + 300, heading)
                w8(o, e + 0x12A, 0)
                w8(o, e + 0x130, 0)
                w8(o, e + 0x131, 0)
                direct(o, 0x624950, [0], ecx=e)
                print(f'  tower {k} ({tx}, {ty}) heading {heading:#x}: strength {o.u32(b + 0x1EC4 + 4 * k):#x}, '
                      f'loading {o.u8(e + 0x130)} {o.u8(e + 0x131)}')
        # Last, out of the area (out, its items freed).
        put(5, 0x781, 0x40)
        w8(o, e + 0x12A, 0)
        substep(e, 'out of the area')
    return during


def town_end_branches(o):
    """The battles' piece 6, part 4: the branches of the town battle's end (method +0x1C, 0x5FE7E0) and its
    destructor (method +0x4, 0x602990 → 0x5FB9F0) that the raids don't reach, by the game's functions called
    directly on the first town battle of town_raids with a defender ship (else the first one):
    - first, another lone ship of the attacker's owner at sea turns raider with its route's end put at the
      battle's town (its approach point): it arrives and joins the battle by the game's way (0x506F33 →
      0x6130B0, 0x60234C), so that the battle has a joined convoy ([+0x2F1C]);
    - the attackers win (0x5FE7E0 with 0) at the town of the player's first office (the battle's town
      retouched), with the player as the defender and his ship fled (the coward's letter 0x3B and 0x4F8CA0),
      the attacker's convoy made of its guide, another ship of its owner and one of a third merchant (a new
      side), the guide nearly full (0x633DB0 hands the wares over to the next ships), the player's office and
      the town with wares and money and their last plunder long ago, and the guide's [+0x15D] the player with
      a captain (the share's letter 0x6A); then again with every ship of the convoy nearly full (they fill up at
      the offices, the other offices are only counted, the town keeps its wares);
    - again at once (the town paid within 14 days: the music 0), with no defender merchant and [+0xAC4] 3;
    - the attackers lose (0x5FE7E0 with 1): with no defender merchant and an attacker fled (most kept); with the
      player as the defender, the attackers sunk or taken and the defender taken (his report has both sides'
      ships taken); with [+0xAC4] 3 (no news);
    - the destructor with the music's fraction at 999 (the event 5, 0x50CB80) and the joined convoy back on its
      way (0x50BFD0)."""
    during_raids = town_raids(o)
    towns, offices, merchants, ships = o.counts()
    player = o.u32(to.PLAYER)

    def town_battles():
        out = []
        for k in range(o.u8(to.BATTLES + 0x404)):
            b = o.u32(to.BATTLES + 4 * k)
            if b and o.u32(b) == 0x67AAA0 and not o.u8(b + 0x5C) and o.u16(b + 0x10) != 0xFFFF:
                out.append(b)
        return out

    def entries(b, head):
        out, k = [], o.u16(b + head)
        while k != 0xFFFF and len(out) < 64:
            out.append(o.u32(o.u32(b + 4) + 4 * k))
            k = o.u16(o.u32(b + 0xC) + 2 * k)
        return out

    def during(o):
        during_raids(o)
        b = first = None
        for _ in range(400):
            o.advance_tick()
            found = town_battles()
            first = first or (found[0] if found else None)
            b = next((x for x in found if o.u16(x + 0x65A) > 0), None)
            if b:
                break
        b = b or first
        if not b:
            raise RuntimeError('no town battle within 400 ticks')
        convoys = o.u16(CONVOY_COUNT)
        town, owner, defender = o.u8(b + 0x1E98), o.u16(b + 0x35A), o.u16(b + 0x35C)
        a = o.u16(b + 0x65E)
        guide = o.u16(convoy_at(o, a) + 0xA)
        print(f'town battle {b:#x} of town {town:#x}: owners {owner:#x} {defender:#x}, ships {o.u16(b + 0x658)} + '
              f'{o.u16(b + 0x65A)}, convoy {a} guide {guide}')
        inside = {o.u32(e + 4) for e in entries(b, 0x10) + entries(b, 0x65C)}
        # A lone ship of the attacker's owner at sea, with a route, raids the battle's town and arrives there.
        join = next((s for s in range(ships) if o.u8(o.ship(s)) == owner and s not in inside
                     and o.u16(o.ship(s) + 0x134) == 0xF and o.u16(o.ship(s) + 8) >= convoys
                     and o.u32(o.ship(s) + 0x24)), None)
        if join is not None:
            run_bytes(o, player_op('pirate', join, 1))
            n = o.u8(0x6DE4B8 + town)
            ax, ay = o.u32(0x6DDB90 + 0x34 * n + 0x20) & 0xFFFF, o.u32(0x6DDB90 + 0x34 * n + 0x24) & 0xFFFF
            s = o.ship(join)
            route = o.u32(s + 0x24)
            points, count = o.u32(route), o.u32(route + 4)
            w16(o, points + 4 * (count - 1), ax)
            w16(o, points + 4 * (count - 1) + 2, ay)
            w16(o, s + 0x34, count - 1)
            w8(o, s + 0x38, town)
            w32(o, s + 0x1C, ax << 16 | 0x7FFF)
            w32(o, s + 0x20, ay << 16 | 0x7FFF)
            w32(o, s + 0x28, 0)
            w32(o, s + 0x2C, 0)
            for _ in range(20):
                o.advance_tick()
                if o.u16(b + 0x2F20) or o.u8(b + 0x5C) or o.u32(to.BATTLES + 4 * o.u16(b + 0x662)) != b:
                    break
            print(f'  ship {join} raids town {town:#x}: joined convoys {o.u16(b + 0x2F20)}, over {o.u8(b + 0x5C)}, '
                  f'ships {o.u16(b + 0x658)} + {o.u16(b + 0x65A)}')
        if o.u32(to.BATTLES + 4 * o.u16(b + 0x662)) != b or o.u8(b + 0x5C):
            raise RuntimeError('the battle ended while the ship joined it')
        side_a, side_d = entries(b, 0x10), entries(b, 0x65C)
        inside = {o.u32(e + 4) for e in side_a + side_d}
        # The attackers win at the town of the player's first office.
        f = o.u16(o.merchant(player) + 0xC)
        plunder_town = o.u8(o.office(f) + 0x2C6) if f < offices else town
        others = [s for s in range(ships) if o.u8(o.ship(s)) == owner and s not in inside and s != guide]
        third = [s for s in range(ships) if o.u8(o.ship(s)) not in (owner, player) and o.u8(o.ship(s)) < merchants
                 and s not in inside]
        chain = [guide] + others[:1] + third[:1]
        nexts = {s: o.u16(o.ship(s) + 6) for s in chain}
        for x, y in zip(chain, chain[1:] + [0xFFFF]):
            w16(o, o.ship(x) + 6, y)
        g = o.ship(guide)

        def nearly_full(s):
            room = o.u32(o.ship(s) + 0x10) - o.u32(o.ship(s) + 0x11C) - o.u32(o.ship(s) + 0x118)
            if room > 600:
                w32(o, o.ship(s) + 0x118, o.u32(o.ship(s) + 0x118) + room - 600)
            return room
        room = nearly_full(guide)
        saved = {k: o.u16(b + k) for k in (0x35C, 0x1E98)}
        saved_ac4 = o.u8(b + 0xAC4)
        w8(o, b + 0x1E98, plunder_town)
        w16(o, b + 0x35C, player)
        if side_d:
            w8(o, side_d[0] + 0x12A, o.u8(side_d[0] + 0x12A) | 2)
        w32(o, o.u32(to.SIEGES) + 0x1024 * plunder_town, 0)
        w32(o, o.town(plunder_town) + 0x700, 3000)
        if f < offices:
            for w in (3, 4, 5, 6, 7, 8, 10):
                w32(o, o.office(f) + 4 + 4 * w, 3000)
        captain15d = o.u8(g + 0x15D)
        w8(o, g + 0x15D, player)
        if o.u16(g + 0x42) >= o.u16(0x6DD892):
            w16(o, g + 0x42, 0)
        print(f'  the attackers win at town {plunder_town:#x} (the player\'s office {f}): convoy {chain}, the guide\'s '
              f'room {room} → 600, captain {o.u16(g + 0x42)}')
        direct(o, 0x5FE7E0, [0], ecx=b)
        # Again, every ship of the convoy nearly full: they fill up at the offices, the other offices are only
        # counted and the town keeps its wares (a full ship's room left is under a barrel).
        w8(o, b + 0x5C, 0)
        w16(o, b + 0x15C, 0)
        w32(o, o.u32(to.SIEGES) + 0x1024 * plunder_town, 0)
        if f < offices:
            for w in (3, 4, 5, 6, 7, 8, 10):
                w32(o, o.office(f) + 4 + 4 * w, 3000)
        for s in chain:
            nearly_full(s)
        direct(o, 0x5FE7E0, [0], ecx=b)
        print(f'  again with the convoy nearly full: town wares {[o.u32(o.town(plunder_town) + 4 + 4 * w) for w in (3, 4, 5)]}')
        for s, n in nexts.items():
            w16(o, o.ship(s) + 6, n)
        w8(o, g + 0x15D, captain15d)

        def again(lost, what):
            w8(o, b + 0x5C, 0)
            w16(o, b + 0x15C, 0)
            direct(o, 0x5FE7E0, [lost], ecx=b)
            print(f'  {what}: [+0x15C] {o.u16(b + 0x15C)}, music {o.u16(b + 0x1E94):#x}, sides {o.u8(b + 0x558)}')
        w16(o, b + 0x35C, 0xFF)
        w8(o, b + 0xAC4, 3)
        again(0, 'again at once, no defender merchant, [+0xAC4] 3')
        w8(o, b + 0xAC4, 0)
        flags = {e: o.u8(e + 0x12A) for e in side_a + side_d}
        taken = {e: o.u8(e + 0x13D) for e in side_a + side_d}
        w8(o, side_a[0] + 0x12A, 2)
        again(1, 'the attackers lose, one fled, no defender merchant')
        # The player as the defender: his report (a letter 0x29, kept) has the ships taken of both sides.
        w16(o, b + 0x35C, player)
        w8(o, side_a[0] + 0x12A, 4)
        if len(side_a) > 1:
            w8(o, side_a[1] + 0x12A, 0)
            w8(o, side_a[1] + 0x13D, 1 - o.u8(side_a[1] + 0x142))
        if side_d:
            w8(o, side_d[0] + 0x12A, 0)
            w8(o, side_d[0] + 0x13D, 1 - o.u8(side_d[0] + 0x142))
        again(1, 'the attackers lose, sunk and taken')
        w8(o, b + 0xAC4, 3)
        again(1, 'the attackers lose, [+0xAC4] 3')
        for e, v in flags.items():
            w8(o, e + 0x12A, v)
        for e, v in taken.items():
            w8(o, e + 0x13D, v)
        w8(o, b + 0xAC4, saved_ac4)
        for k, v in saved.items():
            w16(o, b + k, v)
        # The destructor, the battle over with the music's fraction at 999, the attackers the winners ([+0x130E]
        # 0: the game's two branches by it are the same code; the raids' ends take the other one).
        w8(o, b + 0x5C, 1)
        w16(o, b + 0x15C, 0)
        w8(o, b + 0x130E, 0)
        w16(o, b + 0x1E94, (o.u16(b + 0x1E94) & 0xFC00) | 999)
        joined = o.u16(b + 0x2F20)
        direct(o, 0x602990, [1], ecx=b)
        print(f'  the destructor: {joined} joined convoys, battles {o.u8(to.BATTLES + 0x404)}')
    return during


def town_start_text(o, b, ship, town):
    """The game's text of a town battle's question to the local player on a side (0x5FD102..0x5FD3DD, in 0x5FBFE0
    when the window can open; the oracle takes the busy path, BATTLE_WINDOW_JUMPS), run on its own over the
    battle just set up: [esp+0x14] the attacker ship's offset, [esp+0x20] the town, esi the battle, ecx the
    player, eax side A's owner; up to 0x5FD3E0, the text written. Its writes ([+0x1310], [+0x1E90], [+0x1E91])
    are put back. Returns the text."""
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_ESI, UC_X86_REG_ESP
    uc = o.uc
    kept_text, kept_names = bytes(uc.mem_read(b + 0x1310, 0x200)), bytes(uc.mem_read(b + 0x1E90, 2))
    esp = to.STACK + 0x10_0000 - 0x4000
    uc.mem_write(esp, bytes(0x40))
    uc.mem_write(esp + 0x14, struct.pack('<I', ship * 0x180))
    uc.mem_write(esp + 0x20, struct.pack('<I', town))
    uc.reg_write(UC_X86_REG_ESP, esp)
    uc.reg_write(UC_X86_REG_ESI, b)
    uc.reg_write(UC_X86_REG_ECX, o.u32(to.PLAYER))
    uc.reg_write(UC_X86_REG_EAX, o.u16(b + 0x35A))
    uc.emu_start(0x5FD102, 0x5FD3E0, timeout=60 * 1000 * 1000)
    text = bytes(uc.mem_read(b + 0x1310, 0x200)).split(b'\0')[0].decode('latin-1')
    uc.mem_write(b + 0x1310, kept_text)
    uc.mem_write(b + 0x1E90, kept_names)
    return text


def town_battle_texts(o):
    """The battles' piece 6, part 5: the town battle's window texts that p3vita composes for the viewer only
    (Sailing::town_attack_message, town_end_message; nothing in memory), against the game's. Town battles set up
    directly (0x611D80, 0x5FBFE0) with the player on a side: his ship attacks a town with a guard ship; towns with
    no defence (the battle over at its setup: the end's window, which the game shows); the first pirate's nest;
    his convoy of two ships (when two of his ships are in port in one town) a town with a guard ship; and he
    defends as a town's guard ship ([+0x994]) against a pirate convoy's guide and against an AI merchant's ship
    alone. After each setup, the game's text of the question (town_start_text), printed as «start text». Then
    0x5FE7E0 called directly on the battles that go on, with the player's side human (the busy path's operation
    0x96 doesn't run before the tick's end): the end's kinds [+0xAC4] 0, 1, 2 and 3, won and lost (the oracle
    prints the game's end window, TOWN_END_WINDOW), and once with the player's side the computer's (no window: the
    letter 0x29; p3vita's viewer text all the same). tick_probe --messages prints p3vita's texts."""
    towns, _, merchants, ships = o.counts()
    player = o.u32(to.PLAYER)
    convoys = o.u16(CONVOY_COUNT)
    mine = merchant_ships(o, player)
    if not mine:
        raise RuntimeError('the player has no ship')
    alone = [s for s in range(ships) if o.u8(o.ship(s)) < merchants and o.u8(o.ship(s)) != player
             and o.u16(o.ship(s) + 0x134) == 0xF and o.u16(o.ship(s) + 8) >= convoys]
    pirate_guides = [o.u16(convoy_at(o, c) + 0x10) for c in range(convoys)
                     if o.u8(convoy_at(o, c)) >= merchants and o.u16(convoy_at(o, c) + 0x10) < ships
                     and o.u16(convoy_at(o, c) + 0xA) < ships]

    def in_port(t):
        return [s for s in range(ships) if o.u8(o.ship(s)) < merchants and o.u8(o.ship(s)) != player
                and o.u16(o.ship(s) + 0x134) == 0 and o.u8(o.ship(s) + 0x39) == t and o.u16(o.ship(s) + 8) >= convoys]
    guarded = [(t, in_port(t)[0]) for t in range(towns) if in_port(t)]
    k = next(i for i in range(5) if o.u32(PIRATES + 4 * i))
    pirate = o.u32(PIRATES + 4 * k)
    print(f'the player\'s ships {mine[:4]}; towns with an AI ship in port {[t for t, _ in guarded][:8]}; pirate '
          f'convoy guides {pirate_guides[:3]}; ships alone {alone[:3]}; pirate {k}')

    def attack(town, ship, what):
        slot = direct(o, 0x611D80, [], ecx=to.BATTLES) & 0xFF
        b = o.u32(to.BATTLES + 4 * slot)
        direct(o, 0x5FBFE0, [ship, town], ecx=b)
        print(f'{what}: town {town:#x} by ship {ship} (owner {o.u8(o.ship(ship)):#x}, convoy {o.u16(o.ship(ship) + 8)}):'
              f' slot {slot}, owners {o.u16(b + 0x35A):#x} {o.u16(b + 0x35C):#x}, ships {o.u16(b + 0x658)} + '
              f'{o.u16(b + 0x65A)}, towers {[o.u8(b + 0x1E99 + i) for i in range(4)]}, over {o.u8(b + 0x5C)}')
        if not o.u8(b + 0x5C):
            print(f'  start text: {town_start_text(o, b, ship, town)!r}')
        return b

    def ends(b, side, what):
        print(f'the ends of {what} (the player on side {side}):')
        cases = ((0, 0, 0), (0, 1, 0), (0, 2, 0), (0, 3, 0), (1, 0, 0), (1, 1, 0), (1, 3, 0), (0, 1, 1))
        for lost, kind, computer in cases:
            w8(o, b + 0xAC4, kind)
            w8(o, b + 0x5C, 0)
            w16(o, b + 0x15C, 0)
            w8(o, b + 0x5D + side, computer)
            extra = ', the side the computer\'s' if computer else ''
            print(f'  lost {lost}, [+0xAC4] {kind}{extra}')
            direct(o, 0x5FE7E0, [lost], ecx=b)

    def own_convoy(s):
        """A convoy of its own for a ship alone (0x5062F0, as a raider gets at its town, 0x506F41; out of the sea's
        list first, 0x505B60, else its next [+0x6] would chain another ship into the convoy): the game reads the
        attacker's convoy [+0x65E] without checking it."""
        if o.u16(o.ship(s) + 8) >= convoys:
            if o.u16(o.ship(s) + 0x134) == 0:
                # In port: out of the docked list with a convoy of its own as a town's guard ship (0x50CC90).
                t = o.u8(o.ship(s) + 0x39)
                guard = o.u16(o.town(t) + 0x994)
                w16(o, o.town(t) + 0x994, s)
                direct(o, 0x50CC90, [t], ecx=to.SHIPS)
                w16(o, o.town(t) + 0x994, guard)
                return
            if o.u16(o.ship(s) + 0x134) == 0xF:
                direct(o, 0x505B60, [s], ecx=to.SHIPS)
            head = o.alloc(4)
            w16(o, head, 0xFFFF)
            direct(o, 0x5062F0, [head, s], ecx=to.SHIPS)

    def during(o):
        battles = []
        try:
            s, t = player_docked_ship(o)
            others = docked_ships(o, player, t, (s,))[:1]
        except RuntimeError:
            s, t, others = None, None, []
        free = [x for x in mine if x != s and x not in others]
        lone = next((x for x in free if o.u16(o.ship(x) + 0x134) in (0, 0xF)), None)
        if lone is None:
            lone, others = (s if s is not None else mine[0]), []
        own_convoy(lone)
        # A town with a guard ship (an AI ship in port there), else one with towers.
        ta = guarded[0][0] if guarded else 0
        if guarded:
            w16(o, o.town(ta) + 0x994, guarded[0][1])
        battles.append((attack(ta, lone, 'the player attacks a town'), 0, f'town {ta:#x}'))
        # A town with no towers ([0x768] emptied for the call) and no guard ship: the battle over at its setup,
        # twice (the plunder paid, then not: «recently attacked»).
        tu = next(x for x in range(1, towns) if x != ta)
        w32(o, o.u32(to.SIEGES) + 0x1024 * tu, 0)
        for _ in range(2):
            saved = set_fields(o, [(o.town(tu) + 0x768, 2, 0xFFFF), (o.town(tu) + 0x994, 2, 0xFFFF)])
            attack(tu, lone, 'the player attacks an undefended town')
            restore_fields(o, saved)
        head = o.u16(pirate + 0xC)
        if head < ships and o.u16(o.ship(head) + 8) < convoys:
            w16(o, pirate + 0x10, o.u16(o.ship(head) + 8))
        battles.append((attack(0xFE - k, lone, 'the player attacks the first pirate\'s nest'), 0, 'the nest'))
        used = [ta, tu]
        # His convoy of two ships, if two are in port in one town.
        if s is not None and others:
            make_guide(o, s, t)
            run_bytes(o, player_op('create-convoy', s, 0, 0))
            run_bytes(o, player_op('join-convoy', s, others[0]))
            tc = next(x for x in range(towns) if x not in used)
            used.append(tc)
            attack(tc, s, 'the player\'s convoy attacks a town')
        # He defends a town as its guard ship, against a pirate convoy's guide and an AI ship alone (a convoy of its
        # own).
        for a in pirate_guides[:1] + alone[:1]:
            td = next(x for x in range(towns) if x not in used)
            used.append(td)
            own_convoy(a)
            w16(o, o.town(td) + 0x994, lone)
            b = attack(td, a, 'the player defends a town')
            if o.u16(b + 0x35C) == player and not o.u8(b + 0x5C):
                battles.append((b, 1, f'the defence of town {td:#x}'))
        for b, side, what in battles:
            ends(b, side, what)
    return during


def nest_attack(o, strong=True):
    """The battles' piece 6: the attack on a nest of the Hanse task of type 3 (its fourth start, 0x4EBA71..
    0x4EBC5E), by the game's way: a task of the candidacy made of type 3 in its phase 1 for the first AI merchant
    with three ships sailing alone, against the first pirate's nest; the event queue (0x4D85C0) called directly at
    the task's tick with the merchant's ships standing on the nest (the nest's place moved to the first one, as
    retouch.nest_task):
    - the first ship alone: it leaves the sea (0x505B60) with a convoy of its own for the nest (0x50E280), the nest
      gets its map if it has none and its defence (0x514530: its ships in its convoy) and the battle starts
      (0x611D80, 0x5FBFE0 against 0xFE − k), the task's [+0x14] the ship;
    - the second one with the convoy of a free slot in its [+0x8] (not at sea: 0x50A4E0 fails, the next ship) and
      the third one: it joins the battle (0x515D00, the nest's convoy out; 0x6130B0, 0x60234C);
    then the world runs: the battle steps and ends at the nest (0x5FE7E0, 0x602990 → 0x5FB9F0, 0x514920). With
    `strong` the attackers get the guns of the save's first armed ship, the nest's ships a hull of 1 and its towers
    a damage of 199 (and the battle's towers a strength of 1, when 0x514530 made the nest's map and took the
    damage off): the nest taken, its towers burning (0x515C00) and, with none standing, destroyed (operation 0xB4
    and the task done); without, the nest keeps its ships and towers."""
    towns, offices, merchants, ships = o.counts()
    convoys = o.u16(CONVOY_COUNT)
    ps = pirates(o)
    if not ps:
        raise RuntimeError('no pirate')
    k, p = ps[0]
    nest = o.u8(p + 0xE)

    def sailing_alone(m):
        out, s = [], o.u16(o.merchant(m) + 0xE)
        while s < ships and len(out) < 64:
            if o.u16(o.ship(s) + 0x134) == 0xF and o.u16(o.ship(s) + 8) >= convoys:
                out.append(s)
            s = o.u16(o.ship(s) + 4)
        return out
    m = next((x for x in rt.ai_with_office(o) if len(sailing_alone(x)) >= 3), None)
    if m is None:
        raise RuntimeError('no AI merchant with three ships sailing alone')
    s1, s2, s3 = sailing_alone(m)[:3]
    tasks = candidacy_tasks(o, rt.ai_with_office(o)[0])
    task = next(iter(tasks.values()))
    set_task(o, task, kind=3, merchant=m, started=1, deadline=0x3000, f10=k, f11=nest, f12=1, w14=0xFFFF)
    model = next((s for s in range(ships) if o.u32(o.ship(s) + 0x120)), None)
    print(f'pirate {k} nest {nest}: [+0x0] {o.u32(p):#x}, ships at the nest {o.u16(p + 0x12)}, convoy {o.u16(p + 0x10)}, '
          f'pool {o.u16(p + 8)}; merchant {m}: ships {s1}, {s2}, {s3}; armed model {model}')

    def during(o):
        i, a = task
        now = o.u32(WORLD + 0x14)
        x, y = struct.unpack('<hh', bytes(o.uc.mem_read(o.ship(s1) + 0x1E, 2)) + bytes(o.uc.mem_read(o.ship(s1) + 0x22, 2)))
        w32(o, to.TOWN_TABLE + 0x34 * nest + 0x20, x)
        w32(o, to.TOWN_TABLE + 0x34 * nest + 0x24, y)
        if strong:
            for s in (s1, s3):
                if model is not None:
                    w32(o, o.ship(s) + 0x120, o.u32(o.ship(model) + 0x120))
                    o.uc.mem_write(o.ship(s) + 0x13C, bytes(o.uc.mem_read(o.ship(model) + 0x13C, 0x18)))
            n = o.u16(p + 0x12)
            while n < ships:
                w32(o, o.ship(n) + 0x18, 1)
                n = o.u16(o.ship(n) + 6)
            pool = o.u32(p + 4)
            for e in range(o.u16(p + 8)):
                if o.u8(pool + 8 * e + 3) in (0x30, 0x31):
                    w8(o, pool + 8 * e + 6, 199)
        rt.move_event(o, i, a, now)
        direct(o, 0x4D85C0, [now], ecx=to.EVENTS)
        b = next((o.u32(to.BATTLES + 4 * j) for j in range(o.u8(to.BATTLES + 0x404)) if o.u32(to.BATTLES + 4 * j)
                  and o.u32(o.u32(to.BATTLES + 4 * j)) == 0x67AAA0 and o.u8(o.u32(to.BATTLES + 4 * j) + 0x1E98) == 0xFE - k),
                 None)
        if strong and b:
            # 0x514530 took the damage off the pool's towers when it made the nest's map: the battle's towers weak.
            for t in range(4):
                if o.u32(b + 0x1EC4 + 4 * t):
                    w32(o, b + 0x1EC4 + 4 * t, 1)
        print(f'  ship {s1} attacks: task [+0x14] {o.u16(a + 0x14)}, ship state {o.u16(o.ship(s1) + 0x134):#x}, '
              f'convoy {o.u16(o.ship(s1) + 8)}, nest [+0x0] {o.u32(p):#x} convoy {o.u16(p + 0x10)}, battle {b and hex(b)}'
              + (f': ships {o.u16(b + 0x658)} + {o.u16(b + 0x65A)}, towers {[o.u8(b + 0x1E99 + t) for t in range(4)]}'
                 if b else ''))
        free = o.u16(FREE_CONVOY)
        kept = o.u16(o.ship(s2) + 8)
        w16(o, o.ship(s2) + 8, free)
        for s in (s2, s3):
            w16(o, o.ship(s) + 0x1E, x)
            w16(o, o.ship(s) + 0x22, y)
        rt.move_event(o, i, a, now)
        direct(o, 0x4D85C0, [now], ecx=to.EVENTS)
        w16(o, o.ship(s2) + 8, kept)
        print(f'  ships {s2} (convoy {free}, not at sea) and {s3}: states {o.u16(o.ship(s2) + 0x134):#x} '
              f'{o.u16(o.ship(s3) + 0x134):#x}; battle ships {b and o.u16(b + 0x658)}, joined {b and o.u16(b + 0x2F20)}')
    return during


def nest_attack_weak(o):
    return nest_attack(o, strong=False)


def nest_attack_save(o):
    """For the save's check (re/emu/sesion19/wb.sh): as nest_attack's first attack, but the merchant's first ship
    sailing alone is put on the nest's place (the town table's, which a load reads again from StadtDaten.ini) instead
    of the nest on it; the battle stays (0 days)."""
    _, _, _, ships = o.counts()
    convoys = o.u16(CONVOY_COUNT)
    k, p = pirates(o)[0]
    nest = o.u8(p + 0xE)
    m = s = None
    for x in rt.ai_with_office(o):
        s = o.u16(o.merchant(x) + 0xE)
        while s < ships and not (o.u16(o.ship(s) + 0x134) == 0xF and o.u16(o.ship(s) + 8) >= convoys):
            s = o.u16(o.ship(s) + 4)
        if s < ships:
            m = x
            break
    if m is None:
        raise RuntimeError('no AI merchant with a ship sailing alone')
    task = next(iter(candidacy_tasks(o, rt.ai_with_office(o)[0]).values()))
    set_task(o, task, kind=3, merchant=m, started=1, deadline=0x3000, f10=k, f11=nest, f12=1, w14=0xFFFF)

    def during(o):
        i, a = task
        now = o.u32(WORLD + 0x14)
        x = o.u32(to.TOWN_TABLE + 0x34 * nest + 0x20) & 0xFFFF
        y = o.u32(to.TOWN_TABLE + 0x34 * nest + 0x24) & 0xFFFF
        w32(o, o.ship(s) + 0x1C, x << 16 | 0x7FFF)
        w32(o, o.ship(s) + 0x20, y << 16 | 0x7FFF)
        rt.move_event(o, i, a, now)
        direct(o, 0x4D85C0, [now], ecx=to.EVENTS)
        print(f'  ship {s} of {m} at nest {nest} ({x}, {y}) of pirate {k}: state {o.u16(o.ship(s) + 0x134):#x}, '
              f'battles {o.u8(to.BATTLES + 0x404)}')
    return during


def nest_attack_branches(o):
    """The battles' piece 6: the attack on a nest's branches that nest_attack doesn't reach, each on a pirate of its
    own (as many as the save has) with a task of the candidacy made of type 3 in its phase 1, the nest's place
    moved to the attacker and the event queue (0x4D85C0) called directly:
    - the guide of the first AI convoy at sea (its ships all its owner's): the convoy leaves the sea (0x50A4E0)
      in the state 0x14 and attacks;
    - a ship sailing alone against a nest whose convoy is out ([+0x10] a pirate convoy at sea, 0x515D00) with no
      battle there: a new one, the nest's ships put in that convoy (0x514530);
    - a ship sailing alone against a nest with no ship at home and no map ([+0x12] 0xFFFF, [+0x0] 0: the map made
      in the call, so the new ship's strength is the game's clean one): a new ship (0x516170) with a quarter of
      its hull back at the nest, its convoy left empty.
    After the first, a boarding (0x60C1C9) called directly in its battle with side A's owner the player and the
    nest's convoy's guide sunk: the player's message names the taken ship's owner by the nest's captain."""
    towns, offices, merchants, ships = o.counts()
    convoys = o.u16(CONVOY_COUNT)
    ps = pirates(o)
    guide = owner1 = None
    for c in range(convoys):
        a = convoy_at(o, c)
        g = o.u16(a + 0x10)
        owners, s = set(), o.u16(a + 0xA)
        while s < ships and len(owners) < 3:
            owners.add(o.u8(o.ship(s)))
            s = o.u16(o.ship(s) + 6)
        if o.u8(a) < merchants and o.u16(a + 0x12) == 0xF and g < ships and owners == {o.u8(a)}:
            guide, owner1 = g, o.u8(a)
            break
    lone = []
    for m in rt.ai_with_office(o):
        if m == owner1:
            continue
        s = o.u16(o.merchant(m) + 0xE)
        while s < ships and not (o.u16(o.ship(s) + 0x134) == 0xF and o.u16(o.ship(s) + 8) >= convoys):
            s = o.u16(o.ship(s) + 4)
        if s < ships:
            lone.append((m, s))
    pirate_convoy = next((c for c in range(convoys) if o.u8(convoy_at(o, c)) >= merchants
                          and o.u16(convoy_at(o, c) + 0x12) == 0x12), None)
    tasks = list(candidacy_tasks(o, rt.ai_with_office(o)[0]).values())
    cases = []
    if guide is not None and len(ps) > 0:
        cases.append(('convoy', ps[0], owner1, guide))
    if pirate_convoy is not None and len(ps) > 1 and len(lone) > 0:
        cases.append(('out', ps[1], lone[0][0], lone[0][1]))
    if len(ps) > 2 and len(lone) > 1:
        cases.append(('home', ps[2], lone[1][0], lone[1][1]))
    for (what, (k, p), m, s), task in zip(cases, tasks):
        set_task(o, task, kind=3, merchant=m, started=1, deadline=0x3000, f10=k, f11=o.u8(p + 0xE), f12=1,
                 w14=0xFFFF)
    print(f'cases {[(w, k, m, s) for w, (k, _), m, s in cases]}; pirate convoy at sea {pirate_convoy}')

    def during(o):
        now = o.u32(WORLD + 0x14)
        for (what, (k, p), m, s), (i, a) in zip(cases, tasks):
            nest = o.u8(p + 0xE)
            x, y = struct.unpack('<hh', bytes(o.uc.mem_read(o.ship(s) + 0x1E, 2)) + bytes(o.uc.mem_read(o.ship(s) + 0x22, 2)))
            w32(o, to.TOWN_TABLE + 0x34 * nest + 0x20, x)
            w32(o, to.TOWN_TABLE + 0x34 * nest + 0x24, y)
            if what == 'out':
                w16(o, p + 0x10, pirate_convoy)
            elif what == 'home':
                w16(o, p + 0x12, 0xFFFF)
                w16(o, p + 0x10, 0xFFFF)
                w32(o, p, 0)
            rt.move_event(o, i, a, now)
            direct(o, 0x4D85C0, [now], ecx=to.EVENTS)
            print(f'  {what}: ship {s} of {m} against pirate {k}: state {o.u16(o.ship(s) + 0x134):#x}, convoy '
                  f'{o.u16(o.ship(s) + 8)}; the nest\'s convoy {o.u16(p + 0x10)}, at home {o.u16(p + 0x12)}, map '
                  f'{o.u32(p):#x}; task [+0x14] {o.u16(a + 0x14)}; battles {o.u8(to.BATTLES + 0x404)}')
            b = next((o.u32(to.BATTLES + 4 * j) for j in range(o.u8(to.BATTLES + 0x404)) if o.u32(to.BATTLES + 4 * j)
                      and o.u32(o.u32(to.BATTLES + 4 * j)) == 0x67AAA0
                      and o.u8(o.u32(to.BATTLES + 4 * j) + 0x1E98) == 0xFE - k), None)
            if what == 'convoy' and b and o.u16(b + 0x65C) != 0xFFFF:
                # The player's boarding message at the nest (0x60C1C9): side A's owner the player for the call and
                # the nest's convoy's guide sunk, so the taken ship's owner is named by the nest's captain (method
                # +0x28 at a nest, 0x60C4EC).
                player = o.u32(to.PLAYER)
                owner_a = o.u16(b + 0x35A)
                g = o.u16(convoy_at(o, o.u16(b + 0x660)) + 0x10)
                hull = o.u32(o.ship(g) + 0x18)
                w16(o, b + 0x35A, player)
                w32(o, o.ship(g) + 0x18, 0)
                direct(o, 0x60C1C9, [o.u16(b + 0x10), o.u16(b + 0x65C)], ecx=b)
                text = bytes(o.uc.mem_read(b + 0x1310, 0x100)).split(b'\0')[0].decode('latin-1')
                print(f'  the player\'s boarding at the nest: {text!r}')
                w16(o, b + 0x35A, owner_a)
                w32(o, o.ship(g) + 0x18, hull)
    return during


def nest_fire(o):
    """The battles' piece 6, part 4: the fire of a nest's tower (the event 0x25 of a tower fallen in the nest's
    battle, 0x4E1B84 → 0x515C00), with new events at the tick and the world running: on the first pirate with a
    pool, two of its planned buildings (entries 0x1B of the list from entry 0) made towers 0x30 and 0x31 at
    cells of their own, one undamaged (it burns from 201 to 250, a tick at a time, and leaves the list) and one
    at 240 (from 241); a cell of the nest with no tower; a pirate slot with no pirate, if any; and a town out
    of the world that is no nest (0xFE − town is 5 or more)."""
    towns = o.counts()[0]
    now = o.u32(WORLD + 0x14)
    k = next((i for i in range(5) if o.u32(to.SHIPS + 0xC + 4 * i) and o.u16(o.u32(to.SHIPS + 0xC + 4 * i) + 8)), None)
    if k is None:
        raise RuntimeError('no pirate with a pool')
    p = o.u32(to.SHIPS + 0xC + 4 * k)
    pool, count = o.u32(p + 4), o.u16(p + 8)
    chain, i = [], o.u16(pool + 4)
    while i < count and len(chain) < count:
        chain.append(i)
        i = o.u16(pool + 8 * i + 4)
    planned = [i for i in chain if o.u8(pool + 8 * i + 3) == 0x1B][:2]
    if len(planned) < 2:
        raise RuntimeError(f'pirate {k}: fewer than two planned buildings in its pool ({chain})')
    empty = next((i for i in range(5) if not o.u32(to.SHIPS + 0xC + 4 * i)), None)
    print(f'pirate {k}: pool {count} entries, list {chain}, towers made of {planned}; empty slot {empty}')

    def during(o):
        cells = ((10, 20), (11, 21))
        for (x, y), i, kind, damage in zip(cells, planned, (0x30, 0x31), (0, 240)):
            e = pool + 8 * i
            w8(o, e, x)
            w8(o, e + 1, y)
            w8(o, e + 3, kind)
            w8(o, e + 6, damage)
            new_event_at(o, now, 0x25, w8=i, wA=0, wC=x, wE=y, b10=0xFE - k, b11=1)
        new_event_at(o, now, 0x25, w8=0, wA=0, wC=12, wE=22, b10=0xFE - k, b11=1)
        if empty is not None:
            new_event_at(o, now, 0x25, w8=0, wA=0, wC=10, wE=20, b10=0xFE - empty, b11=1)
        new_event_at(o, now, 0x25, w8=0, wA=0, wC=10, wE=20, b10=towns, b11=1)
    return during


def first_arrival(o):
    """For raid_arrival, in a process of its own: runs the world tick by tick
    until an AI merchant's ship at sea out of a convoy arrives at its
    destination, and prints «first arrival TICKS SHIP TOWN» if no other ship
    arrived there or entered its port (states 2, 3) before it; else «late
    arrival TICKS SHIP TOWN» (its port closed only for its arrival's tick, the
    others aren't touched: arrival_retouch)."""
    towns, _, merchants, ships = o.counts()
    convoys = o.u16(CONVOY_COUNT)
    sea = {s: o.u8(o.ship(s) + 0x38) for s in range(ships) if o.u16(o.ship(s) + 0x134) == 0xF
           and o.u16(o.ship(s) + 8) >= convoys and o.u8(o.ship(s)) < merchants
           and o.u16(o.merchant(o.u8(o.ship(s))) + 8) and o.u8(o.ship(s) + 0x38) < towns}
    arrived = []  # (tick, ship, town)
    for k in range(1, 600):
        o.advance_tick()
        for x in range(ships):
            st, town = o.u16(o.ship(x) + 0x134), o.u8(o.ship(x) + 0x39)
            if ((x in sea and (st != 0xF or town != 0xFF)) or st in (2, 3)) and all(x != y for _, y, _ in arrived):
                arrived.append((k, x, town))
        first = next(((k2, x, t2) for k2, x, t2 in arrived if x in sea), None)
        if first:
            ticks, s, t = first
            if t == sea[s] and not any(t2 == t and x != s for _, x, t2 in arrived):
                print(f'first arrival {ticks} {s} {t}')
            elif t == sea[s]:
                print(f'late arrival {ticks} {s} {t}')
                print(f'others before: {arrived}')
            else:
                print(f'others before: {arrived}')
            return


def arrival_of(o):
    """The first AI ship's arrival at sea (first_arrival in another process: the oracle is deterministic and two
    oracles don't fit in one process): (ticks, ship, town, late); late when another ship arrived at that town before
    it, so that the port must be closed and the ship made a raider only for the tick of its arrival."""
    import subprocess
    a = o.scenario_args
    out = subprocess.run([sys.executable, os.path.abspath(__file__), a.exe, a.save, os.devnull, 'raid_arrival',
                          '--barcos', a.barcos, '--cpr', a.cpr, '--first-arrival'], capture_output=True, text=True)
    for x in out.stdout.splitlines():
        if x.startswith('first arrival') or x.startswith('late arrival'):
            ticks, s, t = (int(v) for v in x.split()[2:5])
            return ticks, s, t, x.startswith('late')
    raise RuntimeError(f'the copy of the world found no arrival: {out.stdout[-500:]} {out.stderr[-500:]}')


def raid_arrival(o):
    """A raiding ship ([+0x15C]) arrives at a port closed by [0x2C8] & 0x200
    and without 0x4000000 (0x506E19): out of the lists, first of the sea's
    again, its town 0xFF (0x50BA80, the Hanse fleet's battle, finds no fleet).
    The AI ship at sea that arrives first (found running another copy of the
    world, arrival_of) gets the flag and its destination the closed port; the
    world runs tick by tick until it arrives, recording the ships' update
    0x506720. When another ship arrived at that town before it (save 1), both
    are set only for its arrival's tick."""
    ticks, s, t, late = arrival_of(o)
    print(f'ship {s} of {o.u8(o.ship(s))} arrives at {t} in {ticks} ticks{" (others there before)" if late else ""}')

    def during(o):
        def close():
            a = o.town(t)
            w32(o, a + 0x2C8, (o.u32(a + 0x2C8) | 0x200) & ~0x4000000)
            w8(o, o.ship(s) + 0x15C, 1)
        if not late:
            close()
        for k in range(ticks):
            if late and k == ticks - 1:
                close()
            o.advance_tick()
        print(f'after {k + 1} ticks: ship {s} state {o.u16(o.ship(s) + 0x134):#x}, town {o.u8(o.ship(s) + 0x39):#x}, '
              f'the sea\'s first {o.u16(to.SHIPS + 0xEA)}')
    return during


# --- The player's actions (docs/formats/operations.md) ---
# The operations the interface queues, run with the game's executor; where
# the viewer builds them, with its very bytes (player_ops, from P3_BUILD).

PLAYER_OPS = os.path.join(os.environ.get('P3_BUILD', 'build'), 'tools', 'oracle', 'player_ops')
IN_BARRELS = (0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1)  # 0x672C14
BEER, GRAIN, TIMBER = 3, 0, 0xB


def player_op(*args):
    """The operation as the viewer builds it (player_ops), or None."""
    import subprocess
    out = subprocess.run([PLAYER_OPS, *[str(a) for a in args]], check=True, capture_output=True, text=True)
    out = out.stdout.strip()
    return None if out == 'none' else bytes.fromhex(out)


def run_bytes(o, op):
    """An operation given as its 0x14 bytes, run with the game's executor."""
    operation(o, *struct.unpack('<5I', op))


def cdiv(a, b):
    """C's division (towards 0)."""
    q = abs(a) // abs(b)
    return q if (a < 0) == (b < 0) else -q


def player_docked_ship(o):
    """The player's first ship in port (state 0, not in a convoy) in a town
    with a market: (ship, town)."""
    towns, _, _, ships = o.counts()
    convoys = o.u16(to.SHIPS + 0xF6)
    s = o.u16(o.merchant(o.u32(to.PLAYER)) + 0xE)
    while s < ships:
        a = o.ship(s)
        t = o.u8(a + 0x39)
        if o.u16(a + 0x134) == 0 and t < towns and o.u16(a + 8) >= convoys and o.u32(o.town(t) + 0x76C) & 0x100:
            return s, t
        s = o.u16(a + 4)
    raise RuntimeError('the player has no ship in port')


def window_used(o, s):
    """The hold the market window says the ship uses, in steps of 200
    (0x4AA9D0: [+0x118] plus [+0x11C], rounded up by the equipment's rest)."""
    a = o.ship(s)
    used = o.s32(a + 0x118) + o.s32(a + 0x11C)
    rest = o.s32(a + 0x11C) - cdiv(o.s32(a + 0x11C), 200) * 200
    if rest:
        used = used - rest + 200
    return cdiv(used, 200)


def window_buy(o, s, t, ware, step):
    """The viewer's 0x02 for the market window's step (0 = all), from what the
    window reads of the town (0x4AAB10) and the ship."""
    unit = 200 if IN_BARRELS[ware] else 2000
    units = cdiv(o.s32(o.town(t) + 4 + 4 * ware), unit)
    return player_op('buy', step, units, ware, o.s32(o.ship(s) + 0x10), window_used(o, s), s, o.u32(to.PLAYER), t)


def window_sell(o, s, t, ware, step):
    unit = 200 if IN_BARRELS[ware] else 2000
    units = cdiv(o.s32(o.ship(s) + 0x54 + 4 * ware), unit)
    return player_op('sell', step, units, ware, s, o.u32(to.PLAYER), t)


def trade_state(o, s, t, ware):
    a = o.ship(s)
    return (f'cargo {o.s32(a + 0x54 + 4 * ware)}, store {o.s32(o.town(t) + 4 + 4 * ware)}, '
            f'money {o.s32(o.merchant(o.u8(a)))}, hold {o.s32(a + 0x118)}+{o.s32(a + 0x11C)}/{o.s32(a + 0x10)}')


def trade_window(o):
    """Operations 0x02 and 0x01 as the viewer queues them (the market window
    0x4A8CF0 with a ship and the town, its mode 1): the player's first ship
    in port, rich, buys beer (barrels) and grain (loads) with each step (1,
    5, 50, all), each clamped by the window to the town's store and the
    hold's room, and sells them back with each step; then grain with the hold
    filled with timber but for 3 loads (the window clamps to the room), and
    beer with the hold past its capacity (the window sends a negative amount:
    0x537450 buys nothing)."""
    s, t = player_docked_ship(o)
    m = o.u32(to.PLAYER)
    a = o.ship(s)
    print(f'ship {s} of the player {m} in town {t}')

    def queue(op, what):
        print(f'{what}: {op.hex() if op else "nothing"}')
        if op:
            run_bytes(o, op)

    def during(o):
        w32(o, o.merchant(m), 10000000)
        for ware in (BEER, GRAIN):
            w32(o, o.town(t) + 4 + 4 * ware, 70 * (200 if IN_BARRELS[ware] else 2000) + 150)
            for step in (1, 5, 50, 0):
                queue(window_buy(o, s, t, ware, step), f'buy {ware} by {step}')
                print('  ' + trade_state(o, s, t, ware))
            for step in (1, 5, 50, 0):
                queue(window_sell(o, s, t, ware, step), f'sell {ware} by {step}')
                print('  ' + trade_state(o, s, t, ware))
        room = o.s32(a + 0x10) - o.s32(a + 0x118) - o.s32(a + 0x11C)
        w32(o, a + 0x54 + 4 * TIMBER, o.s32(a + 0x54 + 4 * TIMBER) + max(0, room // 2000 * 2000 - 3 * 2000))
        direct(o, 0x5182B0, [], ecx=a)
        queue(window_buy(o, s, t, GRAIN, 50), 'grain by 50, room for 3 loads')
        print('  ' + trade_state(o, s, t, GRAIN))
        w32(o, a + 0x54 + 4 * TIMBER, o.s32(a + 0x54 + 4 * TIMBER) + 10 * 2000)
        direct(o, 0x5182B0, [], ecx=a)
        queue(window_buy(o, s, t, BEER, 5), 'beer by 5, the hold past its capacity')
        print('  ' + trade_state(o, s, t, BEER))
    return during


def trade_branches(o):
    """Operations 0x02 (0x537450, with 0x52F3E0) and 0x01 (0x537340), made
    here, on the player's first ship in port. Buying: a town out of the
    world, another town than the ship's, a town the merchant is banned from
    ([+0x608]), more beer and grain than the store has (rounded down to
    barrels and loads), the hold past its capacity (the room < 0), with 100
    coins (0x52F3E0 cuts it down), with 0 coins, with 1 coin (cut to 0),
    1100 of beer with a coin less than its price (0x52E430: the amount isn't a
    barrel, so the loop ends past it), 40 barrels with a coin less than the
    price of 10 and a store of 41 (the loop climbs barrel by barrel), 0 and
    −200, and a normal one. Selling: a town out, a banned town,
    more than the cargo (clamped to it, written back into the operation), in
    another town than the ship's (0x537340 doesn't look), and a normal one."""
    s, t = player_docked_ship(o)
    m = o.u32(to.PLAYER)
    towns, _, _, _ = o.counts()
    a = o.ship(s)
    other = (t + 1) % towns
    print(f'ship {s} of the player {m} in town {t}; other town {other}')

    def trade(code, amount, ware, town):
        operation(o, code, amount & 0xFFFFFFFF, ware | s << 16, m | town << 16)
        print(f'  {code:#x} {amount} of {ware} in {town}: {trade_state(o, s, t, ware)}')

    def during(o):
        w32(o, o.merchant(m), 10000000)
        for ware in (BEER, GRAIN):
            w32(o, o.town(t) + 4 + 4 * ware, 40 * 2000)
        trade(2, 400, BEER, towns)
        trade(2, 400, BEER, other)
        bans = o.u32(o.merchant(m) + 0x608)
        w32(o, o.merchant(m) + 0x608, bans | 1 << (t & 31))
        trade(2, 400, BEER, t)
        trade(1, 200, BEER, t)
        w32(o, o.merchant(m) + 0x608, bans)
        w32(o, o.town(t) + 4 + 4 * BEER, 3 * 200 + 50)
        trade(2, 10 * 200, BEER, t)
        w32(o, o.town(t) + 4 + 4 * GRAIN, 2 * 2000 + 700)
        trade(2, 10 * 2000, GRAIN, t)
        for ware in (BEER, GRAIN):
            w32(o, o.town(t) + 4 + 4 * ware, 40 * 2000)
        timber = o.s32(a + 0x54 + 4 * TIMBER)
        w32(o, a + 0x54 + 4 * TIMBER, timber + o.s32(a + 0x10) + 4000)
        direct(o, 0x5182B0, [], ecx=a)
        trade(2, 400, BEER, t)
        w32(o, a + 0x54 + 4 * TIMBER, timber)
        direct(o, 0x5182B0, [], ecx=a)
        for money in (100, 0, 1):
            w32(o, o.merchant(m), money)
            trade(2, 10 * 200, BEER, t)
        w32(o, o.merchant(m), direct(o, 0x52E430, [BEER, t, 1100], ecx=0x6DE3D8) - 1)
        trade(2, 1100, BEER, t)
        w32(o, o.town(t) + 4 + 4 * BEER, 41 * 200)
        w32(o, o.merchant(m), direct(o, 0x52E430, [BEER, t, 10 * 200], ecx=0x6DE3D8) - 1)
        print(f'  prices of 10 and 40 barrels: {direct(o, 0x52E430, [BEER, t, 10 * 200], ecx=0x6DE3D8)}, '
              f'{direct(o, 0x52E430, [BEER, t, 40 * 200], ecx=0x6DE3D8)}')
        trade(2, 40 * 200, BEER, t)
        w32(o, o.town(t) + 4 + 4 * BEER, 40 * 2000)
        w32(o, o.merchant(m), 10000000)
        trade(2, 0, BEER, t)
        trade(2, -200, BEER, t)
        trade(2, 5 * 200, BEER, t)
        trade(1, 200, BEER, towns)
        trade(1, 1000 * 200, BEER, t)
        trade(2, 5 * 200, BEER, t)
        trade(1, 2 * 200, BEER, other)
        trade(1, 200, BEER, t)
    return during


def town_captain(o, t):
    """A captain at the head of town t's list ([0x82E], next [+0x0]); if the
    list is empty, the head of another town's list moved there."""
    towns, _, _, _ = o.counts()
    captains = o.u16(to.SHIPS + 0xF2)
    if o.u16(o.town(t) + 0x82E) >= captains:
        u = next(u for u in range(towns) if o.u16(o.town(u) + 0x82E) < captains)
        c = o.u16(o.town(u) + 0x82E)
        w16(o, o.town(u) + 0x82E, o.u16(captain_at(o, c)))
        w16(o, captain_at(o, c), 0xFFFF)
        w16(o, o.town(t) + 0x82E, c)
    return o.u16(o.town(t) + 0x82E)


def crew_ops(o):
    """Operations 0x04 (0x537C20), 0x05 (0x537DD0), 0x06 (0x537EE0) and 0x07
    (0x538000), with the viewer's bytes, on the player's first ship in port.
    Hiring sailors: 5; 1000 (cut to the room, 0x5184F0); 5 with a quota of 2
    in the town ([+0xF0 + town] of the merchant); with 1 unemployed in the
    town (0x4F6CA0 gives 0); with the ship's owner 0xFF and 3 unemployed (no
    quota: the town's unemployed to 0); in a convoy (its [+0x18]); a ship
    without sailors and with a captain, and without either; at sea; a ship
    out. Dismissing sailors: 3 of them, all of them exactly, more than it has,
    with a low morale (it goes below 0), with none, at sea. The captain: the
    ship's captain dismissed (to the town's list, 0x526B00), dismissed again
    (none), hired back from the list (the letter 0x04), one not in the list,
    a town out (a ship without a captain hires one first); the ship made the
    guide of a convoy (the game's 0x509B90, as operation 0x15) dismissing its
    captain (the convoy breaks up, 0x50A580)."""
    s, t = player_docked_ship(o)
    m = o.u32(to.PLAYER)
    a = o.ship(s)
    towns, _, _, ships = o.counts()
    print(f'ship {s} of the player {m} in town {t}: sailors {o.u16(a + 0x40)}, captain {o.u16(a + 0x42)}')

    def run(*args):
        run_bytes(o, player_op(*args))
        print(f'  {args}: sailors {o.u16(a + 0x40)}, morale {o.u16(a + 0x3E):#x}, captain {o.u16(a + 0x42)}, '
              f'unemployed {o.u32(o.town(t) + 0x2E4)}, quota {o.u8(o.merchant(m) + 0xF0 + t)}')

    def during(o):
        w32(o, o.town(t) + 0x2E4, 200)
        w8(o, o.merchant(m) + 0xF0 + t, 100)
        run('hire-sailors', s, 5)
        run('hire-sailors', s, 1000)
        run('dismiss-sailors', s, 40)
        w8(o, o.merchant(m) + 0xF0 + t, 2)
        run('hire-sailors', s, 5)
        w8(o, o.merchant(m) + 0xF0 + t, 100)
        w32(o, o.town(t) + 0x2E4, 1)
        run('hire-sailors', s, 5)
        w32(o, o.town(t) + 0x2E4, 3)
        w8(o, a, 0xFF)
        run('hire-sailors', s, 20)
        w8(o, a, m)
        w32(o, o.town(t) + 0x2E4, 200)
        convoy = o.u16(a + 8)
        w16(o, a + 8, 0)
        run('hire-sailors', s, 2)
        w16(o, a + 8, convoy)
        w16(o, a + 0x3E, 0x4000)
        run('dismiss-sailors', s, 3)
        run('dismiss-sailors', s, o.u16(a + 0x40))
        run('hire-sailors', s, 10)
        w16(o, a + 0x3E, 10)
        run('dismiss-sailors', s, 4)
        run('dismiss-sailors', s, 100)
        run('dismiss-sailors', s, 1)
        run('hire-sailors', s, 8)
        if o.u16(a + 0x42) >= o.u16(to.SHIPS + 0xF2):
            run('hire-captain', town_captain(o, t), s, t)
        run('dismiss-captain', s)
        c = o.u16(o.town(t) + 0x82E)
        run('dismiss-captain', s)
        run('dismiss-sailors', s, 100)
        run('hire-sailors', s, 6)
        run('hire-captain', (c + 1) % o.u16(to.SHIPS + 0xF2), s, t)
        run('hire-captain', c, s, towns)
        run('hire-captain', c, s, t)
        run('dismiss-sailors', s, 100)
        run('hire-sailors', s, 6)
        state = o.u16(a + 0x134)
        w16(o, a + 0x134, 4)
        run('dismiss-sailors', s, 1)
        run('dismiss-captain', s)
        w16(o, a + 0x134, state)
        w8(o, a + 0x39, 0xFF)
        run('hire-sailors', s, 1)
        w8(o, a + 0x39, t)
        run('hire-sailors', ships, 1)
        # What 0x519C40 asks of a convoy's guide: 20 sailors, guns (7 pieces of
        # the type 1: [+0x120] 70), a captain (it has one).
        w16(o, a + 0x40, 25)
        for i in range(7):
            w8(o, a + 0x13C + i, 1)
        direct(o, 0x5182B0, [], ecx=a)
        direct(o, 0x509B90, [s, 0, 5, t], ecx=to.SHIPS)
        k = o.u16(a + 8)
        print(f'  convoy {k} guided by {o.u16(o.u32(to.SHIPS + 8) + 0x3C * k + 0x10)}')
        run('dismiss-captain', s)
        print(f'  the ship\'s convoy {o.u16(a + 8)}')
    return during


def repair_ops(o):
    """Operation 0x03 (0x5357AA → 0x52ACD0, the cost 0x52A9E0) with the
    viewer's bytes, on the player's first ship in port: without damage (the
    letter 0x02), half its hull and rich (pays: the state 4), poor (the letter
    with the cost), damaged less than the hull / 100 (the least), the town's
    [+0x818] at 2, 5, 10, 15 and 25 (the five rates); the ship given to an AI
    merchant in debts (pays anyway) and to no one (free); the ship made the
    guide of a convoy (0x509B90) rich and poor (the sums over its ships); a
    ship out, at sea and in the shipyard (state 4: nothing). Operation 0x1D
    (0x539990, with 0x52A950) on that convoy: its town out, in its town (paid),
    in the state 4 (nothing), a convoy out."""
    s, t = player_docked_ship(o)
    m = o.u32(to.PLAYER)
    a = o.ship(s)
    _, _, merchants, ships = o.counts()
    ai = next(x for x in range(merchants) if x != m and o.u16(o.merchant(x) + 8) != 0)
    hull = o.s32(a + 0x14)
    print(f'ship {s} of the player {m} (its [+0x8] {o.u16(o.merchant(m) + 8)}) in town {t}: hull {hull}, '
          f'left {o.s32(a + 0x18)}, capacity {o.s32(a + 0x10)}, the town\'s [+0x818] {struct.unpack("<f", bytes(o.uc.mem_read(o.town(t) + 0x818, 4)))[0]}; '
          f'AI merchant {ai}')

    def repair(left, money=10000000, who=None):
        w32(o, a + 0x18, left)
        w16(o, a + 0x134, 0)
        w32(o, o.merchant(m if who is None else who), money)
        run_bytes(o, player_op('repair', s))
        print(f'  left {left}, money {money}: state {o.u16(a + 0x134)}, money {o.s32(o.merchant(m))}')

    def during(o):
        repair(hull)
        repair(hull // 2)
        repair(hull // 2, 10)
        repair(hull - 1)
        level = o.u32(o.town(t) + 0x818)
        for v in (2.0, 5.0, 10.0, 15.0, 25.0):
            o.uc.mem_write(o.town(t) + 0x818, struct.pack('<f', v))
            repair(hull // 3)
        w32(o, o.town(t) + 0x818, level)
        w8(o, a, ai)
        repair(hull // 2, 0, ai)
        w8(o, a, 0xFF)
        repair(hull // 2)
        w8(o, a, m)
        w16(o, a + 0x40, 25)
        w16(o, a + 0x134, 0)
        w32(o, a + 0x18, hull)
        if o.u16(a + 0x42) >= o.u16(to.SHIPS + 0xF2):
            run_bytes(o, player_op('hire-captain', town_captain(o, t), s, t))
        for i in range(7):
            w8(o, a + 0x13C + i, 1)
        direct(o, 0x5182B0, [], ecx=a)
        direct(o, 0x509B90, [s, 0, 5, t], ecx=to.SHIPS)
        print(f'  convoy {o.u16(a + 8)}')
        repair(hull // 2)
        repair(hull // 2, 10)
        repair(hull)
        k = o.u16(a + 8)
        c = o.u32(to.SHIPS + 8) + 0x3C * k
        w32(o, a + 0x18, hull // 2)
        towns = o.counts()[0]
        for convoy, town, state in ((k, towns, 0), (k, t, 0), (k, t, 4), (o.u16(to.SHIPS + 0xF6), t, 0)):
            w16(o, c + 0x1C, town)
            w16(o, c + 0x12, state)
            run_bytes(o, player_op('repair-convoy', convoy))
            print(f'  convoy {convoy} in {town}, state {state}: now {o.u16(c + 0x12)}, money {o.s32(o.merchant(m))}')
        w16(o, c + 0x1C, t)
        run_bytes(o, player_op('repair', ships))
        w8(o, a + 0x39, 0xFF)
        run_bytes(o, player_op('repair', s))
        w8(o, a + 0x39, t)
        w16(o, a + 0x134, 4)
        run_bytes(o, player_op('repair', s))
    return during


WINDOW_SHIPS = to.SENTINEL + 0xF80  # the copier of 0x504AC0's list (probes_unchecked.cpp kWindowShips)
FPU_POP = to.SENTINEL + 0xF70  # fstp st(0); ret


def window_ships_copier(o):
    """The copier at WINDOW_SHIPS (stdcall: merchant, town, mode, 0): 0x504AC0
    on the ships' object, and its list (a u16 count and the ships, at most
    126) copied into SCRATCH, so that the state holds it (the game's list is
    on the heap); a count 0 for no list. tick_probe's probe makes p3vita's."""
    code = bytes([0x56, 0x57])  # push esi; push edi
    code += bytes([0xFF, 0x74, 0x24, 0x18]) * 4  # the four arguments
    code += bytes([0xB9]) + struct.pack('<I', to.SHIPS)  # mov ecx, the ships' object
    code += bytes([0xE8]) + struct.pack('<i', 0x504AC0 - (WINDOW_SHIPS + len(code) + 5))
    code += bytes([0x85, 0xC0])  # test eax, eax
    copy = bytes([0x0F, 0xB7, 0x08])  # movzx ecx, word [eax]
    copy += bytes([0x83, 0xF9, 0x7E, 0x76, 0x05])  # cmp ecx, 126; jbe +5
    copy += bytes([0xB9]) + struct.pack('<I', 0x7E)  # mov ecx, 126
    copy += bytes([0x41, 0x89, 0xC6])  # inc ecx; mov esi, eax
    copy += bytes([0xBF]) + struct.pack('<I', SCRATCH)  # mov edi, SCRATCH
    copy += bytes([0xFC, 0xF3, 0x66, 0xA5])  # cld; rep movsw
    empty = bytes([0x66, 0xC7, 0x05]) + struct.pack('<IH', SCRATCH, 0)  # mov word [SCRATCH], 0
    copy += bytes([0xEB, len(empty)])  # jmp past the empty case
    code += bytes([0x74, len(copy)]) + copy + empty  # jz the empty case
    code += bytes([0x5F, 0x5E, 0xC2, 0x10, 0x00])  # pop edi; pop esi; ret 0x10
    assert len(code) <= 0x80
    o.uc.mem_write(WINDOW_SHIPS, code)
    o.uc.mem_write(FPU_POP, bytes([0xDD, 0xD8, 0xC3]))


def snippet(o, begin, end, esi):
    """The game's code [begin, end) with esi (a window), on a stack of its own;
    eax at the end."""
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ESI, UC_X86_REG_ESP, UC_X86_REG_FPCW
    uc = o.uc
    uc.reg_write(UC_X86_REG_ESP, to.STACK + to.STACK_SIZE - 0x2000)
    uc.reg_write(UC_X86_REG_ESI, esi)
    uc.reg_write(UC_X86_REG_FPCW, 0x27F)
    uc.emu_start(begin, end)
    return uc.reg_read(UC_X86_REG_EAX)


def town_captains(o, t, n):
    """At least n captains at the head of town t's list ([0x82E], next
    [+0x0]), taken from the heads of other towns' lists; their indices."""
    towns = o.counts()[0]
    count = o.u16(to.SHIPS + 0xF2)
    chain = []
    c = o.u16(o.town(t) + 0x82E)
    while c < count:
        chain.append(c)
        c = o.u16(captain_at(o, c))
    for u in range(towns):
        if len(chain) >= n:
            break
        if u == t or o.u16(o.town(u) + 0x82E) >= count:
            continue
        c = o.u16(o.town(u) + 0x82E)
        w16(o, o.town(u) + 0x82E, o.u16(captain_at(o, c)))
        w16(o, captain_at(o, c), o.u16(o.town(t) + 0x82E))
        w16(o, o.town(t) + 0x82E, c)
        chain.insert(0, c)
    return chain


def ship_windows(o):
    """What the player's windows list and offer (player_actions.hpp), the
    game's functions recorded and repeated with p3vita's: 0x504AC0 (through
    the copier WINDOW_SHIPS) in its modes 0 (the tavern's sailors), 1 (the
    shipyard's repairs) and 3 (the tavern's captains) on the player's first
    ship in port: as loaded, marked [+0x136] & 1, in the state 2, damaged and
    whole, without a captain, made a convoy's guide (0x509B90) with another
    ship of his and without captain chained into the convoy, its flags and
    state, the members damaged or not; a merchant out of the world. The
    tavern's captain 0x5269A0: one kept for the player behind a busy one, one
    kept for no one, none free, an empty list. 0x4F6CA0 with 1, 3 and 200
    unemployed and quotas 2 and 100. The tavern's window run as the game: its
    roll (0x5CC479, with the oracle's rand()) and its count
    (0x5D03A5..0x5D03ED) under and over 50, compared with player_ops
    tavern-sailors (MISMATCH if they differ). Then the viewer's
    bytes with the window's choices: hiring the offered captain for the
    listed ship and the shown number of sailors, dismissing all of them with
    the slider, the repair of the convoy (0x1D)."""
    s, t = player_docked_ship(o)
    m = o.u32(to.PLAYER)
    a = o.ship(s)
    towns, _, merchants, ships = o.counts()
    captains = o.u16(to.SHIPS + 0xF2)
    print(f'ship {s} of the player {m} in town {t}: captain {o.u16(a + 0x42)}, sailors {o.u16(a + 0x40)}')
    window_ships_copier(o)

    def lists(what):
        got = []
        for mode in (0, 1, 3):
            direct(o, WINDOW_SHIPS, [m, t, mode, 0])
            n = o.u16(SCRATCH)
            got.append([o.u16(SCRATCH + 2 + 2 * k) for k in range(min(n, 126))])
        print(f'  {what}: sailors {got[0]}, repairs {got[1]}, captains {got[2]}')

    def offered(what):
        c = direct(o, 0x5269A0, [m], ecx=o.town(t)) & 0xFFFF
        print(f'  {what}: the tavern offers the captain {c:#x}')
        return c

    def allowed(what):
        n = direct(o, 0x4F6CA0, [t], ecx=o.merchant(m)) & 0xFF
        print(f'  {what}: 0x4F6CA0 allows {n}')
        return n

    def tavern(what):
        """The tavern's window as the game runs it: the roll, then the count."""
        window = o.alloc(0x2200)
        w32(o, window + 0x1BFC, t)
        snippet(o, 0x5CC479, 0x5CC4AC, window)
        o.call(FPU_POP, [])
        r = struct.unpack('<f', bytes(o.uc.mem_read(window + 0x1CA0, 4)))[0]
        bits = o.u32(window + 0x1CA0)
        shown = snippet(o, 0x5D03A5, 0x5D03F3, window)  # up to the next text, eax kept
        n = direct(o, 0x4F6CA0, [t], ecx=o.merchant(m)) & 0xFF
        rand_value = round(r * 32768)
        out = player_op_text('tavern-sailors', n, rand_value)
        mine = f'{bits:08x} {shown}'
        print(f'  {what}: the roll {r} ({bits:08x}), 0x4F6CA0 {n}, shown {shown}; p3vita {out}'
              + ('' if out == mine else '  MISMATCH'))
        return shown

    def during(o):
        lists('as loaded')
        hull = o.s32(a + 0x14)
        w32(o, a + 0x18, hull // 2)
        lists('half its hull')
        w8(o, a + 0x136, o.u8(a + 0x136) | 1)
        lists('marked [+0x136] & 1')
        w8(o, a + 0x136, o.u8(a + 0x136) & 0xFE)
        state = o.u16(a + 0x134)
        w16(o, a + 0x134, 2)
        lists('in the state 2')
        w16(o, a + 0x134, 1)
        lists('in the state 1')
        w16(o, a + 0x134, state)
        w32(o, a + 0x18, hull)
        lists('whole')
        captain = o.u16(a + 0x42)
        w16(o, a + 0x42, 0xFFFF)
        lists('without a captain')
        # The tavern's captain.
        chain = town_captains(o, t, 3)
        print(f'  the town\'s captains {chain[:4]}')
        for c in chain:
            w8(o, captain_at(o, c) + 0x8, 0)
            w8(o, captain_at(o, c) + 0xF, 0xFF)
        offered('all free, kept for no one')
        w8(o, captain_at(o, chain[0]) + 0x8, 0x21)
        offered('the first busy')
        w8(o, captain_at(o, chain[1]) + 0xF, m)
        w8(o, captain_at(o, chain[0]) + 0xF, m)
        offered('the second kept for the player, the first busy too')
        w8(o, captain_at(o, chain[1]) + 0xF, (m + 1) % merchants)
        w8(o, captain_at(o, chain[0]) + 0xF, 0xFF)
        offered('the second kept for another merchant')
        for c in chain:
            w8(o, captain_at(o, c) + 0x8, 0x40)
        offered('none free')
        for c in chain:
            w8(o, captain_at(o, c) + 0x8, 0)
        head = o.u16(o.town(t) + 0x82E)
        w16(o, o.town(t) + 0x82E, 0xFFFF)
        offered('an empty list')
        w16(o, o.town(t) + 0x82E, head)
        hired = offered('the list back')
        # The sailors the tavern lets him hire.
        for unemployed, quota in ((1, 100), (3, 2), (200, 30), (200, 100)):
            w32(o, o.town(t) + 0x2E4, unemployed)
            w8(o, o.merchant(m) + 0xF0 + t, quota)
            allowed(f'{unemployed} unemployed, quota {quota}')
            tavern(f'{unemployed} unemployed, quota {quota}')
        # The viewer's bytes with the window's choices.
        listed = []
        direct(o, WINDOW_SHIPS, [m, t, 3, 0])
        listed = [o.u16(SCRATCH + 2 + 2 * k) for k in range(o.u16(SCRATCH))]
        if listed and hired < captains:
            run_bytes(o, player_op('hire-captain', hired, listed[0], t))
            print(f'  hired the captain {hired:#x} for {listed[0]}: its captain {o.u16(o.ship(listed[0]) + 0x42):#x}')
        lists('the captain hired')
        shown = tavern('before hiring')
        run_bytes(o, player_op('hire-sailors', s, shown))
        print(f'  hired {shown}: sailors {o.u16(a + 0x40)}')
        run_bytes(o, player_op('dismiss-sailors', s, o.u16(a + 0x40)))
        print(f'  dismissed all: sailors {o.u16(a + 0x40)}')
        # A convoy guided by the ship (0x509B90, as operation 0x15), with
        # another ship of the player without a captain chained into it.
        if o.u16(a + 0x42) >= captains:
            w16(o, a + 0x42, captain)
        w16(o, a + 0x40, 25)
        for i in range(7):
            w8(o, a + 0x13C + i, 1)
        direct(o, 0x5182B0, [], ecx=a)
        direct(o, 0x509B90, [s, 0, 5, t], ecx=to.SHIPS)
        k = o.u16(a + 8)
        c = o.u32(to.SHIPS + 8) + 0x3C * k
        print(f'  convoy {k}: guide {o.u16(c + 0x10)}, first {o.u16(c + 0xA)}, flags {o.u8(c + 0x14):#x}, '
              f'state {o.u16(c + 0x12)}')
        other = next(x for x in range(ships) if x != s and o.u8(o.ship(x)) < merchants)
        b = o.ship(other)
        saved = (o.u8(b), o.u16(b + 0x42), o.u16(b + 6), o.u16(a + 6), o.s32(b + 0x18))
        w8(o, b, m)
        w16(o, b + 0x42, 0xFFFF)
        w16(o, b + 6, 0xFFFF)
        w16(o, a + 6, other)
        w32(o, b + 0x18, o.s32(b + 0x14))
        lists(f'a convoy with the ship {other}, whole')
        w32(o, b + 0x18, o.s32(b + 0x14) // 2)
        lists(f'the ship {other} damaged')
        flags = o.u8(c + 0x14)
        w8(o, c + 0x14, flags ^ 2)
        lists('the convoy\'s flag 2 changed')
        w8(o, c + 0x14, flags | 2)
        w16(o, c + 0x12, 2)
        lists('the convoy in the state 2')
        w16(o, c + 0x12, 0)
        lists('the convoy in the state 0')
        w16(o, b + 0x42, o.u16(a + 0x42))
        lists(f'the ship {other} with a captain')
        w8(o, a + 0x136, o.u8(a + 0x136) | 1)
        lists('the guide marked [+0x136] & 1')
        w8(o, a + 0x136, o.u8(a + 0x136) & 0xFE)
        w8(o, b, saved[0])
        w16(o, b + 0x42, saved[1])
        w16(o, b + 6, saved[2])
        w16(o, a + 6, saved[3])
        w32(o, b + 0x18, saved[4])
        w32(o, a + 0x18, hull // 2)
        lists('the convoy alone, its guide damaged')
        run_bytes(o, player_op('repair-convoy', k))
        print(f'  the convoy repaired: its state {o.u16(c + 0x12)}')
        direct(o, WINDOW_SHIPS, [merchants, t, 0, 0])
        print(f'  a merchant out of the world: {o.u16(SCRATCH)} ships')
    return during


def office_window(o, f):
    """What the market window keeps of the office f (0x4AA7C0): each ware's
    units (/ 200 or / 2000, the weapons / 10), what it holds in steps of 200
    (all its stock and its [+0x2E8], rounded up to 200) and its room ([+0x2EC]
    · 2000, 0x4A97B6)."""
    a = o.office(f)
    units = [cdiv(o.s32(a + 4 + 4 * w), (200 if IN_BARRELS[w] else 2000) if w < 20 else 10) for w in range(24)]
    used = sum(o.s32(a + 4 + 4 * w) for w in range(24)) + o.s32(a + 0x2E8)
    rest = used - cdiv(used, 200) * 200
    if rest:
        used = used - rest + 200
    return units, cdiv(used, 200), o.u16(a + 0x2EC) * 2000


def game_office_window(o, t):
    """The same from the game: 0x4AA7C0 on a window of the player's office in
    town t (its redraw 0x4AA2A0 skipped): its units [+0x588] and [+0x5E8]."""
    from unicorn.x86_const import UC_X86_REG_EIP, UC_X86_REG_ESP
    window = o.alloc(0x900)
    w32(o, window + 0xF8, t)

    def skip(uc, address, size, data):
        esp = uc.reg_read(UC_X86_REG_ESP)
        uc.reg_write(UC_X86_REG_EIP, o.u32(esp))
        uc.reg_write(UC_X86_REG_ESP, esp + 4)
    hook = o.uc.hook_add(UC_HOOK_CODE, skip, begin=0x4AA2A0, end=0x4AA2A0)
    o.uc.ctl_remove_cache(0x4AA2A0, 0x4AA2A1)
    o.call(0x4AA7C0, [], ecx=window)
    o.uc.hook_del(hook)
    return [o.s32(window + 0x588 + 4 * w) for w in range(24)], o.s32(window + 0x5E8)


def office_trade(o):
    """The player's office (operations 0x08, 0x58, 0x59): in the town of his
    first ship in port, an office (operation 0x5D if he has none) with beer
    and grain at average prices. The market window as the game computes it
    (0x4AA7C0, compared with office_window). The viewer's bytes (player_ops):
    mode 2, the office buys (0x59) and sells (0x58) beer and grain with each
    step; mode 0, the ship takes from the office and gives to it (0x08) with
    each step. Then operations made here for the handlers' branches: 0x08 with
    a ship out, a ware out, the ship in the shipyard, a ship whose owner has
    no office there, a negative amount, the hold full, more than the office
    and the ship have, into an empty hold and an empty office, swords (pieces
    of 10) both ways; 0x58 with a ware out, 0 and a negative amount, no
    office, more than the stock, the office's town out; 0x59 the same, more
    than the store (rounded), short of money (0x52F3E0), without money, a
    store of 0 (an empty stock: 0x5007A0 keeps the average)."""
    s, t = player_docked_ship(o)
    m = o.u32(to.PLAYER)
    a = o.ship(s)
    towns, _, merchants, ships = o.counts()
    SWORD = 0x14

    def state(what):
        f = facilities_office(o, m, t)
        b = o.office(f)
        print(f'  {what}: office beer {o.s32(b + 4 + 4 * BEER)} at {f32(b + 0x3B8 + 4 * BEER):.4f}, '
              f'grain {o.s32(b + 4 + 4 * GRAIN)}; ship beer {o.s32(a + 0x54 + 4 * BEER)} at '
              f'{f32(a + 0xB4 + 4 * BEER):.4f}, grain {o.s32(a + 0x54 + 4 * GRAIN)}; money {o.s32(o.merchant(m))}')

    def queue(op, what):
        print(f'{what}: {op.hex() if op else "nothing"}')
        if op:
            run_bytes(o, op)
            state(what)

    def by_hand(code, amount, ware, index, last):
        """0x08 {amount, ship, ware, player, direction}; 0x58, 0x59 {amount, ware, merchant, player, town}."""
        operation(o, code, amount & 0xFFFFFFFF, index | ware << 16 if code == 8 else ware | index << 16, m | last << 16)

    def f32(address):
        return struct.unpack('<f', bytes(o.uc.mem_read(address, 4)))[0]

    def during(o):
        w32(o, o.merchant(m), 50000000)
        if facilities_office(o, m, t) is None:
            store(o, t)
            operation(o, 0x5D, m, t)
        f = facilities_office(o, m, t)
        b = o.office(f)
        print(f'office {f} of the player {m} in town {t}; ship {s}')
        for ware, n in ((BEER, 30 * 200 + 77), (GRAIN, 12 * 2000 + 900)):
            w32(o, b + 4 + 4 * ware, n)
            o.uc.mem_write(b + 0x3B8 + 4 * ware, struct.pack('<f', 3.25 if ware == BEER else 41.5))
            w32(o, o.town(t) + 4 + 4 * ware, 70 * (200 if IN_BARRELS[ware] else 2000) + 150)
        mine = office_window(o, f)
        game = game_office_window(o, t)
        print(f'  the window: units {mine[0][:4]}, holds {mine[1]}, room {mine[2]}; the game {game[0][:4]}, holds {game[1]}'
              + ('' if (mine[0], mine[1]) == game else '  MISMATCH'))
        state('at first')
        for ware in (BEER, GRAIN):
            for step in (1, 5, 50, 0):
                units, used, room = office_window(o, f)
                tu = cdiv(o.s32(o.town(t) + 4 + 4 * ware), 200 if IN_BARRELS[ware] else 2000)
                queue(player_op('office-buy', step, tu, ware, room, used, m, m, t), f'office buys {ware} by {step}')
            for step in (1, 5, 50, 0):
                units = office_window(o, f)[0]
                queue(player_op('office-sell', step, units[ware], ware, m, m, t), f'office sells {ware} by {step}')
        for ware, n in ((BEER, 40 * 200), (GRAIN, 15 * 2000)):
            w32(o, b + 4 + 4 * ware, n)
        for ware in (BEER, GRAIN):
            for step in (1, 5, 50, 0):
                units = office_window(o, f)[0]
                queue(player_op('office-to-ship', step, units[ware], ware, o.s32(a + 0x10), window_used(o, s), s, m),
                      f'office to ship {ware} by {step}')
            for step in (1, 5, 50, 0):
                su = cdiv(o.s32(a + 0x54 + 4 * ware), 200 if IN_BARRELS[ware] else 2000)
                queue(player_op('ship-to-office', step, su, ware, s, m), f'ship to office {ware} by {step}')
        # 0x08's branches.
        by_hand(8, 200, BEER, ships, 1)
        by_hand(8, 200, 0x18, s, 1)
        w16(o, a + 0x134, 4)
        by_hand(8, 200, BEER, s, 1)
        w16(o, a + 0x134, 0)
        owner = o.u8(a)
        other = next(x for x in range(merchants) if x != m and facilities_office(o, x, t) is None)
        w8(o, a, other)
        by_hand(8, 200, BEER, s, 1)
        w8(o, a, owner)
        by_hand(8, -400, BEER, s, 1)
        state('-400 from the office')
        timber = o.s32(a + 0x54 + 4 * TIMBER)
        w32(o, a + 0x54 + 4 * TIMBER, timber + o.s32(a + 0x10) + 4000)
        direct(o, 0x5182B0, [], ecx=a)
        by_hand(8, 400, BEER, s, 1)
        state('the hold full')
        w32(o, a + 0x54 + 4 * TIMBER, timber)
        direct(o, 0x5182B0, [], ecx=a)
        by_hand(8, 1000 * 200, BEER, s, 1)
        state('more than the office has')
        by_hand(8, 1000 * 200, BEER, s, 0)
        state('more than the ship has')
        by_hand(8, 3 * 200, BEER, s, 1)
        by_hand(8, 2 * 2000, GRAIN, s, 0)
        w32(o, b + 4 + 4 * SWORD, 57)
        o.uc.mem_write(b + 0x3B8 + 4 * SWORD, struct.pack('<f', 12.5))
        by_hand(8, 37, SWORD, s, 1)
        by_hand(8, 13, SWORD, s, 0)
        print(f'  swords: office {o.s32(b + 4 + 4 * SWORD)}, ship {o.s32(a + 0x54 + 4 * SWORD)}')
        # 0x58's and 0x59's branches.
        for code in (0x58, 0x59):
            by_hand(code, 200, 0x18, m, t)
            by_hand(code, 0, BEER, m, t)
            by_hand(code, -200, BEER, m, t)
            by_hand(code, 200, BEER, other, t)
        by_hand(0x58, 1000 * 200, BEER, m, t)
        state('the office sells more than it has')
        w8(o, b + 0x2C6, towns)
        by_hand(0x58, 200, GRAIN, m, t)
        w8(o, b + 0x2C6, t)
        w32(o, o.town(t) + 4 + 4 * BEER, 7 * 200 + 50)
        w32(o, o.town(t) + 4 + 4 * GRAIN, 3 * 2000 + 700)
        by_hand(0x59, 20 * 200, BEER, m, t)
        by_hand(0x59, 20 * 2000, GRAIN, m, t)
        state('more than the store')
        w32(o, o.town(t) + 4 + 4 * BEER, 40 * 2000)
        w32(o, o.merchant(m), 300)
        by_hand(0x59, 20 * 200, BEER, m, t)
        state('300 coins')
        w32(o, o.merchant(m), 0)
        by_hand(0x59, 20 * 200, BEER, m, t)
        w32(o, o.merchant(m), 50000000)
        w32(o, b + 4 + 4 * 2, 0)
        w32(o, o.town(t) + 4 + 4 * 2, 0)
        by_hand(0x59, 2000, 2, m, t)
        print(f'  a store of 0: the office\'s {o.s32(b + 4 + 4 * 2)} at {f32(b + 0x3B8 + 4 * 2)}')
    return during


PIECE_WEIGHT = (1000, 1000, 2000, 2000, 2000, 1000)  # 0x672CB4


def office_artillery(o):
    """Operation 0x09 (0x538210) and the window's mode 5: in the town of the
    player's first ship in port, his office (0x5D if he has none) with pieces
    of each type (one with a weight not a multiple of its piece's), hand
    weapons [+0x2BC] and weapons (the wares 0x14..0x17, a crossbow store of 0);
    the ship with free slots, two of them marked 7. The viewer's bytes
    (player_ops artillery, weapons-to-ship, weapons-to-office) with each step:
    each type and the hand weapons to the ship and back, the weapons both ways
    (the crossbows, none in the office: the game's bug, a 0x09 of hand weapons
    counted by the ship's pieces of type 4). Then by hand: a town out, a ship
    out, the ship in the shipyard, in another town, without an office (its
    owner changed), hand weapons from the ship more and fewer than it has and
    to it with the hold full (unsigned: the count itself), small pieces from
    the ship fewer than it has (an even slot before a 6 skipped), a big piece
    in the slot 23 (it clears the slot 24, the hand weapons' low byte), big
    pieces fewer than it has, to the ship more than the office has and than
    its room, small ones fewer than the slots marked 7, big ones fewer than
    the free pairs; the ship made a convoy's guide (0x509B90) without the
    convoy's flag 2 (nothing from it) and with it."""
    s, t = player_docked_ship(o)
    m = o.u32(to.PLAYER)
    a = o.ship(s)
    towns, _, merchants, ships = o.counts()

    def slots():
        return ''.join('.' if v == 0xFF else str(v) for v in o.uc.mem_read(a + 0x13C, 24))

    def state(what):
        b = o.office(facilities_office(o, m, t))
        pieces = [o.s32(b + 0x124 + 4 * k) // PIECE_WEIGHT[k] for k in range(6)]
        print(f'  {what}: slots {slots()} hand {o.u16(a + 0x154)}; office {pieces} hand {o.s32(b + 0x2BC)}; '
              f'weapons ship {[o.s32(a + 0x54 + 4 * w) for w in range(0x14, 0x18)]} '
              f'office {[o.s32(b + 4 + 4 * w) for w in range(0x14, 0x18)]}')

    def ship_pieces(k):
        return o.u16(a + 0x154) if k < 0 else sum(1 for v in o.uc.mem_read(a + 0x13C, 24) if v == k)

    def office_pieces(k):
        b = o.office(facilities_office(o, m, t))
        return o.s32(b + 0x2BC) if k < 0 else cdiv(o.s32(b + 0x124 + 4 * k), PIECE_WEIGHT[k])

    def queue(op, what):
        print(f'{what}: {op.hex() if op else "nothing"}')
        if op:
            run_bytes(o, op)
            state(what)

    def by_hand(kind, count, town, from_ship, ship=None):
        operation(o, 9, kind & 0xFFFFFFFF, town | (0x80000000 if from_ship else 0), s if ship is None else ship, count)

    def during(o):
        w32(o, o.merchant(m), 50000000)
        if facilities_office(o, m, t) is None:
            store(o, t)
            operation(o, 0x5D, m, t)
        b = o.office(facilities_office(o, m, t))
        for k in range(6):
            w32(o, b + 0x124 + 4 * k, 3 * PIECE_WEIGHT[k] + (500 if k == 1 else 0))
        w32(o, b + 0x2BC, 40)
        for w, n in zip(range(0x14, 0x18), (57, 30, 0, 12)):
            w32(o, b + 4 + 4 * w, n)
        o.uc.mem_write(a + 0x13C, bytes([0xFF] * 24))
        w8(o, a + 0x13D, 7)
        w8(o, a + 0x13F, 7)
        w16(o, a + 0x154, 5)
        direct(o, 0x5182B0, [], ecx=a)
        state('at first')
        for k in range(-1, 6):
            for step in (1, 5, 0):
                queue(player_op('artillery', step, office_pieces(k), k, t, s, 0), f'type {k} to the ship by {step}')
        for w in range(0x14, 0x18):
            for step in (1, 5, 0):
                units = cdiv(o.s32(b + 4 + 4 * w), 10)
                queue(player_op('weapons-to-ship', step, units, w, o.s32(a + 0x10), window_used(o, s), s, m, t,
                                ship_pieces(w - 0x14 + 2)), f'weapon {w:#x} to the ship by {step}')
        for k in range(-1, 6):
            for step in (1, 0):
                queue(player_op('artillery', step, ship_pieces(k), k, t, s, 1), f'type {k} to the office by {step}')
        for w in range(0x14, 0x18):
            for step in (1, 0):
                units = cdiv(o.s32(a + 0x54 + 4 * w), 10)
                queue(player_op('weapons-to-office', step, units, w, s, m), f'weapon {w:#x} to the office by {step}')
        # 0x538210's branches.
        by_hand(1, 1, towns, False)
        by_hand(1, 1, t, False, ships)
        w16(o, a + 0x134, 4)
        by_hand(1, 1, t, False)
        w16(o, a + 0x134, 0)
        by_hand(1, 1, (t + 1) % towns, False)
        owner = o.u8(a)
        other = next(x for x in range(merchants) if x != m and facilities_office(o, x, t) is None)
        w8(o, a, other)
        by_hand(1, 1, t, False)
        w8(o, a, owner)
        w16(o, a + 0x154, 9)
        by_hand(-1, 20, t, True)
        state('20 hand weapons from 9')
        w16(o, a + 0x154, 9)
        by_hand(-1, 4, t, True)
        state('4 hand weapons from 9')
        timber = o.s32(a + 0x54 + 4 * TIMBER)
        w32(o, a + 0x54 + 4 * TIMBER, timber + o.s32(a + 0x10) + 4000)
        direct(o, 0x5182B0, [], ecx=a)
        by_hand(-1, 3, t, False)
        state('3 hand weapons, the hold full')
        by_hand(0, 2, t, False)
        state('2 small pieces, the hold full')
        w32(o, a + 0x54 + 4 * TIMBER, timber)
        direct(o, 0x5182B0, [], ecx=a)
        layout = [0, 1, 0, 6, 2, 6, 1, 0, 0xFF, 7, 0xFF, 7, 0xFF, 0xFF, 0xFF, 0xFF, 3, 6, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 2]
        o.uc.mem_write(a + 0x13C, bytes(layout))
        w16(o, a + 0x154, 0x105)
        direct(o, 0x5182B0, [], ecx=a)
        state('a layout')
        by_hand(0, 2, t, True)
        state('2 small pieces of type 0 from the ship')
        by_hand(2, 1, t, True)
        state('1 big piece of type 2 from the ship (the slot 23 first)')
        by_hand(1, 1, t, True)
        for k in range(6):
            w32(o, b + 0x124 + 4 * k, 9 * PIECE_WEIGHT[k])
        by_hand(5, 1, t, False)
        state('1 small piece, slots marked 7 free')
        by_hand(4, 1, t, False)
        state('1 big piece, free pairs')
        w32(o, b + 0x124 + 4 * 3, 2 * PIECE_WEIGHT[3])
        by_hand(3, 10, t, False)
        state('10 big pieces, the office has 2')
        room = struct.unpack('<i', struct.pack('<I', direct(o, 0x5182B0, [], ecx=a)))[0]
        w32(o, a + 0x54 + 4 * TIMBER, timber + max(0, room - 3500) // 2000 * 2000)
        direct(o, 0x5182B0, [], ecx=a)
        by_hand(1, 10, t, False)
        state('10 small pieces, room for a few')
        w32(o, a + 0x54 + 4 * TIMBER, timber)
        direct(o, 0x5182B0, [], ecx=a)
        # A convoy guided by the ship (0x509B90): 25 sailors, guns, a captain.
        captains = o.u16(to.SHIPS + 0xF2)
        if o.u16(a + 0x42) >= captains:
            run_bytes(o, player_op('hire-captain', town_captain(o, t), s, t))
        w16(o, a + 0x40, 25)
        o.uc.mem_write(a + 0x13C, bytes([1] * 7 + [0xFF] * 17))
        direct(o, 0x5182B0, [], ecx=a)
        direct(o, 0x509B90, [s, 0, 5, t], ecx=to.SHIPS)
        c = o.u32(to.SHIPS + 8) + 0x3C * o.u16(a + 8)
        flags = o.u8(c + 0x14)
        w8(o, c + 0x14, flags & ~2)
        by_hand(1, 2, t, True)
        state('the convoy without its flag 2')
        w8(o, c + 0x14, flags | 2)
        by_hand(1, 2, t, True)
        state('the convoy with its flag 2')
    return during


# --- The convoys (operations 0x15..0x1C, 0x20) and the shipyard's pages ---

CONVOY_CHOICES = to.SENTINEL + 0xE80  # the copier of 0x40C170's choices (probes_unchecked.cpp kConvoyChoices)
CONVOY_SUMMARY = to.SENTINEL + 0xE00  # the copier of 0x4AA4A0's convoy (probes_unchecked.cpp kConvoySummary)


def s32_of(v):
    return struct.unpack('<i', struct.pack('<I', v & 0xFFFFFFFF))[0]


def convoy_copiers(o):
    """The copiers at CONVOY_CHOICES (stdcall: ship) and CONVOY_SUMMARY
    (stdcall: convoy), each on a window of its own that the game's code
    fills: the convoy window's choices (0x40C170, with the ship alone
    selected: its town [+0x1534] and «big» [+0x1535], which convoy_choices
    sets first), a u32 count and the choices into SCRATCH; and what the
    market window keeps of a convoy in its mode 4 (0x4AA4A0 forced, its
    redraw 0x4AA2A0 skipped): its 24 wares' units, what it holds [+0x5E8] and
    its capacity [+0x738], 26 u32 into SCRATCH. tick_probe's probes make
    p3vita's. Returns the choices' window."""
    from unicorn.x86_const import UC_X86_REG_EIP, UC_X86_REG_ESP
    win, sel, market = o.alloc(0x1640), o.alloc(4), o.alloc(0x900)
    w32(o, win + 0x1538, sel)
    w32(o, win + 0x153C, 1)
    w32(o, market + 0xF0, 4)
    code = bytes([0x56, 0x57, 0x8B, 0x44, 0x24, 0x0C])  # push esi; push edi; mov eax, [esp+0xC]
    code += bytes([0x66, 0xA3]) + struct.pack('<I', sel)  # mov [sel], ax
    code += bytes([0xB9]) + struct.pack('<I', win)  # mov ecx, win
    code += bytes([0xE8]) + struct.pack('<i', 0x40C170 - (CONVOY_CHOICES + len(code) + 5))
    code += bytes([0x8B, 0x0D]) + struct.pack('<I', win + 0x1530)  # mov ecx, [win+0x1530]
    code += bytes([0x89, 0x0D]) + struct.pack('<I', SCRATCH)  # mov [SCRATCH], ecx
    code += bytes([0x83, 0xF9, 0x3F, 0x76, 0x05, 0xB9]) + struct.pack('<I', 0x3F)  # cmp ecx, 63; jbe; mov ecx, 63
    code += bytes([0x8B, 0x35]) + struct.pack('<I', win + 0x152C)  # mov esi, [win+0x152C]
    code += bytes([0xBF]) + struct.pack('<I', SCRATCH + 4)  # mov edi, SCRATCH+4
    code += bytes([0xFC, 0xF3, 0xA5, 0x5F, 0x5E, 0xC2, 0x04, 0x00])  # cld; rep movsd; pop edi; pop esi; ret 4
    assert len(code) <= 0x80
    o.uc.mem_write(CONVOY_CHOICES, code)
    code = bytes([0x56, 0x57, 0x8B, 0x44, 0x24, 0x0C])
    code += bytes([0xA3]) + struct.pack('<I', market + 0xF4)  # mov [market+0xF4], eax
    code += bytes([0x6A, 0x01, 0xB9]) + struct.pack('<I', market)  # push 1; mov ecx, market
    code += bytes([0xE8]) + struct.pack('<i', 0x4AA4A0 - (CONVOY_SUMMARY + len(code) + 5))
    code += bytes([0xBE]) + struct.pack('<I', market + 0x588)  # mov esi, market+0x588
    code += bytes([0xBF]) + struct.pack('<I', SCRATCH)  # mov edi, SCRATCH
    code += bytes([0xB9]) + struct.pack('<I', 25)  # mov ecx, 25
    code += bytes([0xFC, 0xF3, 0xA5])  # cld; rep movsd
    code += bytes([0xA1]) + struct.pack('<I', market + 0x738)  # mov eax, [market+0x738]
    code += bytes([0xA3]) + struct.pack('<I', SCRATCH + 0x64)  # mov [SCRATCH+0x64], eax
    code += bytes([0x5F, 0x5E, 0xC2, 0x04, 0x00])
    assert len(code) <= 0x80
    o.uc.mem_write(CONVOY_SUMMARY, code)

    def skip(uc, address, size, data):
        esp = uc.reg_read(UC_X86_REG_ESP)
        uc.reg_write(UC_X86_REG_EIP, o.u32(esp))
        uc.reg_write(UC_X86_REG_ESP, esp + 4)
    o.uc.hook_add(UC_HOOK_CODE, skip, begin=0x4AA2A0, end=0x4AA2A0)
    o.uc.ctl_remove_cache(0x4AA2A0, 0x4AA2A1)
    return win


def convoy_choices(o, win, s, what):
    """The convoy window's choices for the ship s alone (CONVOY_CHOICES)."""
    a = o.ship(s)
    w8(o, win + 0x1534, o.u8(a + 0x39))
    w8(o, win + 0x1535, 1 if o.u8(a + 0xE) >= 2 else 0)
    w32(o, win + 0x152C, 0)
    w32(o, win + 0x1530, 0)
    direct(o, CONVOY_CHOICES, [s])
    got = [s32_of(o.u32(SCRATCH + 4 + 4 * k)) for k in range(min(o.u32(SCRATCH), 63))]
    print(f'  {what}: the convoy window offers {got}')
    return got


def convoy_summary(o, k):
    """What the market window keeps of the convoy k (CONVOY_SUMMARY): its
    units, what it holds (steps of 200) and its capacity."""
    direct(o, CONVOY_SUMMARY, [k])
    return [s32_of(o.u32(SCRATCH + 4 * w)) for w in range(24)], s32_of(o.u32(SCRATCH + 0x60)), s32_of(o.u32(SCRATCH + 0x64))


def make_guide(o, s, t):
    """The ship s fit to guide a convoy (0x519C40): a captain (hired from the
    town's list), 25 sailors, 7 small guns (firepower 70), its whole hull."""
    a = o.ship(s)
    if o.u16(a + 0x42) >= o.u16(to.SHIPS + 0xF2):
        run_bytes(o, player_op('hire-captain', town_captain(o, t), s, t))
    w16(o, a + 0x40, 25)
    o.uc.mem_write(a + 0x13C, bytes([1] * 7 + [0xFF] * 17))
    w32(o, a + 0x18, o.s32(a + 0x14))
    w8(o, a + 0x15C, 0)
    direct(o, 0x5182B0, [], ecx=a)


def docked_ships(o, owner=None, town=None, exclude=(), most=0):
    """The ships of the docked list ([0x6DD888], next [+0x6]) in the state 0
    (up to `most`) and out of a convoy, of `owner` (None: any merchant) in
    `town` (None: any)."""
    _, _, merchants, ships = o.counts()
    out, x = [], o.u16(to.SHIPS + 0xE8)
    while x < ships:
        b = o.ship(x)
        if (x not in exclude and o.u16(b + 0x134) <= most and o.u16(b + 8) >= o.u16(CONVOY_COUNT) and
                (o.u8(b) == owner if owner is not None else o.u8(b) < merchants) and
                (town is None or o.u8(b + 0x39) == town)):
            out.append(x)
        x = o.u16(b + 6)
    return out


def convoy_line(o, k, what):
    c = convoy_at(o, k)
    ships = o.counts()[3]
    members, x = [], o.u16(c + 0xA)
    while x < ships and len(members) < 20:
        members.append(x)
        x = o.u16(o.ship(x) + 6)
    print(f'  {what}: convoy {k} owner {o.u8(c)} guide {o.u16(c + 0x10)} ships {members} state {o.u16(c + 0x12):#x} '
          f'flags {o.u16(c + 0x14):#x} town {o.u16(c + 0x1C)} dest {o.u8(c + 0x1F)} wait {o.u16(c + 0x16):#x}')


def convoy_ops(o):
    """The player's convoys (docs/formats/operations.md): the convoy window's
    choices (0x40C170, through CONVOY_CHOICES) and the viewer's bytes
    (player_ops create-convoy, join-convoy, leave-convoy) on the player's
    first ship in port s, made fit to guide (make_guide), and another of his
    ships b in its town (none in save 1). The choices: as loaded, fit to
    guide, with and without the guild's licence [+0x468] (the town with and
    without its bit 0x80). 0x15 (0x509B90): a private convoy, again (nothing:
    in a convoy already); 0x18 (0x509D10): b joins, an AI ship can't (one
    owner); 0x17 (0x539270): b leaves, the guide disbands it; a public convoy
    to another town (it waits 5 days), and 0x18 into it: b with few sailors,
    with a sixth of its hull, waiting longer than the convoy (state 1), in
    another town, into a convoy with the bit 0x10, a big ship with the bit 4
    and to a town that takes no big ships, an AI ship (it may, if fit), b
    itself; the convoy in the state 9 of an event 0x23 (0x5039F0, the event's
    merchant byte, 0x4F8D20 on the merchant: 0.1 more reputation in the
    towns with the bit 0x40), of 0x34 (the firepower), in the phase 1, and in the
    state 0xA. 0x16 (0x5359B1): a convoy out; 0x17 with a ship out and one
    out of a convoy. 0x20 (0x539CF0): a docked convoy to another town, to
    the same, waiting already, to a town out, in a town out, a convoy out.
    0x1C (0x535A70): the convoy sails, a convoy out."""
    s, t = player_docked_ship(o)
    m = o.u32(to.PLAYER)
    a = o.ship(s)
    towns, _, merchants, ships = o.counts()
    win = convoy_copiers(o)
    others = docked_ships(o, m, t, (s,))
    print(f'ship {s} of the player {m} in town {t}; his other ships there {others}')

    def run(*args):
        op = player_op(*args)
        print(f'  {args}: {op.hex()}')
        run_bytes(o, op)

    def join(guide, x, what):
        run('join-convoy', guide, x)
        print(f'    {what}: ship {x} in convoy {o.u16(o.ship(x) + 8)}')

    def far_town(big_ok=True):
        return next(u for u in range(towns) if u != t and (big_ok or not o.u32(o.town(u) + 0x2C8) & 1))

    def during(o):
        w32(o, o.merchant(m), 10000000)
        convoy_choices(o, win, s, 'as loaded')
        make_guide(o, s, t)
        convoy_choices(o, win, s, 'fit to guide')
        mm, ta = o.merchant(m), o.town(t)
        licence, flags = o.u32(mm + 0x468), o.u32(ta + 0x2C8)
        w32(o, ta + 0x2C8, flags | 0x80)
        w32(o, mm + 0x468, licence | 1 << (t & 31))
        convoy_choices(o, win, s, 'the licence of the town')
        w32(o, mm + 0x468, (licence | 1 << ((t + 1) & 31)) & ~(1 << (t & 31)))
        convoy_choices(o, win, s, 'a licence of another town')
        w32(o, ta + 0x2C8, flags & ~0x80)
        convoy_choices(o, win, s, 'the same, the town without its bit 0x80')
        w32(o, mm + 0x468, 0)
        convoy_choices(o, win, s, 'no licence')
        w32(o, mm + 0x468, licence)
        w32(o, ta + 0x2C8, flags)
        # A private convoy.
        run('create-convoy', s, 0, 0)
        k = o.u16(a + 8)
        convoy_line(o, k, 'private')
        run('create-convoy', s, 0, 0)
        ai = docked_ships(o, None, None, (s,), 1)
        ai = [x for x in ai if o.u8(o.ship(x)) != m]
        x = ai[0]
        home, x_state, x_wait = o.u8(o.ship(x) + 0x39), o.u16(o.ship(x) + 0x134), o.u16(o.ship(x) + 0x138)
        w8(o, o.ship(x) + 0x39, t)
        w16(o, o.ship(x) + 0x134, 0)
        w16(o, o.ship(x) + 0x138, 0)
        convoy_choices(o, win, x, f'an AI ship {x} of {o.u8(o.ship(x))}')
        join(s, x, 'an AI ship into the private convoy')
        w8(o, o.ship(x) + 0x39, home)
        if others:
            b = others[0]
            convoy_choices(o, win, b, f'the player\'s ship {b}')
            join(s, b, 'the player\'s')
            convoy_line(o, k, 'with b')
            run('leave-convoy', b, k, 0, 0)
            convoy_line(o, k, 'b left')
            join(s, b, 'b again')
        run('leave-convoy', s, k, 1, 0)
        print(f'  disbanded: s in {o.u16(a + 8)}, state {o.u16(a + 0x134)}')
        # A public convoy.
        far = far_town(False)
        run('create-convoy', s, 1, far)
        k = o.u16(a + 8)
        c = convoy_at(o, k)
        convoy_line(o, k, f'public to {far}')
        if others:
            b = others[0]
            bb = o.ship(b)
            sailors, hull = o.u16(bb + 0x40), o.s32(bb + 0x18)
            w16(o, bb + 0x40, 1)
            join(s, b, 'few sailors')
            w16(o, bb + 0x40, max(sailors, 12))
            w32(o, bb + 0x18, o.s32(bb + 0x14) // 6)
            join(s, b, 'a sixth of its hull')
            w32(o, bb + 0x18, o.s32(bb + 0x14))
            w16(o, bb + 0x134, 1)
            w16(o, bb + 0x138, o.u16(c + 0x16) + 1)
            join(s, b, 'waiting longer than the convoy')
            w16(o, bb + 0x134, 0)
            w16(o, bb + 0x138, 0)
            w8(o, bb + 0x39, (t + 1) % towns)
            join(s, b, 'in another town')
            w8(o, bb + 0x39, t)
            w16(o, c + 0x14, o.u16(c + 0x14) | 0x10)
            join(s, b, 'the convoy\'s bit 0x10')
            w16(o, c + 0x14, o.u16(c + 0x14) & ~0x10)
            kind = o.u8(bb + 0xE)
            w8(o, bb + 0xE, 2)
            w16(o, c + 0x14, o.u16(c + 0x14) | 4)
            join(s, b, 'big, the convoy\'s bit 4')
            w16(o, c + 0x14, o.u16(c + 0x14) & ~4)
            fa = o.town(far)
            w32(o, fa + 0x2C8, o.u32(fa + 0x2C8) | 1)
            convoy_choices(o, win, b, 'big, to a town of no big ships')
            join(s, b, 'big, to a town of no big ships')
            w32(o, fa + 0x2C8, o.u32(fa + 0x2C8) & ~1)
            w8(o, bb + 0xE, kind)
            convoy_choices(o, win, b, 'b, the public convoy')
            w16(o, bb + 0x134, 1)
            w16(o, bb + 0x138, o.u16(c + 0x16))
            join(s, b, 'waiting as long')
            convoy_line(o, k, 'with b')
        xx = o.ship(x)
        saved = (o.u8(xx + 0x39), o.u16(xx + 0x40), o.s32(xx + 0x18))
        w8(o, xx + 0x39, t)
        w16(o, xx + 0x40, 30)
        w32(o, xx + 0x18, o.s32(xx + 0x14))
        convoy_choices(o, win, x, f'the AI ship {x}, the public convoy')
        join(s, x, 'the AI ship into the public convoy')
        if o.u16(xx + 8) >= o.u16(CONVOY_COUNT):
            w8(o, xx + 0x39, saved[0])
            w16(o, xx + 0x134, x_state)
            w16(o, xx + 0x138, x_wait)
        w16(o, xx + 0x40, saved[1])
        w32(o, xx + 0x18, saved[2])
        convoy_line(o, k, 'public, at last')
        # The convoy of an event (states 9 and 0xA, 0x5039F0).
        e = 0  # a record of the queue, restored after
        record = bytes(o.uc.mem_read(o.u32(to.EVENTS) + 0x18 * e, 0x18))
        r = o.u32(to.EVENTS) + 0x18 * e
        last = o.town(towns - 1)
        lflags = o.u32(last + 0x2C8)
        w32(o, last + 0x2C8, lflags | 0x40)
        state, link = o.u16(c + 0x12), o.u16(c + 2)
        w16(o, c + 2, e)
        y = others[0] if others else x
        yy = o.ship(y)
        w8(o, yy + 0x39, t)
        for kind, phase, cstate in ((0x23, 2, 9), (0x34, 2, 9), (0x24, 1, 9), (0x24, 2, 9), (0x23, 2, 0xA)):
            if o.u16(yy + 8) < o.u16(CONVOY_COUNT):
                w16(o, c + 0x12, 1)
                operation(o, 0x17, y)
            w16(o, yy + 0x134, 0)
            w16(o, yy + 0x138, 0)
            w16(o, c + 0x12, cstate)
            w16(o, a + 0x134, cstate)  # 0x501BE0 takes the convoy's state from its guide's
            w16(o, r + 6, kind)
            w8(o, r + 0x14, t)
            w8(o, r + 0x15, phase)
            o.uc.mem_write(r + 8, bytes([0xFF, o.u8(yy), 0xFF, o.u8(yy), 0, 0, 0, 0]))
            w16(o, yy + 0x40, max(o.u16(yy + 0x40), 30))
            w32(o, yy + 0x18, o.s32(yy + 0x14))
            rep = struct.unpack('<f', bytes(o.uc.mem_read(o.merchant(o.u8(yy)) + 0x120 + 12 * (towns - 1), 4)))[0]
            join(s, y, f'an event {kind:#x} in the phase {phase}, the convoy\'s state {cstate:#x}')
            rep2 = struct.unpack('<f', bytes(o.uc.mem_read(o.merchant(o.u8(yy)) + 0x120 + 12 * (towns - 1), 4)))[0]
            print(f'    the event\'s bytes {bytes(o.uc.mem_read(r + 8, 8)).hex()}, the reputation in the last town {rep} -> {rep2}')
        o.uc.mem_write(r, record)
        w16(o, c + 0x12, state)
        w16(o, a + 0x134, state)
        w16(o, c + 2, link)
        w32(o, last + 0x2C8, lflags)
        # 0x17, 0x16 by hand.
        operation(o, 0x17, ships)
        operation(o, 0x17, docked_ships(o, None, None, (s,), 1)[0])
        operation(o, 0x16, o.u16(CONVOY_COUNT))
        operation(o, 0x17, s)
        print(f'  0x17 for the guide: s in {o.u16(a + 8)}')
        if others:
            run('create-convoy', others[-1], 0, 0)
            print(f'  ship {others[-1]}, which can not guide: in {o.u16(o.ship(others[-1]) + 8)}')
        # 0x20 on a docked convoy, 0x1C.
        run('create-convoy', s, 0, 0)
        k = o.u16(a + 8)
        c = convoy_at(o, k)
        w16(o, c + 0x12, 0)
        w16(o, a + 0x134, 0)
        operation(o, 0x20, k, far, 3)
        convoy_line(o, k, f'0x20 to {far}')
        w16(o, c + 0x12, 0)
        operation(o, 0x20, k, far, 4)
        convoy_line(o, k, '0x20 to the same')
        w16(o, c + 0x12, 1)
        operation(o, 0x20, k, (far + 1) % towns, 4)
        convoy_line(o, k, '0x20 waiting already')
        w16(o, c + 0x12, 0)
        operation(o, 0x20, k, towns, 4)
        town = o.u16(c + 0x1C)
        w16(o, c + 0x1C, 0xFF)
        operation(o, 0x20, k, (far + 2) % towns, 4)
        w16(o, c + 0x1C, town)
        operation(o, 0x20, o.u16(CONVOY_COUNT), far, 4)
        convoy_line(o, k, 'after the 0x20 out')
        w16(o, c + 0x12, 1)
        w16(o, a + 0x134, 1)
        operation(o, 0x1C, k | 0x900 << 16, far, 0, 0)
        convoy_line(o, k, '0x1C')
        print(f'  the guide: state {o.u16(a + 0x134):#x}, destination {o.u8(a + 0x38)}')
        operation(o, 0x1C, o.u16(CONVOY_COUNT) | 0x900 << 16, far, 0, 0)
    return during


def convoy_trade(o):
    """A convoy's trade (0x1A, 0x19: 0x539300; 0x1B: 0x5396B0): the player's
    first ship in port guides a private convoy with up to two of his other
    ships in its town (in save 1 he has none: the convoy is its guide), in a town
    where he has an office (0x5D if none). What the market window keeps of
    the convoy (0x4AA4A0, CONVOY_SUMMARY) and the viewer's bytes from it
    (player_ops convoy-buy, convoy-sell, office-to-convoy, convoy-to-office)
    with each step, beer (barrels) and grain (loads). By hand: an AI ship
    chained into the convoy (it doesn't trade); buying more than the room and
    the store, short of money (each ship cut down), without money, a ware,
    convoy and merchant out, the convoy in another town than the operation's;
    selling more than the cargo, 0; 0x1B from the ships more than they have
    (each gives all), into them with the first one's room under a unit, the
    office empty, without an office."""
    s, t = player_docked_ship(o)
    m = o.u32(to.PLAYER)
    a = o.ship(s)
    towns, _, merchants, ships = o.counts()
    convoy_copiers(o)
    others = docked_ships(o, m, t, (s,))[:2]
    print(f'ship {s} of the player {m} in town {t}; his other ships there {others}')

    def queue(op, what, k):
        print(f'{what}: {op.hex() if op else "nothing"}')
        if op:
            run_bytes(o, op)
            units, used, cap = convoy_summary(o, k)
            print(f'  beer {units[BEER]}, grain {units[GRAIN]}, holds {used}/{cap // 200}; money {o.s32(o.merchant(m))}, '
                  f'store beer {o.s32(o.town(t) + 4 + 4 * BEER)} grain {o.s32(o.town(t) + 4 + 4 * GRAIN)}')

    def by_hand(code, amount, ware, k, town, who=None):
        operation(o, code, amount & 0xFFFFFFFF, ware | k << 16, (m if who is None else who) | town << 16)

    def office_hand(amount, k, ware, direction, who=None):
        operation(o, 0x1B, amount & 0xFFFFFFFF, k | ware << 16, (m if who is None else who) | direction << 16)

    def during(o):
        w32(o, o.merchant(m), 50000000)
        if facilities_office(o, m, t) is None:
            store(o, t)
            operation(o, 0x5D, m, t)
        f = facilities_office(o, m, t)
        b = o.office(f)
        make_guide(o, s, t)
        run_bytes(o, player_op('create-convoy', s, 0, 0))
        k = o.u16(a + 8)
        for x in others:
            run_bytes(o, player_op('join-convoy', s, x))
        convoy_line(o, k, 'the convoy')
        units, used, cap = convoy_summary(o, k)
        print(f'  the window: units {units[:6]}, holds {used}, capacity {cap}')
        for ware in (BEER, GRAIN):
            w32(o, o.town(t) + 4 + 4 * ware, 90 * (200 if IN_BARRELS[ware] else 2000) + 150)
            for step in (1, 5, 50, 0):
                units, used, cap = convoy_summary(o, k)
                tu = cdiv(o.s32(o.town(t) + 4 + 4 * ware), 200 if IN_BARRELS[ware] else 2000)
                queue(player_op('convoy-buy', step, tu, ware, cap, used, k, m, t), f'buy {ware} by {step}', k)
            for step in (1, 5, 50, 0):
                units = convoy_summary(o, k)[0]
                queue(player_op('convoy-sell', step, units[ware], ware, k, m, t), f'sell {ware} by {step}', k)
        for ware, n in ((BEER, 45 * 200 + 33), (GRAIN, 14 * 2000 + 500)):
            w32(o, b + 4 + 4 * ware, n)
            o.uc.mem_write(b + 0x3B8 + 4 * ware, struct.pack('<f', 4.5 if ware == BEER else 38.25))
        for ware in (BEER, GRAIN):
            for step in (1, 5, 50, 0):
                units, used, cap = convoy_summary(o, k)
                ou = cdiv(o.s32(b + 4 + 4 * ware), 200 if IN_BARRELS[ware] else 2000)
                queue(player_op('office-to-convoy', step, ou, ware, cap, used, k, m), f'office to convoy {ware} by {step}', k)
            for step in (1, 5, 50, 0):
                units = convoy_summary(o, k)[0]
                queue(player_op('convoy-to-office', step, units[ware], ware, k, m), f'convoy to office {ware} by {step}', k)
        # 0x539300's branches.
        c = convoy_at(o, k)
        ai = [x for x in docked_ships(o, None, None, (s,), 1) if o.u8(o.ship(x)) != m][0]
        xa = o.ship(ai)
        saved = (o.u16(xa + 6), o.u16(xa + 8), o.u8(xa + 0x39), o.u16(c + 0xA))
        w16(o, xa + 6, o.u16(c + 0xA))
        w16(o, c + 0xA, ai)
        w16(o, xa + 8, k)
        w8(o, xa + 0x39, t)
        by_hand(0x1A, 3 * 200, BEER, k, t)
        convoy_line(o, k, f'with the AI ship {ai} chained in: its beer {o.s32(xa + 0x54 + 4 * BEER)}')
        by_hand(0x19, 2 * 200, BEER, k, t)
        w16(o, xa + 6, saved[0])
        w16(o, xa + 8, saved[1])
        w8(o, xa + 0x39, saved[2])
        w16(o, c + 0xA, saved[3])
        w32(o, o.town(t) + 4 + 4 * BEER, 5000 * 200)
        by_hand(0x1A, 4000 * 200, BEER, k, t)
        convoy_line(o, k, f'more than the room: beer {convoy_summary(o, k)[0][BEER]}')
        by_hand(0x19, 1000 * 200, BEER, k, t)
        w32(o, o.town(t) + 4 + 4 * GRAIN, 3 * 2000 + 700)
        by_hand(0x1A, 20 * 2000, GRAIN, k, t)
        by_hand(0x19, 1000 * 2000, GRAIN, k, t)
        w32(o, o.town(t) + 4 + 4 * BEER, 200 * 200)
        for money in (900, 5000, 0, -100):
            w32(o, o.merchant(m), money & 0xFFFFFFFF)
            by_hand(0x1A, 60 * 200, BEER, k, t)
            print(f'  with {money}: beer {convoy_summary(o, k)[0][BEER]}, money {o.s32(o.merchant(m))}')
        w32(o, o.merchant(m), 50000000)
        by_hand(0x1A, 200, 0x18, k, t)
        by_hand(0x1A, 200, BEER, o.u16(CONVOY_COUNT), t)
        by_hand(0x1A, 200, BEER, k, t, merchants)
        by_hand(0x1A, 200, BEER, k, (t + 1) % towns)
        by_hand(0x19, 0, BEER, k, t)
        by_hand(0x19, 1000 * 200, BEER, k, t)
        # 0x5396B0's branches.
        w32(o, b + 4 + 4 * BEER, 30 * 200 + 50)
        office_hand(-(25 * 200), k, BEER, 1)
        convoy_line(o, k, f'0x1B 25 barrels in (sign dropped): beer {convoy_summary(o, k)[0][BEER]}')
        office_hand(1000 * 200, k, BEER, 0)
        print(f'  0x1B all out: office beer {o.s32(b + 4 + 4 * BEER)}')
        first = o.ship(o.u16(c + 0xA))
        beer = o.s32(first + 0x54 + 4 * BEER)
        room = s32_of(direct(o, 0x5182B0, [], ecx=first))
        w32(o, first + 0x54 + 4 * BEER, beer + max(0, room - 1100) // 200 * 200)
        direct(o, 0x5182B0, [], ecx=first)
        w32(o, b + 4 + 4 * GRAIN, 9 * 2000 + 300)
        office_hand(9 * 2000 + 300, k, GRAIN, 1)
        print(f'  0x1B the first ship\'s room under a load: office grain {o.s32(b + 4 + 4 * GRAIN)}')
        w32(o, first + 0x54 + 4 * BEER, beer)
        room = s32_of(direct(o, 0x5182B0, [], ecx=first))
        w32(o, first + 0x54 + 4 * BEER, beer + max(0, room - 3100) // 200 * 200)
        direct(o, 0x5182B0, [], ecx=first)
        w32(o, b + 4 + 4 * GRAIN, 9 * 2000 + 300)
        office_hand(9 * 2000 + 300, k, GRAIN, 1)
        print(f'  0x1B the first ship\'s room for a load: office grain {o.s32(b + 4 + 4 * GRAIN)}')
        w32(o, first + 0x54 + 4 * BEER, beer)
        direct(o, 0x5182B0, [], ecx=first)
        w32(o, b + 4 + 4 * GRAIN, 0)
        office_hand(2000, k, GRAIN, 1)
        office_hand(2000, k, 0x18, 1)
        office_hand(2000, o.u16(CONVOY_COUNT), GRAIN, 1)
        office_hand(2000, k, GRAIN, 1, merchants)
        other = next(x for x in range(merchants) if x != m and facilities_office(o, x, t) is None)
        office_hand(2000, k, GRAIN, 0, other)
    return during


def ship_name(o, s):
    """A ship's name [+0x160]."""
    return bytes(o.uc.mem_read(o.ship(s) + 0x160, 32)).split(bytes(1))[0]


def shipyard_ops(o):
    """The shipyard's other pages: order a ship (0x2C, the viewer's bytes) in
    a town with a shipyard, cancel it (0x2F, 0x52B110: nine tenths back, the
    letter 0x14, the ship deleted), one whose work started (the letter, −1),
    another merchant's, a ship not ordered, a town out. Upgrade (0x52B670 on
    SCRATCH and 0x0C, 0x508660): the list (0x504AC0 mode 2, WINDOW_SHIPS); the
    player's first ship in port by 1, 2 and 3 levels; with the timber,
    pitch and tools in his office and ships, short of the town's timber, short
    of money, at the level 2 (cleared), of no merchant, a ship out; the
    viewer's bytes to the level 1 and then 2; at sea, the level 2. Rename
    (0x2D, 0x2E: 0x53CCE0 and the names object 0x512D40, 0x512FC0, 0x512B20):
    the viewer's bytes for a short name, a long one (0x2E onto the last name),
    one of the game's names, the long one again (its first piece a new name),
    the short one again (one of the players'), a 0x2E alone on
    a ship whose name isn't the last (a new name from its index's, and with
    12 characters the ship's number's bytes), on a ship of the type's name
    (0x7FFF − type), and 40 names more (the tables grow); the names object's
    counts after each."""
    s, t = player_docked_ship(o)
    m = o.u32(to.PLAYER)
    a = o.ship(s)
    towns, _, merchants, ships = o.counts()
    window_ships_copier(o)
    yard = next(u for u in [t] + list(range(towns)) if o.u32(o.town(u) + 0x76C) & 0x20000 and o.u8(o.town(u) + 0x824) < 0x80)
    N = 0x6DDAA0
    print(f'ship {s} of the player {m} in town {t}; a shipyard in {yard}')

    def names(what):
        print(f'  {what}: names {o.u16(N + 0xD8)}..{o.u16(N + 0xDA)}, table {o.u16(N + 0xDC)}, buffer {o.s32(N + 0xE0)}, '
              f'last {o.u32(N + 0xE4)}; ship {s} name {ship_name(o, s)} '
              f'index {o.u16(a + 0x15E):#x}')

    def orders(what):
        out, x = [], o.u16(o.town(yard) + 0x81C)
        while x < o.counts()[3] and len(out) < 20:
            out.append(x)
            x = o.u16(o.ship(x) + 6)
        print(f'  {what}: orders {out}, money {o.s32(o.merchant(m))}')
        return out

    def cost(levels, what, ship=s, town=None):
        ok = direct(o, 0x52B670, [SCRATCH, ship, levels], ecx=o.town(t if town is None else town)) & 0xFF
        v = [s32_of(o.u32(SCRATCH + 4 * k)) for k in range(16)]
        print(f'  {what}: {ok}, have {v[0:3]} use {v[3:6]} buy {v[6:9]} prices {v[9:12]} cost {v[12]} days {v[13]} '
              f'hold {v[14]} loads {v[15]} slots {o.u8(SCRATCH + 0x40)} {o.u8(SCRATCH + 0x41)}')

    def rename(ship, name):
        for line in player_op_text('rename', ship, name).splitlines():
            run_bytes(o, bytes.fromhex(line))

    def during(o):
        w32(o, o.merchant(m), 5000000)
        # Order and cancel (the shipyard's town with the materials, 0x52B2C0).
        for ware in (0xB, 8, 0xC, 0x11, 0xF):
            w32(o, o.town(yard) + 4 + 4 * ware, 200 * (200 if IN_BARRELS[ware] else 2000))
        run_bytes(o, player_op('order-ship', 0, m, yard))
        run_bytes(o, player_op('order-ship', 1, m, yard))
        first = orders('two ordered')
        if first:
            run_bytes(o, player_op('cancel-order', m, yard, first[-1]))
            orders('the last cancelled')
            w32(o, o.ship(first[0]) + 0x18, 500)
            run_bytes(o, player_op('cancel-order', m, yard, first[0]))
            orders('the first, its work started')
            w32(o, o.ship(first[0]) + 0x18, 0)
            operation(o, 0x2F, (m + 1) % merchants, yard, first[0])
            operation(o, 0x2F, m, yard, s)
            operation(o, 0x2F, m, towns, first[0])
            operation(o, 0x2F, m, yard, ships)
            operation(o, 0x2F, merchants, yard, first[0])
            run_bytes(o, player_op('cancel-order', m, yard, first[0]))
            orders('the first cancelled')
        # Upgrade.
        direct(o, WINDOW_SHIPS, [m, t, 2, 0])
        print(f'  the upgrade list: {[o.u16(SCRATCH + 2 + 2 * k) for k in range(o.u16(SCRATCH))]}')
        w8(o, a + 0xF, 0)
        w32(o, a + 0x18, o.s32(a + 0x14) * 3 // 4)
        for levels in (1, 2, 3, 0):
            cost(levels, f'{levels} levels')
        f = facilities_office(o, m, t)
        if f is not None:
            for ware, n in ((TIMBER, 3 * 2000 + 500), (0xF, 1 * 200), (0xC, 0)):
                w32(o, o.office(f) + 4 + 4 * ware, n)
        w32(o, a + 0x54 + 4 * 0xC, 2 * 200 + 40)
        cost(2, 'with his own wares')
        timber = o.s32(o.town(t) + 4 + 4 * TIMBER)
        w32(o, o.town(t) + 4 + 4 * TIMBER, 0)
        cost(2, 'the town without timber')
        w32(o, o.town(t) + 4 + 4 * TIMBER, timber)
        w32(o, o.merchant(m), 100)
        cost(1, 'short of money')
        w32(o, o.merchant(m), 5000000)
        w8(o, a + 0xF, 2)
        cost(1, 'at the level 2')
        w8(o, a + 0xF, 0)
        owner = o.u8(a)
        w8(o, a, 0xFF)
        cost(1, 'of no merchant')
        w8(o, a, owner)
        cost(1, 'a ship out', ships)
        run_bytes(o, player_op('upgrade', s, 1))
        print(f'  upgraded to 1: level {o.u8(a + 0xF)}, state {o.u16(a + 0x134)}, repairing {o.u16(o.town(t) + 0x81E)}, '
              f'hull {o.s32(a + 0x18)}/{o.s32(a + 0x14)}, cap {o.s32(a + 0x10)}, money {o.s32(o.merchant(m))}')
        w16(o, a + 0x134, 0)
        run_bytes(o, player_op('upgrade', s, 2))
        print(f'  state 7 again: level {o.u8(a + 0xF)}')
        others = docked_ships(o, m, t, (s,))
        if others:
            b = o.ship(others[0])
            w8(o, b + 0xF, 0)
            run_bytes(o, player_op('upgrade', others[0], 2))
            print(f'  ship {others[0]} to 2: level {o.u8(b + 0xF)}, state {o.u16(b + 0x134)}')
        operation(o, 0x0C, s, 0)
        operation(o, 0x0C, ships, 1)
        # Rename.
        names('as loaded')
        rename(s, 'Seeadler')
        names('a short name')
        rename(s, 'Koenigin der Ostsee')
        names('a long name')
        base = bytes(o.uc.mem_read(o.call(0x512B20, [5], ecx=N), 32)).split(bytes(1))[0].decode('latin-1')
        rename(s, base)
        names(f'a name of the game\'s, {base}')
        rename(s, 'Koenigin der Ostsee')
        names('the long name again (its first piece is a new name)')
        rename(s, 'Seeadler')
        names('the short name again (one of the players\' names)')
        other = (docked_ships(o, m, None, (s,)) or docked_ships(o, None, None, (s,), 1))[0]
        operation(o, 0x2E, *struct.unpack('<3I', b' von Danzig\0'), other)
        names(f'0x2E alone on ship {other}: {ship_name(o, other)}')
        operation(o, 0x2E, *struct.unpack('<3I', b'ABCDEFGHIJKL'), other)
        names(f'0x2E of 12 on ship {other}: {ship_name(o, other)}')
        w16(o, o.ship(other) + 0x15E, 0x7FFF - (o.u8(o.ship(other) + 0xE) & 3))
        operation(o, 0x2E, *struct.unpack('<3I', b' II\0\0\0\0\0\0\0\0\0'), other)
        names(f'0x2E on a type\'s name: {ship_name(o, other)}')
        for n in range(40):
            rename(s, f'Kogge {n:02d} aus Wisby')
        names('40 names more')
        operation(o, 0x2D, *struct.unpack('<3I', b'Hansa\0\0\0\0\0\0\0'), ships)
    return during


# --- The moneylender: loans (0x29, 0x2A) and company shares (0x2B, event 0x18) ---

LENDER_LISTS = to.SENTINEL + 0xC00  # the copiers of the window's lists (probes_unchecked.cpp kLenderLists)
LOAN_BUFFER, LOAN_CHANGED = SCRATCH + 0x80, SCRATCH + 0xB8  # the window's 3 terms [+0xF5C] and its flag
YEAR = 0x6DE4A2  # u16


def lender_copiers(o):
    """The copiers at LENDER_LISTS + 0x50·k (stdcall): 0x4D9100 and 0x4D9210
    (merchant: the loans he owes and granted), 0x4D9320 and 0x4D9620 (no
    arguments: the shares on sale and to sell), on the event queue; each
    puts the count in [SCRATCH] and the list (its 0x18-byte or 12-byte
    entries, at most 10 or 20) from SCRATCH + 4, and frees it."""
    for k, (f, merchant, size, most) in enumerate(((0x4D9100, True, 0x18, 10), (0x4D9210, True, 0x18, 10),
                                                    (0x4D9320, False, 0xC, 20), (0x4D9620, False, 0xC, 20))):
        at = LENDER_LISTS + 0x50 * k
        code = bytes([0x56, 0x57, 0x68]) + struct.pack('<I', SCRATCH)  # push esi; push edi; push &count
        if merchant:
            code += bytes([0xFF, 0x74, 0x24, 0x10])  # push the merchant
        code += bytes([0xB9]) + struct.pack('<I', to.EVENTS)  # mov ecx, the queue
        code += bytes([0xE8]) + struct.pack('<i', f - (at + len(code) + 5))
        copy = bytes([0x50, 0x89, 0xC6, 0x8B, 0x0D]) + struct.pack('<I', SCRATCH)  # push eax; mov esi, eax; mov ecx, [count]
        copy += bytes([0x83, 0xF9, most, 0x76, 0x05, 0xB9]) + struct.pack('<I', most)  # at most `most`
        copy += bytes([0x6B, 0xC9, size, 0xBF]) + struct.pack('<I', SCRATCH + 4) + bytes([0xFC, 0xF3, 0xA4])
        here = at + len(code) + 4 + len(copy)
        copy += bytes([0xE8]) + struct.pack('<i', 0x64F7E2 - (here + 5)) + bytes([0x83, 0xC4, 0x04])  # delete; add esp, 4
        code += bytes([0x85, 0xC0, 0x74, len(copy)]) + copy  # test eax, eax; jz the end
        code += bytes([0x5F, 0x5E]) + (bytes([0xC2, 0x04, 0x00]) if merchant else bytes([0xC3]))
        assert len(code) <= 0x50
        o.uc.mem_write(at, code)


def lender_list(o, k, *args):
    """A list through its copier: (the count, its entries)."""
    direct(o, LENDER_LISTS + 0x50 * k, list(args))
    n = o.u32(SCRATCH)
    size, most = (0x18, 10) if k < 2 else (0xC, 20)
    return n, [bytes(o.uc.mem_read(SCRATCH + 4 + size * j, size)) for j in range(min(n, most))]


def loan_slot(o, t, k, rank, first, family, weeks, amount):
    """The town's slot k ([0x9C6 + 6·k]: 0..2 the moneylender's offers, 3..5 the townsmen's requests)."""
    o.uc.mem_write(o.town(t) + 0x9C6 + 6 * k, bytes([rank, first, family, weeks]) + struct.pack('<H', amount))


def loan_terms(o, k):
    return bytes(o.uc.mem_read(LOAN_BUFFER + 0x10 * k, 0x10))


def loan_window(o, function, t, what):
    """0x51CE80 or 0x51D160 on the town, with the window's buffer: (count, the 3 terms)."""
    n = s32_of(direct(o, function, [LOAN_BUFFER, LOAN_CHANGED], ecx=o.town(t)))
    got = [loan_terms(o, k) for k in range(3)]
    print(f'  {what}: {n}, changed {o.u8(LOAN_CHANGED)}: ' + ' '.join(g.hex() for g in got))
    return n, got


def loan_events(o, what):
    for i, a in rt.events(o):
        if o.u16(a + 6) == 1:
            raw = bytes(o.uc.mem_read(a, 0x18))
            print(f'  {what}: loan {i} at {o.u32(a):#x}: {raw[8:].hex()}')


def lender_loans(o):
    """The moneylender's loans. What it offers (0x51CE80, the town's slots
    0..2): as set; with a bad payment history ([+0x1E]); half the player's
    value under 2000 with and without debts ([+0x498]); one that doesn't fit
    the limit (they stop there); his reputation in the town below 0 and over
    30; a value giving more than 2000 extra and an amount past 65535; a
    repayment over 65000; slots without weeks or amount. Taking two (0x29,
    the viewer's bytes) and the debts (0x4D9100); paying one off (0x2A, the
    viewer's bytes) and a debt to an AI merchant (0x29 made here); 0x2A with
    an event that isn't a loan, another debtor, a record out. The townsmen's
    requests (0x51D160, slots 3..5): the first call with the offers still in
    the buffer (no roll), again (it rolls), none; their terms (0x51D270) with
    each kind, a repayment over 65000, one under the rank's floor; granting
    one (0x29, the viewer's bytes) and the loans granted (0x4D9210); the two
    loans brought due in the day (event 0x01)."""
    s, t = player_docked_ship(o)
    m = o.u32(to.PLAYER)
    M = o.merchant(m)
    _, _, merchants, _ = o.counts()
    ai = rt.ai_with_office(o)[0]
    lender_copiers(o)
    print(f'ship {s} of the player {m} in town {t}; the AI merchant {ai}')

    def during(o):
        w32(o, M, 1000000)
        w32(o, M + 0x46C, 300000)
        w32(o, M + 0x498, 0)
        w8(o, M + 0x1E, 0)
        loan_slot(o, t, 0, 0, 1, 2, 12, 3000)
        loan_slot(o, t, 1, 0, 3, 4, 26, 9000)
        loan_slot(o, t, 2, 0, 5, 6, 8, 20000)
        loan_window(o, 0x51CE80, t, 'offers as set')
        w8(o, M + 0x1E, 2)
        loan_window(o, 0x51CE80, t, 'a bad payment history')
        w8(o, M + 0x1E, 0)
        w32(o, M + 0x46C, 3000)
        loan_window(o, 0x51CE80, t, 'half the value under 2000')
        w32(o, M + 0x46C, 300000)
        w32(o, M + 0x498, 149000)
        loan_window(o, 0x51CE80, t, 'debts: the limit 1000')
        w32(o, M + 0x498, 145000)
        loan_window(o, 0x51CE80, t, 'the limit 5000: the second doesn\'t fit')
        w32(o, M + 0x498, 0)
        reputation = o.u32(M + 0x2FC + 4 * t)
        for r in (-12.0, 75.0):
            o.uc.mem_write(M + 0x2FC + 4 * t, struct.pack('<f', r))
            loan_window(o, 0x51CE80, t, f'reputation {r}')
        w32(o, M + 0x2FC + 4 * t, reputation)
        w32(o, M + 0x46C, 2000000)
        loan_slot(o, t, 2, 0, 5, 6, 8, 64000)
        loan_window(o, 0x51CE80, t, 'the extra over 2000, an amount past 65535')
        w32(o, M + 0x46C, 300000)
        loan_slot(o, t, 2, 0, 5, 6, 40, 60000)
        loan_window(o, 0x51CE80, t, 'a repayment over 65000')
        loan_slot(o, t, 0, 0, 1, 2, 0, 3000)
        loan_slot(o, t, 1, 0, 3, 4, 26, 0)
        loan_window(o, 0x51CE80, t, 'no weeks, no amount')
        # Take two, as the window does (it recomputes them every frame).
        loan_slot(o, t, 0, 0, 1, 2, 12, 3000)
        loan_slot(o, t, 1, 0, 3, 4, 26, 9000)
        loan_slot(o, t, 2, 0, 5, 6, 8, 20000)
        for k in range(2):
            n, got = loan_window(o, 0x51CE80, t, f'before taking one ({k})')
            if n > 0:
                run_bytes(o, player_op('loan', got[0].hex()))
        loan_events(o, 'taken')
        n, owed = lender_list(o, 0, m)
        print(f'  he owes {n}: ' + ' '.join(e.hex() for e in owed))
        # A debt to an AI merchant (0x29 made here: lender ai, borrower the player).
        operation(o, 0x29, 2500, 0x30, b'\0\0\0', 2000, 6, 0, ai, m, t, 0x72, fmt='<IIB3sHBBBBBB')
        n, owed = lender_list(o, 0, m)
        print(f'  he owes {n}: ' + ' '.join(e.hex() for e in owed))
        for e in owed:
            if e[0x10] == 0xFF:  # the moneylender's, with the viewer's bytes
                run_bytes(o, player_op('pay-off', m, struct.unpack_from('<H', e, 4)[0]))
                break
        for e in owed:
            if e[0x10] == ai:
                operation(o, 0x2A, (m + 1) % merchants, struct.unpack_from('<H', e, 4)[0])
                operation(o, 0x2A, m, struct.unpack_from('<H', e, 4)[0])
                print(f'  the AI merchant\'s money {o.s32(o.merchant(ai))}, the player\'s {o.s32(M)}')
        other = next(i for i, a in rt.events(o) if o.u16(a + 6) not in (1,))
        operation(o, 0x2A, m, other)
        operation(o, 0x2A, m, o.u16(to.EVENTS + 0xC) + 5)
        n, owed = lender_list(o, 0, m)
        print(f'  he owes {n} now')
        # The townsmen's requests and granting one.
        loan_slot(o, t, 3, 2, 7, 8, 10, 14000)  # just over the rank's floor: the roll may fail
        loan_slot(o, t, 4, 5, 9, 10, 30, 30000)
        loan_slot(o, t, 5, 0, 11, 12, 52, 60000)
        loan_window(o, 0x51CE80, t, 'the offers in the buffer')
        loan_window(o, 0x51D160, t, 'requests (the first call)')
        n, got = loan_window(o, 0x51D160, t, 'requests again (it rolls)')
        loan_window(o, 0x51D160, t, 'requests a third time')
        for kind in (0, 4, 1, 2):
            raw = bytearray(loan_terms(o, 1))
            raw[0xF] = (raw[0xF] & 0xF0) | kind
            o.uc.mem_write(LOAN_BUFFER + 0x10, bytes(raw))
            direct(o, 0x51D270, [LOAN_BUFFER + 0x10], cdecl=True)
            print(f'  kind {kind}: {loan_terms(o, 1).hex()}')
        direct(o, 0x51D270, [LOAN_BUFFER + 0x20], cdecl=True)
        print(f'  the 60000: {loan_terms(o, 2).hex()}')
        raw = bytearray(loan_terms(o, 0))
        raw[0x5] = 7
        o.uc.mem_write(LOAN_BUFFER, bytes(raw))
        direct(o, 0x51D270, [LOAN_BUFFER], cdecl=True)
        print(f'  under the rank\'s floor: {loan_terms(o, 0).hex()}')
        if n > 1:
            run_bytes(o, player_op('loan', loan_terms(o, 1).hex()))
        n, granted = lender_list(o, 1, m)
        print(f'  granted {n}: ' + ' '.join(e.hex() for e in granted))
        for k in (3, 4, 5):
            loan_slot(o, t, k, 0, 0, 0, 0, 0)
        loan_window(o, 0x51D160, t, 'no requests')
        # The taken and the granted loans due in the day.
        now = o.u32(WORLD + 0x14)
        for i, a in rt.events(o):
            if o.u16(a + 6) == 1 and (o.u8(a + 0x10) == m or o.u8(a + 0x11) == m):
                rt.move_event(o, i, a, now + 0x10)
        loan_events(o, 'due')
    return during


def share_events(o, what):
    """The events 0x18 (company shares)."""
    out = []
    for i, a in rt.events(o):
        if o.u16(a + 6) == 0x18:
            raw = bytes(o.uc.mem_read(a, 0x18))
            print(f'  {what}: shares {i} at {o.u32(a):#x}: company {raw[0x10]} holder {raw[0x11]} {raw[0x12]}% '
                  f'offered {raw[0x13]} changed {struct.unpack_from("<I", raw, 8)[0]:#x} '
                  f'value {struct.unpack_from("<i", raw, 0xC)[0]}')
            out.append((i, a))
    return out


def lender_shares(o):
    """Company shares (0x2B, 0x4D98F0) and their dividends (event 0x18,
    0x4E8804). The player sells new shares of his company (the list to sell,
    0x4D9620, and by hand), the pool's offered set by hand, he buys some back
    (the list on sale, 0x4D9320; the viewer's bytes); an AI merchant sells
    shares of his (made here) and the player buys them (a holding), again
    (it grows), sells part back (the viewer's bytes), more than he holds (cut
    down), with none left (nothing); the owner past 45 % (cut down, then
    nothing); a buyer with a negative balance (compared unsigned: he buys)
    and short of the price; both or neither merchants; a pool with none; a
    second pool of the player's company (0x4D9320 drops it). Then the
    dividends: each event 0x18 moved to the quarter's first tick (before now)
    with the companies' value grown: the AI's pool and the player's holding
    (the letter 0x27), the player's pool rich and poor (a debt, the letter
    0x28, [+0x1E] at 0xFE), the value not grown, a company out and no shares
    (the event goes); one run on the queue's head with a tick past the
    year's end (the quarters' loop reads past its table); and the day: the
    pools and holdings work out what they offer."""
    s, t = player_docked_ship(o)
    m = o.u32(to.PLAYER)
    M = o.merchant(m)
    _, _, merchants, _ = o.counts()
    # A holding's offer goes by (company + tick) & 3: two AI merchants whose
    # residues differ from the player's and between them, where they can.
    now = o.u32(WORLD + 0x14)
    ais = sorted(rt.ai_with_office(o), key=lambda x: ((x + now) & 3) == ((m + now) & 3))
    ai = ais[0]
    ai2 = next((x for x in ais[1:] if (x + now) & 3 != (ai + now) & 3), ais[1])
    A = o.merchant(ai)
    lender_copiers(o)
    print(f'the player {m}; the AI merchants {ai}, {ai2}')

    def shares_op(*entry):
        operation(o, 0x2B, *struct.unpack('<2I', struct.pack('<4BI', *entry)))

    def lists(what):
        n, sale = lender_list(o, 2)
        print(f'  {what}: on sale {n}: ' + ' '.join(e.hex() for e in sale))
        n, sell = lender_list(o, 3)
        print(f'  {what}: to sell {n}: ' + ' '.join(e.hex() for e in sell))
        return sale, sell

    def pool(company, holder=0xFF):
        return next(((i, a) for i, a in rt.events(o) if o.u16(a + 6) == 0x18 and o.u8(a + 0x10) == company
                     and o.u8(a + 0x11) == holder), (None, None))

    def during(o):
        for x in (M, A):
            w32(o, x, 5000000)
        w32(o, M + 0x46C, 400000)
        w32(o, A + 0x46C, 250000)
        sale, sell = lists('as loaded')
        if sell and sell[0][1]:
            run_bytes(o, player_op('shares', sell[0].hex()))
        shares_op(m, 20, m, 0xFF, 15000)
        _, p = pool(m)
        w8(o, p + 0x13, 10)
        sale, sell = lists('his pool offering 10')
        for e in sale:
            if e[0] == m:
                run_bytes(o, player_op('shares', e.hex()))
        share_events(o, 'bought back')
        shares_op(ai, 30, ai, 0xFF, 9000)
        _, q = pool(ai)
        w8(o, q + 0x13, 15)
        sale, sell = lists('the AI\'s pool offering 15')
        for e in sale:
            if e[0] == ai:
                run_bytes(o, player_op('shares', e.hex()))
        w8(o, q + 0x13, 5)
        sale, sell = lists('again 5')
        for e in sale:
            if e[0] == ai:
                run_bytes(o, player_op('shares', e.hex()))
        _, h = pool(ai, m)
        if h:
            w8(o, h + 0x13, 10)
        sale, sell = lists('his holding offering 10')
        for e in sell[1:]:
            if e[0] == ai and e[1]:
                run_bytes(o, player_op('shares', e.hex()))
        if h:
            w8(o, h + 0x13, 5)
            shares_op(ai, 2, m, 0xFF, 1)  # fewer than offered
        shares_op(ai, 100, m, 0xFF, 1)
        shares_op(ai, 5, m, 0xFF, 1)
        share_events(o, 'sold back')
        shares_op(m, 40, m, 0xFF, 100)
        shares_op(m, 10, m, 0xFF, 100)
        w32(o, A, 0xFFFFFFFB)
        w8(o, q + 0x13, 3)
        shares_op(m, 3, 0xFF, ai, 50)
        w32(o, A, 10)
        shares_op(m, 3, 0xFF, ai, 50)
        w32(o, A, 5000000)
        shares_op(m, 3, ai2, ai, 50)
        shares_op(m, 3, 0xFF, 0xFF, 50)
        shares_op(ai2, 3, 0xFF, m, 50)
        w8(o, q + 0x12, 0)
        shares_op(ai, 3, 0xFF, m, 50)
        w8(o, q + 0x12, 20)
        shares_op(ai2, 3, m, 0xFF, 1)  # he holds none of ai2's
        w8(o, q + 0x13, o.u8(q + 0x12))
        shares_op(ai, o.u8(q + 0x12) + 2, 0xFF, m, 50)  # more than the pool has: he gets them all the same
        share_events(o, 'the AI\'s pool bought out')
        shares_op(ai, 20, ai, 0xFF, 900)
        w8(o, q + 0x13, 4)
        # The lists with small values: the AI's 300 (the price), the player's 80
        # (his new shares' price) and his own shares counted over 100 %.
        values = o.u32(A + 0x46C), o.u32(M + 0x46C)
        _, p = pool(m)
        held, changed = o.u8(p + 0x12), o.u32(p + 8)
        w32(o, p + 8, 0)  # his pool unchanged for long: new shares to sell
        offered = o.u8(p + 0x13)
        w8(o, p + 0x13, 1)
        for n, (va, vm) in ((10, (300, 80)), (41, (80, 80)), (150, (300, 400000))):
            w8(o, p + 0x12, n)
            w32(o, A + 0x46C, va)
            w32(o, M + 0x46C, vm)
            lists(f'his pool {n} %, the values {va} and {vm}')
        w8(o, p + 0x12, held)
        w8(o, p + 0x13, offered)
        w32(o, p + 8, changed)
        w32(o, A + 0x46C, values[0])
        w32(o, M + 0x46C, values[1])
        # A second pool of the player's company, offering some: 0x4D9320 drops it.
        a = direct(o, 0x4D8CF0, [o.u32(WORLD + 0x14) + 0x80], ecx=to.EVENTS)  # the record's address
        o.uc.mem_write(a + 6, struct.pack('<H', 0x18))
        o.uc.mem_write(a + 8, struct.pack('<Ii', 0, 100000) + bytes([m, 0xFF, 4, 2]))
        _, p = pool(m)
        w8(o, p + 0x13, 1)
        lists('a second pool of his')
        share_events(o, 'before the dividends')
        # Each case on the queue's head, run there (0x4E8804), then back in its
        # place. The quarter's first tick: the last one before now (so that a
        # debt goes after it, as it would in the game).
        now, year = o.u32(WORLD + 0x14), o.u16(YEAR)
        q0 = max((d + 365 * year) << 8 for d in (0, 90, 181, 273) if (d + 365 * year) << 8 <= now)

        def at(i):
            return o.u32(to.EVENTS) + 0x18 * i

        def on_head(i, tick, what):
            rt.move_event(o, i, at(i), 0)
            w32(o, at(i), tick)
            r = direct(o, 0x4E8804, [], ecx=to.EVENTS) & 0xFF
            print(f'  {what}: {r}, tick {o.u32(at(i)):#x}, offered {o.u8(at(i) + 0x13)}, the player {o.s32(M)} '
                  f'[+0x1E] {o.u8(M + 0x1E)}, the AI {o.s32(A)}')
            rt.move_event(o, i, at(i), o.u32(at(i)))

        def grown(i, by):
            w32(o, at(i) + 0xC, o.s32(o.merchant(o.u8(at(i) + 0x10)) + 0x46C) - by)

        ip, iq, ih, ib = pool(m)[0], pool(ai)[0], pool(ai, m)[0], pool(m, ai)[0]
        if iq is not None:
            grown(iq, 60000)
            on_head(iq, q0, "the AI's pool, grown")
        if ih is not None:
            grown(ih, 60000)
            on_head(ih, q0, 'his holding, grown')
            grown(ih, -5000)
            on_head(ih, q0, 'his holding, not grown')
        if ip is not None:
            grown(ip, 60000)
            w32(o, M, 200)
            w8(o, M + 0x1E, 0xFE)
            on_head(ip, q0, "his pool, he can't pay it all")
        if ib is not None:
            grown(ib, 60000)
            w32(o, M, 0)
            w8(o, M + 0x1E, 3)
            on_head(ib, q0, "the AI's holding of his company, he has nothing")
        w32(o, M, 5000000)
        loan_events(o, "the dividends' debts")
        # Other ticks: what the pools and the holdings offer.
        later = q0 + 0x100
        if ip is not None:
            w32(o, at(ip) + 8, now)
            on_head(ip, later, 'his pool, changed just now')
            w32(o, at(ip) + 8, 0)
            value = o.u32(M + 0x46C)
            w32(o, M + 0x46C, 50)
            on_head(ip, later, 'his pool, his value 50')
            w32(o, M + 0x46C, 250000)
            for n in (20, 4):
                w8(o, at(ip) + 0x12, n)
                on_head(ip, later, f'his pool, {n} %')
            w32(o, M + 0x46C, value)
        for i, what in ((ih, 'his holding'), (ib, "the AI's holding")):
            if i is None:
                continue
            company = o.merchant(o.u8(at(i) + 0x10))
            value = o.u32(company + 0x46C)
            w32(o, company + 0x46C, 50)
            on_head(i, later, f'{what}, the value 50')
            w32(o, company + 0x46C, 250000)
            w8(o, at(i) + 0x12, 30)
            for c in (250000 * 16, 250000 * 8, 250000 * 2, 1000):
                w32(o, at(i) + 0xC, c)
                on_head(i, later, f'{what}, its value {c} (by {(o.u8(at(i) + 0x10) + o.u32(WORLD + 0x14)) & 3})')
            w32(o, company + 0x46C, value)
        if iq is not None:
            on_head(iq, (365 * (year + 1) + 10) << 8, "the AI's pool, a tick past the year")
            q1 = min((d + 365 * year) << 8 for d in (90, 181, 273, 365) if (d + 365 * year) << 8 > now)
            on_head(iq, q1 - 0x10, "the AI's pool, just before the next quarter")
        # One out of the world and one without shares (the event goes).
        for company, n in ((merchants, 5), (ai2, 0)):
            i = (direct(o, 0x4D8CF0, [q0], ecx=to.EVENTS) - o.u32(to.EVENTS)) // 0x18  # it returns the record
            o.uc.mem_write(at(i) + 6, struct.pack('<H', 0x18))
            o.uc.mem_write(at(i) + 8, struct.pack('<Ii', 0, 0) + bytes([company, 0xFF, n, 0]))
            on_head(i, q0, f'company {company}, {n} %')
        # And the day, from the quarter's first tick.
        for i, a in share_events(o, 'to the quarter'):
            if o.u8(a + 0x10) < merchants and o.u8(a + 0x12):
                grown(i, 30000)
                rt.move_event(o, i, at(i), q0)
    return during


# --- Building: demolish (0x26), streets (0x25) and the build window ---

def record_at(o, t, r):
    """The address of town t's building record r ([0x75C], 8 bytes each)."""
    return o.u32(o.town(t) + 0x75C) + 8 * r


def build_player_ops(o):
    """Operations 0x26 (0x53CF50, a merchant demolishes a building of his)
    and 0x25 (0x5244F0, streets, with 0x4FB580). Demolishing, for a merchant
    with buildings in his office's lists: a town and a merchant out, a
    merchant without an office there, a cell with nothing (the office
    recounted), the town's bit 0x10 with the cell without and with its bit
    0x80 (the building put on sale first, 0x28: refused), a business (his
    reputation − 1), a house, one moved into the town's works [0x762]
    (found in the third list). Streets: a town and a merchant out (the town's
    way: refused), no office there, short of money, rich with his office
    holding part of the materials, 40 planned
    streets around the middle (16 built), none planned (the rings walk the
    whole grid)."""
    towns, offices, merchants, _ = o.counts()
    for f in range(offices):
        m, t = o.u16(o.office(f) + 0x2C4), o.u8(o.office(f) + 0x2C6)
        recs = office_records(o, t, f) if m < merchants else []
        if any(o.u8(record_at(o, t, r) + 3) < 0x15 for r, _, _ in recs) and \
                any(0x15 <= o.u8(record_at(o, t, r) + 3) < 0x1E for r, _, _ in recs):
            break
    else:
        raise RuntimeError('no office with a business and a house')
    other = next(k for k in range(merchants) if facilities_office(o, k, t) is None)
    a = o.town(t)
    M = o.merchant(m)

    def kind(r):
        return o.u8(record_at(o, t, r) + 3)

    business = next((r, x, y) for r, x, y in recs if kind(r) < 0x15)
    houses = [(r, x, y) for r, x, y in recs if 0x15 <= kind(r) < 0x1E]
    print(f'merchant {m}, office {f} in town {t}: {len(recs)} buildings; merchant {other} without an office there')

    def state(what):
        rep = struct.unpack('<f', bytes(o.uc.mem_read(M + 0x120 + 12 * t, 4)))[0]
        print(f'  {what}: buildings {len(office_records(o, t, f))}, works {len(rt.records_of(o, t, o.u16(a + 0x762)))}, '
              f'free {o.u16(a + 0x760)}, reputation {rep:.3f}, money {o.s32(M)}, streets {o.u16(a + 0x790)}/{o.u16(a + 0x792)}')

    def during(o):
        r, x, y = business
        operation(o, 0x26, towns, m, x, y)
        operation(o, 0x26, t, merchants, x, y)
        operation(o, 0x26, t, other, x, y)
        operation(o, 0x26, t, m, 0x100, y)
        state('nothing there')
        cell = grid_at(o, t, x, y)
        flags, value = o.u8(a + 0x2C8), o.u8(cell)
        w8(o, a + 0x2C8, flags | 0x10)
        w8(o, cell, value & 0x7F)
        operation(o, 0x26, t, m, x, y)
        w8(o, cell, value | 0x80)
        operation(o, 0x28, t, m, x, y)
        operation(o, 0x26, t, m, x, y)
        state('on sale')
        w8(o, cell, value)
        w8(o, a + 0x2C8, flags)
        r, x, y = next((r, x, y) for r, x, y in office_records(o, t, f) if kind(r) < 0x15 and (r, x, y) != business)\
            if sum(1 for q, _, _ in office_records(o, t, f) if kind(q) < 0x15) > 1 else business
        operation(o, 0x26, t, m, x, y)
        state(f'a business ({r}) demolished')
        r, x, y = houses[0]
        operation(o, 0x26, t, m, x, y)
        state(f'a house ({r}) demolished')
        if len(houses) > 1:
            # Another house moved from his office's lists into the town's works.
            r, x, y = houses[1]
            for head in (o.office(f) + 0x2D0, o.office(f) + 0x2CE):
                link = head
                while o.u16(link) < o.u16(a + 0x776) and o.u16(link) != r:
                    link = record_at(o, t, o.u16(link)) + 4
                if o.u16(link) == r:
                    w16(o, link, o.u16(record_at(o, t, r) + 4))
                    w16(o, record_at(o, t, r) + 4, o.u16(a + 0x762))
                    w16(o, a + 0x762, r)
                    break
            operation(o, 0x26, t, m, x, y)
            state(f'a house ({r}) in the works demolished')
        # Streets.
        operation(o, 0x25, towns, m)
        operation(o, 0x25, t, merchants)
        operation(o, 0x25, t, other)
        # Streets still to make ([0x790] < [0x792]; in the saves all are done).
        w16(o, a + 0x792, o.u16(a + 0x790) + 40)
        money = o.s32(M)
        w32(o, M, 10)
        operation(o, 0x25, t, m)
        state('short of money')
        w32(o, M, 5000000)
        b = o.office(f)
        for ware, n in ((0x13, 3 * 2000), (0xB, 1 * 2000 + 500), (0xC, 0)):
            w32(o, b + 4 + 4 * ware, n)
        for ware in (0x13, 0xB, 0xC, 0x11):
            w32(o, a + 4 + 4 * ware, 200 * 2000)
        mx, my = o.u8(a + 0x800), o.u8(a + 0x801)
        g, h = o.u32(a + 0x80C), o.u32(a + 0x7AC)
        planned = 0
        for dx in range(-6, 7):
            for dy in range(-12, 13):
                c = g + h * (mx + dx) + my + dy
                if planned < 40 and o.u8(c) in (0x81,) and (dx + dy) % 3 == 0:
                    w8(o, c, 0x82 if planned % 2 else 0x83)
                    planned += 1
        print(f'  {planned} streets planned around ({mx}, {my})')
        operation(o, 0x25, t, m)
        state('rich, the streets planned')
        operation(o, 0x25, t, m)
        operation(o, 0x25, t, m)
        state('again (none left nearby)')
        # Three planned streets only, the middle and two corners: the rings
        # walk the whole grid, cut at its edges.
        gw = o.s32(a + 0x7A8)
        cells = bytearray(o.uc.mem_read(g, gw * h))
        for i, c in enumerate(cells):
            if c in (0x82, 0x83):
                cells[i] = 0x81
        for x, y in ((mx, my), (1, 2), (gw - 2, h - 3)):
            cells[h * x + y] = 0x82
        o.uc.mem_write(g, bytes(cells))
        w16(o, a + 0x792, o.u16(a + 0x790) + 20)
        operation(o, 0x25, t, m)
        state('three planned streets: the middle and two corners')
        w32(o, M, money)
    return during


def letter_ops(o):
    """Operations 0x70 (0x536AFB → 0x4D6AF0: a merchant's letter settings, which the letters' settings
    window 0x408F50 queues on closing) and 0x71 (0x536B1C: with its [+0x8], 0x4D7960 repairs the letters'
    chains; then 0x4D6780 removes the old ones). Four letters sent to the player (one of each class and
    an old one, 0x4D6530 called directly); the viewer's bytes (player_ops letters, letters-check) for the
    player with the window's values (the days of each class 0..5, the event sound, the notices); 0x70 for a
    merchant out; 0x71 with 0 and with 1 on the mailbox as it is; then 0x71 with 1 after breaking the
    chains: the player's last letter linked back to his first (a loop), to the chronicle's first, the free
    list's head over the count, the free list's head on a letter of his chain; and 0x71 for a merchant
    out."""
    merchants, player = o.counts()[2], o.u32(to.PLAYER)
    M = o.merchant(player)

    def link_of(e):
        return o.u32(MAILBOX) + 0x10 * e + 6

    def chain(head):
        out, e, count = [], o.u16(head), o.u16(MAILBOX + 6)
        while e < count and len(out) <= count:
            out.append(e)
            e = o.u16(link_of(e))
        return out

    def state(what):
        print(f'  {what}: free {o.u16(MAILBOX + 4)}, count {o.u16(MAILBOX + 6)}, chronicle {o.u16(MAILBOX + 8)}, '
              f'the player\'s {chain(M + 0xA)[:12]}, settings {bytes(o.uc.mem_read(M + 0x38, 8)).hex()} '
              f'{o.u8(M + 0x26):#x}')

    def during(o):
        at = _trampolines.get((0x4D6530, 2, False), TRAMPOLINES + 0x40 * len(_trampolines))
        to.MAIL_SITES.add(at + 4 * 2 + 5)
        kinds = {}
        for kind in range(0x86):
            kinds.setdefault(o.u8(0x6C0198 + kind), kind)
        day, month, year = o.u8(0x6DE4A0), o.u8(0x6DE4A1), o.u16(0x6DE4A2)
        for c, old in ((1, False), (2, False), (3, False), (1, True)):
            if c in kinds:
                y = year - 1 if old else year
                o.uc.mem_write(SCRATCH_LETTER, bytes([day, 0x80 | month, y & 0xFF, y >> 8, kinds[c], 0, 0xFF, 0xFF])
                               + bytes(8))
                direct(o, 0x4D6530, [player, SCRATCH_LETTER], ecx=MAILBOX)
        state('four letters sent')
        saved = bytes(o.uc.mem_read(M + 0x38, 8)), o.u8(M + 0x26)
        for days, sound, notices in (((1, 2, 3), 1, 0x2D), ((0, 0, 0), 0, 0x3F), ((4, 1, 5), 1, 0x12)):
            run_bytes(o, player_op('letters', player, *days, sound, notices))
            state(f'settings {days}, sound {sound}, notices {notices:#x}')
        operation(o, 0x70, merchants)
        run_bytes(o, player_op('letters-check', player, 0))
        state('0x71 with 0 (the old letter and the class 5 removed)')
        o.uc.mem_write(M + 0x38, saved[0])
        w8(o, M + 0x26, saved[1])
        run_bytes(o, player_op('letters-check', player, 1))
        state('0x71 with 1, nothing to repair')
        mine = chain(M + 0xA)
        if len(mine) >= 2:
            last = link_of(mine[-1])
            end = o.u16(last)
            w16(o, last, mine[0])
            run_bytes(o, player_op('letters-check', player, 1))
            state('his last letter linked back to his first')
            mine = chain(M + 0xA)
            last = link_of(mine[-1])
            end = o.u16(last)
            if o.u16(MAILBOX + 8) < o.u16(MAILBOX + 6):
                w16(o, last, o.u16(MAILBOX + 8))
                run_bytes(o, player_op('letters-check', player, 1))
                state('his last letter linked to the chronicle\'s first')
                w16(o, last, end)
        free = o.u16(MAILBOX + 4)
        w16(o, MAILBOX + 4, o.u16(MAILBOX + 6) + 3)
        operation(o, 0x71, player, 1)
        state('the free list\'s head over the count')
        mine = chain(M + 0xA)
        if mine and free < o.u16(MAILBOX + 6):
            w16(o, MAILBOX + 4, mine[0])
            operation(o, 0x71, player, 1)
            state('the free list\'s head on his first letter')
        w16(o, MAILBOX + 4, free)
        operation(o, 0x71, merchants, 1)
        state('0x71 for a merchant out')
    return during


def pirate_ops(o):
    """0x518750 (whether a ship's menu lets it turn pirate: called directly on ships) and the operation 0x0D
    (0x5358D5 → 0x5386C0, the menu's «Pirate» / «Civil»). 0x518750 on a ship sailing (0xF) out of a convoy,
    pursued ([+0x50], then [+0x52]), docked, on a town's point; on a sailing convoy's guide and member with
    the convoy's flag 2 and without, of another owner, not sailing. Then 0x0D with 1 and 0 on the free
    sailing ship, on a docked one, on the convoy (its flag 2, another owner, not sailing) and on a ship out
    (the viewer's bytes, player_ops pirate)."""
    towns, _, merchants, ships = o.counts()
    convoys = o.u16(to.SHIPS + 0xF6)

    def ship_state(s):
        return o.u16(o.ship(s) + 0x134)

    free = [s for s in range(ships) if ship_state(s) == 0xF and o.u16(o.ship(s) + 8) >= convoys]
    docked = next(s for s in range(ships) if ship_state(s) == 0 and o.u16(o.ship(s) + 8) >= convoys)
    if not free:
        raise RuntimeError('no ship sailing out of a convoy')
    s = free[0]
    a = o.ship(s)
    # A convoy: one sailing if the save has it, else the free ship's own made one by hand (its [+0x8], the
    # convoy's guide [+0x10], owner, state [+0x12] and flags [+0x14]).
    sailing_convoys = [c for c in range(convoys) if o.u16(convoy_at(o, c) + 0x12) == 0xF and o.u16(convoy_at(o, c) + 0x10) < ships]
    print(f'ship {s} sailing alone, ship {docked} docked; convoys sailing {sailing_convoys[:4]}')

    def during(o):
        def ask(what, k):
            r = direct(o, 0x518750, [], ecx=o.ship(k)) & 0xFF
            print(f'  0x518750 {what} (ship {k}): {r}')
        ask('sailing alone', s)
        old = o.u16(a + 0x50), o.u16(a + 0x52)
        w16(o, a + 0x50, 0)
        ask('pursued at [+0x50]', s)
        w16(o, a + 0x50, old[0])
        w16(o, a + 0x52, 0)
        ask('pursued at [+0x52]', s)
        w16(o, a + 0x52, old[1])
        ask('docked', docked)
        xy = bytes(o.uc.mem_read(a + 0x1C, 8))
        o.uc.mem_write(a + 0x1C, bytes(o.uc.mem_read(o.ship(docked) + 0x1C, 8)))
        ask('on its town\'s point', s)
        o.uc.mem_write(a + 0x1C, xy)
        if sailing_convoys:
            c = sailing_convoys[0]
            C = convoy_at(o, c)
            guide = o.u16(C + 0x10)
            member = o.u16(o.ship(guide) + 6)
            flags, owner, state = o.u8(C + 0x14), o.u8(C), o.u16(C + 0x12)
            w8(o, C + 0x14, flags | 2)
            ask('a sailing convoy\'s guide, its flag 2', guide)
            if member < ships and o.u16(o.ship(member) + 8) == c:
                ask('its member', member)
            pursued = o.u16(o.ship(guide) + 0x50)
            w16(o, o.ship(guide) + 0x50, 0)
            ask('its guide pursued', guide)
            w16(o, o.ship(guide) + 0x50, pursued)
            w8(o, C + 0x14, flags & ~2)
            ask('without the flag 2', guide)
            w8(o, C + 0x14, flags | 2)
            w8(o, C, (owner + 1) % merchants)
            ask('of another owner', guide)
            w8(o, C, owner)
            w16(o, C + 0x12, 0x10)
            ask('not sailing', guide)
            w16(o, C + 0x12, state)
            for flag in (1, 0):
                run_bytes(o, player_op('pirate', guide, flag))
            w8(o, C, (owner + 1) % merchants)
            operation(o, 0x0D, guide, 1)
            w8(o, C, owner)
            w16(o, C + 0x12, 0x10)
            operation(o, 0x0D, guide, 1)
            operation(o, 0x0D, guide, 0)
            w16(o, C + 0x12, state)
            w8(o, C + 0x14, flags & ~2)
            operation(o, 0x0D, guide, 1)
            w8(o, C + 0x14, flags)
        for flag in (1, 0):
            run_bytes(o, player_op('pirate', s, flag))
            print(f'  0x0D {flag}: [+0x3D] {o.u8(a + 0x3D):#x}, [+0x15C] {o.u8(a + 0x15C)}, [+0x136] {o.u8(a + 0x136)}')
        operation(o, 0x0D, docked, 1)
        operation(o, 0x0D, ships, 1)
    return during


def pirate_sailing(o):
    """A day of ships after the ship menu's «Pirate» (0x0D with 1, the viewer's bytes) on the first ships
    sailing out of a convoy (the ship update 0x506720 with their [+0x15C] at 1), and «Civil» on the last of
    them half a day later is left to the queue: only the first operations run here."""
    _, _, _, ships = o.counts()
    convoys = o.u16(to.SHIPS + 0xF6)
    free = [s for s in range(ships) if o.u16(o.ship(s) + 0x134) == 0xF and o.u16(o.ship(s) + 8) >= convoys][:3]
    if not free:
        raise RuntimeError('no ship sailing out of a convoy')
    print(f'ships {free} turn pirate')

    def during(o):
        for s in free:
            run_bytes(o, player_op('pirate', s, 1))
    return during


BUILD_MENU = to.SENTINEL + 0x200  # the copier of the build window's menu 0x47AB20 (probes_unchecked.cpp kBuildMenu)
BUILD_FITS = to.SENTINEL + 0x280  # of whether a building fits a cell, 0x47B8C0 (kBuildFits)
BUILD_MARKS = to.SENTINEL + 0x300  # of where it fits, 0x47B830 (kBuildMarks)
BUILD_RET4 = to.SENTINEL + 0x380  # ret 4: the methods of the window's buttons
TOWN_VIEW = 0x6E51AC  # *: the town view, which the emulator hasn't
BUILD_COST = SCRATCH + 0x80  # 0x51F3A0's 0x44 bytes
# 0x672FC0: the town view's picture of each building type.
BUILD_CATALOG = bytes.fromhex('2c1b0b085d57585b595356645a5e60555c636261545151515050502a2a2a6604030b07000205093352'
                              '656768693701' '5f29292929' '2f2f2e2f2f')
# 0x672F84: each type's class.
BUILD_CLASS = (3, 7, 0, 0, 4, 0, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 10, 10, 3,
               2, 3, 0, 0, 3, 0, 5, 3, 2, 2, 3, 6, 7, 6, 6, 6, 6, 6, 6, 8, 9, 8, 9)
# The cell class (& 0x3F) some types fit (0x47B8C0).
BUILD_CELL = {0x03: 0x1D, 0x05: 0x0E, 0x1F: 0x19, 0x20: 0x1B, 0x21: 0x14, 0x22: 0x1A, 0x23: 0x16, 0x24: 0x17,
              0x25: 0x15, 0x27: 0x18, 0x2F: 0x0F, 0x30: 0x1C}


def build_copiers(o):
    """The copiers of the build window (stdcall), on a window of their own
    (its buttons' methods all ret 4) and, for the menu, a town view of their
    own (*0x6E51AC set during the call, the oracle's put back after): BUILD_MENU (town, category):
    0x47AB20, its 8 types ([+0xC74]) into SCRATCH, the category it shows
    ([+0x104C]) and the reputation's text ([+0x1060]) after; BUILD_FITS
    (town, x, y, type): 0x47B8C0 with the window's type, al and the type after
    into SCRATCH, SCRATCH + 4, and the picture it asks the town view for
    (0x5928A0, stubbed here: its picture and key) into SCRATCH + 0x10, + 0x14
    (−1 if none); BUILD_MARKS (town, type): 0x47B830, summed into SCRATCH by a
    hook (how many cells, the sums of their indices, squares and cubes, and
    whether there is a map) and the map freed. The buttons' texts (0x4C7780)
    and the town view's picture (0x58CC90) are skipped."""
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP
    win, view, vtable = o.alloc(0x1200), o.alloc(0xC330), o.alloc(0x400)
    o.uc.mem_write(BUILD_RET4, bytes([0xC2, 0x04, 0x00]))
    o.uc.mem_write(vtable, struct.pack('<I', BUILD_RET4) * 0x100)
    o.uc.mem_write(win, struct.pack('<I', vtable) * (0x1200 // 4))

    def call(at, code, target):
        return bytes([0xE8]) + struct.pack('<i', target - (at + len(code) + 5))

    def put(at, code):
        assert len(code) <= 0x80
        o.uc.mem_write(at, code)

    at = BUILD_MENU
    # The oracle's own town view (town_oracle.TOWN_VIEW), put back after: kept
    # in the slack of the view's block (alloc rounds 0xC330 up to 0xC340), so
    # the heap stays as it was.
    saved = view + 0xC330
    code = bytes([0x8B, 0x44, 0x24, 0x04, 0xA3]) + struct.pack('<I', view + 0xC324)  # the view's town
    code += bytes([0x8B, 0x44, 0x24, 0x08, 0xA3]) + struct.pack('<I', win + 0x104C)  # the category
    code += bytes([0xA1]) + struct.pack('<I', TOWN_VIEW) + bytes([0xA3]) + struct.pack('<I', saved)
    code += bytes([0xC7, 0x05]) + struct.pack('<II', TOWN_VIEW, view)
    code += bytes([0xB9]) + struct.pack('<I', win)
    code += call(at, code, 0x47AB20)
    code += bytes([0xA1]) + struct.pack('<I', saved) + bytes([0xA3]) + struct.pack('<I', TOWN_VIEW)
    code += bytes([0x56, 0x57, 0xBE]) + struct.pack('<I', win + 0xC74) + bytes([0xBF]) + struct.pack('<I', SCRATCH)
    code += bytes([0xB9]) + struct.pack('<I', 8) + bytes([0xFC, 0xF3, 0xA5, 0x5F, 0x5E])  # rep movsd
    code += bytes([0xA1]) + struct.pack('<I', win + 0x104C) + bytes([0xA3]) + struct.pack('<I', SCRATCH + 0x20)
    code += bytes([0xA1]) + struct.pack('<I', win + 0x1060) + bytes([0xA3]) + struct.pack('<I', SCRATCH + 0x24)
    put(at, code + bytes([0xC2, 0x08, 0x00]))

    at = BUILD_FITS
    code = bytes([0x8B, 0x44, 0x24, 0x10, 0xA3]) + struct.pack('<I', win + 0x1070)  # the window's type
    code += bytes([0xC7, 0x05]) + struct.pack('<II', SCRATCH + 0x10, 0xFFFFFFFF)
    code += bytes([0xC7, 0x05]) + struct.pack('<II', SCRATCH + 0x14, 0xFFFFFFFF)
    code += bytes([0xFF, 0x74, 0x24, 0x0C]) * 3  # town, x, y
    code += bytes([0xB9]) + struct.pack('<I', win)
    code += call(at, code, 0x47B8C0)
    code += bytes([0x25]) + struct.pack('<I', 0xFF) + bytes([0xA3]) + struct.pack('<I', SCRATCH)
    code += bytes([0xA1]) + struct.pack('<I', win + 0x1070) + bytes([0xA3]) + struct.pack('<I', SCRATCH + 4)
    put(at, code + bytes([0xC2, 0x10, 0x00]))

    at = BUILD_MARKS
    code = bytes([0x8B, 0x44, 0x24, 0x08, 0xA3]) + struct.pack('<I', win + 0x1070)
    code += bytes([0xFF, 0x74, 0x24, 0x04, 0xB9]) + struct.pack('<I', win)  # the town
    code += call(at, code, 0x47B830)
    summary = at + len(code)
    code += bytes([0x90, 0x85, 0xC0, 0x74, 0x09, 0x50])  # nop (the hook); test eax, eax; jz; push eax
    code += call(at, code, 0x64F7E2)  # operator delete
    put(at, code + bytes([0x83, 0xC4, 0x04, 0xC2, 0x08, 0x00]))

    def skip(n):
        def f(uc, address, size, data):
            esp = uc.reg_read(UC_X86_REG_ESP)
            uc.reg_write(UC_X86_REG_EIP, o.u32(esp))
            uc.reg_write(UC_X86_REG_ESP, esp + 4 + 4 * n)
        return f

    def picture(uc, address, size, data):
        esp = uc.reg_read(UC_X86_REG_ESP)
        w32(o, SCRATCH + 0x10, o.u32(esp + 4))
        w32(o, SCRATCH + 0x14, o.u32(esp + 8))
        uc.reg_write(UC_X86_REG_EAX, 0)
        uc.reg_write(UC_X86_REG_EIP, o.u32(esp))
        uc.reg_write(UC_X86_REG_ESP, esp + 16)

    def marks(uc, address, size, data):
        eax, esp = uc.reg_read(UC_X86_REG_EAX), uc.reg_read(UC_X86_REG_ESP)
        a = o.town(o.u32(esp + 4))
        sums = [0, 0, 0, 0]
        if eax:
            for i, b in enumerate(bytes(uc.mem_read(eax, o.s32(a + 0x7A8) * o.s32(a + 0x7AC)))):
                if b:
                    sums = [sums[0] + 1, sums[1] + i, sums[2] + i * i, sums[3] + i * i * i]
        for k in range(4):
            w32(o, SCRATCH + 4 * k, sums[k])
        w32(o, SCRATCH + 0x10, 1 if eax else 0)

    for address, f in ((0x4C7780, skip(1)), (0x58CC90, skip(1)), (0x5928A0, picture), (summary, marks)):
        o.uc.hook_add(UC_HOOK_CODE, f, begin=address, end=address)
        o.uc.ctl_remove_cache(address, address + 1)


def build_menu_of(o, t, category, what):
    direct(o, BUILD_MENU, [t, category])
    types = [hex(s32_of(o.u32(SCRATCH + 4 * k))) for k in range(8) if s32_of(o.u32(SCRATCH + 4 * k)) >= 0]
    print(f'  {what}: category {s32_of(category)} -> {s32_of(o.u32(SCRATCH + 0x20))}: {types}'
          f'{", the reputation text" if o.u32(SCRATCH + 0x24) else ""}')
    return types


def build_fits_at(o, t, x, y, kind, what=''):
    """0x47B8C0 through BUILD_FITS: (fits, the window's type after, the picture, its key)."""
    direct(o, BUILD_FITS, [t, x, y, kind])
    got = o.u32(SCRATCH) & 0xFF, s32_of(o.u32(SCRATCH + 4)), o.u32(SCRATCH + 0x10), o.u32(SCRATCH + 0x14)
    if what:
        print(f'  {what}: type {kind:#x} at ({x}, {y}): fits {got[0]}, type {got[1]:#x}, picture {got[2]:#x} {got[3]:#x}')
    return got


def build_marks_of(o, t, kind, what=''):
    direct(o, BUILD_MARKS, [t, kind])
    got = [o.u32(SCRATCH + 4 * k) for k in range(5)]
    print(f'  marks of {kind:#x}{" " + what if what else ""}: {got[0]} cells{"" if got[4] else ", no map"}')
    return got


def build_town(o):
    """The player, his home town (with his office there, or the first town
    of an office of his) and a town where he has none."""
    p = o.u32(to.PLAYER)
    towns = o.counts()[0]
    t = o.u8(o.merchant(p) + 0x19)
    f = facilities_office(o, p, t)
    if f is None:
        t = office_town(o, p)
        f = facilities_office(o, p, t)
    other = next(k for k in range(towns) if facilities_office(o, p, k) is None)
    print(f'player {p}: office {f} in town {t}; no office in town {other}')
    return p, t, f, other


def fortifications(o, t):
    """[(record address, type, pending)] of the town's fortification list [0x768]."""
    a = o.town(t)
    return [(record_at(o, t, r), kind, pending) for r, _, _, _, kind, pending in records(o, t, 0x768)]


def build_menus(o):
    """The build window's menu (0x47AB20 through BUILD_MENU), its costs
    (0x51F3A0) and its marks of where each type fits (0x47B830 through
    BUILD_MARKS). The menu: housing with the player's office, in a town
    without the bits 0xE0 (not the mayor, the mayor without and with
    buildings), and where he has none (no rank: the reputation's text; a
    rank; the mayor); public buildings for the mayor with the town's bits
    [0x76C] one by one (the first four missing, then the tavern, the repair
    dock, the church; the hospital group with 0x200, 0x400, 0x1000) and for
    another player; fortifications for another player (category 3) and the
    mayor without the bit 0x10, with it, with the list's types changed (3
    port bombards and cannons, 6 gate ones, a gate) and the wall pending or
    not; the businesses by the productivity (400 and 401) in the categories
    3, 4, 7 and −1. The costs of the window's types; the marks of every type,
    with the list changed and in a siege."""
    p, t, f, t2 = build_town(o)
    a, a2 = o.town(t), o.town(t2)
    rank_at = o.merchant(p) + 0x39C + t2

    def during(o):
        build_copiers(o)
        flags, mayor, bits, wall = o.u32(a + 0x2C8), o.u8(a + 0x6F1), o.u32(a + 0x76C), o.u16(a + 0x766)
        flags2, mayor2, rank = o.u32(a2 + 0x2C8), o.u8(a2 + 0x6F1), o.u8(rank_at)
        build_menu_of(o, t, 0, 'with his office')
        w32(o, a + 0x2C8, flags & ~0xE0)
        w8(o, a + 0x6F1, (p + 1) & 0xFF)
        build_menu_of(o, t, 0, 'no bits 0xE0, not the mayor')
        w8(o, a + 0x6F1, p)
        w32(o, a + 0x76C, 0)
        build_menu_of(o, t, 0, 'the mayor, nothing built')
        w32(o, a + 0x76C, bits)
        build_menu_of(o, t, 0, 'the mayor')
        w32(o, a + 0x2C8, flags)
        w32(o, a2 + 0x2C8, flags2 | 0x40)
        w8(o, a2 + 0x6F1, (p + 1) & 0xFF)
        w8(o, rank_at, 0)
        build_menu_of(o, t2, 0, 'no office, no rank')
        w8(o, rank_at, 1)
        build_menu_of(o, t2, 0, 'no office, a rank')
        w8(o, rank_at, 0)
        w8(o, a2 + 0x6F1, p)
        build_menu_of(o, t2, 0, 'no office, the mayor')
        w8(o, rank_at, rank)
        w8(o, a2 + 0x6F1, mayor2)
        w32(o, a2 + 0x2C8, flags2)
        build_menu_of(o, o.counts()[0], 0, 'a town out (the first)')
        for value in (0, 0x10, 0x11, 0x13, 0x17, 0x0F, 0x1F, 0x3F, 0x5F, 0x7F, 0x27F, 0x67F, 0x127F, 0x7FFF):
            w32(o, a + 0x76C, value)
            build_menu_of(o, t, 1, f'the mayor, [0x76C] {value:#x}')
        w8(o, a + 0x6F1, (p + 1) & 0xFF)
        for value in (0x210, 0x7FFF):
            w32(o, a + 0x76C, value)
            build_menu_of(o, t, 1, f'not the mayor, [0x76C] {value:#x}')
        w32(o, a + 0x76C, bits)
        build_menu_of(o, t, 2, 'not the mayor')
        w8(o, a + 0x6F1, p)
        forts = fortifications(o, t)
        types = [kind for _, kind, _ in forts]
        print(f'  fortifications: {[hex(k) for k in types]}, pending walls from {wall}')
        w32(o, a + 0x76C, bits & ~0x10)
        build_menu_of(o, t, 2, 'the mayor, no bit 0x10')
        w32(o, a + 0x76C, bits | 0x10)
        for pending in (0xFFFF, o.u16(a + 0x768)):
            w16(o, a + 0x766, pending)
            build_menu_of(o, t, 2, f'the mayor, pending walls {pending:#x}')
        pattern = (0x30, 0x31, 0x30, 0x32, 0x33, 0x32, 0x32, 0x32, 0x32, 0x37)
        for at, kind in zip((r for r, _, _ in forts), pattern):
            w8(o, at + 3, kind)
        build_menu_of(o, t, 2, 'the mayor, the list changed')
        for at, _, _ in forts:
            w8(o, at + 3, 0x36)
        build_menu_of(o, t, 2, 'the mayor, only walls')
        for at, kind, _ in forts:
            w8(o, at + 3, kind)
        w16(o, a + 0x766, wall)
        w32(o, a + 0x76C, bits)
        w8(o, a + 0x6F1, mayor)
        saved = bytes(o.uc.mem_read(a + 0x888, 0x110))
        for k in range(17):
            w16(o, a + 0x888 + 0x10 * k, 400 + (k % 2))
        for category in (3, 4, 7, 0xFFFFFFFF):
            build_menu_of(o, t, category, 'productivity 400 and 401')
        for k in range(17):
            w16(o, a + 0x888 + 0x10 * k, 1000)
        build_menu_of(o, t, 3, 'productivity 1000')
        build_menu_of(o, t, 4, 'productivity 1000')
        o.uc.mem_write(a + 0x888, saved)
        # The costs of the window's types.
        for town, kind in [(t2, 0), (t, 2), (t, 0x28), (t, 0x1E), (t, 0x15), (t, 0x1B), (t, 0x30), (t, 0x31),
                           (t, 0x32), (t, 0x33), (t, 0x34), (t, 0x35), (t, 0x36), (t, 0x37), (t, 0x38)]:
            o.uc.mem_write(BUILD_COST, bytes(0x44))
            w16(o, BUILD_COST + 0x3C, kind)
            w16(o, BUILD_COST + 0x3E, p)
            can = direct(o, 0x51F3A0, [BUILD_COST], ecx=o.town(town)) & 0xFF
            print(f'  0x51F3A0 type {kind:#x} in {town}: {can}, why not {o.u8(BUILD_COST + 0x43)}, '
                  f'cost {o.s32(BUILD_COST + 0x34)}')
        # The marks.
        for kind in range(0x39):
            build_marks_of(o, t, kind)
        build_marks_of(o, t2, 0, 'no office')
        for at, kind in zip((r for r, _, _ in forts), pattern):
            w8(o, at + 3, kind)
        if forts:
            w8(o, forts[-1][0] + 3, 0x38)
            w8(o, forts[-1][0] + 6, 0)
        for kind in range(0x30, 0x37):
            build_marks_of(o, t, kind, 'the list changed')
        for at, kind, pending in forts:
            w8(o, at + 3, kind)
            w8(o, at + 6, pending)
        w32(o, a + 0x2C8, flags | 0x10)
        for kind in (4, 5, 6, 0x0B, 0x15, 0x1E, 0x26, 0x30):
            build_marks_of(o, t, kind, 'in a siege')
        w32(o, a + 0x2C8, flags)
    return during


def grid_bytes(o, t):
    a = o.town(t)
    w, h = o.s32(a + 0x7A8), o.s32(a + 0x7AC)
    return bytes(o.uc.mem_read(o.u32(a + GRID), w * h)), h


def build_candidates(o, t, kind, most=3):
    """Cells to try a type at: up to `most` where it seems to fit (spread over
    the grid) and two beside them."""
    data, h = grid_bytes(o, t)
    cls = BUILD_CLASS[kind]

    def seems(c):
        if kind in BUILD_CELL:
            return c & 0x3F == BUILD_CELL[kind]
        if 0x15 <= kind <= 0x1E:
            return c & 0xA1 == 0xA1 or c & 0xA2 == 0xA2
        if kind in (0x26, 0x29, 0x2A, 0x2B, 0x2C):
            m = (1 << cls) | 0xA0
            return c & m == m
        m = (1 << cls) | 0x20
        return cls < 6 and c & m == m
    good = [i for i, c in enumerate(data) if seems(c)]
    pick = [good[k * (len(good) - 1) // max(most - 1, 1)] for k in range(most)] if good else []
    pick = list(dict.fromkeys(pick))
    near = [i + 1 for i in pick[:2]] + ([len(data) // 2] if not pick else [])
    return [(i // h, i % h) for i in pick + near if 0 <= i < len(data)]


def build_places(o):
    """Whether each building type fits a cell (0x47B8C0 through BUILD_FITS) and
    the viewer's bytes of the town view and the window run by the game. For
    each type (0..0x37): cells where it seems to fit and beside them; the
    fixed points; the houses with one orientation, the other, both (the
    town's [0x774] above 0 and not); a business in a siege inside and outside
    the walls; the towers on their towers, the gate bombard beside a gate and
    on one, the pitch shoot on each kind of gate, in works, the window's type
    following the gate; a town out. Then the viewer's bytes: the 0x21 of a
    house each way, the warehouse, a business, a public building, the towers
    and the pitch shoot where they fit, the office where he has none; the
    well (0x22, its cell from 0x5250D0), the streets (0x25), the next wall
    piece (0x24), demolishing (0x26) and auctioning (0x28) one of his
    buildings."""
    p, t, f, t2 = build_town(o)
    a, a2 = o.town(t), o.town(t2)
    gw, gh = o.s32(a + 0x7A8), o.s32(a + 0x7AC)
    print(f'town {t}: {gw} · {gh} cells at {o.u32(a + GRID):#x}, {o.u16(a + 0x776)} records at {o.u32(a + 0x75C):#x}')

    def during(o):
        build_copiers(o)
        w32(o, o.merchant(p), 50000000)
        store(o, t)
        store(o, t2)
        w32(o, a + 0x700, 50000000)
        for kind in range(0x38):
            for x, y in build_candidates(o, t, kind):
                build_fits_at(o, t, x, y, kind)
        for kind, at in ((0, 0x802), (1, 0x80A), (0x2E, 0x808)):
            for town in (t, t2):
                x, y = o.u8(o.town(town) + at), o.u8(o.town(town) + at + 1)
                build_fits_at(o, town, x, y, kind, f'the fixed point in {town}')
                build_fits_at(o, town, x, (y + 1) & 0xFF, kind, 'beside it')
        build_fits_at(o, o.counts()[0], 1, 1, 6, 'a town out')
        build_fits_at(o, t, 1, 1, 0x40, 'a type out')
        data, h = grid_bytes(o, t)
        houses = {}
        for i, c in enumerate(data):
            r = (1 if c & 0xA1 == 0xA1 else 0) + (2 if c & 0xA2 == 0xA2 else 0)
            houses.setdefault(r, (i // h, i % h))
        side = o.u8(a + 0x774)
        for r, (x, y) in sorted(houses.items()):
            for value in ((1, 0xFF) if r == 3 else (side,)):
                w8(o, a + 0x774, value)
                build_fits_at(o, t, x, y, 0x18, f'a house, orientations {r}, [0x774] {value:#x}')
                build_fits_at(o, t, x, y, 0x1E, f'the warehouse, orientations {r}')
        w8(o, a + 0x774, side)
        flags = o.u32(a + 0x2C8)
        w32(o, a + 0x2C8, flags | 0x10)
        inside = [(i // h, i % h) for i, c in enumerate(data) if c & 0xB0 == 0xB0][:2]
        outside = [(i // h, i % h) for i, c in enumerate(data) if c & 0xB0 == 0x30][:2]
        for x, y in inside + outside:
            build_fits_at(o, t, x, y, 0x0B, 'a sawmill in a siege')
        w32(o, a + 0x2C8, flags)
        forts = records(o, t, 0x768)
        for kind, on in ((0x31, 0x30), (0x33, 0x32)):
            for r in [r for r in forts if r[4] == on][:2]:
                build_fits_at(o, t, r[1], r[2], kind, f'on a {on:#x}')
        gates = [r for r in forts if r[4] in (0x37, 0x38, 0x34, 0x35)]
        for r in gates[:4]:
            sign = 1 if r[4] in (0x38, 0x35) else -1
            for x, y in ((r[1] + sign, r[2] + 2), (r[1] - 3 * sign, r[2] - 6)):
                build_fits_at(o, t, x & 0xFF, y & 0xFF, 0x32, f'beside the gate {r[4]:#x} at ({r[1]}, {r[2]})')
        for r in [r for r in forts if r[4] == 0x32][:1]:
            build_fits_at(o, t, r[1], r[2], 0x32, 'on a gate bombard')
        for r in [r for r in forts if r[4] in (0x37, 0x38)][:4]:
            for kind in (0x34, 0x35):
                build_fits_at(o, t, r[1], r[2], kind, f'on the gate {r[4]:#x}, pending {r[5]}')
            at = record_at(o, t, r[0])
            w8(o, at + 3, 0x38 if r[4] == 0x37 else 0x37)
            build_fits_at(o, t, r[1], r[2], 0x34, 'its kind changed')
            w8(o, at + 6, 5)
            build_fits_at(o, t, r[1], r[2], 0x35, 'in works')
            w8(o, at + 3, r[4])
            w8(o, at + 6, r[5])
        # The viewer's bytes: each building at a cell where it fits now, with
        # the picture the window got when it was chosen unless 0x47B8C0 asked
        # for another; away from the grid's edges, where the game writes past
        # the grid (roadmap, «Corregir los fallos del juego»).
        def build_at(kind, cells, what):
            for x, y in cells:
                if not (4 <= x < gw - 4 and 8 <= y < gh - 4):
                    continue
                got = build_fits_at(o, t, x, y, kind, f'to build: {what}')
                if got[0]:
                    catalog = got[2] & 0xFF if got[2] != 0xFFFFFFFF else BUILD_CATALOG[got[1]]
                    key = got[3] if got[2] != 0xFFFFFFFF else (0x20 if kind == 0x1E else 0x200000)
                    run_bytes(o, player_op('build', p, t, x, y, catalog, key))
                    return x, y
            return None
        data, h = grid_bytes(o, t)
        for r, kind in ((1, 0x1B), (2, 0x18), (3, 0x1E)):
            build_at(kind, [(i // h, i % h) for i, c in enumerate(data)
                            if (1 if c & 0xA1 == 0xA1 else 0) + (2 if c & 0xA2 == 0xA2 else 0) == r][5:40],
                     f'orientations {r}')
        for kind in (0x15, 0x06, 0x0B, 0x04, 0x05, 0x26, 0x2B, 0x29, 0x2C, 0x30):
            build_at(kind, build_candidates(o, t, kind, 12), 'where it seems to fit')
        forts = records(o, t, 0x768)
        build_at(0x31, [(r[1], r[2]) for r in forts if r[4] == 0x30], 'on a port bombard')
        gates = [r for r in forts if r[4] in (0x37, 0x38)]
        build_at(0x32, [((r[1] + (1 if r[4] == 0x38 else -1)) & 0xFF, r[2] + 2) for r in gates], 'beside a gate')
        build_at(0x33, [(r[1], r[2]) for r in records(o, t, 0x768) + records(o, t, 0x762) if r[4] == 0x32],
                 'on a gate bombard')
        for kind in (0x34, 0x35):
            build_at(kind, [(r[1], r[2]) for r in records(o, t, 0x768) if r[4] in (0x37, 0x38) and r[5] == 0],
                     'on a gate')
        x, y = o.u8(a2 + 0x802), o.u8(a2 + 0x803)
        got = build_fits_at(o, t2, x, y, 0, 'the office')
        if got[0]:
            run_bytes(o, player_op('build', p, t2, x, y, BUILD_CATALOG[0], 0))
        w8(o, SCRATCH_X, 0)
        w8(o, SCRATCH_Y, 0)
        direct(o, 0x5250D0, [SCRATCH_X, SCRATCH_Y], ecx=a)
        run_bytes(o, player_op('well', p, t, o.u8(SCRATCH_X), o.u8(SCRATCH_Y)))
        w16(o, a + 0x792, o.u16(a + 0x790) + 5)
        run_bytes(o, player_op('street', t, p))
        pending = records(o, t, 0x766)
        if pending:
            run_bytes(o, player_op('wall', p, t, pending[0][4]))
        # His buildings, or else his works.
        mine = office_records(o, t, f) + [(r[0], r[1], r[2]) for r in records(o, t, 0x762) if r[3] == p]
        print(f'  his buildings and works: {mine[:6]}')
        if mine:
            r, x, y = mine[0]
            run_bytes(o, player_op('demolish', t, p, x, y))
        if len(mine) > 1:
            r, x, y = mine[1]
            run_bytes(o, player_op('auction', t, p, x, y))
        print(f'  his buildings now {len(office_records(o, t, f))}, works {len(records(o, t, 0x762))}')
    return during


# --- The statistics window (statistics_window.cpp; re/emu/sesion15/statistics_notes.md) ---

STATS_BALANCE = to.SENTINEL + 0xD00  # the copier of the balance, 0x4A2F60 (probes_unchecked.cpp kStatsBalance)
STATS_GRAPH = to.SENTINEL + 0xD40  # of the company overview's graphs, 0x4A22A0 (kStatsGraph)
STATS_RET4 = to.SENTINEL + 0xD80  # ret 4: the methods of the window's buttons
STATS_RET = to.SENTINEL + 0xD84  # ret: the drawing DLL's functions (cdecl) the draw calls
STATS_SUPPLY = to.SENTINEL + 0xDC0  # of Suministro's draw, 0x4A0F10 (kStatsSupply)
# The drawing DLL's functions the draw 0x4A0F10 jumps through (0x4BB330...: jmp [slot]); they are 0 here.
STATS_DRAW_SLOTS = (0x6DA858, 0x6DA898, 0x6DA8A8, 0x6DA8E0, 0x6DA8EC, 0x6DA9F4, 0x6DAA08, 0x6DAA28)
STATS_FONTS = 0x6DCD28  # the fonts (0xA8 bytes each): the draw asks the window's two for their size (+0x40)
STATS_ICONS = (0x6CC37C, 0x6CC380, 0x6CC384)  # *: the coin, barrel and bale icons
STATS_VIEW = 0xC0  # the bytes of the view each call copies into SCRATCH; then its FNV-1a and its length
STATS_COLOURS = (0xCC0009, 0x7E009B, 0x006700, 0x009CAE)  # parchment.ini [Parchment1] Farbe0..3
EMPTY_CSTRING = 0x6C7CCC  # *: an empty CString's data
STATS_COMPETITION = to.SENTINEL + 0xEC0  # of the competition's graphs, 0x4A19C0 (kStatsCompetition)
STATS_CHOICE = to.SENTINEL + 0xF00  # of a merchant button's list, 0x49E850 and its sorts (kStatsChoice)
STATS_SLOTS = to.SENTINEL + 0xF40  # of the merchants the window picks when it opens, in 0x49D380 (kStatsSlots)
STATS_TOWNS = to.SENTINEL + 0xF50  # of the towns' graphs, 0x4A3FA0 (kStatsTowns)
STATS_RESIDENTS = to.SENTINEL + 0xF58  # of the towns' residents (entry 6), 0x4A3BB0 (kStatsResidents)
STATS_WARES = to.SENTINEL + 0xF60  # of the comparison's ware button's list, in 0x49F350 (kStatsWares)
STATS_TOWN_LIST = to.SENTINEL + 0xF68  # of the town data's town button's list, in 0x49F350 (kStatsTownList)


def fnv1a(data):
    h = 0x811C9DC5
    for b in data:
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


def cstring_at(o, a, limit):
    """The bytes of the C string at a (at most `limit`), zero-padded to `limit`."""
    if not a:
        return bytes(limit)
    raw = bytes(o.uc.mem_read(a, limit))
    end = raw.find(b'\0')
    return (raw if end < 0 else raw[:end]).ljust(limit, b'\0')


def statistics_copiers(o):
    """The copiers of the statistics window (stdcall), on a window of their own (its buttons' methods all ret 4; the
    buttons' texts, 0x4C7780, kept by a hook): STATS_BALANCE (period, part): 0x4A2F60 with the window's [+0xA4] and
    [+0xBE] 11; STATS_GRAPH (entry, period, part): 0x4A22A0 with [+0xBE] and [+0xC0]. After the call a hook makes
    the window's view (the balance: its 0x2A0 bytes of texts [+0x210] and the title [+0x20C] in 0x40; the graphs:
    u32 [+0x2084], [+0x2088], [+0x208C], u16 [+0xF2], the kinds [+0xF4..+0xFB], the 7 labels' texts [+0xFC..] in
    0x20 each, the texts' 0xE0 bytes [+0x118], the 4 buttons' texts [+0xC28..] in 0x20, the title in 0x40, then the
    series' points as floats) and copies its part `part` (0xC0 bytes) into SCRATCH, its FNV-1a into SCRATCH + 0xC0
    and its length into + 0xC4. statistics_reset before each call puts the window as 0x4A5DD0 leaves it. The game's
    table of the entries' titles 0x6CC830 (0x46C060) and the CRT's float conversions (0x639BBE) are static
    initializers': run here."""
    from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP
    win, vtable = o.alloc(0x3000), o.alloc(0x400)
    o.statistics_window = win
    o.statistics_names = {}
    o.uc.mem_write(STATS_RET4, bytes([0xC2, 0x04, 0x00]))
    o.uc.mem_write(vtable, struct.pack('<I', STATS_RET4) * 0x100)
    o.statistics_vtable = vtable
    o.statistics_list_vtable = o.alloc(0x400)
    o.uc.mem_write(o.statistics_list_vtable, struct.pack('<I', STATS_RET) * 0x100)
    direct(o, 0x46C060, [])
    # The CRT's table of the float conversions (_cfltcvt_init, the first part of _fpmath 0x639BA6, a static
    # initializer the oracle doesn't run): without it sprintf's «%.0f» stops with the runtime error R6002.
    direct(o, 0x639BBE, [])

    def call(at, code, target):
        return bytes([0xE8]) + struct.pack('<i', target - (at + len(code) + 5))

    at = STATS_BALANCE
    code = bytes([0x8B, 0x44, 0x24, 0x04, 0xA3]) + struct.pack('<I', win + 0xA4)  # the period
    code += bytes([0x66, 0xC7, 0x05]) + struct.pack('<IH', win + 0xBE, 11)
    code += bytes([0xB9]) + struct.pack('<I', win)
    code += call(at, code, 0x4A2F60)
    balance_view = at + len(code)
    code += bytes([0x90, 0xC2, 0x08, 0x00])
    assert len(code) <= 0x40
    o.uc.mem_write(at, code)

    at = STATS_GRAPH
    code = bytes([0x8B, 0x44, 0x24, 0x04, 0x66, 0xA3]) + struct.pack('<I', win + 0xBE)  # the entry
    code += bytes([0x8B, 0x44, 0x24, 0x08, 0x66, 0xA3]) + struct.pack('<I', win + 0xC0)  # the period
    code += bytes([0xB9]) + struct.pack('<I', win)
    code += call(at, code, 0x4A22A0)
    graph_view = at + len(code)
    code += bytes([0x90, 0xC2, 0x0C, 0x00])
    assert len(code) <= 0x40
    o.uc.mem_write(at, code)

    # Suministro: the draw 0x4A0F10 (thiscall, a rect) with the town [+0x2440]; its drawing calls do nothing.
    o.uc.mem_write(STATS_RET, bytes([0xC3]))
    for slot in STATS_DRAW_SLOTS:
        w32(o, slot, STATS_RET)
    for k in range(2):
        w32(o, STATS_FONTS + 0xA8 * k, vtable)
    icon = o.alloc(0x40)
    for slot in STATS_ICONS:
        w32(o, slot, icon)
    rect = o.alloc(0x10)
    o.uc.mem_write(rect, struct.pack('<4i', 100, 100, 500, 500))
    at = STATS_SUPPLY
    code = bytes([0x8B, 0x44, 0x24, 0x04, 0xA3]) + struct.pack('<I', win + 0x2440)  # the town
    code += bytes([0x66, 0xC7, 0x05]) + struct.pack('<IH', win + 0xBE, 15)
    code += bytes([0x68]) + struct.pack('<I', rect) + bytes([0xB9]) + struct.pack('<I', win)
    code += call(at, code, 0x4A0F10)
    supply_view = at + len(code)
    code += bytes([0x90, 0xC2, 0x08, 0x00])
    assert len(code) <= 0x40
    o.uc.mem_write(at, code)

    def view_of_supply():
        return b''.join(bytes(o.uc.mem_read(win + field, 0x50)) for field in (0x253C, 0x24EC, 0x258C))

    def supply_hook(uc, address, size, data):
        copy(view_of_supply(), o.u32(uc.reg_read(UC_X86_REG_ESP) + 8))

    o.uc.hook_add(UC_HOOK_CODE, supply_hook, begin=supply_view, end=supply_view)
    o.uc.ctl_remove_cache(supply_view, supply_view + 1)

    def view_of_balance():
        texts = o.u32(win + 0x210)
        out = bytes(o.uc.mem_read(texts, 0x2A0)) if texts else bytes(0x2A0)
        return out + cstring_at(o, o.u32(win + 0x20C), 0x40)

    def view_of_graph():
        series, points, missing = o.u32(win + 0x2084), o.u32(win + 0x2088), o.u32(win + 0x208C)
        count = o.u16(win + 0xF2)
        out = struct.pack('<IIIH', series, points, missing, count) + bytes(o.uc.mem_read(win + 0xF4, 8))
        for k in range(7):
            out += cstring_at(o, o.u32(win + 0xFC + 4 * k), 0x20) if k < count else bytes(0x20)
        out += bytes(o.uc.mem_read(win + 0x118, 0xE0))
        for k in range(4):
            out += o.statistics_names.get(win + 0xC28 + 0xE8 * k, bytes(0x20))
        out += cstring_at(o, o.u32(win + 0x20C), 0x40)
        data = o.u32(win + 0x2080)
        if data:
            for s in range(min(series, 4)):
                out += bytes(o.uc.mem_read(data + 0x208 * s + 8, 4 * points))
        return out

    def copy(view, part):
        chunk = view[STATS_VIEW * part:STATS_VIEW * (part + 1)].ljust(STATS_VIEW, b'\0')
        o.uc.mem_write(SCRATCH, chunk + struct.pack('<II', fnv1a(view), len(view)))

    def balance_hook(uc, address, size, data):
        copy(view_of_balance(), o.u32(uc.reg_read(UC_X86_REG_ESP) + 8))

    def graph_hook(uc, address, size, data):
        copy(view_of_graph(), o.u32(uc.reg_read(UC_X86_REG_ESP) + 12))

    def button_text(uc, address, size, data):
        esp = uc.reg_read(UC_X86_REG_ESP)
        o.statistics_names[uc.reg_read(UC_X86_REG_ECX)] = cstring_at(o, o.u32(esp + 4), 0x20)
        uc.reg_write(UC_X86_REG_EIP, o.u32(esp))
        uc.reg_write(UC_X86_REG_ESP, esp + 8)

    def scroll_bar(uc, address, size, data):
        # 0x4311B0 (thiscall, an argument): the residents' table's scroll bar, the interface's.
        esp = uc.reg_read(UC_X86_REG_ESP)
        uc.reg_write(UC_X86_REG_EIP, o.u32(esp))
        uc.reg_write(UC_X86_REG_ESP, esp + 8)

    for address, f in ((balance_view, balance_hook), (graph_view, graph_hook), (0x4C7780, button_text),
                       (0x4311B0, scroll_bar)):
        o.uc.hook_add(UC_HOOK_CODE, f, begin=address, end=address)
        o.uc.ctl_remove_cache(address, address + 1)


def statistics_reset(o):
    """The copiers' window as 0x4A5DD0 leaves it before an entry's function: its buttons' vtables, no series nor
    texts ([+0x210], [+0x2080..+0x208C] at 0, the count, kinds, labels and texts [+0xF2..+0x1F8) zeroed), an empty
    title, the series' colours; the list's lists and its sort's kind (which the wares' lists leave as it was) 0."""
    win = o.statistics_window
    o.uc.mem_write(win, struct.pack('<I', o.statistics_vtable) * (0x3000 // 4))
    for field in (0x210, 0x2080, 0x2084, 0x2088, 0x208C, 0x1074, 0x1088, 0x1098):
        w32(o, win + field, 0)
    w32(o, win + 0xFC8, o.statistics_list_vtable)  # the list: its show (+0x120) a plain ret
    o.uc.mem_write(win + 0xF2, bytes(0x1F8 - 0xF2))
    o.uc.mem_write(win + 0x24EC, bytes(0xF0))  # Suministro's numbers: 3 arrays of 20
    w16(o, win + 0x208, 0)  # the draw's two fonts
    w16(o, win + 0x20A, 1)
    w16(o, win + 0xF0, 16)  # a row's height
    w32(o, win + 0x20C, o.u32(EMPTY_CSTRING))
    for k in range(4):
        w32(o, win + 0x1F8 + 4 * k, 0xFF000000 | STATS_COLOURS[k])
    o.statistics_names.clear()


def statistics_balance_of(o, period, what):
    statistics_reset(o)
    direct(o, STATS_BALANCE, [period, 0])
    texts = o.u32(o.statistics_window + 0x210)
    raw = bytes(o.uc.mem_read(texts, 0x2A0)) if texts else bytes(0x2A0)
    shown = [raw[0x20 * k:0x20 * k + 0x20].split(b'\0')[0].decode('latin-1') for k in range(20)]
    title = cstring_at(o, o.u32(o.statistics_window + 0x20C), 0x40).split(b'\0')[0].decode('latin-1')
    print(f'  balance {period} {what}: {shown[:3]} ... in {shown[9]} out {shown[16]} total {shown[17]}; {title!r}, '
          f'{shown[18]!r}, {shown[19]!r}')


def statistics_graph_of(o, entry, period, what):
    statistics_reset(o)
    direct(o, STATS_GRAPH, [entry, period, 0])
    win = o.statistics_window
    texts = [cstring_at(o, win + 0x118 + 0x20 * k, 0x20).split(b'\0')[0].decode('latin-1')
             for k in range(o.u16(win + 0xF2))]
    print(f'  graph {entry} period {period} {what}: series {o.u32(win + 0x2084)}, points {o.u32(win + 0x2088)}, '
          f'missing {o.u32(win + 0x208C)}; {texts}')


def statistics_supply_of(o, town, what):
    statistics_reset(o)
    direct(o, STATS_SUPPLY, [town, 0])
    win = o.statistics_window
    office = [o.s32(win + 0x253C + 4 * w) for w in range(20)]
    print(f'  supply in town {town} {what}: office {office[:8]}, town {[o.s32(win + 0x24EC + 4 * w) for w in range(4)]}'
          f', prices {[o.s32(win + 0x258C + 4 * w) for w in range(4)]}')


def statistics_object(o, merchant):
    """The merchant's statistics object (the list *0x6DFCAC by its u16 [+0x0]), or 0."""
    node = o.u32(0x6DFCAC)
    while node and o.u16(node) != merchant:
        node = o.u32(node + 0x230C)
    return node


def statistics_fill(o, node, rnd):
    """Values in every record of the object's rings (days, weeks, months: not the links [+0x6C]) and 5 years made
    and linked: floats (some integral, some halves and fractions, some negative or 0), ints and u16s."""
    floats = (0x0, 0x4, 0x10, 0x14, 0x18, 0x48, 0x4C, 0x64, 0x68)
    ints = (0x8, 0xC, 0x1C, 0x20, 0x24, 0x28, 0x2C, 0x30, 0x34, 0x38, 0x3C, 0x40, 0x44, 0x50, 0x5C, 0x60)

    def record():
        r = bytearray(0x6C)
        for f in floats:
            kind = rnd.randrange(5)
            v = (rnd.randrange(-500, 200000) if kind < 2 else rnd.randrange(0, 4000) + 0.5 if kind == 2
                 else rnd.uniform(-1000.0, 90000.0) if kind == 3 else 0.0)
            struct.pack_into('<f', r, f, v)
        for f in ints:
            struct.pack_into('<i', r, f, rnd.randrange(-2000, 60000) if rnd.randrange(6) else 0)
        for f in (0x54, 0x56, 0x58, 0x5A):
            struct.pack_into('<H', r, f, rnd.randrange(0, 300))
        return bytes(r)

    for first, count in ((0x8, 32), (0xE08, 24), (0x1888, 24)):
        for k in range(count):
            o.uc.mem_write(node + first + 0x70 * k, record())
            if k % 7 == 3:
                # Nothing traded that day: Riqueza's volume of trade at 0.
                for f in (0x14, 0x18, 0x3C, 0x40):
                    w32(o, node + first + 0x70 * k + f, 0)
    head = 0
    for _ in range(5):
        year = o.alloc(0x70)
        o.uc.mem_write(year, record() + struct.pack('<I', head))
        head = year
    w32(o, node + 0x2308, head)
    w16(o, node + 0x6, 5)


def statistics_company(o):
    """The statistics window's company overview: the balance (0x4A2F60 through STATS_BALANCE) and the graphs of
    Riqueza, Barcos and Producción (0x4A22A0 through STATS_GRAPH, with 0x505A10). First without any statistics
    object (none in these saves: the daily statistics are off here), then another merchant's only (the list walked
    to its end), then the player's object made by the game (0x54E170) behind a third merchant's, its records
    filled with values (some days with nothing traded); each period of both, at several dates
    (the 30 days back from the 1st, 15th and 31st, in January and December; the months from January and April) and
    ring positions (the days' at 30 and 31, the months' at 22, 23 and 0), with fewer days, weeks and months recorded
    than points and more; the player out of the merchants for the graphs; his ships with one sunk, one at 0xE, one
    without a captain, one with no health, one with its [+0x130] at 0."""
    import random
    rnd = random.Random(1306)
    player = o.u32(to.PLAYER)
    merchants = o.counts()[2]
    other = next(m for m in range(merchants) if m != player)
    ships = [s for s in range(o.counts()[3]) if o.u8(o.ship(s)) == player]
    print(f'player {player}, other merchant {other}, his ships {ships[:6]}')

    def during(o):
        statistics_copiers(o)
        for period in range(4):
            statistics_balance_of(o, period, 'no object')
        for entry in (12, 13, 14):
            statistics_graph_of(o, entry, 0, 'no object')
        # Another merchant's object only: the list walked to its end.
        direct(o, 0x54E170, [other], ecx=0x6DFC90)
        statistics_balance_of(o, 0, 'only another merchant\'s object')
        statistics_graph_of(o, 12, 0, 'only another merchant\'s object')
        # The player's, and a third merchant's at the head: his is the second.
        direct(o, 0x54E170, [player], ecx=0x6DFC90)
        third = next(m for m in range(merchants) if m not in (player, other))
        direct(o, 0x54E170, [third], ecx=0x6DFC90)
        node = statistics_object(o, player)
        print(f'  the player\'s object {node:#x}, the list\'s head {o.u32(0x6DFCAC):#x}')
        statistics_fill(o, node, rnd)
        statistics_fill(o, statistics_object(o, other), rnd)
        day, month, year = o.u8(0x6DE4A0), o.u8(0x6DE4A1), o.u16(0x6DE4A2)
        for d, m, cur, mcur, recorded in ((15, 4, 31, 23, (100, 30, 30)), (1, 0, 30, 22, (5, 3, 0)),
                                          (31, 11, 0, 0, (40, 24, 25)), (1, 3, 7, 5, (29, 23, 23)),
                                          (20, 0, 12, 1, (31, 25, 24))):
            w8(o, 0x6DE4A0, d)
            w8(o, 0x6DE4A1, m)
            w16(o, node + 0x2, cur)
            w8(o, node + 0x4, mcur % 24)
            w8(o, node + 0x5, mcur)
            for k, v in enumerate(recorded):
                w16(o, 0x6DFC90 + 2 * k, v)
            what = f'day {d} month {m}, rings {cur} {mcur}, recorded {recorded}'
            for period in range(4):
                statistics_balance_of(o, period, what)
                for entry in (12, 13, 14):
                    statistics_graph_of(o, entry, period, what)
        w8(o, 0x6DE4A0, day)
        w8(o, 0x6DE4A1, month)
        w16(o, 0x6DE4A2, year)
        # His ships: one sunk, one at 0xE, one without a captain, one with no health, one with its [+0x130] at 0.
        changes = [(0x134, 0x11), (0x134, 0xE), (0x42, 0xFFFF), (0x18, 0), (0x130, 0)]
        for s, (field, value) in zip(ships, changes):
            if field in (0x134, 0x42, 0x130):
                w16(o, o.ship(s) + field, value)
            else:
                w32(o, o.ship(s) + field, value)
            statistics_graph_of(o, 13, 0, f'ship {s} [+{field:#x}] = {value:#x}')
        for s in ships:
            w16(o, o.ship(s) + 0x134, 0x11)
        statistics_graph_of(o, 13, 1, 'all his ships sunk')
        w32(o, to.PLAYER, merchants)
        statistics_graph_of(o, 12, 0, 'the player out of the merchants')
        w32(o, to.PLAYER, player)
        # Suministro in the towns of his offices; in the first, a ware its workshops need more of than it holds and
        # one with nothing held but needed.
        towns = [t for t in range(o.counts()[0]) if facilities_office(o, player, t) is not None]
        print(f'  his offices\' towns {towns}')
        for k, t in enumerate(towns[:3]):
            if k == 0:
                f = o.office(facilities_office(o, player, t))
                w32(o, f + 0x4 + 4 * 2, 3000)
                w32(o, f + 0x64 + 4 * 2, 5000)
                w32(o, f + 0x4 + 4 * 5, 0)
                w32(o, f + 0x64 + 4 * 5, 40)
            statistics_supply_of(o, t, 'his office')
    return during


def stats_copy(o, view, part):
    """A window's view into the scratch globals: its part `part` (STATS_VIEW bytes), its FNV-1a and its length."""
    chunk = view[STATS_VIEW * part:STATS_VIEW * (part + 1)].ljust(STATS_VIEW, b'\0')
    o.uc.mem_write(SCRATCH, chunk + struct.pack('<II', fnv1a(view), len(view)))


def competition_copiers(o):
    """The copiers of the competition and the towns' pages (stdcall), on statistics_copiers's window (run it
    first). The window's choices go in as u32 arguments, a byte each: wares [+0xAC..+0xAF], towns [+0xB0..+0xB3],
    merchants [+0xB4..+0xB7], single (the ware [+0xBC] and the town [+0xBD]).
    STATS_COMPETITION (entry, period, merchants, part): 0x4A19C0 with [+0xBE] and [+0xC0]; its view: u32 [+0x2084],
    [+0x2088], [+0x208C], u16 [+0xF2], the kinds [+0xF4..+0xFB], the 7 labels' texts in 0x20 each, the texts' 0xE0
    bytes [+0x118], the 4 buttons' texts [+0xC28..] in 0x20, the title in 0x40, u16 [+0xC0] after the call, then each
    series' 0x208 bytes (colour, merchant, points).
    STATS_TOWNS (entry, period, wares, towns, single, part): 0x4A3FA0; its view as the competition's with the text of
    the button [+0x214] (0x20) after the 4 buttons', the window's four towns [+0xB0..+0xB3] after the period (the town
    data's population may change the first), and up to 5 series (the export balance's zero line); the town data's
    first three entries leave the series' [+0x4] unset (what the heap held): 0 in the view.
    STATS_RESIDENTS (part): 0x4A3BB0 (entry 6); its view: each row of [+0x210] (u32 town, its text as a C string in
    0x3C), u32 [+0x29F0], the title in 0x40.
    STATS_CHOICE (entry, slot, wares, towns, merchants, single, sort, part): 0x49E850(slot) (the list of a town's or
    merchant's button; the list [+0xFC8] with its show a ret) and, with sort 1, the list's sort by name (0x471FF0 for
    the towns, 0x4720D0 for the merchants: o.statistics_sort, set by choice_of), with 2 also by key 0x471EA0; its view:
    u16 [+0x106C], [+0x106E], u32 [+0x108C], [+0x1098], [+0x1094] − the window, the call's al, then the list's bytes
    and keys (u32).
    STATS_WARES (sort, part): the window's update from 0x49FC1C to 0x49FCB4 (the ware button's list of the comparison,
    with esi the window and ebp 0), and with sort 1 the list's sort by name 0x471F10; its view: u16 [+0x106C],
    [+0x106E], u32 [+0x108C], the bytes.
    STATS_TOWN_LIST (sort, part): the same from 0x49FB41 to 0x49FBEB (the town data's town button's list) and the sort
    0x471FF0; its view: u16 [+0x106C], [+0x106E], u32 [+0x108C], [+0x1088], [+0x1094] − the window, the bytes.
    STATS_SLOTS (wares, towns, merchants, part): the part of 0x49D380 that fixes the first town and ware and fills the
    empty merchant slots (0x49DA61..0x49DB82, with esi the window and its frame); its view, [+0xAC..+0xB7].
    All but the first jump to their code on the heap (the sentinel page's room is short); a hook at the end of the
    game's code goes back for the last two."""
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP
    win = o.statistics_window

    def rel(at, code, target):
        return struct.pack('<i', target - (at + len(code) + 4))

    def store(arg, field, word=False):
        """mov eax, [esp + 4·arg]; mov [win + field], eax (or ax)."""
        return (bytes([0x8B, 0x44, 0x24, 4 * arg]) + (bytes([0x66, 0xA3]) if word else bytes([0xA3])) +
                struct.pack('<I', win + field))

    def stub(fixed, at):
        o.uc.mem_write(fixed, bytes([0xE9]) + struct.pack('<i', at - (fixed + 5)))

    at = STATS_COMPETITION
    code = store(1, 0xBE, True) + store(2, 0xC0, True) + store(3, 0xB4)
    code += bytes([0xB9]) + struct.pack('<I', win) + bytes([0xE8])
    code += rel(at, code, 0x4A19C0)
    competition_view = at + len(code)
    code += bytes([0x90, 0xC2, 0x10, 0x00])
    assert len(code) <= 0x40
    o.uc.mem_write(at, code)

    at = o.alloc(0x80)
    code = store(1, 0xBE, True) + store(2, 0xC0, True) + store(3, 0xAC) + store(4, 0xB0) + store(5, 0xBC, True)
    code += bytes([0xB9]) + struct.pack('<I', win) + bytes([0xE8])
    code += rel(at, code, 0x4A3FA0)
    towns_view = at + len(code)
    code += bytes([0x90, 0xC2, 0x18, 0x00])
    o.uc.mem_write(at, code)
    stub(STATS_TOWNS, at)

    at = o.alloc(0x40)
    code = bytes([0x66, 0xC7, 0x05]) + struct.pack('<IH', win + 0xBE, 6)
    code += bytes([0xB9]) + struct.pack('<I', win) + bytes([0xE8])
    code += rel(at, code, 0x4A3BB0)
    residents_view = at + len(code)
    code += bytes([0x90, 0xC2, 0x04, 0x00])
    o.uc.mem_write(at, code)
    stub(STATS_RESIDENTS, at)

    result = o.alloc(4)
    o.statistics_sort = o.alloc(4)
    at = o.alloc(0xC0)
    code = store(1, 0xBE, True) + store(3, 0xAC) + store(4, 0xB0) + store(5, 0xB4) + store(6, 0xBC, True)
    code += bytes([0xFF, 0x74, 0x24, 0x08])  # push the slot
    code += bytes([0xB9]) + struct.pack('<I', win) + bytes([0xE8])
    code += rel(at, code, 0x49E850)
    code += bytes([0xA2]) + struct.pack('<I', result)  # mov [result], al
    by_name = bytes([0xB9]) + struct.pack('<I', win + 0xFC8) + bytes([0xFF, 0x15]) + struct.pack('<I', o.statistics_sort)
    by_key = bytes([0xB9]) + struct.pack('<I', win + 0xFC8) + bytes([0xE8])
    tail = bytes([0x83, 0x7C, 0x24, 0x1C, 0x02, 0x75, len(by_key) + 4])  # cmp [sort], 2; jne view
    code += bytes([0x83, 0x7C, 0x24, 0x1C, 0x00, 0x74, len(by_name) + len(tail) + len(by_key) + 4])
    code += by_name + tail + by_key
    code += rel(at, code, 0x471EA0)
    choice_view = at + len(code)
    code += bytes([0x90, 0xC2, 0x20, 0x00])
    o.uc.mem_write(at, code)
    stub(STATS_CHOICE, at)

    at = o.alloc(0x60)
    code = bytes([0x53, 0x55, 0x56, 0x57, 0x83, 0xEC, 0x38])  # the update's saved registers and frame
    code += bytes([0xBE]) + struct.pack('<I', win) + bytes([0x31, 0xED])  # mov esi, the window; xor ebp, ebp
    code += bytes([0xE9])
    code += rel(at, code, 0x49FC1C)
    wares_back = at + len(code)
    sort = bytes([0xB9]) + struct.pack('<I', win + 0xFC8) + bytes([0xE8])
    code += bytes([0x83, 0x7C, 0x24, 0x4C, 0x00, 0x74, len(sort) + 4])  # cmp [sort], 0; je view
    code += sort
    code += rel(at, code, 0x471F10)
    wares_view = at + len(code)
    code += bytes([0x90, 0x83, 0xC4, 0x38, 0x5F, 0x5E, 0x5D, 0x5B, 0xC2, 0x08, 0x00])
    o.uc.mem_write(at, code)
    stub(STATS_WARES, at)

    at = o.alloc(0x60)
    code = bytes([0x53, 0x55, 0x56, 0x57, 0x83, 0xEC, 0x38])  # the update's saved registers and frame
    code += bytes([0xBE]) + struct.pack('<I', win) + bytes([0x31, 0xED])  # mov esi, the window; xor ebp, ebp
    code += bytes([0xE9])
    code += rel(at, code, 0x49FB41)
    town_list_back = at + len(code)
    sort = bytes([0xB9]) + struct.pack('<I', win + 0xFC8) + bytes([0xE8])
    code += bytes([0x83, 0x7C, 0x24, 0x4C, 0x00, 0x74, len(sort) + 4])  # cmp [sort], 0; je view
    code += sort
    code += rel(at, code, 0x471FF0)
    town_list_view = at + len(code)
    code += bytes([0x90, 0x83, 0xC4, 0x38, 0x5F, 0x5E, 0x5D, 0x5B, 0xC2, 0x08, 0x00])
    o.uc.mem_write(at, code)
    stub(STATS_TOWN_LIST, at)

    at = o.alloc(0x60)
    code = bytes([0x53, 0x55, 0x56, 0x57, 0x83, 0xEC, 0x3C])  # the function's saved registers and frame
    for k, field in enumerate((0xAC, 0xB0, 0xB4)):
        code += bytes([0x8B, 0x44, 0x24, 0x50 + 4 * k, 0xA3]) + struct.pack('<I', win + field)
    code += bytes([0xBE]) + struct.pack('<I', win)  # mov esi, the window
    code += bytes([0xE9])
    code += rel(at, code, 0x49DA61)
    slots_back = at + len(code)
    code += bytes([0x83, 0xC4, 0x3C, 0x5F, 0x5E, 0x5D, 0x5B, 0xC2, 0x10, 0x00])
    o.uc.mem_write(at, code)
    stub(STATS_SLOTS, at)

    def graph_view(with_choice):
        series, points, missing = o.u32(win + 0x2084), o.u32(win + 0x2088), o.u32(win + 0x208C)
        count = o.u16(win + 0xF2)
        out = struct.pack('<IIIH', series, points, missing, count) + bytes(o.uc.mem_read(win + 0xF4, 8))
        for k in range(7):
            out += cstring_at(o, o.u32(win + 0xFC + 4 * k), 0x20) if k < count else bytes(0x20)
        out += bytes(o.uc.mem_read(win + 0x118, 0xE0))
        for k in range(4):
            out += o.statistics_names.get(win + 0xC28 + 0xE8 * k, bytes(0x20))
        if with_choice:
            out += o.statistics_names.get(win + 0x214, bytes(0x20))
        out += cstring_at(o, o.u32(win + 0x20C), 0x40)
        out += struct.pack('<H', o.u16(win + 0xC0))
        if with_choice:
            out += bytes(o.uc.mem_read(win + 0xB0, 4))
        data = o.u32(win + 0x2080)
        unset = with_choice and o.u16(win + 0xBE) <= 2  # the town data's 0..2 leave the series' [+0x4] unset
        if data:
            for s in range(min(series, 5 if with_choice else 4)):
                one = bytearray(o.uc.mem_read(data + 0x208 * s, 8 + 4 * points))
                if unset:
                    one[4:8] = bytes(4)
                out += bytes(one)
        return out

    def view_of_residents():
        rows = o.u32(win + 0x210)
        out = b''
        if rows:
            for k in range(o.u32(0x6DE4B0)):
                out += struct.pack('<I', o.u32(rows + 0x40 * k)) + cstring_at(o, rows + 0x40 * k + 4, 0x3C)
        return out + struct.pack('<I', o.u32(win + 0x29F0)) + cstring_at(o, o.u32(win + 0x20C), 0x40)

    def view_of_choice():
        count = o.u16(win + 0x106E)
        out = struct.pack('<HHIIIB', o.u16(win + 0x106C), count, o.u32(win + 0x108C), o.u32(win + 0x1098),
                          (o.u32(win + 0x1094) - win) & 0xFFFFFFFF, o.u8(result))
        merchants, keys = o.u32(win + 0x1074), o.u32(win + 0x1088)
        out += bytes(o.uc.mem_read(merchants, count)) if merchants and count else b''
        out += bytes(o.uc.mem_read(keys, 4 * count)) if keys and count else b''
        return out

    def view_of_wares():
        count = o.u16(win + 0x106E)
        out = struct.pack('<HHI', o.u16(win + 0x106C), count, o.u32(win + 0x108C))
        merchants = o.u32(win + 0x1074)
        return out + (bytes(o.uc.mem_read(merchants, count)) if merchants and count else b'')

    def view_of_town_list():
        count = o.u16(win + 0x106E)
        out = struct.pack('<HHIII', o.u16(win + 0x106C), count, o.u32(win + 0x108C), o.u32(win + 0x1088),
                          (o.u32(win + 0x1094) - win) & 0xFFFFFFFF)
        merchants = o.u32(win + 0x1074)
        return out + (bytes(o.uc.mem_read(merchants, count)) if merchants and count else b'')

    def at_esp(offset, view):
        def hook(uc, address, size, data):
            stats_copy(o, view(), o.u32(uc.reg_read(UC_X86_REG_ESP) + offset))
        return hook

    def back(address, offset, view):
        def hook(uc, address_, size, data):
            esp = uc.reg_read(UC_X86_REG_ESP)
            if view:
                stats_copy(o, view(), o.u32(esp + offset))
            uc.reg_write(UC_X86_REG_EIP, address)
        return hook

    hooks = ((competition_view, at_esp(16, lambda: graph_view(False))), (towns_view, at_esp(24, lambda: graph_view(True))),
             (residents_view, at_esp(4, view_of_residents)), (choice_view, at_esp(32, view_of_choice)),
             (wares_view, at_esp(0x38 + 0x10 + 8, view_of_wares)), (0x49FCB4, back(wares_back, 0, None)),
             (town_list_view, at_esp(0x38 + 0x10 + 8, view_of_town_list)), (0x49FBEB, back(town_list_back, 0, None)),
             (0x49DB82, back(slots_back, 0x3C + 0x10 + 0x10, lambda: bytes(o.uc.mem_read(win + 0xAC, 12)))))
    for address, f in hooks:
        o.uc.hook_add(UC_HOOK_CODE, f, begin=address, end=address)
        o.uc.ctl_remove_cache(address, address + 1)


WINDOW_WARES = (0, 2, 3, 11)  # the window's choices at start (0x49E380)
WINDOW_TOWNS = (0, 1, 2, 3)


def pack_slots(slots):
    return int.from_bytes(bytes(slots), 'little')


def competition_of(o, entry, period, slots, what):
    statistics_reset(o)
    direct(o, STATS_COMPETITION, [entry, period, pack_slots(slots), 0])
    win = o.statistics_window
    texts = [cstring_at(o, win + 0x118 + 0x20 * k, 0x20).split(b'\0')[0].decode('latin-1')
             for k in range(o.u16(win + 0xF2))]
    names = [o.statistics_names.get(win + 0xC28 + 0xE8 * k, b'').split(b'\0')[0].decode('latin-1') for k in range(4)]
    print(f'  competition {entry} period {period} slots {list(slots)} {what}: series {o.u32(win + 0x2084)}, points '
          f'{o.u32(win + 0x2088)}, missing {o.u32(win + 0x208C)}, period now {o.u16(win + 0xC0)}; {names}; {texts}')


def towns_of(o, entry, period, towns, what, wares=WINDOW_WARES, ware=0, town=0):
    statistics_reset(o)
    direct(o, STATS_TOWNS, [entry, period, pack_slots(wares), pack_slots(towns), ware | town << 8, 0])
    win = o.statistics_window
    texts = [cstring_at(o, win + 0x118 + 0x20 * k, 0x20).split(b'\0')[0].decode('latin-1')
             for k in range(o.u16(win + 0xF2))]
    names = [o.statistics_names.get(win + 0xC28 + 0xE8 * k, b'').split(b'\0')[0].decode('latin-1') for k in range(4)]
    title = cstring_at(o, o.u32(win + 0x20C), 0x40).split(b'\0')[0].decode('latin-1')
    print(f'  towns {entry} period {period} towns {list(towns)} ware {ware} {what}: series {o.u32(win + 0x2084)}, '
          f'points {o.u32(win + 0x2088)}, missing {o.u32(win + 0x208C)}; {title!r} {names}; {texts}')


def residents_of(o, what):
    statistics_reset(o)
    direct(o, STATS_RESIDENTS, [0])
    win = o.statistics_window
    rows = o.u32(win + 0x210)
    shown = [(o.u32(rows + 0x40 * k), cstring_at(o, rows + 0x40 * k + 4, 0x3C).split(b'\0')[0].decode('latin-1'))
             for k in range(o.u32(0x6DE4B0))] if rows else []
    print(f'  residents {what}: total {o.u32(win + 0x29F0)}, {shown[:6]}')


def choice_of(o, entry, slot, slots, sort, what, wares=WINDOW_WARES, towns=WINDOW_TOWNS, ware=0, town=0):
    """The list of a merchant's (16..18: `slots` the merchants), a town's (7..10: `towns`) or a ware's button (3..5:
    `wares`)."""
    statistics_reset(o)
    w32(o, o.statistics_sort, 0x471FF0 if 7 <= entry <= 10 else 0x471F10 if 3 <= entry <= 5 else 0x4720D0)
    direct(o, STATS_CHOICE, [entry, slot, pack_slots(wares), pack_slots(towns), pack_slots(slots), ware | town << 8,
                             sort, 0])
    win = o.statistics_window
    count = o.u16(win + 0x106E)
    listed = list(o.uc.mem_read(o.u32(win + 0x1074), count)) if count else []
    shown = list(slots) if entry >= 16 else list(towns) if entry >= 7 else list(wares)
    print(f'  choice {entry} slot {slot} of {shown} sort {sort} {what}: none {o.u32(win + 0x108C)}, {listed}')


def wares_of(o, sort):
    statistics_reset(o)
    direct(o, STATS_WARES, [sort, 0])
    win = o.statistics_window
    count = o.u16(win + 0x106E)
    print(f'  wares sort {sort}: {list(o.uc.mem_read(o.u32(win + 0x1074), count)) if count else []}')


def town_list_of(o, sort):
    statistics_reset(o)
    direct(o, STATS_TOWN_LIST, [sort, 0])
    win = o.statistics_window
    count = o.u16(win + 0x106E)
    print(f'  town list sort {sort}: {list(o.uc.mem_read(o.u32(win + 0x1074), count)) if count else []}')


def slots_of(o, slots, what, wares=WINDOW_WARES, towns=WINDOW_TOWNS):
    statistics_reset(o)
    direct(o, STATS_SLOTS, [pack_slots(wares), pack_slots(towns), pack_slots(slots), 0])
    out = list(o.uc.mem_read(o.statistics_window + 0xAC, 12))
    print(f'  slots {list(wares)} {list(towns)} {list(slots)} {what}: {out[:4]} {out[4:8]} {out[8:]}')
    return out[8:]


def statistics_competition(o):
    """The statistics window's competition (Competencia): the graphs of the merchants' company value, loading
    capacity and popularity (0x4A19C0 through STATS_COMPETITION), the merchants the window picks when it opens (part
    of 0x49D380 through STATS_SLOTS) and the list a merchant's button opens, sorted by name and by its key (0x49E850,
    0x4720D0, 0x471EA0 through STATS_CHOICE). First without any statistics object; then with the objects of six
    merchants (made by the game, 0x54E170; their rings and 5 years filled), today's company values set so that the
    player (already shown) is the richest and comes after the best so far (every empty slot ends empty) and so that he
    isn't; the graphs by each period (the period 0 turned into 1), with fewer weeks and months recorded than points and
    more, the years' count of the towns 1 (it stops), 5 and 130 (128 points), an object with fewer years of its own, a
    merchant without an object, nobody shown; the lists for each slot and entry, sorted each way, with a merchant
    whose [+0x8] has the bit 4, two family names that differ at a Latin-1 letter (Gutenbach and Gödecke: signed
    chars), two equal family names, and company values below 0 (their bits sorted as ints)."""
    import random
    rnd = random.Random(1611)
    player = o.u32(to.PLAYER)
    merchants = o.counts()[2]
    others = [m for m in range(merchants) if m != player]
    # The merchants the lists offer (no bit 4 in [+0x8]) first.
    others.sort(key=lambda m: (o.u8(o.merchant(m) + 0x8) & 4) != 0)
    print(f'player {player}, merchants {merchants}, the lists\' first {others[:8]}')

    def name(m):
        a = o.merchant(m)
        first = cstring_at(o, o.u32(a + 0xE8), 0x20).split(b'\0')[0].decode('latin-1')
        family = cstring_at(o, o.u32(a + 0xE4), 0x20).split(b'\0')[0].decode('latin-1')
        return f'{first} {family}'

    def during(o):
        statistics_copiers(o)
        competition_copiers(o)
        print('  names: ' + ', '.join(f'{m} {name(m)}' for m in range(merchants)))
        none = [0xFF] * 4
        # Without any statistics object.
        slots = slots_of(o, none, 'no object')
        slots_of(o, [others[0], 0xFF, merchants + 1, 0xFF], 'no object, slot 0 kept')
        for entry in (16, 17, 18):
            competition_of(o, entry, 1, slots, 'no object')
        competition_of(o, 16, 0, slots, 'period 0, no object')
        competition_of(o, 17, 3, slots, 'no years')
        competition_of(o, 18, 2, none, 'nobody shown')
        for entry in (16, 17, 18):
            choice_of(o, entry, 0, slots, 1, 'only the player shown')
        # Six merchants' objects (each made at the list's head: the walk goes from the last made).
        chosen = others[:5]
        chosen.insert(2, player)
        for m in reversed(chosen):
            direct(o, 0x54E170, [m], ecx=0x6DFC90)
        nodes = {m: statistics_object(o, m) for m in chosen}
        for m in chosen:
            statistics_fill(o, nodes[m], rnd)
        walk = []
        node = o.u32(0x6DFCAC)
        while node:
            walk.append(o.u16(node))
            node = o.u32(node + 0x230C)
        print(f'  the objects\' walk {walk}')

        def today(values):
            for m, v in zip(walk, values):
                node = nodes[m]
                o.uc.mem_write(node + 0x8 + 0x70 * o.u16(node + 0x2), struct.pack('<f', v))

        # The player (third in the walk) the richest and after the best so far: the empty slots end empty.
        today([100.0, 300.0, 900.0, 50.0, 20.0, 10.0])
        slots_of(o, none, 'the player the richest, after the best so far')
        slots_of(o, [0xFF, walk[1], 0xFF, 0xFF], 'the player the richest, the best already shown')
        # He isn't: the empty slots get the others by their value.
        today([100.0, 50.0, 300.0, 200.0, -5.0, 250.0])
        slots = slots_of(o, none, 'the player not the richest')
        slots_of(o, [merchants, 0xFF, walk[3], merchants + 7], 'two slots set, two past the merchants')
        # The graphs.
        for period in (0, 1, 2):
            competition_of(o, 16, period, slots, 'the slots picked')
        slots = [player, walk[0], walk[3], walk[5]]
        for entry in (16, 17, 18):
            for period in (0, 1, 2):
                competition_of(o, entry, period, slots, 'objects')
        w16(o, 0x6DFC92, 10)
        w16(o, 0x6DFC94, 30)
        for entry in (16, 17, 18):
            competition_of(o, entry, 1, slots, '10 weeks recorded')
            competition_of(o, entry, 2, slots, '30 months recorded')
        without = next(m for m in others if m not in chosen)
        mixed = [walk[0], without, 0xFF, player]
        competition_of(o, 17, 2, mixed, 'a merchant without an object, an empty slot')
        # The years: the towns' list (nodes of 12 bytes, linked at [+0x8]) and the count [0x6DFC98] it keeps.

        def town_years(n):
            head = 0
            for _ in range(n):
                year = o.alloc(12)
                o.uc.mem_write(year, struct.pack('<III', 0, 0, head))
                head = year
            w32(o, 0x6DFC90 + 0x18, head)
            w16(o, 0x6DFC98, n)

        town_years(1)
        competition_of(o, 16, 3, slots, 'one year: it stops')
        town_years(5)
        w16(o, nodes[walk[1]] + 0x6, 2)
        for entry in (16, 17, 18):
            competition_of(o, entry, 3, mixed, '5 years, one object with 2')
        town_years(130)
        competition_of(o, 17, 3, slots, '130 years: 128 points')
        town_years(5)
        # The lists: names (Gutenbach and Gödecke differ at a Latin-1 letter; two equal family names), a merchant
        # with the bit 4 of [+0x8], company values below 0.
        families = o.u16(0x6DDB76)
        index = {}
        for i in range(families):
            text = cstring_at(o, direct(o, 0x512AE0, [i], ecx=0x6DDAA0), 0x20).split(b'\0')[0]
            index.setdefault(text, i)
        listed = [m for m in others if m not in slots and not o.u8(o.merchant(m) + 0x8) & 4]
        for m, family in zip(listed, (b'Gutenbach', b'G\xf6decke', b'Kinski', b'Kinski')):
            if family in index:
                w8(o, o.merchant(m) + 0x1D, index[family])
                direct(o, 0x4F6450, [], ecx=o.merchant(m))
        print('  names now: ' + ', '.join(f'{m} {name(m)}' for m in listed))
        flagged = listed[-1]
        w8(o, o.merchant(flagged) + 0x8, o.u8(o.merchant(flagged) + 0x8) | 4)
        for m, v in zip(listed[:3], (-5000.0, -100.0, 0.0)):
            o.uc.mem_write(o.merchant(m) + 0x46C, struct.pack('<f', v))
        for entry in (16, 17, 18):
            for slot in range(4):
                for sort in (0, 1, 2):
                    choice_of(o, entry, slot, slots, sort, 'slots full')
        alone = [slots[0], 0xFF, 0xFF, 0xFF]
        for entry in (16, 17, 18):
            choice_of(o, entry, 0, alone, 2, 'one merchant shown')
            choice_of(o, entry, 1, alone, 2, 'one merchant shown, an empty slot')
    return during


def town_records(o, count, rnd, towns):
    """A list of `count` records of the towns (nodes of 12 bytes: the towns' count, the data, the next one), newest
    first, each `towns(k)` towns of 0x204 random bytes: the four classes (one sum past 2^31 here and there), the ships,
    the production (some 0, some below 0) and the stock of the 24 wares. Returns the head."""
    head = 0
    for k in reversed(range(count)):
        n = towns(k)
        data = o.alloc(max(1, n) * 0x204)
        for t in range(n):
            r = bytearray(0x204)
            for f in range(0, 0x1E0, 4):
                struct.pack_into('<i', r, f, rnd.randrange(-300, 90000) if rnd.randrange(5) else 0)
            for c in range(4):
                struct.pack_into('<I', r, 0x1E0 + 4 * c, rnd.randrange(0, 9000))
            if rnd.randrange(23) == 0:
                struct.pack_into('<I', r, 0x1E0, 0x7FFFF000)
            struct.pack_into('<I', r, 0x1FC, rnd.randrange(0, 60))
            o.uc.mem_write(data + 0x204 * t, bytes(r))
        node = o.alloc(12)
        o.uc.mem_write(node, struct.pack('<III', n, data, head))
        head = node
    return head


def statistics_comparison(o):
    """The statistics window's town comparison (Comparativa de ciudades): the towns' residents (0x4A3BB0 through
    STATS_RESIDENTS), the graphs of the residents, the incoming ships, a ware's production and stock (0x4A3FA0 with
    0x4A2AB0 through STATS_TOWNS), the town buttons' lists sorted by name and by key (0x49E850, 0x471FF0, 0x471EA0
    through STATS_CHOICE), the ware button's list (in 0x49F350, 0x471F10 through STATS_WARES) and what the window
    fixes when it opens (0x49D380 through STATS_SLOTS). First without the towns' records (the residents from the
    towns, the graphs empty, no years); then with weeks (26, more than the points; one of them without the last two
    towns), months (10, fewer) and years (1: it stops; 5; 130: 128 points), random values (a class sum past 2^31, some
    productions 0 or below 0, a ware never produced); each entry by each period, with an empty slot, without any town,
    without any ware; the wares in loads, barrels and pieces; the lists of each slot, sorted each way, with all four
    towns shown and with one; the town names as the window takes them (by the town's index: the game's bug)."""
    import random
    rnd = random.Random(1607)
    towns = o.counts()[0]
    print(f'towns {towns}, the town numbers {[o.u8(0x6DE4B8 + t) for t in range(towns)]}')

    def during(o):
        statistics_copiers(o)
        competition_copiers(o)
        lists = 0x6DFC90 + 0xC  # days, weeks, months, years
        for k in range(4):
            w32(o, lists + 4 * k, 0)
        everyone = list(WINDOW_TOWNS)
        # Without the towns' records.
        residents_of(o, 'no weeks')
        for entry in (7, 8, 9, 10):
            towns_of(o, entry, 1, everyone, 'no records')
        towns_of(o, 9, 2, everyone, 'no records, ware 3', ware=3)
        towns_of(o, 7, 3, everyone, 'no years')
        towns_of(o, 8, 0, everyone, 'period 0, no records')
        for entry in (7, 8, 9, 10):
            choice_of(o, entry, 1, [0xFF] * 4, 2, 'no records')
        wares_of(o, 0)
        wares_of(o, 1)
        slots_of(o, [0xFF] * 4, 'the first town and ware past', wares=[30, 2, 0xFF, 11], towns=[0xFF, 5, 0xFF, 2])
        slots_of(o, [0xFF] * 4, 'kept', wares=[23, 0xFF, 0xFF, 0xFF], towns=[towns - 1, 0xFF, 0xFF, 0xFF])
        # Weeks, months and years of records.
        w32(o, lists + 4, town_records(o, 26, rnd, lambda k: towns - 2 if k == 7 else towns))
        w32(o, lists + 8, town_records(o, 10, rnd, lambda k: towns))
        w16(o, 0x6DFC92, 26)
        w16(o, 0x6DFC94, 10)
        # A ware nobody produces (the production's smallest stays at its start: 0).
        node = o.u32(lists + 4)
        while node:
            n, data, nxt = struct.unpack('<III', bytes(o.uc.mem_read(node, 12)))
            for t in range(n):
                w32(o, data + 0x204 * t + 0x60 + 4 * 5, 0)
            node = nxt
        # The newest week's town 1 with its classes' sum past 2^31 (the residents' table takes it as 0).
        head = o.u32(o.u32(lists + 4) + 4)
        w32(o, head + 0x204 + 0x1E0, 0x7FFFF000)
        w32(o, head + 0x204 + 0x1E4, 0x20000)
        residents_of(o, 'the newest week')
        shown = [everyone, [0, 0xFF, 2, towns - 1], [0xFF, 0xFF, 0xFF, 1]]
        for entry in (7, 8, 9, 10):
            for period in (0, 1, 2):
                towns_of(o, entry, period, shown[period], 'records')
        for entry in (9, 10):
            for ware in (0, 3, 5, 20, 23):
                towns_of(o, entry, 1, everyone, 'wares', ware=ware)
        towns_of(o, 7, 1, [0xFF] * 4, 'no town')
        towns_of(o, 9, 1, everyone, 'no ware', wares=[24, 0xFF, 30, 0xFF], ware=3)

        def years(n):
            w32(o, lists + 12, town_records(o, n, rnd, lambda k: towns) if n else 0)
            w16(o, 0x6DFC98, n)

        years(1)
        towns_of(o, 8, 3, everyone, 'one year: it stops')
        years(5)
        for entry in (7, 8, 9, 10):
            towns_of(o, entry, 3, everyone, '5 years', ware=3)
        years(130)
        towns_of(o, 10, 3, everyone, '130 years', ware=20)
        years(0)
        # The lists' keys from a newest week without the last two towns.
        w32(o, lists + 4, town_records(o, 3, rnd, lambda k: towns - 2 if k == 0 else towns))
        for entry in (7, 8, 9, 10):
            for slot in range(4):
                for sort in (0, 1, 2):
                    choice_of(o, entry, slot, [0xFF] * 4, sort, 'four towns shown', ware=3)
            choice_of(o, entry, 0, [0xFF] * 4, 2, 'one town shown', towns=[2, 0xFF, 0xFF, 0xFF], ware=20)
    return during


def edit_town_records(o, head, edit):
    """edit(k, t, record) on each town's record (a bytearray of 0x204) of the list from `head` (k from the newest)."""
    node, k = head, 0
    while node:
        n, data, nxt = struct.unpack('<III', bytes(o.uc.mem_read(node, 12)))
        for t in range(n):
            r = bytearray(o.uc.mem_read(data + 0x204 * t, 0x204))
            edit(k, t, r)
            o.uc.mem_write(data + 0x204 * t, bytes(r))
        node, k = nxt, k + 1
    return head


def statistics_town_data(o):
    """The statistics window's town data (Ciudades): the graphs of a town's classes, needs covered, quality of life,
    export balance, production and stock (0x4A3FA0's handlers 0..3 and their fills, through STATS_TOWNS), the texts
    beside them (the newest week's classes), the ware buttons' lists (0x49E906 through STATS_CHOICE, sorted by
    0x471F10) and the town button's list (0x49FB41 through STATS_TOWN_LIST, sorted by 0x471FF0). First without the
    towns' records; then with weeks (26; the newest and the 8th without the last two towns), months (10) and years
    (1: it stops; 5; 130), the town data's fields set per town: town 1 nothing below 0 (the needs in three series, the
    export balance raised by a tenth of its largest), town 2 needs and balances below 0 (the zero lines), town 3 a
    balance of 0 everywhere (raised by 100), town 4 only the ware 0's balance below 0 (not shown, it still moves the
    zero line); a class past 2^31 in the newest week; each entry by each period and town; an empty ware slot, no
    ware, no town; the comparison's first town «none» set to 0 by the population (and kept at the towns' count)."""
    import random
    rnd = random.Random(3105)
    towns = o.counts()[0]
    print(f'towns {towns}, the town numbers {[o.u8(0x6DE4B8 + t) for t in range(towns)]}')

    def fields(k, t, r):
        for base in (0x1F0, 0x1F6):
            for c in range(3):
                v = rnd.randrange(0, 400)
                if t == 2 and rnd.randrange(4) == 0:
                    v = rnd.randrange(-300, 0)
                struct.pack_into('<h', r, base + 2 * c, v)
        for w in range(24):
            if t == 1:
                v = rnd.randrange(0, 20000)
            elif t == 3:
                v = 0
            elif t == 4:
                v = rnd.randrange(-9000, 0) if w == 0 else rnd.randrange(0, 5000)
            else:
                v = rnd.randrange(-15000, 20000)
            struct.pack_into('<i', r, 0x120 + 4 * w, v)

    def during(o):
        statistics_copiers(o)
        competition_copiers(o)
        lists = 0x6DFC90 + 0xC  # days, weeks, months, years
        for k in range(4):
            w32(o, lists + 4 * k, 0)
        everyone = list(WINDOW_TOWNS)
        # Without the towns' records.
        for entry in range(6):
            towns_of(o, entry, 1, everyone, 'no records', town=2)
        towns_of(o, 0, 3, everyone, 'no years', town=2)
        towns_of(o, 1, 0, everyone, 'period 0, no records', town=2)
        for entry in (3, 4, 5):
            for slot in range(4):
                choice_of(o, entry, slot, [0xFF] * 4, slot & 1, 'the window\'s wares')
            choice_of(o, entry, 1, [0xFF] * 4, 1, 'one ware', wares=[0xFF, 5, 0xFF, 0xFF])
            choice_of(o, entry, 0, [0xFF] * 4, 1, 'one ware elsewhere', wares=[0xFF, 5, 0xFF, 0xFF])
            choice_of(o, entry, 2, [0xFF] * 4, 0, 'the same ware twice', wares=[7, 7, 30, 0xFF])
        town_list_of(o, 0)
        town_list_of(o, 1)
        # Weeks, months and years of records.
        w32(o, lists + 4, edit_town_records(o, town_records(o, 26, rnd, lambda k: towns - 2 if k in (0, 7) else towns),
                                            fields))
        w32(o, lists + 8, edit_town_records(o, town_records(o, 10, rnd, lambda k: towns), fields))
        w16(o, 0x6DFC92, 26)
        w16(o, 0x6DFC94, 10)
        head = o.u32(o.u32(lists + 4) + 4)
        w32(o, head + 0x204 + 0x1E4, 0x80000005)  # the newest week's town 1: a class past 2^31
        for entry in range(6):
            for period in (1, 2):
                for town in (1, 2, 3, 4, towns - 1):
                    towns_of(o, entry, period, everyone, 'records', town=town)
        wares = [0xFF, 5, 0xFF, 20]
        for entry in (3, 4, 5):
            towns_of(o, entry, 1, everyone, 'empty slots', wares=wares, town=2)
            towns_of(o, entry, 1, everyone, 'without the ware 0', wares=[2, 3, 11, 0xFF], town=4)
        towns_of(o, 4, 1, everyone, 'no ware', wares=[24, 0xFF, 30, 0xFF], town=1)
        towns_of(o, 2, 1, [0xFF] * 4, 'no town', town=1)
        towns_of(o, 0, 1, [0xFF, 1, 2, 3], 'the first town none', town=1)
        towns_of(o, 0, 1, [towns, 0xFF, 1, 0xFF], 'the first town at the count', town=1)
        towns_of(o, 0, 2, [0xFF, 0xFF, 0xFF, 0xFF], 'no town at all', town=1)

        def years(n):
            w32(o, lists + 12, edit_town_records(o, town_records(o, n, rnd, lambda k: towns), fields) if n else 0)
            w16(o, 0x6DFC98, n)

        years(1)
        towns_of(o, 2, 3, everyone, 'one year: it stops', town=1)
        years(5)
        for entry in range(6):
            towns_of(o, entry, 3, everyone, '5 years', town=2)
        years(130)
        towns_of(o, 3, 3, everyone, '130 years', wares=[0, 20, 3, 0xFF], town=1)
        years(0)
    return during


CITIZEN_STATS = to.SENTINEL + 0xFC0  # of the citizens' window's update, 0x40B570 (probes_unchecked.cpp kCitizenStats)


def citizen_copier(o):
    """The copier at CITIZEN_STATS (stdcall: town, part): the citizens' window's update 0x40B570 (the top bar's man)
    from 0x40B595, past its close button, with ebp a window of its own (zeroed before each call, as a window that
    has shown nothing yet: the update writes what changed) and the town view *0x6E51AC a fake one whose [+0xC324] is
    the town (put back after); its redraw 0x4B9650 skipped by a hook. Its view: the window's [+0xA4..+0xBF] (the
    moods, the residents, the arrows) and [+0x1AC] (the shortage of dwellings)."""
    from unicorn.x86_const import UC_X86_REG_EIP, UC_X86_REG_ESP
    win, view = o.alloc(0x200), o.alloc(0xC400)
    saved = o.alloc(4)
    o.citizen_window = win
    o.citizen_active = False
    at = o.alloc(0x80)
    code = bytes([0x8B, 0x44, 0x24, 0x04, 0xA3]) + struct.pack('<I', view + 0xC324)  # the town
    code += bytes([0xA1]) + struct.pack('<I', 0x6E51AC) + bytes([0xA3]) + struct.pack('<I', saved)
    code += bytes([0xC7, 0x05]) + struct.pack('<II', 0x6E51AC, view)
    call_at = len(code)
    code += bytes([0xE8, 0, 0, 0, 0])  # call the entry below
    citizen_view = at + len(code)
    code += bytes([0x90, 0xA1]) + struct.pack('<I', saved) + bytes([0xA3]) + struct.pack('<I', 0x6E51AC)
    code += bytes([0xC2, 0x08, 0x00])
    entry = len(code)
    code += bytes([0x83, 0xEC, 0x1C, 0x55, 0xBD]) + struct.pack('<I', win)  # sub esp, 0x1C; push ebp; mov ebp, win
    code += bytes([0xE9]) + struct.pack('<i', 0x40B595 - (at + len(code) + 5))
    code = bytearray(code)
    code[call_at + 1:call_at + 5] = struct.pack('<i', entry - (call_at + 5))
    o.uc.mem_write(at, bytes(code))
    o.uc.mem_write(CITIZEN_STATS, bytes([0xE9]) + struct.pack('<i', at - (CITIZEN_STATS + 5)))

    def view_hook(uc, address, size, data):
        stats_copy(o, bytes(o.uc.mem_read(win + 0xA4, 0x1C)) + bytes(o.uc.mem_read(win + 0x1AC, 1)),
                   o.u32(uc.reg_read(UC_X86_REG_ESP) + 8))

    def redraw(uc, address, size, data):
        if o.citizen_active:  # 0x4B9650 (thiscall, a rect): the interface's
            esp = uc.reg_read(UC_X86_REG_ESP)
            uc.reg_write(UC_X86_REG_EIP, o.u32(esp))
            uc.reg_write(UC_X86_REG_ESP, esp + 8)

    for address, f in ((citizen_view, view_hook), (0x4B9650, redraw)):
        o.uc.hook_add(UC_HOOK_CODE, f, begin=address, end=address)
        o.uc.ctl_remove_cache(address, address + 1)


def citizens_of(o, town, what):
    o.uc.mem_write(o.citizen_window, bytes(0x200))
    o.citizen_active = True
    direct(o, CITIZEN_STATS, [town, 0])
    o.citizen_active = False
    win = o.citizen_window
    moods = struct.unpack('<3h', bytes(o.uc.mem_read(win + 0xA4, 6)))
    residents = struct.unpack('<4I', bytes(o.uc.mem_read(win + 0xAC, 16)))
    arrows = list(o.uc.mem_read(win + 0xBC, 3))
    print(f'  citizens of town {town} {what}: {list(residents)}, moods {list(moods)}, arrows {arrows}, shortage '
          f'{o.u8(win + 0x1AC)}')


def citizen_window(o):
    """The top bar's man, the citizens' window (0x40B570 through CITIZEN_STATS): every town of the save as it is; then
    a town retouched for each mood's bounds (the satisfaction [+0x300 + 2k] at −11, −10, −1, 0, 9, 10, 19, 20, 29,
    30, and the extremes), each arrow (twice [+0x308 + 2k] below, equal to and above it) and the shortage of
    dwellings (the residents at 95 % of [+0x2F8 + 2k] and one more; a number whose · 100 wraps below 0)."""
    towns = o.counts()[0]

    def during(o):
        citizen_copier(o)
        for t in range(towns):
            citizens_of(o, t, 'as it is')
        town = o.town(1)
        bounds = [-11, -10, -1, 0, 9, 10, 19, 20, 29, 30, -32768, 32767]
        for k in range(0, len(bounds), 3):
            for c in range(3):
                value = bounds[k + c] if k + c < len(bounds) else 0
                w16(o, town + 0x300 + 2 * c, value & 0xFFFF)
                w16(o, town + 0x308 + 2 * c, ((value // 2) + (c - 1)) & 0xFFFF)  # twice: below, near, above
            citizens_of(o, 1, f'moods {bounds[k:k + 3]}')
        for c in range(3):
            w16(o, town + 0x300 + 2 * c, 8)
            w16(o, town + 0x308 + 2 * c, 4)  # twice equal: no arrow
        w16(o, town + 0x2F8, 20)
        w32(o, town + 0x2D8, 19)  # 1900 against 1900: no shortage
        w16(o, town + 0x2FA, 1000)
        w32(o, town + 0x2DC, 10)
        w16(o, town + 0x2FC, 1000)
        w32(o, town + 0x2E0, 10)
        citizens_of(o, 1, 'at 95 %, twice equal')
        w32(o, town + 0x2D8, 20)  # 2000 against 1900
        citizens_of(o, 1, 'past 95 %')
        w32(o, town + 0x2D8, 0x02000000)  # · 100 wraps below 0
        citizens_of(o, 1, 'wrapped')
        w32(o, town + 0x2D8, 5)
        w32(o, town + 0x2E0, 960)  # the third class past 95 % of its 1000
        citizens_of(o, 1, 'the third class short')
    return during


def town_symbols(o):
    """The battles' piece 6, part 5: the town's situation as the local player knows it (0x535300 on the towns'
    history 0x6DE588), which the sea map's event symbols show: every town as it is; then the player's lag for town 1
    at 0, 5, 15, 16 and 0xFF (the day wraps modulo 16), with town 1's [+0x2C8] and its history's 16 days retouched
    (each day a different bit, all the event bits now, and a ware short); the player's slot's table pointer 0 and
    his slot another merchant's (the day after, for every town)."""
    towns, _, merchants, _ = o.counts()
    player = o.u32(to.PLAYER)
    history = 0x6DE588
    slot = next((k for k in range(8) if o.u8(history + 0xA00 + k) == player), None)
    if slot is None:
        raise RuntimeError('the player has no slot in the towns\' history')
    table = o.u32(history + 0xA08 + 4 * slot)
    print(f'the player {player}: slot {slot}, table {table:#x}, lags {list(o.uc.mem_read(table, towns))}; day '
          f'{o.u32(0x6DE4B4) >> 8 & 0xF}')

    def known(t, what):
        r = direct(o, 0x535300, [t], ecx=history)
        print(f'  town {t} ({what}): {r:#010x}')

    def during(o):
        for t in range(towns):
            known(t, 'as it is')
        saved = set_fields(o, [(o.town(1) + 0x2C8, 4, o.u32(o.town(1) + 0x2C8)), (table + 1, 1, o.u8(table + 1))] +
                           [(history + 0x40 + 4 * d, 4, o.u32(history + 0x40 + 4 * d)) for d in range(16)])
        w32(o, o.town(1) + 0x2C8, 0x4009E18 | 3 << 18)
        for d in range(16):
            w32(o, history + 0x40 + 4 * d, (1 << d) | 0x800 | (d % 0x1F) << 18)
        for lag in (0, 5, 15, 16, 0xFF):
            w8(o, table + 1, lag)
            known(1, f'lag {lag}')
        w32(o, history + 0xA08 + 4 * slot, 0)
        known(1, 'the table\'s pointer 0')
        w32(o, history + 0xA08 + 4 * slot, table)
        w8(o, history + 0xA00 + slot, (player + 1) % merchants)
        for t in range(towns):
            known(t, 'no slot')
        w8(o, history + 0xA00 + slot, player)
        restore_fields(o, saved)
    return during


def player_op_text(*args):
    """player_ops's output as text."""
    import subprocess
    out = subprocess.run([PLAYER_OPS, *[str(a) for a in args]], check=True, capture_output=True, text=True)
    return out.stdout.strip()


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
    'road_expired': Scenario(road_expired, EVENTS_FUNCTIONS, 1, ['4eb25f']),
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
    'gate_costs': Scenario(gate_costs, BUILD_FUNCTIONS, 0, ['523e3d']),
    'pier_edge': Scenario(pier_edge, [0x524920], 0, ['@524ad8']),
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
    'town_tick_fixes': Scenario(town_tick_fixes, [0x51C360, 0x5109B0, 0x5101D0], 0, []),
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
    # sieges.cpp
    'siege_ops': Scenario(siege_ops, [0x535760], 0, ['@536a72>537025', '@633cdf']),
    'siege_branches': Scenario(siege_branches, [0x535760, 0x629C60, 0x62E810, 0x62B2B0, 0x6281F0, 0x51D5F0, 0x633DB0,
                                                0x635220, 0x635440], 0, [
        '@629c9b>62b28f', '@62e8f2', '@62b3ce', '@6290e9', '@62927e', '@51d64c', '@634600', '@6352b4',
        '@6353ad', '@6355c5']),
    'siege_fixes': Scenario(siege_fixes, [0x535760], 0, []),
    'siege_gates': Scenario(siege_gates, [0x535760], 0, ['@62a3ba', '@62a49f']),
    'office_ops': Scenario(office_ops, [0x535760], 0, [
        '@51b91b', '@51b9a8', '@51b8f7>51b9ff', '@51b915>51b9ff', '@5362bc>537025', '@5362cd>537025', '@53ce59',
        '@53cec7', '@53ce93>53cef2']),
    'battle_join': Scenario(battle_join, [0x535760], 0, ['@61131b', '@611332', '@611315>6113b7', '@536cec>537025',
                                                          '@536cdd>537025']),
    'player_pirate': Scenario(player_pirate, [0x535760, 0x506720], 1, [
        '@53b91a', '@5095fd', '@5096a4>509706', '@509783', '@53b9d7', '@53b914>53ba2f', '@53b89f>53ba2f']),
    'player_pirate_lists': Scenario(player_pirate_lists, [0x535760], 0, ['@4f5960', '@4f595e>4f5976']),
    'player_pirate_new': Scenario(player_pirate_new, [0x535760], 0, ['@5096a6', '@5096fd', '@509783']),
    'siege_sale': Scenario(siege_sale, [0x535760, 0x4DAF10], 0, ['@633ae7', '@4dafc7']),
    'siege_pitch': Scenario(siege_pitch, [0x535760, 0x528070, 0x6281F0], 1, ['@5281b1', '@63692e', '@6326cc',
                                                                             '@63276a']),
    # operations.cpp
    'operation_guards': Scenario(operation_guards, [0x535760], 0, [
        '@5365f5>537025', '@53663a>537025', '@536640', '@53657b>537025', '@536584>537025', '@5365b1>537025',
        '@535e12>537025', '@535c7b>537025', '@535e4c>537025', '@53664f', '@5366f3>537025', '@53675f>537025',
        '@53fecd>54000a', '@53dac4>53dbcd', '@53dc01>53dc75', '@53dc15>53dc75', '@53fa91>53faf8',
        '@542f42>54346e', '@5433e3', '@5051fe']),
    'operation_convoys': Scenario(operation_convoys, [0x535760], 0, [
        '@539b5f>539c52', '@539b85>539c52', '@539bac>539c52', '@539c6f>539ce1', '@539c91>539ce1',
        '@539cb4>539ce1', '@539c2a']),
    'operation_loans': Scenario(operation_loans, [0x535760], 0, [
        '@539dd0>539f28', '@539e01>539f28', '@539e81>539e88', '@539ea1', '@539ee5', '@4f8fe0']),
    'operation_sale': Scenario(operation_sale, [0x535760], 0, [
        '@53d1a4>53d2a6', '@53d1b8>53d2a6', '@53d1ec>53d2a6', '@53d228>53d2a6', '@53d224', '@53d294',
        '@53d28f']),
    # events.cpp
    'event_queue_branches': Scenario(event_queue_branches, [0x4D85C0, 0x4DB330, 0x535760], 0, [
        '@4d89d2', '@4dfcb7', '@4db37b', '@4db48d', '@4d8d01']),
    'ai_debt_branches': Scenario(ai_debt_branches, [0x4F42B0, 0x4F9580, 0x4F4A20, 0x4F4B40, 0x4F4E50], 0, [
        '@4f4303>4f4322', '@4f958b>4f95b7', '@4f95a1', '@4f4a44>4f4b30', '@4f4a4e>4f4b30', '@4f4b60>4f4e46',
        '@4f4e69', '@4f5bea', '@4f4ebc', '@4db2d4', '@4f4ed3']),
    'sale_branches': Scenario(sale_branches, [0x535760, 0x4DAF10, 0x4DAFE0, 0x4DAB00], 0, [
        '@4daf24', '@4daf49', '@4db011>4db0f7', '@4db032>4db0f7', '@4dab21>4daefe', '@4dab48>4daefe', '@4db0c4',
        '@4dab66>4daefe', '@4dac3c>4dad70', '@4dacdf>4dad70', '@4dab8d>4daefe', '@4dabb2>4daefe']),
    'ice_storm_branches': Scenario(ice_storm_branches, [0x4E45C4, 0x4E43E4], 0, [
        '@4e46be', '@4e475a', '@4e4847', '@4e4547>4e443a']),
    'jobs_captains': Scenario(jobs_captains, [0x4E8E24, 0x4E2634], 0, [
        '@4e8ebe', '@4e8f02', '@4e8f17>4e9040', '@4e8ef3>4e9040', '@4e26e0', '@4fe104', '@4e27c9']),
    # events_towns.cpp
    'debt_mail_branches': Scenario(debt_mail_branches, [0x4DB4E0, 0x4D6780, 0x535760], 0, [
        '@4f679f>4f67c0', '@4db6e0>4db5cc', '@4d6798>4d690e', '@4d6800', '@538aab>538c2c', '@538aca>538c2c']),
    'fire_branches': Scenario(fire_branches, [0x4D85C0], 0, [
        '@4e975f>4e977d', '@4e97c4>4e9811', '@4e9827', '@4e9863>4e98df', '@4e9898']),
    'wedding_feast_branches': Scenario(wedding_feast_branches, [0x4D85C0, 0x50E000, 0x531470, 0x5316E0], 0, [
        '@4e7cb2>4e81d2', '@4e7cd6>4e81d2', '@4e7dff', '@50e066', '@50e144', '@4e23f3>4e260a', '@4e2447', '@4e24fc',
        '@50e09b', '@5314cb>531583', '@531518>53156c']),
    'event03_branches': Scenario(event03_branches, [0x4DE790, 0x4DCEA0, 0x4DDA40, 0x4DCBF0, 0x535760], 0, [
        '@4de8af>4de9b2', '@4dd25a>4dd298', '@4ddabb>4ddbf9', '@4ddad1>4ddbf9', '@4ddb7f>4ddbf9', '@4dcc3d>4dcd7b',
        '@530c2c>530c58']),
    'child_names': Scenario(child_names, [0x4F69B0], 0, ['@512ac3', '@512ab9', '@4f6b22']),
    'yard_branches': Scenario(yard_branches, [0x535760], 0, ['@52a3ab', '@52a3c8', '@52a3ed', '@52a413', '@52a723']),
    'captain_retire_branches': Scenario(captain_retire_branches, [0x4D85C0], 0, [
        '@4ddcf5>4ddd46', '@4ddcfc', '@4ddcf5>4ddd7b', '@4ddcf5>4ddde1', '@4ddcf5>4dddc0', '@4ddcf5>4dde2d',
        '@4ddce7>4dde2d', '@509917', '@4ddc27>4dde2d', '@4ddcbf']),
    # events_mayor.cpp
    'alderman_hanse_votes': Scenario(alderman_hanse_votes, [0x4DBBD0, 0x4DC1F0], 0, [
        '@4dbdbc', '@4dbdcc', '@4dbdd8', '@4dc301', '@4dc49f']),
    'ship_making_branches': Scenario(ship_making_branches, [0x507D60, 0x52A8D0, 0x4F4400, 0x509250], 0, [
        '@507e87', '@507ee3', '@507f31', '@507f5c', '@507d7c', '@52a8ec', '@52a91b', '@4f44cf', '@5093f7']),
    'auction_phases': Scenario(auction_phases, [0x4D85C0], 0, [
        '@4e2d37', '@4e2e35', '@4e2ea7', '@4e2ec1>4e37a3', '@4e3205', '@4e32ff', '@4e33c4', '@4e3709',
        '@4e2d0d>4e37a3']),
    'building_sale_branches': Scenario(building_sale_branches, [0x4F4550, 0x5256F0], 0, [
        '@4f4781', '@4f47e2', '@4f4838', '@4f487e>4f48ff', '@5257cc>5259b8', '@525861>525822']),
    # events_rare.cpp
    'game_end_branches': Scenario(game_end_branches, [0x4E4A44], 0, [
        '@4e4cd9', '@4e4c16', '@4e4bb7>4e4bc8', '@4e4c07>4e4c0c', '@4e4c09', '@4e4b90>4e4bd6', '@4e4b50',
        '@4e4f7f', '@4e4e0d', '@4e4dd4>4e4ef8', '@4e4f49', '@4e4f70>4e4f75', '@4e4f72', '@4e4f5e>4e4f75',
        '@4e4da4>4e4f15', '@4e4f47', '@4e51a1', '@4e50b5', '@4e506a>4e5078', '@4e5178', '@4e519c',
        '@4e4a76>4e4b50', '@4e518e>4e4c0c', '@4e5043>4e5085', '@4e5256', '@4e5265', '@4e5357', '@4e5366', '@4e53af',
        '@4e54d0', '@4e54e9', '@4e4b63>4e55e2']),
    'siege_fire_branches': Scenario(siege_fire_branches, [0x4D85C0, 0x4DB100, 0x4E9094], 0, [
        '@4e1c90', '@4e1cc3>4e1e48', '@4e1d51>4e1e48', '@4e1d7f', '@4e1e76>4e2133', '@4e1eba>4e2133',
        '@4e1ec2>4e2133', '@4e1eca>4e2133', '@4e1f20>4e2133', '@4e1f67>4e2133', '@4e1ff5', '@4e1f07>4e1f86',
        '@4e1fcd', '@4e1fba>4e2133', '@4e202c>4e2133', '@4e20ee>4e2133', '@4eabe4>4eafbd', '@4ecb4d>4ecdd5',
        '@4ecb72>4ecdd5', '@4ecbdd>4ecdc4', '@4ecc45>4ecdc4', '@4db11b>4db210', '@4db141>4db210',
        '@4db1df>4db210', '@4db1e1', '@4e90d7>4e948e', '@4e925c']),
    'pirate_event_branches': Scenario(pirate_event_branches, [0x4DFD44, 0x4E0334, 0x4EA594], 0, [
        '@4dfe0e', '@4dfe1b>4e00e8', '@4dfe4d', '@4dfe7b', '@4dfe84', '@4dfe47>4e00e8', '@4dfeac>4e00e8',
        '@4dfdf0>4e00e8', '@4e0399', '@4e048d', '@4e04e3', '@4e05a1', '@4e0889', '@4ea5c7>4eab5b',
        '@4ea8f1>4eab5b']),
    'council_task_branches': Scenario(council_task_branches, [0x4E9A94, 0x532C50, 0x4EAFE4], 0, [
        '@4e9ae1>4ea093', '@4e9c16', '@4e9c62', '@4e9c6e', '@4e9c97>4e9d73', '@4e9cad', '@4e9ce8', '@4e9d31',
        '@4e9d59', '4e9d6b>4e9d73', '@4e9d9e', '@4e9dcb', '@4e9ddd', '@4e9bef>4ea093', '@4e9ff6', '@4ea064',
        '@4ea07c', '@4ea07a>4ea093', '@4ea088', '@532c67>532ccc', '@532c79>532ccc', '@532c84>532ccc',
        '@532cde>532d24', '@532cfe>532d24', '@532d37>532d24', '@532d55>532d24', '@532d62>532d24', '@532d8b',
        '@532d7e>532d96', '@532d89>532d96', '@532da6', '@532dbf', '@532de8', '@532c8e>532ccc', '@4eb070',
        '@4eb0a8']),
    'expedition_branches': Scenario(expedition_branches, [0x4E6A54], 0, ['@4e6a7f>4e79f1', '@4e7142']),
    # mail.cpp
    'mail_branches': Scenario(mail_branches, [0x4D6530], 0, ['@4d653e>4d6670', '@4d6553>4d6670', '@4d65fc',
                                                            '@4d6b66']),
    # events_big.cpp
    'site_start_houses': Scenario(site_start_houses, [0x500F50], 0, ['@500f79>501355', '@501399>5013ae']),
    # ship_trade.cpp
    'ship_trade_branches': Scenario(ship_trade_branches, [0x526AA0, 0x519920], 0, [
        '@526ab7>526ada', '526ad3>526ada', '@519a37']),
    # ship_ai.cpp
    'ship_ai_branches': Scenario(ship_ai_branches, [0x519BA0, 0x501BE0, 0x506720], 0, [
        '@519c03>519c38', '@501d8b', '@506a27']),
    # sailing.cpp
    'ship_delete_branches': Scenario(ship_delete_branches, [0x507FB0], 0, [
        '@507fd8>5083a0', '@5080ed>5083a0', '@50817b']),
    'pursue_branches': Scenario(pursue_branches, [0x50B030], 0, [
        '@50b083', '@50b0b8', '@50b0ed', '@50b189', '@50b11a', '@50b13c', '@50b183>50b221', '@50b1f4>50b221',
        '@50b1f6', '@50b1c0>50b1ed', '@50b22b']),
    'raid_arrival': Scenario(raid_arrival, [0x506720], 0, ['@506e19']),
    # The battles' piece 4 (battle/attack.cpp)
    'chase_battle': Scenario(chase_battle, [0x50B030], 0, [
        '@50b274', '@50b2f6', '@50b321', '@50b40c', '@50b500', '@50b59d', '@50b5bc', '@50b627', '@50b66f',
        '@50b6bc', '@50b79c', '@50b826', '@50b84b', '@50b893', '@50b8d5', '@50b9b8', '@50b9b2>50ba46', '@50b884',
        '@50ba5e']),
    'hanse_fleet': Scenario(hanse_fleet, [0x50BA80], 0, [
        '@50bb2f', '@50bace', '@50bb09', '@50bb56', '@50bbba', '@50bbb8>50bc08', '@50bc08']),
    'raid_fleet': Scenario(raid_fleet, [0x506720], 0, ['@506e19', '@506e4d>506e7c']),
    # convoys.cpp
    'convoy_states': Scenario(convoy_states, [0x506720], 0, [
        '@5071d4', '@507255', '@507300', '@50760c', '@50761e', '@507854', '@50786e', '@507c7f']),
    'convoy_ship_branches': Scenario(convoy_ship_branches, [0x50A580, 0x502110, 0x501EB0], 0, [
        '@50a5ad>50aac3', '@502186', '@501fd2']),
    # battle/
    'battle_branches': Scenario(battle_branches, [0x535760, 0x540520, 0x50BFD0, 0x509250], 0, [
        '@540d3e', '@50c1fb', '@50c20b>50c33c', '@5095f7>5097a1']),
    # 0x60B357: kind 2 or 3, the centre inside, a side crossed (with or without the side's pointer), no touch;
    # 0x608850's rock (automatic, human; each turn and none) and islet (automatic, human).
    'battle_rocks': Scenario(battle_rocks, [0x608850], 0, [
        '@60b379', '@60b3c2', '@60b679', '@60bc1f', '@60bc1d>60bc33', '@60bc41',
        '@608996', '@6089cb', '@608abf', '@608be1', '@608bf0', '@608c0d', '@608c1c', '@608bd1>608bff',
        '@608c01>608c2c', '@608d15', '@608d37', '@608d35>608d3c']),
    # the player's actions
    'trade_window': Scenario(trade_window, [0x535760], 0, ['@5375b0', '@5373c2', '@5375ae>5375fc']),
    'trade_branches': Scenario(trade_branches, [0x535760, 0x52E430, 0x52E1D0], 0, [
        '@537480>5375fc', '@53748e>5375fc', '@5374c4>5375fc', '@5374fa', '@537518', '@537534', '@537502>5375fc',
        '@537589', '@52f3f5', '@52f4c6', '@52f4ac', '@52f4f0>52f509', '@5375ae>5375fc', '@5375b0',
        '@537366>53744b', '@53739c>53744b', '@5373b7', '@5373c2']),
    'crew_ops': Scenario(crew_ops, [0x535760], 0, [
        '@537c66', '@537cad', '@537cd7', '@537ce9', '@537cfb', '@537d31', '@537d5d', '@537d95', '@537c54>537dc7',
        '@4f6cc1', '@4f6cd9', '@4f6bf7', '@4f6c03', '@537dfe>537ed3', '@537e0c>537ed3', '@537e42', '@537e6c',
        '@537eb8>537ec0', '@537eba', '@537f76', '@537f74>537fcb', '@538038>5380fb', '@53807f', '@5380f0']),
    'repair_ops': Scenario(repair_ops, [0x535760], 0, [
        '@52ad16', '@52ad51', '@52adf8', '@52add0', '@52ae21', '@52ae4d', '@52ae8a', '@52aeb6', '@52af01', '@52af5d',
        '@52af85', '@52aa17', '@52aa52', '@52aa59', '@52aa6d', '@52aa89', '@52aa90', '@5357b9>537025',
        '@5357da>537025', '@5357e8>537025', '@5399bb>539b24', '@5399dd>539b24', '@5399ec>539b24', '@5399f2']),
    'ship_windows': Scenario(ship_windows, [0x535760, WINDOW_SHIPS, 0x5269A0, 0x4F6CA0], 0, [
        '@504ad9', '@504b71', '@504b67>504b81', '@504b6f>504b81', '@504bd7>504c62', '@504c0c>504c62',
        '@504c13>504c62', '@504c29>504c53', '@504c36>504c62', '@504c47>504c62', '@504c51>504c62', '@504c53',
        '@504d3e>504de8', '@504d5c>504de8', '@504d62', '@504d95>504de8', '@504dc5', '@504dc3>504dd5',
        '@5269d3>5269e1', '@5269df>526a42', '@526a1f>526a26', '@526a24>526a42', '@526a37',
        '@4f6cc1', '@4f6cd7>4f6cdb', '@4f6cd9', '@5cc479', '@5d03d6', '@5d03d4>5d03ed']),
    'office_trade': Scenario(office_trade, [0x535760], 0, [
        '@538110>538200', '@53811b>538200', '@53813c>538200', '@53815d>538200', '@53816b', '@53817f', '@538199',
        '@5381c0', '@5381d2', '@5180ca>5181be', '@518139', '@5180e7', '@51815b', '@51817e', '@51819d',
        '@4ff6fa>4ff772', '@4ff763', '@4ff70c', '@53d2b9>53d3b1', '@53d2c4>53d3b1', '@53d2e4>53d3b1', '@53d2fb',
        '@53d30e>53d3b1', '@53d324', '@53d3cd>53d52a', '@53d3d6>53d52a', '@53d3f6>53d52a', '@53d438', '@53d444',
        '@53d460', '@53d498', '@53d4ba>53d52a', '@5007f4>50080d', '@5007f6']),
    'office_artillery': Scenario(office_artillery, [0x535760], 0, [
        '@538226>538679', '@53823d>538679', '@538259>538679', '@53826c>538679', '@53828c>538679', '@5382a1>5384a3',
        '@5382d8>538679', '@5382e4>538357', '@5382fa>538327', '@5382fc', '@538366>53843c', '@538380>538390',
        '@53838e>5383c0', '@538395>5383c4', '@5383f9>538665', '@538458>538665', '@5384a9>538509', '@5384cc',
        '@5384d8', '@538522', '@538541', '@53854e>538604', '@53856b', '@538596', '@5385ba>5385e5', '@53864e',
        '@538625>538658']),
    'convoy_ops': Scenario(convoy_ops, [0x535760, CONVOY_CHOICES], 0, [
        '@509d87', '@509d8b>509f6c', '@509d93', '@509da5>509f6c', '@509db4>509f6c', '@509dcc>509f6c', '@509ddc>509f6c',
        '@509deb>509f6c', '@509e1c>509f6c', '@509e43>509f6c', '@509e50', '@509ea4', '@509f1d', '@509f5d', '@503a5e',
        '@503a62>503ab3', '@503a9a', '@4f8d55', '@5392c1', '@5392c9', '@539280>5392fe', '@53929b>5392fe',
        '@5359bf>537025', '@5359da', '@539d48', '@539d2d>539dae', '@539d34>539dae', '@539d11>539dae',
        '@539d3e>539dae', '@539d00>539dae', '@535aa5', '@535ab4', '@535a7f>537025', '@509bbf>509d06', '@509bf0>509d06',
        '@509c60', '@509c71', '@40c1c5', '@40c249', '@40c255', '@40c279', '@40c3e2']),
    'convoy_trade': Scenario(convoy_trade, [0x535760, CONVOY_SUMMARY], 0, [
        '@5393ea', '@5393fc', '@5393e0>53941b', '@539431', '@539484>53969f', '@5394e0', '@5394f0', '@53954b',
        '@53957c', '@53966b', '@53936d>53969f', '@539318>53969f', '@539323>53969f', '@539334>53969f', '@5397ce',
        '@539780', '@539816>53992f', '@53988d>5398dd', '@5398fb', '@539893', '@539714>53992f', '@5396c4>53992f',
        '@5396cf>53992f', '@5396e0>53992f', '@4aa5cd']),
    'shipyard_ops': Scenario(shipyard_ops, [0x535760, 0x52B670, WINDOW_SHIPS], 0, [
        '@52b23f', '@52b235', '@52b177>52b28d', '@52b1b7>52b28d', '@52b1ab>52b28d', '@52b156>52b28d', '@535cc4>537025',
        '@52b6ed', '@52b70a', '@52b71e', '@52b814', '@52b895', '@52b89e', '@52b8d4', '@52ba35', '@52b6bf', '@52b68f',
        '@50874b', '@508702>508a88', '@508684>508a88', '@508678>508a88', '@508953', '@53cd24', '@53cd6f', '@53cd73',
        '@53ccff>53ce21', '@512e03', '@512e1e', '@512e46', '@512e56', '@512e74', '@512ec1', '@512edb', '@512ff1',
        '@504cda']),
    'lender_loans': Scenario(lender_loans, [0x535760, 0x51CE80, 0x51D160, 0x51D270, 0x4D85C0] +
                             [LENDER_LISTS + 0x50 * k for k in range(4)], 1, [
        '@51cec4', '@51cef0', '@51cf3e', '@51cf4d', '@51cf1b', '@51cf6e>51d13e', '@51cf78>51d13e',
        '@51cff6>51d14b', '@51d10c>51d14b', '@51d132', '@51d190>51d245', '@51d1c6', '@51d23a', '@51d36c>51d38f',
        '@51d37d', '@51d39e>51d403', '@51d3b9>51d403', '@51d3e9>51d403', '@51d3eb', '@539f46>539fae',
        '@539f4d>539fae', '@539f59>539fae', '@539f68', '@539f91', '@539f8f>539fae', '@4d9141', '@4d9251']),
    'lender_shares': Scenario(lender_shares, [0x535760, 0x4E8804, 0x4D85C0] +
                              [LENDER_LISTS + 0x50 * k for k in range(4)], 1, [
        '@4d992f>4d9c4a', '@4d994f>4d9c4a', '@4d995d>4d9c4a', '@4d9996>4d9c4a', '@4d99a3', '@4d99a9', '@4d99b8',
        '@4d99be', '@4d99f4>4d9c4a', '@4d9a7a', '@4d9a39', '@4d9a9e>4d9c4a', '@4d9acf>4d9c4a', '@4d9afe>4d9c4a',
        '@4d9b0b', '@4d9b14', '@4d9b23', '@4d9b3b', '@4d9b90>4d9c4a', '@4d9ba8', '@4d9c54', '@4d9c14',
        '@4d93f7', '@4d9559', '@4d955e', '@4d9592', '@4d95a1', '@4d97c9', '@4d9789', '@4d9793', '@4d97b4',
        '@4d9875', '@4d9883', '@4e8852>4e8c75', '@4e885d>4e8c75', '@4e8895', '@4e8a60', '@4e88f7', '@4e897f',
        '@4e89c4', '@4e8a19', '@4e8a59', '@4e8a57>4e8a62', '@4e8945', '@4e8aa5', '@4e8af5', '@4e8b22',
        '@4e8b20>4e8b43', '@4e8b3d', '@4e8ba5', '@4e8bad>4e8b40', '@4e8bb4', '@4e8c30', '@4e8c1a', '@4e8c1f',
        '@4e8c2b', '@4e8c4a']),
    'build_player_ops': Scenario(build_player_ops, [0x535760], 0, [
        '@53cf68>53d112', '@53cf7c>53d112', '@53cfb0>53d112', '@53cfea>53d112', '@53d050', '@53d084>53d112',
        '@53d0f1', '@53d0ef>53d10a', '@53d012', '@51f095', '@524639', '@52452e>524770', '@524551>524770',
        '@5245ed', '@524769', '@4fb5bc', '@4fb6f1>4fb8da', '@4fb60c', '@4fb629', '@4fb64d', '@4fb66a',
        '@4fb70b', '@4fb80d', '@4fb826']),
    'pirate_ops': Scenario(pirate_ops, [0x535760, 0x518750], 0, [
        '@5386ff', '@538706', '@53871b', '@5386f9>5387fc', '@538729', '@53876b', '@5387cc', '@5387a5>5387fc',
        '@5387ac>5387fc', '@5387b2>5387fc', '@5386d5>5387fc', '@518765>518777', '@51876b>518777', '@518775>5187ce',
        '@5187b1>518777', '@5187bf>518777', '@5187c6>518777', '@5187cc>518777', '@5187e4']),
    'pirate_sailing': Scenario(pirate_sailing, [0x535760, 0x506720], 1, ['@538706']),
    # The battles' piece 6: a raider arriving at a town starts a town battle (0x506F33: 0x6130B0, 0x611D80,
    # the setup 0x5FBFE0), with pirate_sailing's raiders.
    'town_attack': Scenario(pirate_sailing, [0x611D80, 0x6130B0, 0x5FBFE0, 0x50CC90], 1, [
        '@506f96', '@506fc9', '@5fbfe0']),
    # 0x5FBFE0: a town's tower found or not, the guard ship, the nest and its tower, a human side (the busy path),
    # side A a convoy or a ship alone, side D a convoy, the formation's two axes and its retries.
    'town_battle_setup': Scenario(town_battle_setup, [0x611D80, 0x5FBFE0, 0x50CC90], 0, [
        '@5fc158', '@5fc136', '@5fc285', '@5fc2c2', '@5fc3d4', '@5fc69a', '@5fd483', '@5fca3a', '@5fc997',
        '@5fcd80', '@6000a9', '@6000f0']),
    # Part 5: what the sea map's town symbols show (the town's situation as the player knows it, 0x535300).
    'town_symbols': Scenario(town_symbols, [0x535300], 0, ['@53535a', '@535365', '@535380']),
    # Part 5: the window's texts of a town battle with the player on a side (the viewer's messages).
    'town_battle_texts': Scenario(town_battle_texts, [0x5FBFE0, 0x5FE7E0], 0, [
        '@5fd1e1', '@5fd222', '@5fd176', '@5fd2a7', '@5fd30a', '@5fd36d', '@5fd3a0',
        '@5ff5e8', '@5ff61c', '@5ff630', '@5ff643', '@5ff677', '@5ff69b', '@5ff6bb', '@5ff6e3', '@5ff6ee',
        '@5ff716', '@5ff72a', '@5ff75b']),
    # Part 3: the town battles' steps (0x5FD510 and what it calls).
    'town_step_branches': Scenario(town_step_branches, [0x5FD510, 0x600770, 0x601C7B, 0x624950, 0x60130B, 0x621770,
                                                        0x60234C], 0, [
        '@6023c1', '@602652', '@6028e1',
        '@60090b', '@601d85', '@601d8c', '@600a2e', '@6251a0', '@60132e', '@6015f9', '@5fd530',
        '@623b27', '@623bfe', '@623cdb', '@623db3', '@6228ea', '@62295f', '@6229dc', '@622841', '@622bf3', '@622cac',
        '@621838', '@6218c3', '@6219e2']),
    'pirate_town_attack': Scenario(pirate_town_attack, [0x506720, 0x5FD510, 0x611D80, 0x5FBFE0], 3, [
        '@507c4c']),
    'convoy_raid': Scenario(convoy_raid, [0x506720, 0x5FD510, 0x611D80, 0x5FBFE0, 0x60234C], 3, [
        '@50797e']),
    'town_raids': Scenario(town_raids, [0x5FD510], 2, [
        '@5fdc1f', '@5fe6be', '@600843', '@6247c6', '@624d7c', '@6252ae', '@625c4d', '@622b09']),
    # The fourth start: the attack on a nest of the Hanse task of type 3 (0x4EBA71), the battle to its end.
    'nest_attack': Scenario(nest_attack, EVENTS_FUNCTIONS + [0x514530, 0x5FBFE0, 0x60234C, 0x5FD510, 0x5FE7E0,
                                                            0x602990, 0x514920], 2, [
        '@4ebb20', '@4eba98>4ebca7', '@4ebb9a', '@4ebbd3', '@4ebc02', '@5145c9', '@51486a', '@5fec0e', '@5fbb12',
        '@5fbe3c', '@514a32', '@514b5f', '@542e40', '@4ec354']),
    'nest_attack_weak': Scenario(nest_attack_weak, EVENTS_FUNCTIONS + [0x514530, 0x5FBFE0, 0x60234C, 0x5FD510,
                                                                      0x5FE7E0, 0x602990, 0x514920], 2, [
        '@4ebb20', '@4ebc02', '@5fee49']),
    'nest_attack_save': Scenario(nest_attack_save, EVENTS_FUNCTIONS + [0x514530, 0x5FBFE0], 0, ['@4ebb20', '@4ebc02']),
    'nest_attack_branches': Scenario(nest_attack_branches, EVENTS_FUNCTIONS + [0x514530, 0x5FBFE0, 0x50A4E0,
                                                                          0x60C1C9], 0, [
        '@4eba9e', '@4ebb20', '@4ebb9a', '@51484d', '@514797', '@514820', '@514568', '@60c508']),
    # Part 4: a nest's tower burning (the event 0x25 of a nest, 0x515C00).
    'nest_fire': Scenario(nest_fire, [0x4D85C0, 0x515C00], 1, [
        '@4e1bd3', '@4e1baf>4e1e48', '@4e1bbe>4e1e48', '@4e1be0', '@515c6c', '@515c71', '@515cc4', '@515c54']),
    # Part 4: the town battle's end (0x5FE7E0), the plunder (0x633DB0 with a convoy) and the destructor.
    'town_end_branches': Scenario(town_end_branches, [0x60234C, 0x5FE7E0, 0x602990, 0x633DB0], 0, [
        '@6023c1', '@5fe8fb', '@5fe9e0', '@5fead2', '@5feadd', '@5feb0f', '@5feb74', '@5feb86', '@5fedb1',
        '@5fedc8', '@5fed8d>5feeaf', '@5fefc8', '@5ff01a', '@5ff0bb', '@5ff0fc', '@5ff201', '@5ff2cf', '@5ff303',
        '@5ff4e6', '@6340a6', '@6340be', '@634163', '@6341a1', '@633f99>6341ba', '@634306', '@634323', '@634400',
        '@634576', '@5fbc8d', '@5fbd15']),
    'statistics_company': Scenario(statistics_company, [STATS_BALANCE, STATS_GRAPH, STATS_SUPPLY], 0, [
        '@4a3071', '@4a308d', '@4a35b2', '@4a35bd>4a35c2', '@4a35bf', '@4a353b', '@4a354a', '@4a356f', '@4a3580',
        '@4a3591', '@4a3aaa', '@4a3aba', '@4a3af7', '@4a3b05', '@4a3b0e', '@4a3b1a', '@4a3b26',
        '@4a2330>4a2a8e', '@4a241a>4a2a72', '@4a244f', '@4a2465', '@4a2496', '@4a24d9', '@4a24cf', '@4a24cd>4a250c',
        '@4a2567', '@4a269b', '@4a2839', '@4a2849', '@4a2859', '@4a2985', '@4a297f', '@4a29af', '@4a29fd', '@4a2a27',
        '@505a49', '@505aab', '@505af2', '@505a8d>505b09', '@505a93>505b09', '@505b23',
        '@4a126b', '@4a1272', '@4a1293', '@4a12f4', '@4a135d', '@4a135b>4a13b3', '@4a13f9']),
    'statistics_competition': Scenario(statistics_competition, [STATS_COMPETITION, STATS_CHOICE, STATS_SLOTS], 0, [
        '@4a19e3', '@4a1af4', '@4a1b04', '@4a1b2e', '@4a1b54', '@4a1b63>4a223d', '@4a1b8b>4a223d', '@4a1c06>4a1c91',
        '@4a1c0c', '@4a1e2f>4a21af', '@4a1ebb', '@4a1f0c>4a2163', '@4a1fe9', '@4a1fef', '@4a2096', '@4a201f',
        '@4a2026', '@4a214d', '@4a21ef>4a223d',
        '@49ed6f', '@49eece', '@49f021', '@49ee3a>49eea0', '@49ee5a>49eea0', '@49ef96>49efef', '@49f0e9>49f185',
        '@49eebd>49f2b2', '@49f010>49f2b2', '@49f17b', '@49f143',
        '@47217b', '@472157', '@47214c>4721bd', '@471ece', '@471ecc>471ef9',
        '@49dac8', '@49daec>49db75', '@49db02>49db75', '@49db25>49db6b', '@49db3f', '@49db63']),
    'statistics_comparison': Scenario(statistics_comparison, [STATS_TOWNS, STATS_RESIDENTS, STATS_CHOICE, STATS_WARES,
                                                              STATS_SLOTS], 0, [
        '@4a3cf0', '@4a3cd7', '@4a3d7a',
        '@4a3fcb', '@4a40e6', '@4a4118', '@4a40a1', '@4a40c9', '@4a40db>4a5d34', '@4a4222', '@4a4258>4a5d34',
        '@4a427d>4a5d34', '@4a4790', '@4a489f', '@4a4820>4a486d', '@4a55bf', '@4a5655', '@4a5692', '@4a56d7',
        '@4a57c8', '@4a58c0',
        '@4a2bb1', '@4a2c21', '@4a2c82', '@4a2da0', '@4a2cb2', '@4a2cec', '@4a2d02', '@4a2d46', '@4a2d77', '@4a2e07',
        '@4a2e4c', '@4a2e82', '@4a2b5f', '@4a2b8f',
        '@49e9d2', '@49ea86', '@49eae4', '@49eb36', '@49ebb0', '@49eb94', '@49ebd9', '@49ec7f', '@472059', '@471f79',
        '@49fc1c', '@49da8b', '@49dab0']),
    'statistics_town_data': Scenario(statistics_town_data, [STATS_TOWNS, STATS_CHOICE, STATS_TOWN_LIST], 0, [
        '@4a41c0', '@4a41ef', '@4a40db>4a5d34', '@4a4258>4a5d34', '@4a427d>4a5d34',
        '@4a42c2', '@4a43c4', '@4a44ed', '@4a4659', '@4a458b>4a45ea', '@4a46f9>4a474a',
        '@4a4d4b', '@4a4d49>4a4d52', '@4a4d86', '@4a4dc9', '@4a4dfb',
        '@4a4e75', '@4a4eb7', '@4a4ed8', '@4a4f09', '@4a4f59', '@4a4f6b',
        '@4a5016', '@4a5058', '@4a5079', '@4a50aa', '@4a50fa', '@4a510c',
        '@4a51db', '@4a5219', '@4a5232', '@4a5262', '@4a5293', '@4a52e7', '@4a5300', '@4a530a',
        '@4a53c5', '@4a541c', '@4a5457', '@4a54e3', '@4a5537', '@4a5572',
        '@49e906', '@49e99f', '@49e9ae', '@49fb41', '@472059', '@471f79']),
    'citizen_window': Scenario(citizen_window, [CITIZEN_STATS], 0, [
        '@40b5db', '@40b629', '@40b627>40b62e', '@40b653', '@40b668', '@40b680', '@40b698', '@40b6b3>40b6ba',
        '@40b6b5', '@40b6fb', '@40b6ff']),
    'letter_ops': Scenario(letter_ops, [0x535760, 0x4D6530], 0, [
        '@4d6b01', '@4d6aff>4d6b35', '@536b23', '@536b21>536b2d', '@4d79b7', '@4d79e9', '@4d79f0', '@4d7a43',
        '@4d7a30', '@4d7a7e', '@4d7a6b']),
    'build_menus': Scenario(build_menus, [0x535760, 0x51F3A0, BUILD_MENU, BUILD_MARKS], 0, [
        '@47abc7', '@47ad24', '@47abd9', '@47ac7d', '@47accb', '@47ad03', '@47ad4d', '@47ad78', '@47ad89',
        '@47ad9a', '@47ada7', '@47adbc', '@47adcc', '@47ade5', '@47ae06', '@47ae17', '@47ae23', '@47b108',
        '@47aee4', '@47aef7', '@47af0a', '@47af14', '@47af22', '@47af3d', '@47af7a', '@47afbf', '@47b006',
        '@47b04c', '@47b0a0', '@47b155', '@47b1e5', '@47b84e', '@47b866', '@47b886', '@47b8a0', '@47b8b2',
        '@4fd169', '@4fd17e', '@4fd187', '@4fd1f5', '@4fd211', '@4fd223', '@4fd23a', '@4fd251', '@4fd268',
        '@4fd27f', '@4fd296', '@4fd2ad', '@4fd2c4', '@4fd2db', '@4fd2ef', '@4fd312', '@4fd326', '@4fd332',
        '@4fd33e', '@4fd352', '@4fd366', '@4fd38c', '@4fd4fb', '@4fd561', '@4fd618', '@524248', '@524288',
        '@52427b', '@524317', '@5243cd', '@52445f', '@524494']),
    'build_places': Scenario(build_places, [0x535760, BUILD_FITS, 0x5250D0], 0, [
        '@47b927', '@47b959', '@47b965', '@47b97e', '@47b98a', '@47b9c3', '@47b9e0', '@47b9f7', '@47ba4b',
        '@47ba54', '@47ba6c', '@47ba95', '@47ba9e', '@47bacf', '@47bb00', '@47bb31', '@47bb62', '@47bb93',
        '@47bbc4', '@47bbf5', '@47bc26', '@47bc57', '@47bc88', '@47bca5', '@47bcbc', '@47bced', '@47bd83',
        '@47bd56', '@47bdbd', '@47bdde', '@47bf2b', '@47be37', '@47be90', '@47bef7', '@47bf34', '@47bf9b',
        '@47bd39']),
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
            print(f'{name}: {((s.setup.__doc__ or "").strip().splitlines() or [""])[0]}')
        return
    p = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    p.add_argument('exe')
    p.add_argument('save')
    p.add_argument('out')
    p.add_argument('scenario', choices=sorted(SCENARIOS) + ['inspect'])
    p.add_argument('--barcos', metavar='DATA', required=True)
    p.add_argument('--cpr', required=True)
    p.add_argument('--days', type=int)
    p.add_argument('--ticks', type=int, help='ticks instead of days (a progress line each of the first 32, then every 32)')
    p.add_argument('--functions')
    p.add_argument('--seed', type=int, default=1)
    p.add_argument('--first-arrival', action='store_true', help=argparse.SUPPRESS)
    p.add_argument('--write', metavar='OUT', help='at the end, the save the game writes: OUT uncompressed, OUT.pat '
                   'compressed, OUT.pst (scripts/write-scenario.sh with RETOUCH=unchecked)')
    a = p.parse_args()
    o = to.TownOracle(a.exe, open(a.save, 'rb').read(), a.seed, a.barcos, rest=list(to.REST), cpr=a.cpr)
    o.scenario_args = a  # a scenario may run another copy of the world (raid_arrival)
    if a.first_arrival:
        o.load()
        first_arrival(o)
        return
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
        ticks = days * 256 if a.ticks is None else a.ticks
        import time
        started = time.time()
        for t in range(ticks):
            o.advance_tick()
            if a.ticks is not None and (t < 32 or t % 32 == 31):
                print(f'tick {t + 1}: {rec.count} samples, {time.time() - started:.0f} s', flush=True)
        f.write(b'E')
    print(f'{rec.count} samples in {a.out}')
    blocks.report(s.targets)
    if a.write:
        from write_oracle import prepare_game_object, write_files
        prepare_game_object(o)
        write_files(o, a.write, a.write + '.pat')
        print(f'save written in {a.write} ({a.write}.pat, {a.write}.pst)')


if __name__ == '__main__':
    main()
