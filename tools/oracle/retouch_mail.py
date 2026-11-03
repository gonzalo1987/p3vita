"""Retouched states for the mailbox (docs/formats/mail.md): records like
town_oracle.py --barcos starting from the save changed by a scenario, for the
branches of sending a letter (0x4D6530) the saves don't reach. tick_probe
repeats each call from the game's state just before it (without --continuo:
p3vita doesn't know the retouch).

usage: python3 retouch_mail.py <Patrician3.exe> <save.raw> <out.probe> <days> <scenario>
       --barcos DATA --cpr p2arch0_eng.cpr [--functions F,F...] [--letters connected|all|none]
       [--write OUT]
By default it records 4f7bb0 (the merchant's tick). With --write OUT, at the
end the game saves the state reached (write_oracle.py): OUT the uncompressed
.pat, OUT.pat compressed and OUT.pst (scripts/write-scenario.sh).

Scenarios:
  grow       the mailbox without free entries (its free head [0x6DD734] =
             its count): the first letter grows it by 200 (0x4D6B40)
  promotion  the days run unrecorded until day 1 of the next month starts;
             then the human player has rank 1 in his home town, the third
             component of his reputation at 20 in every town and 10 000 000
             in money: his tick of that day raises him to rank 2 and sends
             the letter 0x06 to the chronicle
  privateer  retouch_pirates.py's event_attack (pirates attack a ship of the
             human player), then every pirate's ship becomes a privateer of
             the player ([+0x15D]): the shares of their plunders and
             take-overs (0x60C1C9, operation 0x91) go to the player with the
             letter 0x30. Record 4d85c0,506720,535760
  reputation as promotion, with rank 4, no money and the third component
             of his reputation at 100 (and his letters 0x5F turned into
             0x5E): the company's value is too low for rank 5, so 0x4F7510
             sends the letters 0x5E and 0x5F
  commission events 0x05 (a merchant's task, 0x4E5B84) of the human player
             in the next tick: the 16 kinds starting (the letter 0x19 or
             0x0A), the 16 at their end without marks in the towns (the
             result 0x4B; kind 15 the punishment) and kinds 3 and 9 with two
             marks (failed: 0x4A). Record 4d85c0
  hulk       the player's home town ready for the hulk (its level [0x827]
             −1, [0x810] high, [0x2C8] 0, [0x76C] | 0x20000) and an event
             0x06 in the next tick: the letter 0x81 to every merchant.
             Record 4d85c0
  wall_bill  the player's home town without a merchant mayor, its store
             full and a single pending wall piece, which the player pays
             (operation 0x24, 0x522C70): the offices' shares, with the
             letter 0x2F to the player. Record 535760, 0 days
  convoy_fees direct calls of 0x50A580 (a convoy ends at its destination)
             on two AI convoys with 2 ships or more, at their destination
             town ([+0x1C] = [+0x1F], [+0x1E] another one): the first led by
             the player ([+0x0]: the others pay, the letter 0x31 to him), the
             second with its last ship the player's (he pays: 0x32). Record
             50a580, 0 days
  pirate_hunt events 0x1E (0x4DFD44) of the player in the next ticks: state
             3 with a ship (the letter 0x41), state 3 with his [+0x1B] 4 up
             (0x42 and 4000), state 4 in the town of his first office with
             its last ware high (0x43). Record 4d85c0
  council    two votes of the council (event 0x2E, 0x4E9A94) ending at the
             day's last tick: in the player's home town with him as mayor
             (proposal 0, passes: the extra tax, event 0x31, a day later, with
             the letters 0x6C), and in the next town with an AI mayor and the
             player asked (proposal 3, fails): the letters 0x6B. Record
             4d85c0, 2 days
  hanse_task the player leads the Hanse ([0x6DE52D]; the meeting in his home
             town): events 0x30 (0x4EA594) in the first phase (kinds 0, 2, 5,
             7: the letter 0x77 to each merchant or to him), the second
             (kinds 1 and 5: the reminder, not for 5) and the third (kind 3
             fails; kind 4 with [+0xC] 6 is exempt). Record 4d85c0
  hanse_task_ai an AI merchant leads the Hanse: events 0x30 of the kinds 0,
             2 (the captain of a pirate object, 0x515AE0) and 1 in the first
             phase (0x77 to each merchant), then the meeting a day later
             (0x78). Record 4d85c0, 2 days
  expedition the save's first event 0x12 (an expedition, 0x4E6A54) arrives
             in the next tick, the convoy the player's, its second ship with a
             hull of 10 (it sinks: 0x34 from 0x502B20) and the world's
             [+0x89] at 0 (the first to reach the new world: 0x56 from
             0x5319D0). Only the tutorials have one. Record 4d85c0
  expedition_lost the same with its only (first) ship sunk too (no report)
             and its first point at the first of Expeditionen.dat
  expedition_four, expedition_villages, expedition_new_world,
  expedition_nothing, expedition_contract: the expedition (the convoy and
             its ships the player's) with its route points at the places of
             world objects not found yet: 4 trading posts with [+0x38] 2
             (a contract) and the arrival's tick chosen for pirates that
             steal; a village and 3 posts, pirates that fail, wine and iron
             goods loaded (sold to the village); 3 villages and a post; no
             object (nothing found); 2 posts with a contract of the player
             each, signed before by calling 0x52C6C0 (traded with). The
             report 0x63 with the texts of 0x4E6A54, 0x52BFE0 and 0x52C6C0
  debts_notices events 0x01 (0x4DB4E0) with [+0x12] (only the notices):
             the player owes (0x12), he is repaid (0x26, [+0xC] a tick with
             its low byte 0) and the notice 0x79 to him as the creditor
  debts_office, debts_ship, debts_partial: the player owes 5000 to a town
             (an event 0x01 in the next tick; 0x4F6570): without money and
             with beer in his office there (0x13 by the office), without
             money and with beer on his ship in that port and his office
             there empty (by the ship), and with 1000 in a town where he
             has neither (part of it in money). Record 4d85c0
  captain    the operation 0x12 for the player's first ship with a captain
             (+60 to his skills; 0x538A80): if his [+0xC] rises, the letter
             0x4D. Record 535760
  mayor_election direct calls of 0x528E60 (the mayor's election) in the
             player's home town (the letter 0x47 with its text reaches him)
             and in the next town; the news 0x55. Record 528e60, 0 days
  alderman_merchant, alderman_citizen: the save's event 0x20 (the
             Alderman's election, 0x4DBBD0) in its last step a day later,
             with an AI merchant, a town with a citizen mayor, another town
             and the player as candidates; an AI merchant or the citizen wins
             (the player never: becoming Alderman ends the game): 0x5299F0's
             letter 0x47 to each merchant and the news 0x57. Record 4d85c0, 2
             days
  hanse_vote an event 0x21 (the Hanse's vote, 0x4DC1F0) in its last step,
             the proposal failing: the letter 0x0B to each merchant
  auction_end auctions (events 0x36 and 0x0A, 0x4E2CD4) in their last step
             in the next ticks: a building of the player won by an AI
             merchant (0x76), by himself (0x7D) and unsold (0x7E), an AI
             merchant's won by the player (0x7F), the player's ships (in
             port, in no convoy) won by the AI merchant (0x2C), by himself
             (0x2D) and unsold (0x2E), and an AI ship won by the player
             (0x5D). Record 4d85c0
  hanse_task_letters the Hanse tasks of an AI merchant's candidacy
             (retouch_unchecked.candidacy_tasks) given to the player: the
             road and the pirate nest not started (0x4EBF54: 0x80; 0x6E and
             0x84) and the pirate ship hunted, done (0x4EC354: 0x6F to the
             chronicle and to him). Record 4d85c0,535760, 2 days
  siege_letters two sieges (operations 0x8E run at once: the news 0x53 of
             0x629C60), the first army retouched to collect the ransom in
             its next tick (0x6281F0: the news 0x38 with the money) and the
             second one besieger against 20 defenders (it leaves: 0x38 with
             0xFFFFFFFF); and in a third town's list [+0x1020] four of the
             prince's payments to the player due (0x636230: the letters 0x85;
             the last over 65 535). Record 535760,633a90,6349f0
  operation_letters operations for the human player at the end of the
             first tick: his ships' captains retire (0x13: one in port and
             one at sea if he has them, else the one in port run at once with
             its [+0x39] 0xFF for that call; the letter 0x52), his wedding the
             next day in his home town (0x60: 0x33), his first office's
             administrator gains skill (0x67, an AI office's administrator
             lent if it has none: 0x7C) and a child (0x87, married 300 days
             ago: 0x7A). Record 535760
  diet_letters the Hanse's missions (0x4E09D4) in their last phases: a 0x24
             protecting a town whose satisfaction falls under −20 (phase 4,
             then 5: the letter 0x5C with the town + 0x100), a 0x23 past its
             deadline with the fleet a convoy of the player's ships if he has
             one (phase 6, then 5: 0x5C counting them), a 0x34 in phase 2 with
             the player twice and an AI merchant in its list (0x4C). Record
             4d85c0
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import town_oracle as to  # noqa: E402
from town_oracle import WORLD  # noqa: E402

FUNCTIONS = (0x4F7BB0,)


def w8(o, a, v):
    o.uc.mem_write(a, bytes([v & 0xFF]))


def w16(o, a, v):
    o.uc.mem_write(a, struct.pack('<H', v & 0xFFFF))


def w32(o, a, v):
    o.uc.mem_write(a, struct.pack('<I', v & 0xFFFFFFFF))


def wf(o, a, v):
    o.uc.mem_write(a, struct.pack('<f', v))


def grow(o, rec):
    count = o.u16(to.MAIL + 6)
    print(f'mailbox: {count} entries, free head {o.u16(to.MAIL + 4)} -> {count}')
    w16(o, to.MAIL + 4, count)


def promotion(o, rec):
    rec.active = False
    while o.u8(WORLD) != 1:
        o.advance_tick()
    rec.active = True
    m = o.u32(to.PLAYER)
    ma = o.merchant(m)
    home = o.u8(ma + 0x19)
    print(f'player {m}, home town {home}, rank {o.u8(ma + 0x39C + home)} -> 1, day {o.u8(WORLD)}.{o.u8(WORLD + 1)}, '
          f'tick {o.u32(WORLD + 0x14):#x}')
    w8(o, ma + 0x39C + home, 1)
    w32(o, ma, 10000000)
    for i in range(o.u32(WORLD + 0x10)):
        wf(o, ma + 0x124 + 12 * i, 20.0)


def privateer(o, rec):
    import retouch_pirates as rp
    rp.event_attack(o)
    player = o.u32(to.PLAYER)
    merchants, ships = o.counts()[2], o.counts()[3]
    n = 0
    for i in range(ships):
        s = o.ship(i)
        if o.u8(s) == 0xFF and o.u8(s + 0x15D) >= merchants:
            w8(o, s + 0x15D, player)
            n += 1
    print(f'{n} pirate ships are privateers of the player {player}')


def reputation(o, rec):
    promotion(o, rec)
    m = o.u32(to.PLAYER)
    ma = o.merchant(m)
    w8(o, ma + 0x39C + o.u8(ma + 0x19), 4)
    w32(o, ma, 0)
    for i in range(o.u32(WORLD + 0x10)):
        wf(o, ma + 0x124 + 12 * i, 100.0)
    # A letter 0x5F already in his chain stops them (0x4D7AC0): those become 0x5E.
    mail, count = o.u32(to.MAIL), o.u16(to.MAIL + 6)
    i, n = o.u16(ma + 0xA), 0
    while i < count and n < count:
        if o.u8(mail + 16 * i + 4) == 0x5F:
            w8(o, mail + 16 * i + 4, 0x5E)
        i, n = o.u16(mail + 16 * i + 6), n + 1


def commission(o, rec):
    import retouch as rt
    towns, _, merchants, _ = o.counts()
    m = o.u32(to.PLAYER)
    home = o.u8(o.merchant(m) + 0x19)
    other = next(i for i in range(merchants) if i != m)
    queue = rt.events(o)
    now = o.u32(WORLD + 0x14)
    cases = [(kind, 0, 0) for kind in range(16)] + [(kind, 1, 0) for kind in range(16)] + [(3, 1, 2), (9, 1, 2)]
    assert len(queue) > len(cases) + 8
    for k, (kind, state, marks) in enumerate(cases):
        i, a = queue[-1 - k]
        town_b = (home + 1 + k) % towns
        w16(o, a + 6, 5)
        w8(o, a + 8, m)
        w8(o, a + 9, (home + 2 + k) % towns)
        w8(o, a + 0xA, other)
        w8(o, a + 0xB, town_b)
        w8(o, a + 0x10, kind)
        w8(o, a + 0x11, state)
        w16(o, a + 0x12, 0x5000)
        w16(o, a + 0x14, 0x1234 + 0x111 * k)
        w16(o, a + 0x16, 0x77 + k)
        for j in range(4):  # the marks of 0x4E5E1E: the town [+0xB]'s slots, then [+0x9]'s
            for t, n in ((town_b, marks), ((home + 2 + k) % towns, 0)):
                slot = o.town(t) + 0x6DC + j
                if j < n:
                    w8(o, slot, m)
                elif o.u8(slot) == m:
                    w8(o, slot, 0xFF)
        rt.move_event(o, i, a, now + 1)
    print(f'{len(cases)} events 0x05 of the player {m} (home {home}), other merchant {other}')


def hulk(o, rec):
    import retouch as rt
    m = o.u32(to.PLAYER)
    t = o.u8(o.merchant(m) + 0x19)
    a = o.town(t)
    w8(o, a + 0x827, 0xFF)
    w32(o, a + 0x810, 0x7FFFFFFF)
    w32(o, a + 0x2C8, 0)
    w32(o, a + 0x76C, o.u32(a + 0x76C) | 0x20000)
    i, e = rt.events(o)[-1]
    w16(o, e + 6, 6)
    rt.move_event(o, i, e, o.u32(WORLD + 0x14) + 1)
    print(f'town {t} ready for the hulk; event {i} 0x06 in the next tick')


def wall_bill(o, rec):
    import retouch_unchecked as ru
    m = o.u32(to.PLAYER)
    t = o.u8(o.merchant(m) + 0x19)
    a = o.town(t)
    w8(o, a + 0x6F1, 0xFF)
    pending = ru.records(o, t, 0x766)
    print(f'town {t}: {len(pending)} pending pieces, first {pending[:3]}')
    if not pending:
        return
    ru.store(o, t)
    w32(o, a + 0x700, 50000000)
    w32(o, o.merchant(m), 50000000)
    first = pending[0]
    w16(o, o.u32(a + 0x75C) + 8 * first[0] + 4, 0xFFFF)
    ru.operation(o, 0x24, m, t, 0 if first[4] == 0x36 else 1)
    print(f'pending now {ru.records(o, t, 0x766)}')


def convoy_fees(o, rec):
    towns, _, merchants, ships = o.counts()
    m = o.u32(to.PLAYER)
    convoys = o.u32(to.SHIPS + 8)
    count = o.u16(to.SHIPS + 0xF6)
    chosen = []
    for c in range(count):
        a = convoys + 0x3C * c
        members, s = [], o.u16(a + 0xA)
        while s < ships and len(members) < 50:
            members.append(s)
            s = o.u16(o.ship(s) + 6)
        if len(members) >= 2 and o.u8(a) < merchants and o.u8(a) != m and o.u8(a + 0x1F) < towns:
            chosen.append((c, a, members))
        if len(chosen) == 2:
            break
    print(f'convoys {[c for c, _, _ in chosen]}')
    for k, (c, a, members) in enumerate(chosen):
        t = o.u8(a + 0x1F)
        w16(o, a + 0x1C, t)
        w8(o, a + 0x1E, (t + 1) % towns)
        if k == 0:
            w8(o, a, m)
        else:
            w8(o, o.ship(members[-1]), m)
        import retouch_unchecked as ru
        ru.direct(o, 0x50A580, [c], ecx=to.SHIPS)


def new_event(o, tick, kind):
    """A new event of `kind` at `tick` (0x4D8CF0); its address."""
    e = o.call(0x4D8CF0, [tick], ecx=to.EVENTS)
    w16(o, e + 6, kind)
    return e


def player_offices(o, m):
    """The player's offices (his chain from [+0xC], next [+0x2C8])."""
    out, i = [], o.u16(o.merchant(m) + 0xC)
    while i < o.u16(WORLD + 8):
        out.append(i)
        i = o.u16(o.office(i) + 0x2C8)
    return out


