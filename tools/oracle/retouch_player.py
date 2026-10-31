"""The operations the viewer queues for the player, checked against the game.

Each scenario puts the oracle's world in a retouched state (with the helpers
of retouch.py), queues the operation the viewer would queue there, with the
very same bytes (printed by player_ops, built from p3data/hanse_tasks.cpp
like the viewer's), and records the executor (0x535760) and the event queue
(0x4D85C0): tick_probe then repeats each call with p3vita and compares the
world after it. What the viewer reads to choose the operation is taken here
with the game's own code: the task list of the task window (0x4EC5F4), the
road materials (0x4EC704) and the office (0x5308A0); see
docs/cloud/alderman-viewer.md.

usage: python3 retouch_player.py <Patrician3.exe> <save.raw> <out.probe> <days> <functions>
       <scenario> --barcos DATA --cpr p2arch0_eng.cpr --player-ops BUILD/tools/oracle/player_ops

Scenarios (operation 0x4B, the task window 0x5E87D0: the player, a candidate
made with the game's code, takes the row of the task of that type):
  take_found       the task to found a town (type 0)
  take_road        the road (type 1)
  take_ship        the pirate ship (type 2)
  take_grow        the town that grows (type 4)
  take_two         the town that grows and then the pirate ship in the same
                   tick: the second is refused (the player already has one)
  take_not_candidate  an AI merchant is the candidate; the player, who isn't,
                   tries to take a task: refused
Operation 0x3B, the road window 0x5F2A20 (the player with the road, 5
sections left, in the town of its first office; wood and tools in the office,
wood, bricks and hemp in its first ship, docked there):
  give_road        one per material with the amounts 1, 5, 50 and all, each
                   clamped as the window does (to what is available and to
                   what is still needed)
  give_needed      1 section left: «all» of each material, clamped to what is
                   still needed
The election of the Alderman (event 0x20, 0x4DBBD0), moved to the start of
the next day (tick low byte 0: the first phase), with the player at level 4
as `--debug-state level4` of the viewer leaves it (mayor of its home town,
the third component of its reputation at 20 in every town, [+0x37] = 4, the
home town with [0x2C8] & 0x40):
  election_level4  the player can stand: the election runs its phases that day
The player's vote (operation 0x3F, the Hanse window 0x5E8280), on the
election day (tick 0x20, after the first phase) unless said otherwise:
  vote_self        for its own candidacy
  vote_other       for another candidate
  vote_twice       twice in the same tick: the second is refused (its bit is cleared)
  vote_early       the day before the election: refused (no election today)
  vote_late        the day after: refused
  vote_rank3       its rank at home lowered to 3 first: refused
  vote_bad_choice  for the candidate 4: its bit is cleared, nothing counted
The same operation in a vote of the Hanse diet (event 0x21, made for the
next day: proposal 5 about a town without [0x2C8] & 0x40), 0x20 ticks after:
  diet_yes, diet_no, diet_abstain
Records 535760 and 4d85c0, for example 2 days.
"""

import argparse
import os
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import retouch as rt  # noqa: E402
import town_oracle as to  # noqa: E402

WORLD = to.WORLD
TASK_LIST = 0x4EC5F4  # thiscall on the event queue (count, out): the tasks without merchant
ROAD_MATERIALS = 0x4EC704  # thiscall on the event queue (event, out[12 u16])
MERCHANT_OFFICE = 0x5308A0  # thiscall on the world (merchant, town, create): the office or 0
ROAD_WARES = (0x0B, 0x13, 0x0C, 0x11)  # 0x672BF4: wood, bricks, tools, hemp
ROAD_AMOUNTS = (1, 5, 50, 0xFFFFFFFF)  # the road window's options (0x5EED40)
# 0x672BF8: the pairs of towns (numbers of StadtDaten.ini) a road can join.
ROAD_PAIRS = ((0x03, 0x04), (0x09, 0x0A), (0x0B, 0x0D), (0x0C, 0x0E), (0x1C, 0x1F), (0x24, 0x25), (0x26, 0x27))
IN_BARRELS = (0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1)  # 0x672C14

PLAYER_OPS = None  # Set by main (--player-ops).


def player_op(*args):
    """The operation as the viewer builds it (player_ops)."""
    out = subprocess.run([PLAYER_OPS, *[str(a) for a in args]], check=True, capture_output=True, text=True)
    return bytes.fromhex(out.stdout.strip())


def window_tasks(o):
    """The task window's rows: 0x4EC5F4(5, out), as the game's window calls it."""
    out = o.alloc(10)
    n = o.call(TASK_LIST, [5, out], ecx=to.EVENTS)
    rows = [o.u16(out + 2 * i) for i in range(n & 0xFF)]
    base = o.u32(to.EVENTS)
    return [(i, o.u8(base + 0x18 * i + 0xC)) for i in rows]


