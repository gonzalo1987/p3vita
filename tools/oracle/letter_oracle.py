"""The game's text of each letter of a merchant (docs/formats/mail.md, §4): runs
the letter window's composer 0x47F3B0 in the emulator on a fake window whose
list points at each entry of the merchant's chain, and takes the text when it
is complete (0x48654B: the buffer in eax, its length in esi); then it jumps to
the composer's own exit that frees it (0x4865E5), past the window's code.

usage: python3 letter_oracle.py <Patrician3.exe> <save.raw> <out.txt> --cpr p2arch0_eng.cpr
       [--barcos DATA] [--merchant N] [--types 29,47] [--spanish SPANISH_EXE]
Each line of the output: entry index, type, and the text's bytes in hex
(Windows-1252 with the game's control codes, as the window gets them). By
default the human player's chain (0x6DFC14). scripts/letters-check.sh compares
it with p3vita's composer.

With --spanish, the world is loaded with the English exe as always and the
Spanish exe's composer (0x47F450) composes the letters (spanish_exe.py): the
Spanish game's code and texts, the towns' names of the English archive.
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import town_oracle as to  # noqa: E402

COMPOSER = 0x47F3B0
DONE = 0x48654B  # The text is complete: eax the buffer, esi its length.
EXIT = 0x4865E5  # The composer's exit for a type without text: frees the buffers.
# 0x51AC60 (thiscall on the letter) makes the picture of the letters 0x46 and
# 0x6E through the graphics library, which the emulator hasn't: stubbed. It
# returns p3vita's number for the picture (kLetterMapPicture, letters.hpp),
# where the game returns its texture's handle; the window's [+0xB8] keeps it
# and the next picture frees it first (0x4BB490): cleared before each letter.
PICTURE = 0x51AC60
MAP_PICTURE = 1


def compose_all(o, merchant, types=None, run=None):
    """The letters of the merchant's chain in `o` (the English oracle), composed
    by `run` (o itself, or a spanish_exe.SpanishExe made from it)."""
    from unicorn import UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESI, UC_X86_REG_ESP
    run = run or o
    code = run.code if run is not o else (lambda a: a)
    uc = run.uc
    stub = run.alloc(16)
    uc.mem_write(stub, bytes([0xC2, 0x04, 0x00]))  # ret 4: the control's method [+0x34]
    vtable = run.alloc(0x40)
    uc.mem_write(vtable + 0x34, struct.pack('<I', stub))
    rows = run.alloc(0x14)
    window = run.alloc(0x600)
    uc.mem_write(window, bytes(0x600))
    uc.mem_write(window + 0xA0, struct.pack('<I', rows))
    uc.mem_write(window + 0x560, struct.pack('<I', vtable))
    captured = {}

    def done(uc, address, size, user_data):
        buffer = uc.reg_read(UC_X86_REG_EAX)
        length = uc.reg_read(UC_X86_REG_ESI)
        captured['text'] = bytes(uc.mem_read(buffer, length))
        uc.reg_write(UC_X86_REG_EIP, code(EXIT))

    def picture(uc, address, size, user_data):
        captured['picture'] = uc.reg_read(UC_X86_REG_ECX)
        esp = uc.reg_read(UC_X86_REG_ESP)
        uc.reg_write(UC_X86_REG_EAX, MAP_PICTURE)
        uc.reg_write(UC_X86_REG_ESP, esp + 4)
        uc.reg_write(UC_X86_REG_EIP, struct.unpack('<I', uc.mem_read(esp, 4))[0])

    uc.hook_add(UC_HOOK_CODE, done, begin=code(DONE), end=code(DONE))
    uc.ctl_remove_cache(code(DONE), code(DONE) + 1)
    uc.hook_add(UC_HOOK_CODE, picture, begin=code(PICTURE), end=code(PICTURE))
    uc.ctl_remove_cache(code(PICTURE), code(PICTURE) + 1)
    mail, count = o.u32(to.MAIL), o.u16(to.MAIL + 6)
    out = []
    index, seen = o.u16(o.merchant(merchant) + 0xA), 0
    while index < count and seen < count:
        kind = o.u8(mail + 16 * index + 4)
        if types is None or kind in types:
            # The row as the list makes it (0x47C520): the entry, its type,
            # [+0x5], unread, and the date as «DD.MM.YYYY» (type 0x03 uses it).
            entry = bytes(o.uc.mem_read(mail + 16 * index, 16))
            day, month, year = entry[0], (entry[1] & 0xF) + 1, struct.unpack_from('<H', entry, 2)[0]
            date = f'{day // 10 % 10}{day % 10}.{month // 10 % 10}{month % 10}.{year // 1000 % 10}{year // 100 % 10}' \
                   f'{year // 10 % 10}{year % 10}'.encode()
            uc.mem_write(rows, struct.pack('<HHHH', index, kind, entry[5], entry[1] >> 7) + date + bytes(2))
            uc.mem_write(window + 0xD2, struct.pack('<H', 0))
            uc.mem_write(window + 0xB8, bytes(4))
            captured.clear()
            try:
                run.call(code(COMPOSER), [], ecx=window)
                if 'picture' in captured and captured['picture'] != mail + 16 * index:
                    print(f'entry {index}: the picture made for {captured["picture"]:#x}, not the letter',
                          file=sys.stderr)
                out.append((index, kind, captured.get('text')))
            except RuntimeError:
                out.append((index, kind, False))  # The game's composer failed (a bad pointer from the fields).
        index, seen = o.u16(mail + 16 * index + 6), seen + 1
    return out


def make_offers(o):
    """--offers: the five offers tools/p3save/main.cpp's offer_recipe makes (the offers' letters 0x1E and 0x1F can't be
    composed from a save, which hasn't the offers' list), run with the game's executor (0x5F). Returns a note."""
    ships, towns, player = o.u16(0x6DD894), o.u32(0x6DE4B0), o.u32(to.PLAYER)
    own = other = None
    i, seen = o.u16(0x6DD888), 0
    while i < ships and seen < ships:
        owner = o.u8(o.ship(i))
        if own is None and owner == player:
            own = i
        elif other is None and owner != player and owner >= towns:
            other = owner
        i, seen = o.u16(o.ship(i) + 6), seen + 1
    if own is None or other is None:
        return 'no ship of the player and of another merchant in the docked list: no offers'
    town = o.u8(o.ship(own) + 0x39)
    for number, price, a, b, kind, what in ((5001, 500, player, other, 2, 1000), (5002, 800, player, other, 0, 3 | 2 << 16),
                                            (5003, 1500, player, other, 1, own), (5004, 300, other, player, 2, 700),
                                            (5005, 0, other, player, 0, 0 | 1 << 16)):
        op = struct.pack('<IIiBBBBI', 0x5F, number, price, a, b, town, kind, what).ljust(to.OPERATION_SIZE, b'\0')
        buffer = o.alloc(to.OPERATION_SIZE)
        o.uc.mem_write(buffer, op)
        o.tick_operations = []
        o.call(to.EXECUTOR_TRAMPOLINE, list(struct.unpack('<5I', op[:0x14])) + [to.EXECUTOR_MAGIC], ecx=buffer)
    return f'offers: the player {player}, merchant {other}, ship {own}, town {town}'


