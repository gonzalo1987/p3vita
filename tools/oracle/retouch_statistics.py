"""A retouched state for the merchants' statistics objects: records with the
game's code (town_oracle.py, with the ships, the rest of advance_time and the
statistics) the calls that only a bankruptcy makes (0x4F5D80, the operation
0x65, which the saves don't reach): after the first day (which builds the
objects), from Python, 0x54E1F0 on the merchant of the middle object (out of
the list), on a merchant without an object and on one past the number of
merchants (nothing), then 0x54E170 on that one (back, now at the head)
and again (nothing: it has one); then DAYS more days. tick_probe repeats
each call from the game's state just before it.

usage: python3 retouch_statistics.py <Patrician3.exe> <save.raw> <out.probe> DAYS
       --barcos DATA --cpr p2arch0_eng.cpr
It records 0x54E1F0, 0x54E170 and 0x54DE90.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import town_oracle as to  # noqa: E402

REMOVE = 0x54E1F0
ADD = 0x54E170
REMOVE_TRAMPOLINE = to.SENTINEL + 0x190
ADD_TRAMPOLINE = to.SENTINEL + 0x1B0


def objects(o):
    """The merchants of the statistics objects, from the head of the list."""
    out = []
    p = o.u32(to.STATISTICS_OBJECT + 0x1C)
    while p:
        out.append(o.u16(p))
        p = o.u32(p + 0x230C)
    return out


def main():
    p = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    p.add_argument('exe')
    p.add_argument('save')
    p.add_argument('out')
    p.add_argument('days', type=int)
    p.add_argument('--barcos', metavar='DATA', required=True)
    p.add_argument('--cpr', required=True)
    a = p.parse_args()
    o = to.TownOracle(a.exe, open(a.save, 'rb').read(), 1, a.barcos, rest=list(to.REST), cpr=a.cpr,
                      statistics=True)
    o.load()
    for at, target in ((REMOVE_TRAMPOLINE, REMOVE), (ADD_TRAMPOLINE, ADD)):
        _, build = to.trampoline(target, 1)
        o.uc.mem_write(at, build(at))
    with open(a.out, 'wb') as f:
        f.write(b'P3PROBE1')
        rec = to.Recorder(o, [REMOVE, ADD, to.STATISTICS], f, 1 << 30)
        for _ in range(256):
            o.advance_tick()
        rec.active = True
        before = objects(o)
        count = o.u16(to.WORLD + 0xA)
        first = before[len(before) // 2]
        without = next(m for m in range(count) if m not in before)
        for m in (first, without, count + 3):
            o.call(REMOVE_TRAMPOLINE, [m], ecx=to.STATISTICS_OBJECT)
        o.call(ADD_TRAMPOLINE, [first], ecx=to.STATISTICS_OBJECT)
        o.call(ADD_TRAMPOLINE, [first], ecx=to.STATISTICS_OBJECT)
        after = objects(o)
        for _ in range(a.days):
            for _ in range(256):
                o.advance_tick()
        f.write(b'E')
    print(f'{rec.count} samples in {a.out}; merchants {count}, objects {len(before)} -> {len(after)}; '
          f'removed and added again {first} (head {before[:3]} -> {after[:3]}), without an object {without}')


if __name__ == '__main__':
    main()