def take(o, m, kind):
    """The player takes the window's row of that type: the operation 0x4B."""
    row = next((i for i, k in window_tasks(o) if k == kind), None)
    if row is None:
        raise RuntimeError(f'no task of type {kind} on offer')
    o.tick_operations.append(player_op('take', m, row))
    print(f'0x4B: merchant {m} takes the task {row} (type {kind})')


def take_scenario(kind):
    def scenario(o):
        m = o.u32(to.PLAYER)
        rt.run_candidacy(o, m)
        take(o, m, kind)
    scenario.__name__ = {0: 'take_found', 1: 'take_road', 2: 'take_ship', 4: 'take_grow'}[kind]
    return scenario


def take_two(o):
    m = o.u32(to.PLAYER)
    rt.run_candidacy(o, m)
    take(o, m, 4)
    take(o, m, 2)


def take_not_candidate(o):
    rt.run_candidacy(o, rt.ai_with_office(o)[0])
    m = o.u32(to.PLAYER)
    if o.u8(o.merchant(m) + 0x37) != 0:
        raise RuntimeError('the player is already a candidate')
    take(o, m, 4)


def road_for_player(o, sections):
    """The player with the road (started, `sections` left), moved to the pair
    of 0x672BF8 that has the town of its first office (the window needs the
    pair: 0x4EC704), with materials in the office and in its first ship, docked
    there. Without such a pair, the road's first town and only the ship.
    Returns (event index, town)."""
    m = o.u32(to.PLAYER)
    task = rt.road_task(o, m)
    rt.give(o, task, m, f12=sections)
    first = o.u16(o.merchant(m) + 0xC)
    towns = o.u32(WORLD + 0x10)
    ids = [o.u8(o.town(i) + 0x2C1) for i in range(towns)]
    home = next(i for i in range(towns) if first in rt.office_list(o, i))
    pair = next((p for p in ROAD_PAIRS if ids[home] in p and all(x in ids for x in p)), None)
    if pair:
        rt.w8(o, task[1] + 0x10, ids.index(pair[0]))
        rt.w8(o, task[1] + 0x11, ids.index(pair[1]))
        town = home
    else:
        town = o.u8(task[1] + 0x10)
    if town == home:
        office = o.office(first)
        rt.w32(o, office + 0x4 + 4 * 0x0B, 7 * 2000 + 500)  # 7.25 loads of wood
        rt.w32(o, office + 0x4 + 4 * 0x0C, 3 * 200)  # 3 barrels of tools
    ship = o.u16(o.merchant(m) + 0xE)
    rt.w8(o, o.ship(ship) + 0x39, town)
    rt.w8(o, o.ship(ship) + 0x136, o.u8(o.ship(ship) + 0x136) & ~1)
    for ware, amount in ((0x0B, 5 * 2000), (0x13, 2 * 2000), (0x11, 3 * 2000)):
        rt.w32(o, o.ship(ship) + 0x54 + 4 * ware, amount)
    print(f'road task {task[0]} in town {town}: office {first}, ship {ship}')
    return task[0], town


def window_materials(o, m, event, town):
    """Per material, (still needed, available) as the road window shows them."""
    out = o.alloc(24)
    o.call(ROAD_MATERIALS, [event, out], ecx=to.EVENTS)
    v = struct.unpack('<12H', bytes(o.uc.mem_read(out, 24)))
    office = o.call(MERCHANT_OFFICE, [m, town, 0], ecx=WORLD)
    ships = o.u16(to.SHIPS + 0xF4)
    result = []
    for k, ware in enumerate(ROAD_WARES):
        unit = 200 if IN_BARRELS[ware] else 2000
        available = o.u32(office + 4 + 4 * ware) // unit if office else 0
        s = o.u16(o.merchant(m) + 0xE)
        while s < ships:
            a = o.ship(s)
            if o.u8(a + 0x39) == town and not o.u8(a + 0x136) & 1:
                available += o.u32(a + 0x54 + 4 * ware) // unit
            s = o.u16(a + 4)
        needed = (v[k] - v[4 + k] - v[8 + k]) & 0xFFFF
        result.append((needed, available))
    return result


def window_amount(chosen, available, needed):
    """0x5F2DA3..0x5F2DEB (as road_window_amount in hanse_tasks.cpp)."""
    smaller = min(chosen, available)
    return smaller if smaller < needed else needed