def main():
    p = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    p.add_argument('exe')
    p.add_argument('save')
    p.add_argument('out')
    p.add_argument('--cpr', required=True)
    p.add_argument('--barcos', metavar='DATA')
    p.add_argument('--merchant', type=int)
    p.add_argument('--types', help='only these types (hex, comma-separated)')
    p.add_argument('--spanish', metavar='EXE', help="the Spanish Patrician3.exe: its composer's texts")
    p.add_argument('--offers', action='store_true', help='make five offers first (make_offers), as p3save letters offers')
    a = p.parse_args()
    o = to.TownOracle(a.exe, open(a.save, 'rb').read(), 1, a.barcos, cpr=a.cpr)
    o.load()
    if a.offers:
        print(make_offers(o), file=sys.stderr)
    run = None
    if a.spanish:
        import spanish_exe
        run = spanish_exe.SpanishExe(o, a.spanish)
    merchant = a.merchant if a.merchant is not None else o.u32(to.PLAYER)
    types = {int(t, 16) for t in a.types.split(',')} if a.types else None
    letters = compose_all(o, merchant, types, run)
    with open(a.out, 'w') as f:
        for index, kind, text in letters:
            f.write(f'{index}\t{kind:#04x}\t{"-" if text is None else "!" if text is False else text.hex()}\n')
    with_text = sum(1 for _, _, t in letters if t)
    print(f'merchant {merchant}: {len(letters)} letters, {with_text} with a text, in {a.out}')
    if run is not None and run.reads_outside:
        print(f'the Spanish code read {len(run.reads_outside)} globals not copied:', file=sys.stderr)
        for address, eip in sorted(run.reads_outside.items())[:40]:
            print(f'  {address:#x} (eip {eip:#x})', file=sys.stderr)


if __name__ == '__main__':
    main()
