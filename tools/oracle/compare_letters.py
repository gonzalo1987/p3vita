"""Compares the letters' texts of the game (letter_oracle.py) and of p3vita
(p3save ... letters), entry by entry: equal, different (the first
difference shown, decoded) or not rewritten yet in p3vita (?).

usage: python3 compare_letters.py <game.txt> <p3vita.txt> [--max N]
Exit status 0 if no rewritten letter differs.
"""
import argparse
import collections


def read(path):
    out = {}
    with open(path) as f:
        for line in f:
            index, kind, text = line.rstrip('\n').split('\t')
            out[int(index)] = (int(kind, 16), text)
    return out


def show(b):
    return b.decode('cp1252', 'replace').replace('\n', '\\n')


def main():
    p = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    p.add_argument('game')
    p.add_argument('p3vita')
    p.add_argument('--max', type=int, default=6)
    a = p.parse_args()
    game, ours = read(a.game), read(a.p3vita)
    by_type = collections.defaultdict(collections.Counter)
    shown = 0
    for index, (kind, text) in sorted(game.items()):
        mine = ours.get(index)
        if mine is None or mine[0] != kind:
            by_type[kind]['missing'] += 1
            continue
        if text == '!':
            by_type[kind]['failed in the game'] += 1
        elif mine[1] == '?':
            by_type[kind]['not rewritten'] += 1
        elif mine[1] == text:
            by_type[kind]['equal'] += 1
        else:
            by_type[kind]['different'] += 1
            if shown < a.max:
                shown += 1
                g = bytes.fromhex(text) if text != '-' else b''
                m = bytes.fromhex(mine[1]) if mine[1] != '-' else b''
                k = next((i for i in range(min(len(g), len(m))) if g[i] != m[i]), min(len(g), len(m)))
                print(f'entry {index} type {kind:#04x}: first difference at byte {k} ({len(g)} and {len(m)} bytes)')
                print(f'   game:   ...{show(g[max(0, k - 40):k + 60])}')
                print(f'   p3vita: ...{show(m[max(0, k - 40):k + 60])}')
    bad = 0
    for kind in sorted(by_type):
        c = by_type[kind]
        bad += c['different'] + c['missing']
        print(f'{kind:#04x}: ' + ', '.join(f'{n} {name}' for name, n in sorted(c.items())))
    return 1 if bad else 0


if __name__ == '__main__':
    raise SystemExit(main())