def give(o, sections, choices):
    m = o.u32(to.PLAYER)
    event, town = road_for_player(o, sections)
    for k, ((needed, available), chosen) in enumerate(zip(window_materials(o, m, event, town), choices)):
        if needed == 0 or available == 0:
            print(f'material {k}: needed {needed}, available {available}: the button is off')
            continue
        amount = window_amount(chosen, available, needed)
        o.tick_operations.append(player_op('road', event, amount, town, m, ROAD_WARES[k]))
        print(f'0x3B: material {k} (ware {ROAD_WARES[k]:#x}): needed {needed}, available {available},'
              f' chosen {chosen:#x} -> {amount}')


def give_road(o):
    give(o, 5, ROAD_AMOUNTS)


def give_needed(o):
    give(o, 1, (0xFFFFFFFF,) * 4)


def level4(o):
    """The player as --debug-state level4 leaves it (src/app/debug_states.cpp)."""
    m = o.u32(to.PLAYER)
    ma = o.merchant(m)
    home = o.u8(ma + 0x19)
    rt.w8(o, o.town(home) + 0x6F1, rt.owner(o, m))
    rt.w8(o, ma + 0x39C + home, 6)  # the mayor's rank at home, as 0x4F75F0 leaves it
    for i in range(o.u32(WORLD + 0x10)):
        rt.wf(o, ma + 0x124 + 12 * i, 20.0)
    rt.w8(o, ma + 0x37, 4)
    rt.w32(o, o.town(home) + 0x2C8, o.u32(o.town(home) + 0x2C8) | 0x40)
    return m, home


def election_tomorrow(o):
    """The event 0x20 moved to the first phase of the next day; returns (index, address)."""
    i, a = next((i, a) for i, a in rt.events(o) if o.u16(a + 6) == 0x20)
    tick = (o.u32(WORLD + 0x14) & ~0xFF) + 0x100
    rt.move_event(o, i, a, tick)
    print(f'election: event {i} at tick {tick:#x}')
    return i, a


def election_level4(o):
    m, home = level4(o)
    print(f'player {m}, home town {home}, alderman {o.u8(0x6DE52D)} (town {o.u8(0x6DE52E)})')
    return election_tomorrow(o)


def at(tick, act):
    """A hook for main: act(o) just before the world tick `tick`."""
    def hook(o):
        if o.u32(WORLD + 0x14) == tick:
            act(o)
    return hook


def candidate_of(o, m):
    """The row k of the merchant m among the election's candidates, or None."""
    for i, a in rt.events(o):
        if o.u16(a + 6) == 0x20:
            rows = list(bytes(o.uc.mem_read(a + 8, 4)))
            return rows.index(0x80 | m) if 0x80 | m in rows else None
    return None


def vote(o, choice):
    m = o.u32(to.PLAYER)
    o.tick_operations.append(player_op('vote', m, choice))
    print(f'0x3F: merchant {m} votes {choice} at tick {o.u32(WORLD + 0x14):#x}')


def vote_scenario(name, choose, offset=0x20, times=1, rank=None):
    """The player at level 4 and the election tomorrow; `offset` ticks after
    the election's first phase (0x20: that day, after it), the vote(s) for
    choose(o, m), with its rank at home changed first if `rank`."""
    def scenario(o):
        election_level4(o)
        start = next(o.u32(a) for i, a in rt.events(o) if o.u16(a + 6) == 0x20)

        def act(o):
            m = o.u32(to.PLAYER)
            if rank is not None:
                rt.w8(o, o.merchant(m) + 0x39C + o.u8(o.merchant(m) + 0x19), rank)
            for _ in range(times):
                vote(o, choose(o, m))
        return at(start + offset, act)
    scenario.__name__ = name
    return scenario


def own(o, m):
    k = candidate_of(o, m)
    return 0 if k is None else k  # None before the election (vote_early)


def other(o, m):
    k = candidate_of(o, m)
    return 0 if k != 0 else 1


def show_diet(o, i):
    a = o.u32(to.EVENTS) + 0x18 * i
    raw = bytes(o.uc.mem_read(a, 0x18))
    print(f'diet {i} at {o.u32(WORLD + 0x14):#x}: type {o.u16(a + 6):#x} to give {raw[0x10:0x12].hex()}'
          f' yes/no {raw[0x12:0x14].hex()} abstain {raw[0x14:0x16].hex()} humans {raw[0x16]:#x}')


