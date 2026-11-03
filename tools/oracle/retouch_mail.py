"""Retouched states for the mailbox (docs/formats/mail.md): records like
town_oracle.py --barcos starting from the save changed by a scenario, for the
branches of sending a letter (0x4D6530) the saves don't reach. tick_probe
repeats each call from the game's state just before it (without --continuo:
p3vita doesn't know the retouch).

usage: python3 retouch_mail.py <Patrician3.exe> <save.raw> <out.probe> <days> <scenario>
       --barcos DATA --cpr p2arch0_eng.cpr [--functions F,F...] [--letters connected|all|none]
By default it records 4f7bb0 (the merchant's tick).

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


SCENARIOS = {f.__name__: f for f in (grow, promotion, privateer, reputation, commission, hulk, wall_bill, convoy_fees)}


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


if __name__ == '__main__':
    main()
