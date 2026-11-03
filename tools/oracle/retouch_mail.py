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
  all_letters three letters of each type with a letter text (but those
             with a buffer or record) sent to the player at once with the
             game's 0x4D6530 (run with --letters all), their fields small
             random values (LETTERS_SEED; small bytes, small u16, and small
             u16 with a random s32 at [+0xC]), and for the types p3vita
             composes, more with real fields (real_fields): for
             scripts/letters-check.sh on the save the game writes (--write),
             0 days
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


# The types with a letter text (0x47F3B0's table 0x48664C) but no buffer or record.
NO_TEXT = (0x06, 0x08, 0x09, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x1C, 0x23, 0x35, 0x38, 0x39, 0x3A, 0x3C, 0x40, 0x44,
           0x45, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5F, 0x71, 0x72)
WITH_BUFFER = (0x29, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x47, 0x63, 0x71, 0x83)
LETTER_TYPES = [t for t in range(0x86) if t not in NO_TEXT and t not in WITH_BUFFER]


def read_delete(o, rec):
    """Operations 0x6E (a letter read: 0x4D69D0) and 0x6F (a letter deleted:
    0x4D6A50), which the letter window queues: on the player's letters (the
    head, one in the middle, one with a buffer), on the chronicle (−2 and −1),
    and on letters they don't reach (another merchant's chain, past the
    mailbox, a negative merchant for 0x6E)."""
    m = o.u32(to.PLAYER)
    mm = o.merchant(m)
    count = o.u16(to.MAIL + 6)
    base = o.u32(to.MAIL)

    def entry(i):
        return base + 16 * i

    def chain(head):
        out = []
        while head < count and len(out) < count:
            out.append(head)
            head = o.u16(entry(head) + 6)
        return out

    mine, news = chain(o.u16(mm + 0xA)), chain(o.u16(to.MAIL + 8))
    if len(mine) < 6:
        raise SystemExit(f'the player has only {len(mine)} letters')
    for i in mine[:6]:
        w8(o, entry(i) + 1, o.u8(entry(i) + 1) | 0x80)  # unread
    other = next(k for k in range(o.counts()[2]) if k != m)
    with_buffer = next((i for i in mine[3:] if o.u8(entry(i) + 4) in (0x29, 0x47, 0x63, 0x83, 0x3C, 0x3D, 0x3E, 0x3F,
                                                                       0x40, 0x71)), mine[5])
    ops = [(0x6E, m, mine[0]), (0x6E, m, mine[2]), (0x6E, m, news[0] if news else count), (0x6E, m, count),
           (0x6E, m, count + 5), (0x6E, other, mine[1]), (0x6E, -1, mine[1]), (0x6E, -2, mine[1]),
           (0x6F, m, mine[1]), (0x6F, m, mine[0]), (0x6F, m, with_buffer), (0x6F, other, mine[4]),
           (0x6F, m, count), (0x6F, m, news[0] if news else count)]
    if len(news) >= 3:
        ops += [(0x6F, -2, news[0]), (0x6F, -1, news[2])]
    for op, who, i in ops:
        o.tick_operations.append(struct.pack('<IiIII', op, who, i, 0, 0))
    print(f'the player {m}: {len(mine)} letters, the chronicle {len(news)}; the letter with a buffer {with_buffer} '
          f'(type {o.u8(entry(with_buffer) + 4):#x}); {len(ops)} operations')


def ticker_lines(o, rec):
    """Ticker lines (0x548CA0, region TK) that reach the player only in the
    phases that summon the ranks from 4 on: the player of rank 6 in every
    town; at the next day's start a Hanse vote about his home town (event
    0x21 in phase 0: 0x4DC2E8), the Alderman's election (0x20 in phase 0:
    0x4DBD14), a council in his home town (0x2E: 0x4E9B90) and the diet's
    blockade of another town (0x24 in phase 1: 0x4E0DB4)."""
    m = o.u32(to.PLAYER)
    mm = o.merchant(m)
    towns = o.counts()[0]
    for t in range(towns):
        w8(o, mm + 0x39C + t, 6)
    home = o.u8(mm + 0x19)
    other = (home + 1) % towns
    day = (o.u32(WORLD + 0x14) + 0x100) & ~0xFF
    e = new_event(o, day, 0x21)
    w8(o, e + 8, o.u8(0x6DE52E))
    w8(o, e + 9, home)
    new_event(o, day, 0x20)
    e = new_event(o, day, 0x2E)
    w32(o, e + 8, 20000)
    w8(o, e + 0xC, home)
    w8(o, e + 0xD, 0)
    w8(o, e + 0x16, m)
    e = o.call(0x4D8CF0, [day + 1], ecx=to.EVENTS)
    w16(o, e + 6, 0x24)
    o.uc.mem_write(e + 8, bytes([0xFF] * 8))
    w32(o, e + 0x10, 0)
    w8(o, e + 0x14, other)
    w8(o, e + 0x15, 1)
    w16(o, e + 0x16, 0xFFFF)
    print(f'the player {m} of rank 6; at {day:#x} a Hanse vote about {home}, the Alderman\'s election, a council '
          f'in {home} and the diet blockading {other}')


def player_ships(o):
    """The player's ships in port, not in a convoy, as (ship, town)."""
    towns, _, _, ships = o.counts()
    convoys = o.u16(to.SHIPS + 0xF6)
    out, s = [], o.u16(o.merchant(o.u32(to.PLAYER)) + 0xE)
    while s < ships:
        a = o.ship(s)
        if o.u16(a + 0x134) == 0 and o.u8(a + 0x39) < towns and o.u16(a + 8) >= convoys:
            out.append((s, o.u8(a + 0x39)))
        s = o.u16(a + 4)
    return out


