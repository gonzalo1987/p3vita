"""Compares the ticker lines of the game (ticker_oracle.py) and of p3vita
(p3save ... ticker), pair by pair: the same text, or the game's code failing
(an argument past its table: not compared), or different (shown decoded).

usage: python3 compare_ticker.py <game.txt> <p3vita.txt> [--max N]
Exit status 0 if no compared pair differs.
"""
import argparse


def read(path):
    out = []
    for line in open(path):
        kind, e, text = line.rstrip('\n').split('\t')
        out.append((int(kind, 16), int(e), text))
    return out


def show(text):
    return text if text in ('-', '!') else bytes.fromhex(text).decode('cp1252', 'replace').replace('\n', '\\n')


def main():
    p = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    p.add_argument('game')
    p.add_argument('p3vita')
    p.add_argument('--max', type=int, default=8)
    a = p.parse_args()
    game, ours = read(a.game), read(a.p3vita)
    if [(k, e) for k, e, _ in game] != [(k, e) for k, e, _ in ours]:
        print('the pairs differ')
        return 1
    failed = [g for g in game if g[2] == '!']
    different = [(g, o) for g, o in zip(game, ours) if g[2] != '!' and g[2] != o[2]]
    shown = sum(1 for g in game if g[2] not in ('-', '!'))
    print(f'{len(game)} pairs, {shown} shown, {len(failed)} failed in the game, {len(different)} different')
    for g, o in different[:a.max]:
        print(f'  kind {g[0]:#04x} e {g[1]}:')
        print(f'    game:   {show(g[2])}')
        print(f'    p3vita: {show(o[2])}')
    return 1 if different else 0


if __name__ == '__main__':
    raise SystemExit(main())
