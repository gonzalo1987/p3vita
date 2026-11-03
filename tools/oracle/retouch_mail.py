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


SCENARIOS = {f.__name__: f for f in (grow, promotion)}


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