def ticker_ships(o, rec):
    """The ticker lines of the player's ships alone (0x548CA0, region TK):
    each of his ships in port ordered to the next town (operation 0x00,
    0x537670): they dock there («The snaikka X has docked in Y.», kind 5,
    0x519D72)."""
    towns = o.counts()[0]
    sent = []
    for s, t in player_ships(o)[:4]:
        op = o.alloc(20)
        o.uc.mem_write(op, struct.pack('<IIIii', 0, s, (t + 1) % towns, 0, 0))
        o.call(to.SET_SAIL, [], ecx=op)
        sent.append(s)
    print(f'the player\'s ships {sent} sail to the next towns')


def ship_sunk(o, rec):
    """A ship of the player sinks at sea (0x506CF2: 0x519D90's letter 0x1B,
    0x519E87, and the ticker line 0x16): two of his ships alone in port sail
    to the next town, the ticks go on until they are at sea, then their hull
    is left at 4 (the sea wears 1 every 4 ticks); with two, the second loses
    its captain (one sinks with its captain, if it had one, one without)."""
    towns = o.counts()[0]
    ships = [s for s, _ in player_ships(o)[:2]]
    if not ships:
        raise SystemExit('the player has no ship alone in port')
    for s in ships:
        t = o.u8(o.ship(s) + 0x39)
        op = o.alloc(20)
        o.uc.mem_write(op, struct.pack('<IIIii', 0, s, (t + 1) % towns, 0, 0))
        o.call(to.SET_SAIL, [], ecx=op)
    for _ in range(512):
        if all(o.u16(o.ship(s) + 0x134) == 0xF for s in ships):
            break
        o.advance_tick()
    at_sea = [s for s in ships if o.u16(o.ship(s) + 0x134) == 0xF]
    for s in at_sea:
        w32(o, o.ship(s) + 0x18, 4)
    captains = [o.u16(o.ship(s) + 0x42) for s in at_sea]
    if len(at_sea) > 1:
        w16(o, o.ship(at_sea[1]) + 0x42, 0xFFFF)
    print(f'the player\'s ships {at_sea} at sea with a hull of 4 (captains {captains}, the second one\'s taken)')


def ticker_shipyard(o, rec):
    """Direct calls to the shipyard's work (with FUNCS 5083b0,508aa0; days 0:
    the ships' lists are retouched): a ship of the player in his town's
    building list [0x81C], 10 short of its hull (0x5083B0: «Launch in X»,
    kind 6, 0x508514), and two in its repair list [0x81E], one in state 6
    («Repairs completed in X», kind 7) and one in 7 («Ship improvements
    completed in X.», 0xA) (0x508AA0: 0x508BC5)."""
    import retouch_unchecked as ru
    ships = player_ships(o)
    if len(ships) < 3:
        raise SystemExit(f'the player has {len(ships)} ships alone in port')
    (s0, t), (s1, _), (s2, _) = ships[:3]
    a = o.town(t)
    for s, status in ((s0, 0), (s1, 6), (s2, 7)):
        sa = o.ship(s)
        w32(o, sa + 0x18, o.s32(sa + 0x14) - 10)
        w8(o, sa + 0x136, 0)
        w16(o, sa + 0x134, status)
        w16(o, sa + 6, 0xFFFF)
    w16(o, a + 0x81C, s0)
    w16(o, o.ship(s1) + 6, s2)
    w16(o, a + 0x81E, s1)
    ru.direct(o, 0x5083B0, [t, 1000], ecx=to.SHIPS)
    ru.direct(o, 0x508AA0, [t, 1000], ecx=to.SHIPS)
    print(f'in town {t}: the player\'s ship {s0} launched, {s1} and {s2} repaired')