def pirate_hunt(o, rec):
    m = o.u32(to.PLAYER)
    ma = o.merchant(m)
    home = o.u8(ma + 0x19)
    now = o.u32(WORLD + 0x14)
    seen = o.u8(ma + 0x1B)
    if seen < 4:
        w8(o, ma + 0x1B, 4)
        seen = 4
    office = player_offices(o, m)[0]
    town = o.u8(o.office(office) + 0x2C6)
    w32(o, o.office(office) + 4 + 4 * 23, 1000)  # the last ware: enough for the pirates
    for k, (state, place, ship, before) in enumerate(((3, home, 0x0123, 0), (3, home, 0xFFFF, seen - 4),
                                                      (4, town, 0xFFFF, 0))):
        e = new_event(o, now + 1 + k, 0x1E)
        # State 4 doesn't move the event's tick: with [+0x8] or [+0xC] still
        # ahead (keep) the queue would run it again in the same tick forever.
        until = now if state == 4 else now + 0x10000
        w32(o, e + 8, until)
        w32(o, e + 0xC, until)
        w8(o, e + 0x10, state)
        w8(o, e + 0x11, place)
        w16(o, e + 0x12, ship)
        w8(o, e + 0x14, 5)
        w8(o, e + 0x15, 3)
        w8(o, e + 0x16, m)
        w8(o, e + 0x17, before)
    print(f'events 0x1E of the player {m}: state 3 in {home} (0x41, then 0x42 with [+0x1B] {seen}), '
          f'state 4 in {town} (his office {office}: 0x43)')


