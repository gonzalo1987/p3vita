"""The Spanish exe's code on the world an English oracle loaded: for the texts
that the Spanish game composes with code of its own (the letters' composer
0x47F3B0 differs in some cases, not only in its texts; docs/oracle.md, «The
Spanish exe»).

The oracle loads saves with the English exe (town_oracle.py). SpanishExe makes
a second emulator with the Spanish Patrician3.exe (GOG 1.1, Spanish) mapped,
copies the English one's heap as it is (the world's objects keep their
addresses) and the game's globals moved to the Spanish layout: both exes have
the same code but their data is laid out differently (the Spanish texts are
longer; a few objects differ in size). STATE_RUNS and CODE come from an
alignment of the two exes' whole code, pairing the addresses each instruction
uses (re/emu/sesion5/mail/s4/data_map.py, outside the repository).

usage (from another script):
    o = town_oracle.TownOracle(english_exe, save, ...); o.load()
    s = SpanishExe(o, spanish_exe)
    s.call(s.code(0x47F3B0), [], ecx=window)   # an English address, translated
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import save_oracle as so  # noqa: E402
import town_oracle as to  # noqa: E402

from unicorn import (UC_ARCH_X86, UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_UNMAPPED, UC_HOOK_MEM_WRITE,  # noqa: E402
                     UC_MODE_32, Uc)

# The game's globals, English [begin, end) and the Spanish address minus the
# English one. Copied in this order (a later run overwrites an earlier one
# where the Spanish layout is shorter: 8 bytes less between 0x6DE3BE and
# 0x6DE3D4). Below 0x6DD5BC the globals are the interface's and the texts'
# buffers (not copied; SpanishExe.reads_outside tells whether the code read
# any of them).
STATE_RUNS = (
    (0x6DD5BC, 0x6DD628, 0x21FA4),
    (0x6DD628, 0x6DE3D4, 0x21FA8),
    (0x6DE3D4, 0x6F1CDC, 0x21FA0),
)
# Globals of the exe's initialized data that the English oracle writes.
STATE_WORDS = (
    (to.SETTINGS_POINTER, -0x10C8),  # 0x6CC3E8: the game's settings
    (so.NAVIGATION, -0x10C8),  # 0x6CBDC8: the navigation object
)
# English code addresses and their Spanish ones (checked byte by byte).
CODE = {
    so.OPERATOR_NEW: 0x64FCE9,  # 0x64F7B9
    so.OPERATOR_DELETE: 0x64FD12,  # 0x64F7E2
    so.MALLOC: 0x63CF51,  # 0x63CB41 (the CRT's functions are in another order: +0x410)
    so.FREE: 0x63A6E6,  # 0x63A1B6
    to.RAND: 0x63A133,  # 0x639C03
    to.ENQUEUE: 0x54AF40,  # 0x54AA70
    0x548CA0: 0x549170,  # a message to the player (QUIET)
    0x47F3B0: 0x47F450,  # the letters' composer
    0x48654B: 0x486639,  # its text complete (letter_oracle.DONE)
    0x4865E5: 0x4866D3,  # its exit without a text (letter_oracle.EXIT)
    0x51AC60: 0x51B110,  # a letter's picture (letter_oracle.PICTURE)
    0x47DD40: 0x47DDE0,  # the chronicle (chronicle_oracle.py)
    0x47E57B: 0x47E61B,  # its text complete (chronicle_oracle.DONE)
    0x47E599: 0x47E639,  # its end, freeing the text (chronicle_oracle.EXIT)
    0x5483D0: 0x5488A0,  # the ticker line (ticker_oracle.py)
    0x5487BA: 0x548C8A,  # its line handed to the interface (ticker_oracle.SHOWN)
    0x5487C5: 0x548C95,  # past the interface's call (ticker_oracle.SKIP)
}
TEXT_INITIALIZERS = (0x46BE60, 0x46C0A0)  # English 0x46BD80, 0x46BFC0
# What the Spanish emulator skips, as the English oracle's QUIET: address: bytes popped.
SKIPPED = {CODE[to.ENQUEUE]: 4, CODE[0x548CA0]: 0x18}
BSS = 0x6CB000  # The Spanish exe's data without contents in the file starts here.


class SpanishExe(to.TownOracle):
    """A second emulator with the Spanish exe and the English oracle's world.
    Not built through TownOracle.__init__: only the pieces that the code of
    the texts needs (imports, allocation, rand)."""

    def __init__(self, english, exe, watch_reads=True):
        data = open(exe, 'rb').read()
        pe = struct.unpack_from('<I', data, 0x3C)[0]
        sections = struct.unpack_from('<H', data, pe + 6)[0]
        optional_size = struct.unpack_from('<H', data, pe + 20)[0]
        base = struct.unpack_from('<I', data, pe + 24 + 28)[0]
        image_size = struct.unpack_from('<I', data, pe + 24 + 56)[0]
        stamp = struct.unpack_from('<I', data, pe + 8)[0]
        uc = self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        uc.mem_map(base, (image_size + 0xFFF) & ~0xFFF)
        uc.mem_write(base, data[:0x1000])
        for i in range(sections):
            o = pe + 24 + optional_size + i * 40
            vsize, va, rsize, raw = struct.unpack_from('<IIII', data, o + 8)
            uc.mem_write(base + va, data[raw:raw + min(rsize, max(vsize, rsize))])
        if bytes(uc.mem_read(CODE[0x47F3B0], 3)) != bytes([0x6A, 0xFF, 0x68]):
            raise RuntimeError(f'{exe} is not the Spanish GOG 1.1 Patrician3.exe (time stamp {stamp:#x})')
        # Everything the English emulator has outside its exe and its imports'
        # area: the page of fs:0, the stack, the sentinel, the heap (up to its
        # top), the save's file...
        image = (base, base + ((english_image_size(english) + 0xFFF) & ~0xFFF))
        imports = (min(english.imports) & ~0xFFF, (max(english.imports) + 0x1000) & ~0xFFF)
        for begin, end, perms in english.uc.mem_regions():
            end += 1
            if image[0] <= begin < image[1] or imports[0] <= begin < imports[1]:
                continue
            uc.mem_map(begin, end - begin, perms)
            top = min(end, english.heap_top) if begin == so.HEAP else end
            for at in range(begin, top, 0x100000):
                uc.mem_write(at, bytes(english.uc.mem_read(at, min(0x100000, top - at))))
        for begin, end, delta in STATE_RUNS:
            uc.mem_write(begin + delta, bytes(english.uc.mem_read(begin, end - begin)))
        for address, delta in STATE_WORDS:
            uc.mem_write(address + delta, bytes(english.uc.mem_read(address, 4)))
        # The English oracle's allocator and state, carried on.
        self.heap_top = english.heap_top
        self.sizes = dict(english.sizes)
        self.free_blocks = {k: list(v) for k, v in english.free_blocks.items()}
        self.requested = dict(english.__dict__.get('requested', {}))
        self.rand_state = english.rand_state
        self.navdata = english.navdata
        self.cpr = english.cpr
        self.skipped = dict(SKIPPED)
        self.save = english.save
        uc.hook_add(UC_HOOK_MEM_UNMAPPED, self._unmapped)
        for address in (CODE[so.OPERATOR_NEW], CODE[so.MALLOC], CODE[so.OPERATOR_DELETE], CODE[so.FREE],
                        *self.skipped):
            uc.hook_add(UC_HOOK_CODE, self._spanish_stub, begin=address, end=address)
        uc.hook_add(UC_HOOK_CODE, self._rand, begin=CODE[to.RAND], end=CODE[to.RAND])
        self.setup_imports(exe, constructors=())
        self.tls = dict(english.tls)
        for f in TEXT_INITIALIZERS:  # The Spanish exe's town_oracle.TEXT_INITIALIZERS.
            self.call(f, [])
        # Reads of the Spanish globals that were not copied (below the runs)
        # before the code wrote them (the CRT's sprintf writes and reads its
        # own there): what the English oracle may hold and the copy misses.
        self.reads_outside = {}
        self.written = set()
        if watch_reads:
            end = STATE_RUNS[0][0] + STATE_RUNS[0][2] - 1
            uc.hook_add(UC_HOOK_MEM_READ, self._read_outside, begin=BSS, end=end)
            uc.hook_add(UC_HOOK_MEM_WRITE, self._write_outside, begin=BSS, end=end)

    @staticmethod
    def code(english):
        """The Spanish address of an English code address in CODE."""
        return CODE[english]

    def _spanish_stub(self, uc, address, size, user_data):
        if address in (CODE[so.OPERATOR_NEW], CODE[so.MALLOC]):
            result, pop = self.alloc(self._arg(0)), 0
        elif address in self.skipped:
            result, pop = 0, self.skipped[address]
        else:
            self.release(self._arg(0))
            result, pop = 0, 0
        self._returns(uc, result, pop)

    def _read_outside(self, uc, access, address, size, value, user_data):
        from unicorn.x86_const import UC_X86_REG_EIP
        if address in self.written or address in (a + d for a, d in STATE_WORDS):
            return
        self.reads_outside.setdefault(address, uc.reg_read(UC_X86_REG_EIP))

    def _write_outside(self, uc, access, address, size, value, user_data):
        self.written.update(range(address, address + size))


def english_image_size(english):
    """The size of the English exe's image in the oracle (its PE header at 0x400000)."""
    pe = english.u32(0x400000 + 0x3C)
    return english.u32(0x400000 + pe + 24 + 56)