def real_fields(t, rnd, towns, ships, captains, merchants=30):
    """Letters of type t with the fields [+0x8..+0xF] a real one has (from the
    code that sends it), for the types p3vita composes (letters.cpp): they
    reach the branches the small random fields miss and stay in range (out of
    range the game reads past its tables and composes whatever it finds)."""
    def h(*v):
        return struct.pack('<4H', *v)
    if t == 0x00:  # a ship launched: [+0xA] the ship's type (4: a convoy)
        return [h(0, kind, 0, 0) for kind in range(5)]
    if t == 0x01:  # a repair: the type (4: a convoy), the condition, the bill
        return [struct.pack('<HHi', kind, rnd.randrange(101), rnd.randrange(1, 20000)) for kind in range(5)]
    if t == 0x02:
        # No repair (operation 0x03, 0x52AE8A): the ship's name number [+0x15E]
        # (past the 300 names: a type's word), its type (4: in a convoy), the
        # bill. Without a bill the game names a ship by that number (a bug):
        # below the number of ships, so that it is one.
        return [struct.pack('<HHi', name, kind, bill) for name, kind, bill in
                ((rnd.randrange(ships), 0, 0), (rnd.randrange(ships), 1, -rnd.randrange(1, 9000)),
                 (rnd.randrange(300), 2, rnd.randrange(1, 9000)), (300 + rnd.randrange(4), 3, rnd.randrange(1, 9000)),
                 (0xFFFF, rnd.randrange(4), rnd.randrange(1, 9000)), (rnd.randrange(300), 4, rnd.randrange(1, 9000)))]
    if t == 0x03:  # works done: the building 0..0x38
        return [h(b, 0, 0, 0) for b in sorted(rnd.sample(range(0x39), 8)) + [0x38]]
    if t in (0x04, 0x05):  # a captain hired, leaving: the captain, his ship
        return [h(rnd.randrange(captains), rnd.randrange(ships), 0, 0) for _ in range(4)]
    if t == 0x07:  # back from a route: a ship name (0x8000: a convoy), the family and first names, the profit
        return [struct.pack('<HBBi', name, rnd.randrange(86), first, profit) for name, first, profit in
                ((rnd.randrange(300), rnd.randrange(80), 0),
                 (0x8000 | rnd.randrange(300), rnd.randrange(80), rnd.randrange(1, 9000)),
                 (rnd.randrange(300), 0x80 | rnd.randrange(80), -rnd.randrange(1, 9000)))]
    if t == 0x0A:  # a charge: the names, the reason 5..8 (events_big.cpp, start_letter)
        return [h(rnd.randrange(80), rnd.randrange(86), reason, 0) for reason in (5, 6, 7, 8)]
    if t == 0x0B:  # a Hanse vote (events_mayor.cpp): the town, kind << 8 | reason, the votes for and against
        return [h(rnd.randrange(towns) if kind or rnd.randrange(2) else 0xFF, kind << 8 | rnd.randrange(7), yes, no)
                for kind in range(6) for yes, no in ((3, 9), (9, 3))]
    if t == 0x11:  # the weekly bill: the taxes, the property tax (u32)
        return [struct.pack('<II', a, b) for a, b in
                ((rnd.randrange(1, 5000), rnd.randrange(1, 3000)), (0, rnd.randrange(1, 3000)), (rnd.randrange(1, 5000), 0))]
    if t == 0x12:  # a loan due: the amount, the year, the month (0 or 1..12) and the day (up to past its month's end)
        return [h(rnd.randrange(1, 30000), 1300 + rnd.randrange(60), m, d) for m, d in
                ((0, 5), (2, 29), (4, 31), (12, 32), (rnd.randrange(1, 13), rnd.randrange(1, 29)))]
    if t == 0x13:  # an impounding: a ship's name number, kind 0..2, settled or not, the amount (u32)
        return [struct.pack('<HBBI', rnd.randrange(300), kind, settled, rnd.randrange(1, 90000))
                for kind in range(3) for settled in (0, 1)]
    if t == 0x14:  # an order cancelled: what is paid back (s32), day << 2 | type, year · 12 + month
        return [struct.pack('<iBBH', paid, 0, rnd.randrange(1, 29) << 2 | rnd.randrange(4),
                            (1300 + rnd.randrange(60)) * 12 + rnd.randrange(12)) for paid in (rnd.randrange(9000), -1)]
    if t == 0x15:  # a burglary: family, first name, the wares' bits, their value, the gold
        return [struct.pack('<BBHHH', rnd.randrange(86), rnd.randrange(80), wares, value, gold) for wares, value, gold in
                ((0, 0, 0), (0, 0, rnd.randrange(1, 900)), (rnd.randrange(1, 0x10000), rnd.randrange(1, 900), 0),
                 (rnd.randrange(1, 0x10000), rnd.randrange(1, 900), rnd.randrange(1, 900)), (0x10000 >> 1, 5, 7))]
    if t == 0x16:  # a burglary failed: the names
        return [h(rnd.randrange(80), rnd.randrange(86), 0, 0) for _ in range(2)]
    if t == 0x17:  # a traveller: the town to, month << 5 | day, the pay, the ship (in range or not)
        return [h(rnd.randrange(towns), rnd.randrange(1, 13) << 5 | rnd.randrange(1, 29), rnd.randrange(1, 900), ship)
                for ship in (rnd.randrange(ships), ships + 3)]
    if t == 0x19:
        # A task's charge (events_big.cpp, start_letter): the names, the kind
        # (0..4, 9..15; 5..8 go as 0x0A), a town, and a ship's name number
        # (kind 3: a merchant's names).
        return [struct.pack('<BBBBH2x', rnd.randrange(80), rnd.randrange(86), kind, rnd.randrange(towns),
                            rnd.randrange(80) | rnd.randrange(86) << 8 if kind == 3 else rnd.randrange(300))
                for kind in (0, 1, 2, 3, 4, 9, 10, 11, 12, 13, 14, 15)]
    if t == 0x1A:  # a transport order: family, first, the ware, the amount, months << 5 | day
        return [struct.pack('<BBHHH', rnd.randrange(86), rnd.randrange(80), ware, rnd.randrange(1, 90),
                            rnd.randrange(30) << 5 | rnd.randrange(1, 29)) for ware in (rnd.randrange(24), 0, 3, 23)]
    if t == 0x1B:  # a ship sunk: place << 2 | type, first << 11, its name, its captain's family (0xFF: none), sailors
        return [struct.pack('<HHHBB', rnd.randrange(64) << 2 | kind, rnd.randrange(32) << 11 | rnd.randrange(0x800),
                            rnd.randrange(300), family, rnd.randrange(1, 60))
                for kind in range(4) for family in (rnd.randrange(86), 0xFF)]
    if t == 0x1D:
        return [h(0, 0, 0, 0)]
    if t == 0x24:  # a feast: the guests, the result 0..3
        return [h(0, rnd.randrange(1, 300), k, 0) for k in range(4)]
    if t == 0x25:  # a dowry: two ships' types (the second 0..3 or none: 4) and their goods
        return [h(a, b, ga, gb) for a, b, ga, gb in
                ((rnd.randrange(4), rnd.randrange(4), rnd.randrange(1, 90), rnd.randrange(1, 90)),
                 (rnd.randrange(4), rnd.randrange(4), 0, rnd.randrange(1, 90)),
                 (rnd.randrange(4), rnd.randrange(4), rnd.randrange(1, 90), 0), (rnd.randrange(4), 4, 0, 0))]
    if t == 0x26:  # a loan repaid: the amount, the year, the month (0 or 1..12), the day
        return [h(rnd.randrange(1, 30000), 1300 + rnd.randrange(60), m, d) for m, d in
                ((0, 5), (2, 29), (rnd.randrange(1, 13), rnd.randrange(1, 29)))]
    if t == 0x27:  # a company's value: the merchant, the shares, the change (s32)
        return [struct.pack('<HHi', rnd.randrange(merchants), rnd.randrange(1, 100), change)
                for change in (rnd.randrange(1, 30000), -rnd.randrange(1, 30000), 0)]
    if t == 0x28:  # a dividend not paid: two amounts, a tick
        return [struct.pack('<HHI', rnd.randrange(1, 9000), rnd.randrange(1, 9000), rnd.randrange(1 << 24))]
    if t == 0x2A:  # a pirate's news: sex, adjective, rank 0..7, names, the pirate's names, the outcome
        return [bytes([sex, rnd.randrange(5), rnd.randrange(8), rnd.randrange(80), rnd.randrange(86), rnd.randrange(80),
                       rnd.randrange(86), outcome]) for sex, outcome in ((0, 0), (1, 0xFF), (1, 1), (0, 3))]
    if t == 0x2C:  # a ship auctioned: the buyer's names, the ship's name, the price, the fee
        return [struct.pack('<BBHHH', rnd.randrange(80), rnd.randrange(86), rnd.randrange(300), rnd.randrange(1, 60000),
                            rnd.randrange(1, 3000))]
    if t == 0x2D:  # the player bought his own ship: its name, the price, the fee
        return [h(0, rnd.randrange(300), rnd.randrange(1, 60000), rnd.randrange(1, 3000))]
    if t == 0x2E:  # the ship not sold: its name
        return [h(0, 0, rnd.randrange(300), 0)]
    if t == 0x2F:  # the wall's bill: family, first (| 0x80: a lady), the mayor's family and first, the amount / 100
        return [bytes([rnd.randrange(86), rnd.randrange(80) | lady, rnd.randrange(86), rnd.randrange(80)]) +
                struct.pack('<HH', rnd.randrange(1, 600), 0) for lady in (0, 0x80)]
    if t == 0x30:  # a pirate's share: the captain's first and family (| 0x80), the victim's, the share, name << 2 | type
        return [bytes([rnd.randrange(80), rnd.randrange(86) | plunder, rnd.randrange(80) | woman, rnd.randrange(86)]) +
                struct.pack('<HH', rnd.randrange(1, 20000), rnd.randrange(300) << 2 | rnd.randrange(4))
                for plunder, woman in ((0, 0), (0x80, 0x80))]
    if t == 0x31:  # an escort's bonus: the bonus, the orlog ship's name
        return [h(rnd.randrange(1, 9000), 0, 0, rnd.randrange(300))]
    if t == 0x32:  # a convoy's arrival: the sum, sex, rank, the leader's family and first
        return [struct.pack('<HHBBBB', rnd.randrange(1, 9000), 0, sex, rnd.randrange(8), rnd.randrange(86),
                            rnd.randrange(80)) for sex in (0, 1)]
    if t == 0x33:  # a wedding: the town, family, first, month 0..11, day, year
        return [struct.pack('<HBBBBH', rnd.randrange(towns), rnd.randrange(86), rnd.randrange(80), rnd.randrange(12),
                            rnd.randrange(1, 29), 1300 + rnd.randrange(60))]
    if t == 0x34:  # a ship lost on the expedition: its name, its type, the tick
        return [struct.pack('<HHI', rnd.randrange(300), rnd.randrange(4),
                            ((1300 + rnd.randrange(60)) * 365 + rnd.randrange(365)) << 8 | rnd.randrange(256))]
    if t == 0x36:  # the wall's extension: first (past 0x80 a woman's), family, the sum's high and low halves
        return [h(first, rnd.randrange(86), rnd.randrange(2), rnd.randrange(65536))
                for first in (rnd.randrange(80), 0x80 | rnd.randrange(1, 80))]
    if t == 0x37:  # a special tax (u32)
        return [struct.pack('<II', rnd.randrange(1, 90000), 0)]
    if t == 0x3B:  # a coward: 0/1, the names
        return [h(k, rnd.randrange(80), rnd.randrange(86), 0) for k in (0, 1)]
    if t == 0x41:  # a pirate hunt: the town, the ship's name, first (past 0x7F a woman's), family
        return [h(rnd.randrange(towns), rnd.randrange(300), first, rnd.randrange(86))
                for first in (rnd.randrange(80), 0x80 | rnd.randrange(80))]
    if t == 0x42:  # the hunt ended: the town, the reward, the names
        return [h(rnd.randrange(towns), rnd.randrange(1, 9000), rnd.randrange(80), rnd.randrange(86))]
    if t == 0x43:  # an office plundered: the town
        return [h(rnd.randrange(towns), 0, 0, 0)]
    if t == 0x48:  # a Hanse mission: rank, first, family, kind 0..2, the town, ships, the leader's first and family
        return [bytes([rnd.randrange(8), rnd.randrange(80) | woman, rnd.randrange(86), kind, rnd.randrange(towns), ships,
                       rnd.randrange(80), rnd.randrange(86)])
                for kind in range(3) for ships, woman in ((0, 0), (1, 0x80), (2, 0))]
    if t == 0x4A:  # an acquittal: family, first (| 0x80), the names [+0xA], [+0xB], the town, the charge 0..8
        return [bytes([rnd.randrange(86), rnd.randrange(80) | (0x80 if charge & 1 else 0), rnd.randrange(86),
                       rnd.randrange(80)]) + struct.pack('<HH', rnd.randrange(towns), charge) for charge in range(10)]
    if t == 0x4B:  # a verdict: the sentence 0..2, a first name, fine / ware / town, amount, charge 0..9, names, town
        out = []
        for charge in range(10):
            sentence = charge % 3
            a = (rnd.randrange(1, 200), rnd.randrange(20), rnd.randrange(towns))[sentence]
            out.append(bytes([sentence, rnd.randrange(80), a & 0xFF, rnd.randrange(1, 60), charge, rnd.randrange(80),
                              rnd.randrange(86), rnd.randrange(towns) if charge < 3 else rnd.randrange(86)]))
        return out
    if t in (0x4D, 0x4E, 0x4F, 0x50, 0x51, 0x52):
        # A captain's letters: his first and family names (u16), a ship's
        # name or the player's first name [+0xC], a wage or the player's
        # family [+0xE] (as bytes: [+0xE], [+0xF] the player's family and first).
        if t in (0x4F, 0x51):  # [+0xE] a family name (u16)
            return [h(rnd.randrange(80) | k, rnd.randrange(86) | k, rnd.randrange(80), rnd.randrange(86)) for k in (0, 1)]
        return [h(rnd.randrange(80) | k, rnd.randrange(86) | k, rnd.randrange(300), rnd.randrange(80) << 8 | rnd.randrange(86))
                for k in (0, 1)]
    if t in (0x5A, 0x5B):  # a delivery: a bonus per unit, the ware, the amount, family, first (| 0x80)
        return [struct.pack('<HHHBB', bonus, ware, rnd.randrange(1, 90), rnd.randrange(86), rnd.randrange(80) | woman)
                for bonus, ware, woman in ((rnd.randrange(1, 50), 0, 0), (0, 3, 0x80), (rnd.randrange(1, 50), 23, 0))]
    if t == 0x5C:  # a boycott ended: first (past 0x7F a woman's), family, the town (+ 0x100: a blockade), a ship lent
        return [h(first, rnd.randrange(86), town, lent) for first, town, lent in
                ((rnd.randrange(80), rnd.randrange(towns), 0), (0x80 | rnd.randrange(80), 0x100 | rnd.randrange(towns), 1))]
    if t == 0x5D:  # a ship bought at an auction: family, first, its name, its type, the price / 10
        return [struct.pack('<BBHHH', rnd.randrange(86), rnd.randrange(80), rnd.randrange(300), rnd.randrange(4),
                            rnd.randrange(1, 6000))]
    if t == 0x5E:
        return [h(0, 0, 0, 0)]
    if t == 0x60:
        return [h(0, 0, 0, 0)]
    if t in (0x64, 0x65):  # the prince: five amounts (some 0), the day, the month 1..12, a flag
        return [bytes([rnd.randrange(30) * (k & 1), rnd.randrange(30) * (k >> 1 & 1), rnd.randrange(30) * (k >> 2 & 1),
                       rnd.randrange(30) * (k >> 3 & 1), rnd.randrange(30) * (k >> 4 & 1), rnd.randrange(1, 29),
                       rnd.randrange(1, 13), rnd.choice((0, 0xFE))]) for k in (1, 2, 3, 0x1F, 0x15, 0x0E, 0x10)]
    if t in (0x66, 0x67, 0x68):  # cart convoys: two towns (the same, for 0x68's other text)
        a = rnd.randrange(towns)
        return [h(a, rnd.randrange(towns), 0, 0), h(a, a, 0, 0)]
    if t == 0x69:
        return [h(0, 0, 0, 0)]
    if t == 0x6A:  # a town plundered: the captain's first and family, the town, the ship's name, the share (s32)
        return [bytes([rnd.randrange(80), rnd.randrange(86), rnd.randrange(towns), rnd.randrange(256)]) +
                struct.pack('<i', rnd.randrange(1, 90000))]
    if t == 0x6B:  # the council: an amount or the new and old tax rates, the votes against and for, the proposal 0..3
        return [struct.pack('<IBBH', amount, against, yes, proposal) for proposal in range(4) for against, yes in ((2, 7), (7, 2))
                for amount in ((rnd.randrange(1, 9000),) if proposal == 0 else (rnd.randrange(1, 9) | rnd.randrange(1, 9) << 8,))]
    if t == 0x6C:  # the extra tax: the player's part (sign bit: a shortfall), the total
        return [struct.pack('<II', part, rnd.randrange(1, 90000)) for part in (rnd.randrange(1, 9000), 0x80000000 | rnd.randrange(1, 9000))]
    if t in (0x73, 0x75):  # a theft or fire: three amount | ware << 8 (0x1800: none), family, first
        def lost():
            return rnd.randrange(1, 50) | rnd.randrange(24) << 8
        return [struct.pack('<HHHBB', a, b, c, rnd.randrange(86), rnd.randrange(80))
                for a, b, c in ((lost(), lost(), lost()), (0x1800, lost(), 0xFFFF), (0x1800, 0x1800, 0x1800))]
    if t == 0x74:  # a break-in: family, first (| 0x80), rank (+ 0x100: hired), the culprit's family (| 0x8000), first, rank, adjective
        return [bytes([rnd.randrange(86), rnd.randrange(80) | woman, rnd.randrange(8), hired, rnd.randrange(86),
                       rnd.randrange(80) | (0x80 if hired else 0), rnd.randrange(8), rnd.randrange(5)])
                for woman, hired in ((0, 0), (0x80, 1))]
    if t == 0x80:  # an overland route: the Alderman's first (| 0x80) and family, two towns, a merchant, the day, year · 12 + month
        return [bytes([rnd.randrange(80) | woman, rnd.randrange(86), rnd.randrange(towns), rnd.randrange(towns),
                       rnd.randrange(merchants), rnd.randrange(1, 29)]) +
                struct.pack('<H', (1300 + rnd.randrange(60)) * 12 + rnd.randrange(12)) for woman in (0, 0x80)]
    if t == 0x82:  # a respected trader: the adjective, the rank, first (| 0x80), family
        return [h(rnd.randrange(10), rnd.randrange(8), rnd.randrange(80) | woman, rnd.randrange(86)) for woman in (0, 0x80)]
    if t == 0x84:  # a Hanse order: adjective, rank, family, first (| 0x80), the day, month 0..11, the year
        return [bytes([rnd.randrange(10), rnd.randrange(8), rnd.randrange(86), rnd.randrange(80) | woman,
                       rnd.randrange(1, 29), rnd.randrange(12)]) + struct.pack('<H', 1300 + rnd.randrange(60))
                for woman in (0, 0x80)]
    if t in (0x76, 0x7D, 0x7E, 0x7F):  # a building at an auction: first (| 0x80), family, the other's names, the building, the price
        return [bytes([rnd.randrange(80) | woman, rnd.randrange(86), rnd.randrange(80), rnd.randrange(86), 0,
                       rnd.randrange(0x39)]) + struct.pack('<H', rnd.randrange(1, 900)) for woman in (0, 0x80)]
    if t == 0x6D:  # the council's invitation: the Alderman's adjective, first (| 0x80), family; the mayor's adjective, first, family
        return [bytes([rnd.randrange(5), rnd.randrange(80) | a, rnd.randrange(86), rnd.randrange(10), rnd.randrange(80) | m,
                       rnd.randrange(86), 0, 0]) for a, m in ((0, 0), (0x80, 0x80))]
    if t in (0x6F, 0x70):  # a Hanse task: the task (| 0x80 candidacy), first (| 0x80), family, the Alderman's first and family, two towns or names
        return [bytes([task | cand, 0, rnd.randrange(80) | woman, rnd.randrange(86), rnd.randrange(80) | alder,
                       rnd.randrange(86), rnd.randrange(towns), rnd.randrange(towns)])
                for task, cand, woman, alder in ((0, 0, 0, 0), (1, 0x80, 0x80, 0x80), (2, 0, 0, 0), (3, 0x80, 0, 0), (4, 0, 0x80, 0))]
    if t == 0x77:  # the Hanse leader: a town (or past them), the issue, names, closing 0..3, first (| 0x80), family, kind 0..2
        return [bytes([town, issue, rnd.randrange(24) if issue == 0 else rnd.randrange(86), rnd.randrange(86) | closing << 8 & 0,
                       rnd.randrange(80) | woman, rnd.randrange(86), kind, 0])
                for kind, issue, town, closing, woman in ((0, 0, rnd.randrange(towns), 0, 0), (0, 1, rnd.randrange(towns), 1, 0),
                                                          (0, 2, rnd.randrange(towns), 2, 0), (0, 3, rnd.randrange(towns), 3, 0),
                                                          (0, 4, rnd.randrange(towns), 0, 0), (0, 0, 0xFE, 1, 0),
                                                          (0, 5, rnd.randrange(towns), 0, 0x80), (0, 6, rnd.randrange(towns), 0, 0),
                                                          (1, 0, 0, 0, 0), (2, 0, 0, 0, 0))]
    if t == 0x78:  # a Hanse Day: day, month 1..12, a town, subject << 4 | reason, a ware, adjective, first (| 0x80), family
        return [bytes([rnd.randrange(1, 29), rnd.randrange(1, 13), rnd.randrange(towns), subject << 4 | reason,
                       rnd.randrange(24), rnd.randrange(5), rnd.randrange(80) | woman, rnd.randrange(86)])
                for subject, reason, woman in ((0, 0, 0), (1, 0, 0x80), (2, 1, 0), (3, 2, 0), (4, 3, 0x80), (5, 4, 0),
                                               (1, 5, 0), (2, 6, 0))]
    if t == 0x79:  # a debtor: the debt's low half, an event (none), rank | high bits, his family, the player's first (| 0x80) and family
        return [struct.pack('<HHBBBB', rnd.randrange(65536), 0xFFFF, rnd.randrange(8) | high << 6, rnd.randrange(86),
                            rnd.randrange(80) | woman, rnd.randrange(86)) for high, woman in ((0, 0), (3, 0x80), (1, 0))]
    if t == 0x7A:  # a child: its first name (| 0x80 a girl)
        return [h(rnd.randrange(80) | girl, 0, 0, 0) for girl in (0, 0x80)]
    if t == 0x7B:  # a monument: the adjective, the first name (| 0x80 a woman), the family name
        return [h(rnd.randrange(5), rnd.randrange(80) | woman, rnd.randrange(86), 0) for woman in (0, 0x80)]
    if t == 0x7C:  # a manager's experience
        return [h(rnd.randrange(1, 9), 0, 0, 0)]
    if t == 0x81:
        return [h(0, 0, 0, 0)]
    if t == 0x85:  # the prince's payment: the text, the payment's low and high halves
        return [h(k, rnd.randrange(65536), 0, 0) for k in range(2)]  # 2 texts (the 3rd slot is null)
    if t == 0x61:  # an office's capacity
        return [h(rnd.randrange(1, 900), 0, 0, 0)]
    if t == 0x62:  # the company's value (event 0x18): family | first << 8 or 0xFFFF, the rise's halves, the shares
        return [h(holder, rnd.randrange(3), rnd.randrange(65536), rnd.randrange(1, 100))
                for holder in (0xFFFF, rnd.randrange(80) << 8 | rnd.randrange(86), (0x80 | rnd.randrange(80)) << 8 | rnd.randrange(86))]
    if t == 0x49:  # the guards' bill: the mayor's first (past 0x7F a woman's) and family, the share (s32)
        return [struct.pack('<HHi', first, rnd.randrange(86), rnd.randrange(-500, 9000))
                for first in (rnd.randrange(80), 0x80 | rnd.randrange(80))]
    if t == 0x4C:  # a mission not joined: rank, first, family, kind 0..2, the town, ships
        return [bytes([rnd.randrange(8), rnd.randrange(80) | woman, rnd.randrange(86), kind, rnd.randrange(towns), ships,
                       0, 0]) for kind, woman, ships in ((0, 0, 0), (1, 0x80, 0), (2, 0, 1), (2, 0x80, 3))]
    if t == 0x2B:  # too late: the names, an amount
        return [h(rnd.randrange(80), rnd.randrange(86), rnd.randrange(1, 9000), 0)]
    if t == 0x20:  # a burglary in an office: the wares' bits, the goods destroyed, the gold stolen
        return [h(0, wares, goods, gold) for wares, goods, gold in
                ((0, 0, 0), (0, 0, rnd.randrange(1, 9000)), (rnd.randrange(1, 0x10000), rnd.randrange(1, 90), 0),
                 (rnd.randrange(1, 0x10000), rnd.randrange(1, 90), rnd.randrange(1, 9000)))]
    if t == 0x21:  # a suitor: the go-between's names, the sum, the offer's last day of the year
        return [h(rnd.randrange(80), rnd.randrange(86), rnd.randrange(1, 9000), day) for day in (rnd.randrange(365), 360)]
    if t == 0x22:  # a proposal: the go-between's names, a town, the other's names, the dowry, a day
        return [bytes([rnd.randrange(80), rnd.randrange(86), 0, rnd.randrange(256), rnd.randrange(80) | sex,
                       rnd.randrange(86), rnd.randrange(256), rnd.randrange(28)]) for sex in (0, 0x80)]
    if t == 0x18:  # the traveller's thanks: 0..3, an amount, the town
        return [h(k, rnd.randrange(1, 900), rnd.randrange(towns), 0) for k in range(3)]  # 3 texts (the 4th slot is null)
    return []