def council(o, rec):
    towns, _, merchants, _ = o.counts()
    m = o.u32(to.PLAYER)
    home = o.u8(o.merchant(m) + 0x19)
    other = next(i for i in range(merchants) if i != m)
    second = (home + 1) % towns
    w8(o, o.town(home) + 0x6F1, m)
    w8(o, o.town(second) + 0x6F1, other)
    now = o.u32(WORLD + 0x14)
    last = now | 0xFF  # the day's last tick: the vote ends
    for k, (town, proposal, asked, yes, no, tax) in enumerate(((home, 0, other, 10, 2, 0), (second, 3, m, 3, 8, 7))):
        e = new_event(o, last + 0x100 * k, 0x2E)
        w32(o, e + 8, 20000)
        w8(o, e + 0xC, town)
        w8(o, e + 0xD, proposal)
        w8(o, e + 0xE, 0)
        w8(o, e + 0xF, 0)
        w8(o, e + 0x10, yes)
        w8(o, e + 0x11, no)
        w8(o, e + 0x14, tax)
        w8(o, e + 0x16, asked)
    print(f'council votes: in {home} (the player {m} mayor, proposal 0 passes: the extra tax a day later), '
          f'in {second} (mayor {other}, the player asked, proposal 3 fails)')


def hanse_leader(o, m, town):
    w8(o, 0x6DE52D, m)
    w8(o, 0x6DE52E, town)


