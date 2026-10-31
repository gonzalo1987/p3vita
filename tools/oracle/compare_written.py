"""Compares two uncompressed .pat files byte for byte, section by section: the
one the game writes (write_oracle.py, town_oracle.py --write) and the one
p3data writes (p3save write, tick_probe --write).

The game writes some of its pointers into the file (the route of a ship, the
buffers of some notices and events...). In the emulator they are heap
addresses (or, if the oracle didn't set them again, the ones of the process
that saved the original file) and p3data writes 1 (or 0) instead: a
difference inside a u32 that is 1 in p3data's file and above 0xFFFF in the
game's, or 0 in p3data's and an emulator address in the game's, is counted
apart as a pointer, not as a difference.

usage: python3 compare_written.py <game.raw> <p3vita.raw> [--max N]
Exit status 0 if there are no differences other than pointers.
"""
import struct
import sys

SEPARATOR = b'\xfe\xaf\x00\x00'
NAMES = ['header', 'world', 'ships', 'mailbox', 'records 0xDC', 'events', 'battles', 'names']


def looks_like_pointer(v):
    return 0x0100_0000 <= v < 0x0110_0000 or 0x0400_0000 <= v < 0x0500_0000 or 0x1000_0000 <= v < 0x1800_0000


def sections(d):
    """(start, end) of the 8 sections: the header up to the first separator
    (0x5C) and then the last 7 separators of the file."""
    seps = []
    pos = len(d) - 4
    while pos > 0x5C and len(seps) < 7:
        if d[pos:pos + 4] == SEPARATOR:
            seps.append(pos)
            pos -= 4
        else:
            pos -= 1
    seps = [0x5C] + sorted(seps)
    return [(0, 0x5C)] + [(seps[k] + 4, seps[k + 1]) for k in range(len(seps) - 1)]


def compare(game, ours, limit=20):
    differences = pointers = 0
    shown = 0
    by_section = {}
    for name, (g0, g1), (o0, o1) in zip(NAMES, sections(game), sections(ours)):
        a, b = game[g0:g1], ours[o0:o1]
        if len(a) != len(b):
            print(f'{name}: {len(a)} bytes in the game, {len(b)} in p3vita')
        i = 0
        n = min(len(a), len(b))
        while i < n:
            if a[i] == b[i]:
                i += 1
                continue
            # A pointer: some u32 that covers byte i is an address in the game and 0 or 1 in p3vita.
            pointer_at = None
            for j in range(max(0, i - 3), min(i, n - 4) + 1):
                va, vb = struct.unpack_from('<I', a, j)[0], struct.unpack_from('<I', b, j)[0]
                if (vb == 1 and va > 0xFFFF) or (vb == 0 and looks_like_pointer(va)):
                    pointer_at = j
                    break
            if pointer_at is not None:
                pointers += 1
                by_section[name] = by_section.get(name, 0) + 1
                i = pointer_at + 4
                continue
            differences += 1
            if shown < limit:
                shown += 1
                print(f'{name}+{i:#x} (file {g0 + i:#x}): game {a[i:i + 8].hex()} p3vita {b[i:i + 8].hex()}')
            i += 1
    if by_section:
        print('pointers: ' + ', '.join(f'{k} {v}' for k, v in by_section.items()))
    return differences, pointers


def main():
    args = sys.argv[1:]
    limit = 20
    if '--max' in args:
        k = args.index('--max')
        limit = int(args[k + 1])
        del args[k:k + 2]
    if len(args) != 2:
        print(__doc__)
        sys.exit(2)
    game, ours = open(args[0], 'rb').read(), open(args[1], 'rb').read()
    differences, pointers = compare(game, ours, limit)
    print(f'{len(game)} and {len(ours)} bytes: {differences} different, {pointers} pointers')
    sys.exit(1 if differences or len(game) != len(ours) else 0)


if __name__ == '__main__':
    main()