def all_letters(o, rec):
    import random
    import retouch_unchecked as ru
    m = o.u32(to.PLAYER)
    mm = o.merchant(m)
    for k in (0x38, 0x39, 0x3A):
        w8(o, mm + k, 0)  # every class kept forever
    rnd = random.Random(int(os.environ.get('LETTERS_SEED', '1')))
    towns, _, merchants, ships = o.counts()
    captains = o.u16(0x6DD892)
    letter = o.alloc(0x10)
    sent = 0
    for t in LETTER_TYPES:
        # Three letters of each type, their fields [+0x8..+0xF]: small bytes;
        # small u16; small u16 at [+0x8] and [+0xA] and a random s32 at [+0xC]
        # (a bill, a profit or a loss). Then those of real_fields.
        letters = []
        for variant in range(3):
            if variant == 0:
                letters.append(bytes(rnd.randrange(4) for _ in range(8)))
            elif variant == 1:
                letters.append(struct.pack('<4H', *(rnd.randrange(4) for _ in range(4))))
            else:
                letters.append(struct.pack('<2Hi', rnd.randrange(4), rnd.randrange(4), rnd.randrange(-50000, 50000)))
        letters += real_fields(t, rnd, towns, ships, captains, merchants)
        for fields in letters:
            body = bytes([0, 0x80, 0xFF, 0xFF, t, rnd.randrange(towns), 0xFF, 0xFF]) + fields
            o.uc.mem_write(letter, body)
            ru.direct(o, 0x4D6530, [m, letter], ecx=to.MAIL)
            sent += 1
    print(f'{sent} letters of {len(LETTER_TYPES)} types sent to the player {m}')