def first_pirate(o):
    return next(k for k in range(5) if o.u32(to.SHIPS + 0xC + 4 * k))


def hanse_task(o, rec):
    m = o.u32(to.PLAYER)
    home = o.u8(o.merchant(m) + 0x19)
    hanse_leader(o, m, home)
    now = o.u32(WORLD + 0x14)
    pirate = first_pirate(o)
    cases = ((0, 0, 1), (0, 2, 1), (0, 5, 1), (0, 7, 1), (0xA00, 1, 1), (0x1500, 3, 1), (0xA00, 5, 1), (0x1500, 4, 6))
    for k, (ago, kind, c) in enumerate(cases):
        e = new_event(o, now + 1 + k, 0x30)
        w32(o, e + 8, now - ago)
        w8(o, e + 0xC, c)
        w8(o, e + 0x12, 3 + k)
        w8(o, e + 0x13, kind)
        w8(o, e + 0x14, pirate + 5 if kind == 2 else 0x41 + k)
        w8(o, e + 0x15, 0x21 + k)
    print(f'the player {m} leads the Hanse (meeting in {home}); events 0x30 (days ago, kind, [+0xC]): {cases}')


def hanse_task_ai(o, rec):
    towns, _, merchants, _ = o.counts()
    m = o.u32(to.PLAYER)
    leader = o.u8(0x6DE52D)
    if leader >= merchants or o.u16(o.merchant(leader) + 8) == 0:
        leader = next(i for i in range(merchants) if i != m and o.u16(o.merchant(i) + 8))
    town = o.u8(0x6DE52E) if o.u8(0x6DE52E) < towns else 0
    hanse_leader(o, leader, town)
    now = o.u32(WORLD + 0x14)
    pirate = first_pirate(o)
    for k, kind in enumerate((0, 2, 1)):
        e = new_event(o, now + 1 + k, 0x30)
        w32(o, e + 8, now)
        w8(o, e + 0xC, 1 if k else 0)
        w8(o, e + 0x12, 3 + k)
        w8(o, e + 0x13, kind)
        w8(o, e + 0x14, pirate if kind == 2 else 0x41 + k)
        w8(o, e + 0x15, 0x21 + k)
    print(f'the AI merchant {leader} leads the Hanse (meeting in {town}); events 0x30 of the kinds 0, 2 '
          f'(pirate object {pirate}), 1')


def expedition(o, rec):
    import retouch as rt
    ships = o.counts()[3]
    found = [(i, a) for i, a in rt.events(o) if o.u16(a + 6) == 0x12]
    print(f'events 0x12: {[(i, o.u32(a), o.u16(a + 8), o.u16(a + 0xA)) for i, a in found]}')
    if not found:
        return
    i, a = found[0]
    convoy = o.u32(to.SHIPS + 8) + 0x3C * o.u16(a + 8)
    members, s = [], o.u16(convoy + 0xA)
    while s < ships and len(members) < 50:
        members.append(s)
        s = o.u16(o.ship(s) + 6)
    if len(members) > 1:
        w32(o, o.ship(members[1]) + 0x18, 10)  # a ship that sinks on the way: the letter 0x34
    print(f'convoy owner {o.u8(convoy)}, guide {o.u16(convoy + 0x10)}, ships\' owners {[o.u8(o.ship(s)) for s in members]}')
    w8(o, convoy, o.u32(to.PLAYER))  # the player's, and its ships: the letters reach him
    for s in members:
        w8(o, o.ship(s), o.u32(to.PLAYER))
    w8(o, o.merchant(o.u32(to.PLAYER)) + 0x3A, 0)  # and he keeps the trading information (class 3: 0x63)
    w8(o, WORLD + 0x89, 0)  # nobody has reached the new world: the news 0x56 (0x5319D0)
    w16(o, a + 0xA, 4)
    rt.move_event(o, i, a, o.u32(WORLD + 0x14) + 1)
    print(f'convoy {o.u16(a + 8)} of {o.u8(convoy)} arrives in the next tick; ships {members}')
    return convoy, members


def game_rolls(seed, draws):
    """The game's generator (0x46C320..0x46C380) seeded with `seed`: the
    values of the draws (n, ...)."""
    out = []
    for n in draws:
        x = (0xA205B063 - seed * 0x44BF19B3) & 0xFFFFFFFF
        seed = (x >> 1) | ((x & 1) << 31)
        out.append(seed % n)
    return out


