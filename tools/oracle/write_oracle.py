"""Savegame writer oracle: runs in Unicorn the code of Patrician3.exe that saves
a game (docs/cloud/savegames.md) on a state loaded like town_oracle.py does,
and writes the uncompressed .pat (and, with --packed, the compressed one) as
the game would write it. tools/p3save (`p3save write`) and tick_probe --write
write the same from p3data's state; compare them byte for byte.

The game's save function is 0x5472F0. What runs here, in its order:
  0x4D6780(i)  on the mailbox 0x6DD730 for each merchant i (expires notices)
  0x549C60     size of the .pat
  0x549CC0     writes the .pat (version 0x85) on the game object 0x6DF2F0
  0x54C410     compresses it (thiscall on the object built by 0x54C3F0)
The .pst (0x54D7F0, 0x54D870 on 0x6DFC90) isn't written: p3data doesn't
read the statistics.

usage: python3 write_oracle.py <Patrician3.exe> <save.raw> <out.raw> [--packed OUT] [--cpr p2arch0_eng.cpr]
       [--no-expire]
--no-expire skips 0x4D6780. With town_oracle.py --write OUT the same is
written after the days it runs.
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from save_oracle import FILE  # noqa: E402

GAME = 0x6DF2F0  # the object of 0x549F40 and 0x5472F0
MAILBOX = 0x6DD730
EXPIRE_MAIL = 0x4D6780  # thiscall on the mailbox, (merchant)
PAT_SIZE = 0x549C60
WRITE_PAT = 0x549CC0  # thiscall on GAME, (out) → end
COMPRESSOR_INIT = 0x54C3F0  # thiscall, builds the compressor's object
COMPRESS = 0x54C410  # thiscall, (src, dst, size) → compressed size
MERCHANT_COUNT = 0x6DE4AA
BATTLES = 0x6E55D0
READ_BATTLES = 0x611AE0  # thiscall on BATTLES, (data, version) → end
SEPARATOR_BYTES = b'\xfe\xaf\x00\x00'


def prepare_game_object(o):
    """What 0x549F40 reads and town_oracle.py doesn't: the three u32 after the
    settings in [+0x8D4], [+0x8D8] (2 if it is above 4) and [+0x8DC] of the
    game object 0x6DF2F0 (with [+0x900] cleared, 0x14 bytes), and the battles
    (0x611AE0 on 0x6E55D0, the third section from the end)."""
    o.uc.mem_write(GAME + 0x900, bytes(0x14))
    a, b, c = struct.unpack_from('<III', o.save, 4 + 0x4C)
    if b > 4:
        b = 2
    o.uc.mem_write(GAME + 0x8D4, struct.pack('<III', a, b, c))
    separators = []
    pos = len(o.save) - 4
    while pos > 0 and len(separators) < 3:
        if o.save[pos:pos + 4] == SEPARATOR_BYTES:
            separators.append(pos)
        pos -= 1
    start, end = separators[2] + 4, separators[1]
    version = struct.unpack_from('<I', o.save, 0)[0] >> 16
    got = o.call(READ_BATTLES, [FILE + start, version], ecx=BATTLES) - FILE
    if got != end:
        raise RuntimeError(f'the battles end at {got}, not at {end}')


def game_save(o, expire=True):
    """Runs the save code on the loaded state; returns (raw, packed)."""
    if expire:
        for i in range(o.u16(MERCHANT_COUNT)):
            o.call(EXPIRE_MAIL, [i], ecx=MAILBOX)
    size = o.call(PAT_SIZE, [], ecx=GAME)
    out = o.alloc(size + 0x10)
    end = o.call(WRITE_PAT, [out], ecx=GAME)
    if end - out != size:
        raise RuntimeError(f'0x549CC0 wrote {end - out} bytes and 0x549C60 said {size}')
    raw = bytes(o.uc.mem_read(out, size))
    compressor = o.alloc(0x40)
    o.call(COMPRESSOR_INIT, [], ecx=compressor)
    packed_buffer = o.alloc(size + 4 + 0x10)
    n = o.call(COMPRESS, [out, packed_buffer, size], ecx=compressor)
    packed = bytes(o.uc.mem_read(packed_buffer, n))
    return raw, packed


def write_files(o, out, packed_out=None, expire=True):
    raw, packed = game_save(o, expire)
    with open(out, 'wb') as f:
        f.write(raw)
    if packed_out:
        with open(packed_out, 'wb') as f:
            f.write(packed)
    return raw, packed


def main():
    from town_oracle import TownOracle
    p = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    p.add_argument('exe')
    p.add_argument('save')
    p.add_argument('out')
    p.add_argument('--packed', help='also the compressed .pat')
    p.add_argument('--cpr', help='p2arch0_eng.cpr, for the names object (scripts/Namen*.txt)')
    p.add_argument('--no-expire', action='store_true', help="don't run 0x4D6780 before writing")
    a = p.parse_args()
    o = TownOracle(a.exe, open(a.save, 'rb').read(), cpr=a.cpr)
    o.load()
    prepare_game_object(o)
    raw, packed = write_files(o, a.out, a.packed, not a.no_expire)
    print(f'{len(raw)} bytes in {a.out}' + (f', {len(packed)} compressed in {a.packed}' if a.packed else ''))


if __name__ == '__main__':
    main()