def diet_scenario(name, choice):
    """The player at level 4 and a vote of the Hanse diet (event 0x21) made
    for the start of the next day: proposal 5 (the town pays 80000) about a
    town without the 0x40 flag; 0x20 ticks after, the player votes `choice`
    (0 yes, 1 no, 2 abstain)."""
    def scenario(o):
        m, home = level4(o)
        towns = o.u32(WORLD + 0x10)
        town = next(i for i in range(towns) if i != home and not o.u32(o.town(i) + 0x2C8) & 0x40)
        tick = (o.u32(WORLD + 0x14) & ~0xFF) + 0x100
        a = o.call(0x4D8CF0, [tick], ecx=to.EVENTS)
        o.uc.mem_write(a + 6, struct.pack('<H', 0x21) + bytes([o.u8(0x6DE52E), town, 0, 0, 0, 0, 5]) + bytes(9))
        print(f'diet: event {(a - o.u32(to.EVENTS)) // 0x18} at tick {tick:#x} about town {town}')
        index = (a - o.u32(to.EVENTS)) // 0x18
        vote_at, show_at = at(tick + 0x20, lambda o: vote(o, choice)), at(tick + 0x22, lambda o: show_diet(o, index))
        return lambda o: (vote_at(o), show_at(o))
    scenario.__name__ = name
    return scenario


def print_election(o):
    """The election's event and the Alderman after the run."""
    for i, a in rt.events(o):
        if o.u16(a + 6) == 0x20:
            raw = bytes(o.uc.mem_read(a, 0x18))
            print(f'election {i}: tick {o.u32(a):#x} candidates {raw[8:12].hex()} votes {raw[12:16].hex()}'
                  f' to give {raw[16:20].hex()} humans {raw[0x14]:#x}')
        if o.u16(a + 6) == 0x21:
            raw = bytes(o.uc.mem_read(a, 0x18))
            print(f'diet {i}: tick {o.u32(a):#x} to give {raw[0x10:0x12].hex()} yes/no {raw[0x12:0x14].hex()}'
                  f' abstain {raw[0x14:0x16].hex()} humans {raw[0x16]:#x}')
    print(f'alderman {o.u8(0x6DE52D)} (town {o.u8(0x6DE52E)})')


SCENARIOS = {f.__name__: f for f in (take_scenario(0), take_scenario(1), take_scenario(2), take_scenario(4), take_two,
                                     take_not_candidate, give_road, give_needed, election_level4,
                                     vote_scenario('vote_self', own), vote_scenario('vote_other', other),
                                     vote_scenario('vote_twice', own, times=2),
                                     vote_scenario('vote_early', own, offset=-0x10),
                                     vote_scenario('vote_late', own, offset=0x110),
                                     vote_scenario('vote_rank3', own, rank=3),
                                     vote_scenario('vote_bad_choice', lambda o, m: 4),
                                     diet_scenario('diet_yes', 0), diet_scenario('diet_no', 1),
                                     diet_scenario('diet_abstain', 2))}


def main():
    global PLAYER_OPS
    p = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    p.add_argument('exe')
    p.add_argument('save')
    p.add_argument('out')
    p.add_argument('days', type=int)
    p.add_argument('functions')
    p.add_argument('scenario', choices=sorted(SCENARIOS))
    p.add_argument('--barcos', metavar='DATA', required=True)
    p.add_argument('--cpr', required=True)
    p.add_argument('--player-ops', required=True)
    p.add_argument('--max', type=int, default=1 << 30)
    a = p.parse_args()
    PLAYER_OPS = a.player_ops
    # The end of the election when a human merchant wins (0x4DBBD0): the
    # windows (*0x6CC7E8, 0x469380 and 0x469AF0; and 0x5499F0 when it is the
    # player), which p3vita doesn't reproduce either (events_mayor.cpp). Only
    # in this script: town_oracle.py isn't changed.
    to.EVENT_JUMPS.setdefault(0x4DC041, 0x4DC0F2)
    o = to.TownOracle(a.exe, open(a.save, 'rb').read(), 1, a.barcos, rest=list(to.REST), cpr=a.cpr)
    o.load()
    # The vote (0x3F) isn't in town_oracle.OPERATIONS (part B's list): only
    # here, so that the oracle runs it at the end of the tick.
    o.operations = set(o.operations) | {0x3F}
    hook = SCENARIOS[a.scenario](o)
    with open(a.out, 'wb') as f:
        f.write(b'P3PROBE1')
        rec = to.Recorder(o, [int(x, 16) for x in a.functions.split(',')], f, a.max)
        rec.active = True
        for day in range(a.days):
            for _ in range(256):
                if callable(hook):
                    hook(o)
                o.advance_tick()
            if rec.count >= a.max:
                break
        f.write(b'E')
    print(f'{rec.count} samples in {a.out}')
    print_election(o)
    m = o.u32(to.PLAYER)
    for i, e in rt.events(o):
        if o.u16(e + 6) == 0x32:
            raw = bytes(o.uc.mem_read(e, 0x18))
            mine = ' (the player)' if raw[0xD] == m else ''
            print(f'task {i}: type {raw[0xC]} merchant {raw[0xD]}{mine} started {raw[0xF]} data {raw[0x10:].hex()}')


if __name__ == '__main__':
    main()
