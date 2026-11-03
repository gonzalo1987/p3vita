"""The game's chronicle (docs/formats/mail.md, §4): runs 0x47DD40 in the
emulator, which composes the news list (the letters sent to −2, from
[0x6DD738]) into one buffer for a text window, and takes the buffer when it
is complete (0x47E57B: the buffer at [esp+0x1C], its last byte a space at
edi, then a 0); then it jumps to the function's end, which frees it
(0x47E599), past the window.

usage: python3 chronicle_oracle.py <Patrician3.exe> <save.raw> <out.txt> --cpr p2arch0_eng.cpr
       [--barcos DATA] [--spanish SPANISH_EXE]
The output: a line with the text's bytes in hex (Windows-1252 with the game's
control codes, the final space included), and a line «changed M:V ...» with
the merchants whose [+0x1C] it changed (0x4F6420) and the new values. scripts/chronicle-check.sh compares
it with p3vita's (p3save ... chronicle). With --spanish, the Spanish exe's
chronicle (0x47DDE0) on the world the English exe loads (spanish_exe.py).
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import town_oracle as to  # noqa: E402

CHRONICLE = 0x47DD40
DONE = 0x47E57B  # The text is complete: the buffer at [esp+0x1C], edi + 1 bytes (two pushes below).
EXIT = 0x47E599  # Frees the buffer and returns (with esp back over the two pushes).


def compose(o, run=None):
    """The chronicle of the world in `o` (the English oracle), composed by
    `run` (o itself, or a spanish_exe.SpanishExe made from it)."""
    from unicorn import UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EDI, UC_X86_REG_EIP, UC_X86_REG_ESP
    run = run or o
    code = run.code if run is not o else (lambda a: a)
    uc = run.uc
    captured = {}

    def done(uc, address, size, user_data):
        esp = uc.reg_read(UC_X86_REG_ESP)
        buffer = run.u32(esp + 0x1C)
        captured['text'] = bytes(uc.mem_read(buffer, uc.reg_read(UC_X86_REG_EDI) + 1))
        uc.reg_write(UC_X86_REG_ESP, esp + 8)
        uc.reg_write(UC_X86_REG_EIP, code(EXIT))

    uc.hook_add(UC_HOOK_CODE, done, begin=code(DONE), end=code(DONE))
    uc.ctl_remove_cache(code(DONE), code(DONE) + 1)
    merchants = o.counts()[2]
    before = [run.u8(o.merchant(m) + 0x1C) for m in range(merchants)]
    run.call(code(CHRONICLE), [], ecx=0)
    # What it wrote into the merchants (0x4F6420: the bit 0x80 of [+0x1C]).
    compose.changed = [(m, run.u8(o.merchant(m) + 0x1C)) for m in range(merchants)
                       if run.u8(o.merchant(m) + 0x1C) != before[m]]
    return captured.get('text')


def main():
    p = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    p.add_argument('exe')
    p.add_argument('save')
    p.add_argument('out')
    p.add_argument('--cpr', required=True)
    p.add_argument('--barcos', metavar='DATA')
    p.add_argument('--spanish', metavar='EXE', help="the Spanish Patrician3.exe: its chronicle")
    a = p.parse_args()
    o = to.TownOracle(a.exe, open(a.save, 'rb').read(), 1, a.barcos, cpr=a.cpr)
    o.load()
    run = None
    if a.spanish:
        import spanish_exe
        run = spanish_exe.SpanishExe(o, a.spanish)
    text = compose(o, run)
    with open(a.out, 'w') as f:
        f.write(('-' if text is None else text.hex()) + '\n')
        f.write('changed ' + ' '.join(f'{m}:{v:#04x}' for m, v in compose.changed) + '\n')
    count, head = o.u16(to.MAIL + 6), o.u16(to.MAIL + 8)
    news, seen = 0, set()
    while head < count and head not in seen:
        seen.add(head)
        news += 1
        head = o.u16(o.u32(to.MAIL) + 16 * head + 6)
    print(f'{news} news, {len(text) if text else 0} bytes in {a.out}')
    if run is not None and run.reads_outside:
        print(f'the Spanish code read {len(run.reads_outside)} globals not copied:', file=sys.stderr)
        for address, eip in sorted(run.reads_outside.items())[:40]:
            print(f'  {address:#x} (eip {eip:#x})', file=sys.stderr)


if __name__ == '__main__':
    main()