def expedition_to(o, objects, mode=None, pirates=None, cargo=()):
    """The save's first expedition arrives with its route points at the
    places of the world objects `objects` (not found yet, their tick 0: the
    point has to be the place), its [+0x38] `mode` (2: contracts), the
    arrival's tick chosen for the pirates (None: neither pirates nor storm;
    'stole', 'failed') and (ware, amount) loaded on its first ship."""
    import retouch as rt
    ships = o.counts()[3]
    found = [(i, a) for i, a in rt.events(o) if o.u16(a + 6) == 0x12]
    if not found:
        print('no event 0x12')
        return None
    i, a = found[0]
    m = o.u32(to.PLAYER)
    convoy = o.u32(to.SHIPS + 8) + 0x3C * o.u16(a + 8)
    members, s = [], o.u16(convoy + 0xA)
    while s < ships and len(members) < 50:
        members.append(s)
        s = o.u16(o.ship(s) + 6)
    w8(o, convoy, m)
    for s in members:
        w8(o, o.ship(s), m)
    w8(o, o.merchant(m) + 0x3A, 0)
    for k in range(4):
        if k < len(objects):
            b = 0x6DEFB0 + 0x2C * objects[k]
            place = o.u8(b + 4)
            x, y = o.u16(0x673858 + 4 * place), o.u16(0x673858 + 4 * place + 2)
            w8(o, b + 6, 0xFF)
            w32(o, b + 8, 0)
        else:
            x, y = 1, 1  # nowhere
        w16(o, convoy + 0x24 + 4 * k, x)
        w16(o, convoy + 0x26 + 4 * k, y)
        w8(o, convoy + 0x34 + k, 3)
    if mode is not None:
        w16(o, convoy + 0x38, mode)
    for ware, amount in cargo:
        w32(o, o.ship(members[0]) + 0x54 + 4 * ware, amount)
    w16(o, a + 0xA, 4)
    now = o.u32(WORLD + 0x14)
    for d in range(1, 4000):
        r = game_rolls(now + d, [11] + [0x4CCC] * len(members) + [11])
        if (pirates is None and r[0] <= 8) or (pirates == 'stole' and r[0] == 10 and r[-1] == 10) or \
                (pirates == 'failed' and r[0] == 10 and r[-1] <= 5):
            break
    rt.move_event(o, i, a, now + d)
    print(f'convoy {o.u16(a + 8)} of {m} arrives in {d} ticks (pirates: {pirates}); ships {members}; objects {objects}')
    return convoy, members


def expedition_four(o, rec):
    expedition_to(o, [0, 1, 2, 3], mode=2, pirates='stole')


def expedition_villages(o, rec):
    expedition_to(o, [12, 4, 5, 6], pirates='failed', cargo=((7, 4000), (12, 4000)))


def expedition_new_world(o, rec):
    expedition_to(o, [12, 13, 14, 7], cargo=((7, 4000),))


def expedition_nothing(o, rec):
    expedition_to(o, [])


