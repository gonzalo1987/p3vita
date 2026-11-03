"""Compares the chronicle of the game (chronicle_oracle.py) and of p3vita
(p3save ... chronicle), piece by piece: the title, then each news (the text
between two of the game's ornaments «\\n\\n\\c\\t16063_», with its date's
header when it has one). Prints how many are equal and the first
differences, decoded.

usage: python3 compare_chronicle.py <game.txt> <p3vita.txt> [--max N]
Exit status 0 if they are the same bytes.
"""
import argparse

ORNAMENT = b'\n\n\\c\\t16063_'


def read(path):
    lines = open(path).read().split('\n')
    text = lines[0].strip()
    changed = lines[1].strip() if len(lines) > 1 else ''
    return (None if text == '-' else bytes.fromhex(text)), changed


def show(b):
    return b.decode('cp1252', 'replace').replace('\n', '\\n')


def main():
    p = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    p.add_argument('game')
    p.add_argument('p3vita')
    p.add_argument('--max', type=int, default=6)
    a = p.parse_args()
    (game, game_changed), (ours, our_changed) = read(a.game), read(a.p3vita)
    if game is None:
        print('the game made no chronicle')
        return 1
    if game_changed != our_changed:
        print(f'the merchants written differ: game «{game_changed}», p3vita «{our_changed}»')
    if game == ours:
        print(f'the same: {game.count(ORNAMENT)} news, {len(game)} bytes; {game_changed or "changed nothing"}')
        return 0 if game_changed == our_changed else 1
    g, o = game.split(ORNAMENT), (ours or b'').split(ORNAMENT)
    different = [k for k in range(max(len(g), len(o))) if k >= len(g) or k >= len(o) or g[k] != o[k]]
    print(f'different: {len(different)} of {len(g)} pieces (p3vita {len(o)}); {len(game)} and {len(ours or b"")} bytes')
    for k in different[:a.max]:
        print(f'  piece {k}:')
        print(f'    game:   {show(g[k]) if k < len(g) else "(none)"}')
        print(f'    p3vita: {show(o[k]) if k < len(o) else "(none)"}')
    return 1


if __name__ == '__main__':
    raise SystemExit(main())
