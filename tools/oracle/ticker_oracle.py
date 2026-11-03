"""The game's ticker lines (docs/formats/mail.md, §6): runs 0x5483D0 (on the
real-time object 0x6DF2F0) in the emulator for every kind 0..0x2A, with
arguments taken from the loaded world (the player's ships and
others in port and at sea, a ship past the ships, towns and a town past
them, small numbers, a land transport's two towns), and takes the text it
hands to the interface (0x5487BA: the CString copy at [esp]); then it skips
the interface's call (0x42B4C0 on *0x6CBB40). A kind that shows nothing never
gets there.

usage: python3 ticker_oracle.py <Patrician3.exe> <save.raw> <out.txt> --cpr p2arch0_eng.cpr
       [--barcos DATA] [--spanish SPANISH_EXE]
Each line of the output: kind, argument, and the text in hex (- for none, !
when the game's code failed: an argument past its table).
p3save ... ticker EXE OUT.txt prints p3vita's for the same pairs
(scripts/ticker-check.sh compares them).
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import town_oracle as to  # noqa: E402

TICKER = 0x5483D0  # thiscall on REALTIME, (kind, e), ret 8
SHOWN = 0x5487BA  # the line, a CString at [esp], is handed to the interface next
SKIP = 0x5487C5  # past the interface's call (esp + 4: its argument)
REALTIME = 0x6DF2F0


def pairs(o):
    """The (kind, argument) pairs: by what each kind reads."""
    towns, _, _, ships = o.counts()
    player = o.u32(to.PLAYER)
    own, docked, sailing, destined = [], [], [], []
    s = o.u16(o.merchant(player) + 0xE)
    while s < ships and len(own) < 4:
        own.append(s)
        s = o.u16(o.ship(s) + 4)
    for s in range(ships):
        (docked if o.u8(o.ship(s) + 0x39) < towns else sailing).append(s)
        if o.u8(o.ship(s) + 0x38) < towns:
            destined.append(s)
    chosen = sorted(set(own + docked[:3] + sailing[:3] + destined[:3] + [ships - 1, ships, ships + 7]))
    town_args = sorted({0, 1, 2, towns // 2, towns - 1, towns, towns + 3})
    out = []
    for kind in range(0x2B):
        if kind in (0x00, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x0A, 0x0B, 0x0E, 0x22, 0x23, 0x24, 0x25, 0x28):
            args = chosen
        elif kind == 0x16:
            args = chosen + [0, 1, 5, 300, 0xFFFF]
        elif kind == 0x1D:
            args = [0, 1, 2, 7]
        elif kind == 0x29:
            args = [a | b << 8 for a, b in ((0, 1), (2, 0), (1, towns - 1), (towns + 2, 3))]
        else:
            args = town_args
        out += [(kind, e & 0xFFFF) for e in args]
    return out


def compose(o, todo, run=None):
    from unicorn import UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EIP, UC_X86_REG_ESP
    run = run or o
    code = run.code if run is not o else (lambda a: a)
    uc = run.uc
    captured = {}

    def shown(uc, address, size, user_data):
        esp = uc.reg_read(UC_X86_REG_ESP)
        p = run.u32(esp)
        text = bytearray()
        while p and len(text) < 2000:
            c = run.u8(p + len(text))
            if c == 0:
                break
            text.append(c)
        captured['text'] = bytes(text)
        uc.reg_write(UC_X86_REG_ESP, esp + 4)
        uc.reg_write(UC_X86_REG_EIP, code(SKIP))

    uc.hook_add(UC_HOOK_CODE, shown, begin=code(SHOWN), end=code(SHOWN))
    uc.ctl_remove_cache(code(SHOWN), code(SHOWN) + 1)
    realtime = REALTIME if run is o else REALTIME + 0x21FA0  # spanish_exe.STATE_RUNS
    out = []
    for kind, e in todo:
        captured.clear()
        try:
            run.call(code(TICKER), [kind, e], ecx=realtime)
            out.append((kind, e, captured.get('text')))
        except RuntimeError:
            out.append((kind, e, False))  # The game read a bad pointer (an argument past its table).
    return out


def main():
    p = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    p.add_argument('exe')
    p.add_argument('save')
    p.add_argument('out')
    p.add_argument('--cpr', required=True)
    p.add_argument('--barcos', metavar='DATA')
    p.add_argument('--spanish', metavar='EXE', help="the Spanish Patrician3.exe (0x5488A0)")
    a = p.parse_args()
    o = to.TownOracle(a.exe, open(a.save, 'rb').read(), 1, a.barcos, cpr=a.cpr)
    o.load()
    run = None
    if a.spanish:
        import spanish_exe
        run = spanish_exe.SpanishExe(o, a.spanish)
    lines = compose(o, pairs(o), run)
    with open(a.out, 'w') as f:
        for kind, e, text in lines:
            f.write(f'{kind:#04x}\t{e}\t{"-" if text is None else "!" if text is False else text.hex()}\n')
    print(f'{len(lines)} lines, {sum(1 for *_, t in lines if t is not None)} shown, in {a.out}')
    if run is not None and run.reads_outside:
        print(f'the Spanish code read {len(run.reads_outside)} globals not copied:', file=sys.stderr)
        for address, eip in sorted(run.reads_outside.items())[:40]:
            print(f'  {address:#x} (eip {eip:#x})', file=sys.stderr)


if __name__ == '__main__':
    main()