def expedition_contract(o, rec):
    got = expedition_to(o, [0, 1], mode=2)
    if not got:
        return
    convoy, members = got
    index = (convoy - o.u32(to.SHIPS + 8)) // 0x3C
    m = o.u32(to.PLAYER)
    # Contracts of the player at the objects 0 and 1, signed before (0x52C6C0
    # called here with may_create, on a CString of the oracle's): the
    # arrival trades with them. The ships bring half of what one that buys
    # asks for (it goes on) and all of the other's (it is met), and the
    # convoy has money for one that sells.
    text = o.alloc(4)
    o.write_u32(text, o.u32(to.EMPTY_CSTRING))
    for k, b in enumerate((0x6DEFB0, 0x6DEFB0 + 0x2C)):
        o.call(0x52C6C0, [index, 0, text, 2, 1, 0], ecx=b)
        head = o.u32(b + 0x24)
        n = o.u32(head)
        while n != head and o.u16(n + 8) != m:
            n = o.u32(n)
        if n == head:
            print(f'no contract at object {k}')
            continue
        side, buys, amount = o.u8(n + 0xA), o.u8(n + 0xB), o.u32(n + 0xC)
        ware = o.u8(b + 2 + side) if buys else o.u8(b + side)
        if buys:
            w32(o, o.ship(members[0]) + 0x54 + 4 * ware, 200 * (amount // 2 if k == 0 else amount))
        print(f'contract at object {k}: side {side}, buys {buys}, ware {ware}, {amount} barrels')
    w32(o, convoy + 0x20, 10000000)


def debt_event(o, tick, amount, creditor, debtor, town, notice=0, due=0, extra=(0, 0)):
    e = new_event(o, tick, 0x01)
    w32(o, e + 8, amount)
    w32(o, e + 0xC, due)
    w8(o, e + 0x10, creditor)
    w8(o, e + 0x11, debtor)
    w8(o, e + 0x12, notice)
    w8(o, e + 0x14, town)
    w8(o, e + 0x15, extra[0])
    w8(o, e + 0x17, extra[1])
    return e


def debts_notices(o, rec):
    m = o.u32(to.PLAYER)
    home = o.u8(o.merchant(m) + 0x19)
    now = o.u32(WORLD + 0x14)
    debt_event(o, now + 1, 12345, 0xFF, m, home, notice=1, due=now + 0x1234)  # 0x12 to the debtor
    debt_event(o, now + 2, 23456, m, 0xFF, home, notice=1, due=(now + 0x2000) & ~0xFF)  # 0x26: repaid
    debt_event(o, now + 3, 0x31234, m, 0xFF, home, notice=1, due=now + 0x3001, extra=(0x15, 0x27))  # 0x79
    print(f'events 0x01 with [+0x12]: the player {m} owes; he is repaid; a notice 0x79 to him')


def player_debt(o, town, money, amount):
    m = o.u32(to.PLAYER)
    w32(o, o.merchant(m), money)
    debt_event(o, o.u32(WORLD + 0x14) + 1, amount, 0xFF, m, town)
    print(f'the player {m} with {money} owes {amount} to the town {town}')


def debts_office(o, rec):
    m = o.u32(to.PLAYER)
    office = player_offices(o, m)[0]
    w32(o, o.office(office) + 4 + 4 * 3, 4000)  # beer: something to sell
    player_debt(o, o.u8(o.office(office) + 0x2C6), 0, 5000)


def debts_ship(o, rec):
    m = o.u32(to.PLAYER)
    ships = o.counts()[3]
    s = o.u16(o.merchant(m) + 0xE)
    while s < ships and o.u16(o.ship(s) + 0x134) >= 0xC:
        s = o.u16(o.ship(s) + 4)
    if s >= ships:
        print('no ship of the player in a port')
        return
    town = o.u8(o.ship(s) + 0x39)
    for office in player_offices(o, m):
        if o.u8(o.office(office) + 0x2C6) == town:
            for w in range(24):
                w32(o, o.office(office) + 4 + 4 * w, 0)
    w32(o, o.ship(s) + 0x54 + 4 * 3, 4000)
    print(f'ship {s} in town {town} with beer')
    player_debt(o, town, 0, 5000)


def debts_partial(o, rec):
    towns, _, _, ships = o.counts()
    m = o.u32(to.PLAYER)
    busy = {o.u8(o.office(f) + 0x2C6) for f in player_offices(o, m)}
    s = o.u16(o.merchant(m) + 0xE)
    while s < ships:
        busy.add(o.u8(o.ship(s) + 0x39))
        s = o.u16(o.ship(s) + 4)
    town = next(t for t in range(towns) if t not in busy)
    player_debt(o, town, 1000, 5000)


def captain(o, rec):
    m = o.u32(to.PLAYER)
    ships, captains = o.counts()[3], o.u16(to.SHIPS + 0xF2)
    s = o.u16(o.merchant(m) + 0xE)
    while s < ships and o.u16(o.ship(s) + 0x42) >= captains:
        s = o.u16(o.ship(s) + 4)
    if s >= ships:
        print('no ship of the player with a captain')
        return
    o.tick_operations.append(struct.pack('<5I', 0x12, s, 60, 60, 0))
    print(f'operation 0x12 for the player\'s ship {s} (captain {o.u16(o.ship(s) + 0x42)})')


def mayor_election(o, rec):
    import retouch_unchecked as ru
    m = o.u32(to.PLAYER)
    home = o.u8(o.merchant(m) + 0x19)
    w8(o, 0x6DE52A, 0)
    for t in (home, (home + 1) % o.counts()[0]):
        ru.direct(o, 0x528E60, [], ecx=o.town(t))
        print(f'election in town {t}: the mayor {o.u8(o.town(t) + 0x6F1)}')


def alderman_vote(o, merchant_wins):
    import retouch as rt
    towns, _, merchants, _ = o.counts()
    m = o.u32(to.PLAYER)
    found = [(i, a) for i, a in rt.events(o) if o.u16(a + 6) == 0x20]
    if not found:
        print('no event 0x20')
        return
    i, a = found[0]
    citizen = next(t for t in range(towns) if o.u8(o.town(t) + 0x6F1) >= merchants)
    other = next(t for t in range(towns) if t != citizen)
    ai = next(k for k in range(merchants) if k != m and o.u16(o.merchant(k) + 8))
    # The player among the candidates but never the winner: becoming Alderman
    # ends the game (its window, 0x5499F0).
    candidates = [0x80 | ai, citizen, other, 0x80 | m]
    votes = [20, 5, 3, 1] if merchant_wins else [5, 20, 3, 1]
    for k in range(4):
        w8(o, a + 8 + k, candidates[k])
        w8(o, a + 0xC + k, votes[k])
        w8(o, a + 0x10 + k, 0)
    now = o.u32(WORLD + 0x14)
    rt.move_event(o, i, a, ((now + 0x100) & ~0xFF) | 0xFE)
    print(f'event 0x20 ends a day later: candidates {candidates}, votes {votes}')


def alderman_merchant(o, rec):
    alderman_vote(o, True)


def alderman_citizen(o, rec):
    alderman_vote(o, False)


def hanse_vote(o, rec):
    m = o.u32(to.PLAYER)
    now = o.u32(WORLD + 0x14)
    e = new_event(o, ((now + 1) & ~0xFF) + 0xF0 if (now & 0xFF) < 0xF0 else now + 0x100, 0x21)
    w8(o, e + 8, o.u8(0x6DE52E))
    w8(o, e + 9, o.u8(o.merchant(m) + 0x19))
    w8(o, e + 0xA, 2)
    w8(o, e + 0xE, 4)
    w8(o, e + 0x12, 3)
    w8(o, e + 0x13, 9)
    print(f'an event 0x21 in its last step (the proposal fails 3 to 9): the letter 0x0B to each merchant')


def auction_end(o, rec):
    towns, _, merchants, ships = o.counts()
    m = o.u32(to.PLAYER)
    now = o.u32(WORLD + 0x14)
    ai = next(k for k in range(merchants) if k != m and o.u16(o.merchant(k) + 8) and o.u16(o.merchant(k) + 0xC) < o.u16(WORLD + 8)
              and o.u16(o.merchant(k) + 0xE) < ships)
    cases = []

    def first_record(merchant):
        for f in (player_offices(o, merchant)):
            r = o.u16(o.office(f) + 0x2CE)
            t = o.u8(o.office(f) + 0x2C6)
            if r < o.u16(o.town(t) + 0x776):
                return t, r
        return None

    def ships_of(merchant):
        # Ships in port and in no convoy: selling one in a convoy (0x509250)
        # is a branch p3vita doesn't reproduce yet (events_mayor.cpp).
        convoys = o.u16(to.SHIPS + 0xF6)
        out, s = [], o.u16(o.merchant(merchant) + 0xE)
        while s < ships and len(out) < 8:
            if o.u16(o.ship(s) + 8) >= convoys and o.u16(o.ship(s) + 0x134) < 0xC:
                out.append(s)
            s = o.u16(o.ship(s) + 4)
        return out

    mine = first_record(m)
    theirs = first_record(ai)
    if mine:
        cases += [('b', mine, ai), ('b', mine, m), ('b', mine, 0xFF)]  # 0x76 to him; 0x7D; 0x7E
    if theirs:
        cases.append(('b', theirs, m))  # 0x7F to him
    my_ships, their_ships = ships_of(m), ships_of(ai)
    for s, winner in zip(my_ships, (ai, m, 0xFF)):  # 0x2C; 0x2D; 0x2E
        cases.append(('s', (o.u8(o.ship(s) + 0x39) % towns, s), winner))
    if their_ships:
        cases.append(('s', (o.u8(o.ship(their_ships[0]) + 0x39) % towns, their_ships[0]), m))  # 0x5D to him
    for k, (kind, (town, item), winner) in enumerate(cases):
        e = new_event(o, now + 1 + k, 0x36 if kind == 'b' else 0x0A)
        w16(o, e + 8, item)
        w16(o, e + 0xA, 0xFFFF)
        w16(o, e + 0xC, 0xFFFF)
        w16(o, e + 0xE, 12345)
        w16(o, e + 0x10, 100)
        w16(o, e + 0x12, 0)
        w8(o, e + 0x14, 7)
        w8(o, e + 0x15, town)
        w8(o, e + 0x16, 0)
        w8(o, e + 0x17, winner)
    print(f'auctions ending (kind, (town, item), winner): {cases}; the player {m}, the AI merchant {ai}')


def hanse_task_letters(o, rec):
    import retouch as rt
    import retouch_unchecked as ru
    m = o.u32(to.PLAYER)
    tasks = ru.candidacy_tasks(o, rt.ai_with_office(o)[0])
    if 1 in tasks:  # the road, not started: 0x80
        ru.set_task(o, tasks[1], merchant=m, started=0, at=1)
    if 3 in tasks:  # the pirate nest, not started: 0x6E and 0x84
        ru.set_task(o, tasks[3], merchant=m, started=0, at=2)
    if 2 in tasks:  # a pirate ship hunted ([+0x16] = 1): done, 0x6F to him too
        ru.set_task(o, tasks[2], merchant=m, started=1, deadline=0x3000, f16=1, at=3)
    print(f'the player {m} gets the tasks {sorted(k for k in tasks if k in (1, 2, 3))}')


def siege_letters(o, rec):
    import retouch as rt
    import retouch_unchecked as ru
    towns = o.counts()[0]
    m = o.u32(to.PLAYER)
    ready = [i for i in range(towns) if rt.siege_ready(o, i)]
    a, b = ready[0], ready[1]
    for t, kind, r in ((a, 1, 0x80), (b, 2, 0x31)):
        ru.operation(o, 0x8E, t, kind, r, 1)  # 0x629C60: the news 0x53
    # a: the ransom in its army's next tick (0x633DB0, the news 0x38 with the money).
    army = o.u32(ru.siege_object(o, a) + 8)
    w16(o, army + 0x42, 0)
    w16(o, army + 0x3C, (o.u16(army + 0x68) >> 1) + 1)
    w32(o, ru.siege_object(o, a), 0)
    w32(o, o.town(a) + 0x700, 12345)
    # b: one besieger against many defenders: the army leaves (the news 0x38, 0xFFFFFFFF).
    army = o.u32(ru.siege_object(o, b) + 8)
    for f in (0x42, 0x3C, 0x3A):
        w16(o, army + f, 0)
    w8(o, army + 0x78, 0)
    w8(o, army + 0x79, 0)
    w32(o, army, 0x10000)
    w32(o, army + 0x21C, 0x10000)
    for k in range(1, 5):
        w32(o, army + 0x21C + 4 * k, 0)
    w8(o, o.town(b) + 0x998, 20)
    # c: the prince's payments due (0x636230, the letters 0x85 to the player):
    # for no town, for a town that paid its ransom, for one that didn't, and
    # one over 65 535 (the letter keeps its low 16 bits).
    c = next(i for i in range(towns) if i not in (a, b))
    other = next(i for i in range(towns) if i not in (a, b, c))
    nodes = ((bytes([3, 4, 5, 6, 7]), 0xFF, 0), (bytes([9, 8, 7, 6, 5]), other, 1), (bytes([2, 2, 2, 2, 2]), other, 0),
             (bytes([255] * 5), 0xFF, 0))
    head = 0
    for wares, town, paid in reversed(nodes):
        node = o.alloc(0x14)
        o.uc.mem_write(node, wares + bytes([m, town, 1, 1, paid, 0, 0]) + struct.pack('<II', 0, head))
        head = node
    w32(o, ru.siege_object(o, c) + 0x1020, head)
    print(f'sieges of {a} (the ransom) and {b} (it leaves); 4 payments to the player {m} in the list of {c}')


def operation_letters(o, rec):
    m = o.u32(to.PLAYER)
    mm = o.merchant(m)
    now, towns = o.u32(WORLD + 0x14), o.u32(WORLD + 0x10)
    ships, captains = o.u16(to.SHIPS + 0xF4), o.u16(to.SHIPS + 0xF2)
    home = o.u8(mm + 0x19)
    # 0x13: his ships with a captain retire (0x52): one in port and one at sea if he has them.
    with_captain, s = [], o.u16(mm + 0xE)
    while s < ships:
        if o.u16(o.ship(s) + 0x42) < captains:
            with_captain.append(s)
        s = o.u16(o.ship(s) + 4)
    chosen = [x for x in (next((x for x in with_captain if o.u8(o.ship(x) + 0x39) < towns), None),
                          next((x for x in with_captain if o.u8(o.ship(x) + 0x39) >= towns), None)) if x is not None]
    if chosen and o.u8(o.ship(chosen[-1]) + 0x39) < towns:
        # None at sea: the last one run now instead, with its [+0x39] out of
        # the towns for that call (the letter's town is then his home town;
        # retouch.py capitan_jugador has the one in port). Not queued again:
        # with both the oracle hung later on (not looked into; probably the
        # same captain retiring twice, which the game never does: 0x4DCEA0
        # marks him). Before queuing the others: ru.operation empties the
        # queue.
        import retouch_unchecked as ru
        x = chosen.pop()
        town = o.u8(o.ship(x) + 0x39)
        w8(o, o.ship(x) + 0x39, 0xFF)
        ru.operation(o, 0x13, x, o.u16(o.ship(x) + 0x42))
        w8(o, o.ship(x) + 0x39, town)
    for x in chosen:
        o.tick_operations.append(struct.pack('<5I', 0x13, x, o.u16(o.ship(x) + 0x42), 0, 0))
    # 0x60: his wedding in his home town the next day (0x33).
    when = (now + 0x180) & ~0xFF
    o.tick_operations.append(struct.pack('<IIHBBBBBBBBH', 0x60, when, 5500, 0x31, 7, 0x85, 3, home, 1, 0, m, 0))
    # 0x67: the administrator of his first office gains skill (0x7C); an AI
    # office's administrator is lent to it if it has none.
    office = player_offices(o, m)[0]
    admin = o.u16(o.office(office) + 0x2F2)
    if admin >= captains:
        lender = next((f for f in range(o.u16(WORLD + 8)) if o.u16(o.office(f) + 0x2F2) < captains), None)
        admin = o.u16(o.office(lender) + 0x2F2) if lender is not None else 0
        w16(o, o.office(office) + 0x2F2, admin)
    o.tick_operations.append(struct.pack('<5I', 0x67, admin, m, o.u8(o.office(office) + 0x2C6), 0xC8))
    # 0x87: he married 300 days ago: a child (0x7A).
    w8(o, mm + 0x31, home)
    w32(o, mm + 0x28, now - 18 * 365 * 256)
    w32(o, mm + 0x2C, now - 300 * 256)
    o.tick_operations.append(struct.pack('<IBB4B10x', 0x87, m, 11, 1, 2, 3, 4))
    print(f'the player {m}: captains retire on ships {chosen}, a wedding in {home}, the administrator {admin} '
          f'of office {office}, a child')


def diet_letters(o, rec):
    m = o.u32(to.PLAYER)
    now, towns = o.u32(WORLD + 0x14), o.u32(WORLD + 0x10)
    ships, convoys = o.u16(to.SHIPS + 0xF4), o.u16(to.SHIPS + 0xF6)
    ai = next(k for k in range(o.u16(WORLD + 0xA)) if o.u16(o.merchant(k) + 8))
    home = o.u8(o.merchant(m) + 0x19)
    t1, t2, t3 = [t for t in range(towns) if t != home][:3]
    # The convoy of a ship of the player, if one is in a convoy (its ships are counted in 0x5C).
    convoy, s = 0xFFFF, o.u16(o.merchant(m) + 0xE)
    while s < ships:
        if o.u16(o.ship(s) + 8) < convoys:
            convoy = o.u16(o.ship(s) + 8)
            break
        s = o.u16(o.ship(s) + 4)

    def mission(tick, kind, town, phase, members=(), deadline=0, fleet=0xFFFF):
        e = o.call(0x4D8CF0, [tick], ecx=to.EVENTS)
        w16(o, e + 6, kind)
        o.uc.mem_write(e + 8, bytes((list(members) + [0xFF] * 8)[:8]))
        w32(o, e + 0x10, deadline)
        w8(o, e + 0x14, town)
        w8(o, e + 0x15, phase)
        w16(o, e + 0x16, fleet)

    # 0x24 protecting t1, its satisfaction under −20: phase 4 ends, then 5 (0x5C, town + 0x100).
    w32(o, o.town(t1) + 0x2C8, o.u32(o.town(t1) + 0x2C8) | 0x600)
    w16(o, o.town(t1) + 0x304, 0x10000 - 30)
    mission(now + 1, 0x24, t1, 4)
    # 0x23 in t2 past its deadline with the player's convoy: phase 6 ends, then 5 (0x5C).
    mission(now + 2, 0x23, t2, 6, deadline=now, fleet=convoy)
    # 0x34 in phase 2 with the player twice and an AI merchant in the list (0x4C).
    mission(now + 3, 0x34, t3, 2, members=(m, ai, m))
    print(f'the player {m}: 0x24 ending in {t1}, 0x23 in {t2} with the convoy {convoy}, 0x34 in {t3} '
          f'(the AI merchant {ai})')


def expedition_lost(o, rec):
    convoy, members = expedition(o, rec)
    w32(o, o.ship(members[0]) + 0x18, 10)
    points = o.u32(0x6DF2E0)  # Expeditionen.dat: u16 x, u16 y each
    w16(o, convoy + 0x24, o.u16(points))
    w16(o, convoy + 0x26, o.u16(points + 2))
    print(f'its first ship sinks too; its first point at the first of Expeditionen.dat '
          f'({o.u16(points)}, {o.u16(points + 2)})')


SCENARIOS = {f.__name__: f for f in (grow, promotion, privateer, reputation, commission, hulk, wall_bill, convoy_fees,
                                     pirate_hunt, council, hanse_task, hanse_task_ai, expedition, expedition_lost,
                                     expedition_four, expedition_villages, expedition_new_world, expedition_nothing,
                                     expedition_contract, debts_notices, debts_office, debts_ship, debts_partial,
                                     captain, mayor_election, alderman_merchant, alderman_citizen, hanse_vote,
                                     auction_end, hanse_task_letters, siege_letters,
                                     operation_letters, diet_letters)}


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
    p.add_argument('--letters', choices=('connected', 'all', 'none'), default='connected')
    p.add_argument('--write', metavar='OUT', help='at the end, the save the game writes: OUT uncompressed, OUT.pat '
                   'compressed, OUT.pst')
    a = p.parse_args()
    functions = [int(x, 16) for x in a.functions.split(',') if x]
    o = to.TownOracle(a.exe, open(a.save, 'rb').read(), 1, a.barcos, rest=list(to.REST), cpr=a.cpr,
                      statistics=True, letters=a.letters)
    o.load()
    with open(a.out, 'wb') as f:
        f.write(b'P3PROBE1')
        rec = to.Recorder(o, functions, f, 1 << 30)
        rec.active = True
        SCENARIOS[a.scenario](o, rec)
        for _ in range(a.days * 256):
            o.advance_tick()
        f.write(b'E')
    print(f'{rec.count} samples in {a.out}')
    print(f'mailbox: {o.u16(to.MAIL + 6)} entries, free head {o.u16(to.MAIL + 4)}, '
          f'chronicle head {o.u16(to.MAIL + 8)}')
    if a.write:
        from write_oracle import prepare_game_object, write_files
        prepare_game_object(o)
        write_files(o, a.write, a.write + '.pat')
        print(f'save written in {a.write} ({a.write}.pat, {a.write}.pst)')


if __name__ == '__main__':
    main()