def all_news(o, rec):
    """The chronicle's 21 cases (0x47DD40) and their branches: news sent to −2
    with fields that reach each branch (a man and a woman, a merchant past the
    merchants, the pirates' wins and losses, the siege's three ends, the Hanse
    tasks' five kinds and one past them...), a few of the types without a line,
    and LETTERS_SEED random ones of each case."""
    import random
    import retouch_unchecked as ru
    towns, _, merchants, ships = o.counts()
    rnd = random.Random(int(os.environ.get('LETTERS_SEED', '1')))
    man = next(k for k in range(merchants) if o.u8(o.merchant(k) + 0x1A) != 0)
    woman = next((k for k in range(merchants) if o.u8(o.merchant(k) + 0x1A) == 0), man)
    # The bit 0x80 of [+0x1C] the other way round, for the chronicle's 0x4F6420 to write it.
    w8(o, o.merchant(man) + 0x1C, o.u8(o.merchant(man) + 0x1C) | 0x80)
    w8(o, o.merchant(woman) + 0x1C, o.u8(o.merchant(woman) + 0x1C) & 0x7F)
    letter = o.alloc(0x10)
    news = []

    def b(*v):
        return bytes(v) + bytes(8 - len(v))

    def h(*v):
        return struct.pack('<4H', *(list(v) + [0] * (4 - len(v))))

    for m in (man, woman):
        news += [(0x06, h(1, 3, m, 5)), (0x06, h(2, 7, m, 20)), (0x0C, h(m, 4, 2)), (0x0D, h(m, 3, 0, 0x35)),
                 (0x0E, h(m, 2, 0x1D)), (0x0E, h(m, 0xFFFF, 0x0A)), (0x1C, b(m, 5)), (0x1C, b(m, 0x20)),
                 (0x23, h(m, 3 | 5 << 8, 2, 17)), (0x56, h(m, 9, 3))]
    news += [(0x06, h(1, 3, merchants + 2, 5)), (0x0C, h(merchants, 1, 1)), (0x0D, h(merchants + 1, 1, 0, 0x11)),
             (0x0E, h(merchants, 1, 1)), (0x1C, b(min(merchants + 1, 255), 3))]
    for sex in (0, 1):
        for lost in (0, 0xFF, 1, 3):
            news.append((0x2A, b(sex, 4, 3, 7, 9, 11, 13, lost)))
        news.append((0x57, b(rnd.randrange(towns), sex, 5, 6, 8, 4)))
    news += [(0x0B, h(rnd.randrange(towns))), (0x35, b()), (0x36, b()), (0x53, h(1250)), (0x58, b()), (0x59, b()),
             (0x38, struct.pack('<HHHH', 19, 0xFFFF, 0xFFFF, 0)), (0x38, struct.pack('<HIH', 23, 74000, 6)),
             (0x38, struct.pack('<HIH', 8, 3_000_000_000, 0)),
             (0x39, b(2, 3, 0xFF, 0xFF, 0, 0)), (0x39, b(2, 3, 9, 4, 0, 0)), (0x39, b(2, 3, 0x88, 4, 0, 0)),
             (0x39, b(5, 4, 9, 4, 6, 1)), (0x39, b(5, 4, 0x88, 4, 6, 1)),
             (0x3A, struct.pack('<I4B', 12000, 0x25, 9, 4, 0)), (0x3A, struct.pack('<I4B', 12000, 0x25, 0x88, 4, 0)),
             (0x3A, struct.pack('<I4B', 12000, 0x25, 0xFF, 4, 0)), (0x3A, struct.pack('<I4B', 0x01009C40, 0xA5, 9, 4, 2)),
             (0x3A, struct.pack('<I4B', 50000, 0xC3, 0x88, 4, 2)),
             (0x55, h(4, 6, 40, 119)), (0x7B, h(5, 12, 7)), (0x7B, h(3, 0x84, 7)),
             (0x08, b(1, 2)), (0x0F, b(3)), (0x72, h(1, 2))]
    for kind in (0, 1, 2, 3, 4, 5, 0x82):
        for first in (6, 0x86):
            news.append((0x6F, b(kind, 3 + (5 << 5), first, 9, 2, 3, rnd.randrange(towns), rnd.randrange(towns))))
    # Random ones with small fields (0..3, also the u16 ones: a town, a table's
    # index or a name past its table makes the game read garbage and crash).
    for _ in range(int(os.environ.get('NEWS_RANDOM', '2'))):
        for t in (0x06, 0x0B, 0x0C, 0x0D, 0x0E, 0x1C, 0x23, 0x2A, 0x38, 0x39, 0x3A, 0x53, 0x55, 0x56, 0x57, 0x6F, 0x7B):
            m = rnd.choice((man, woman))
            fields = bytes(rnd.randrange(4) for _ in range(8))
            if t in (0x06, 0x0B, 0x0C, 0x0D, 0x0E, 0x23, 0x38, 0x53, 0x55, 0x56, 0x7B):
                fields = h(*(rnd.randrange(4) for _ in range(4)))
            if t == 0x06:
                fields = fields[:4] + struct.pack('<H', m) + fields[6:]
            elif t in (0x0C, 0x0D, 0x0E, 0x23, 0x56):
                fields = struct.pack('<H', m) + fields[2:]
            elif t == 0x1C:
                fields = bytes([m]) + fields[1:]
            elif t == 0x57:
                fields = fields[:1] + bytes([rnd.randrange(2)]) + fields[2:]  # the sex: the titles' half
            news.append((t, fields))
    for t, fields in news:
        body = bytes([0, 0x80, 0xFF, 0xFF, t, rnd.randrange(towns), 0xFF, 0xFF]) + fields
        o.uc.mem_write(letter, body)
        ru.direct(o, 0x4D6530, [0xFFFFFFFE, letter], ecx=to.MAIL)
    print(f'{len(news)} news sent to the chronicle (the man {man}, the woman {woman})')


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
                                     operation_letters, diet_letters, all_letters, read_delete, all_news, ticker_lines,
                                     ticker_ships, ticker_shipyard, ship_sunk)}


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
        if os.environ.get('PLAYER_RANK'):
            # The player's rank in every town (the Hanse days, the votes and
            # the councils summon the ranks from 4 on: their ticker lines).
            pm = o.merchant(o.u32(to.PLAYER))
            for t in range(o.counts()[0]):
                w8(o, pm + 0x39C + t, int(os.environ['PLAYER_RANK']))
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
