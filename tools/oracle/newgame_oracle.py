"""New game oracle: runs in Unicorn the code of Patrician3.exe that starts a new
game (roadmap, phase 2, «Partida nueva desde cero»; re/emu/sesion24/new_game_notes.md)
and writes the uncompressed .pat the game would save right after it.

What the new game window does when the player starts (0x499252..0x49985C),
in its order:
  the settings *0x6CC3E8 (0x4C bytes; here from --settings-from, a test save's,
      with the year, the home town and the rest given below);
  the setup object (0x25C bytes, 0x544830) with the map (0x5448A0 reads
      Maps/<name>.gmf; here the same fields are written from the .gmf);
  0x547F10: srand(time) (here --seed, the oracle's rand) and 0x544E50 on the
      setup object, which runs the new game's operations at once (0x535760:
      0xB7, 0xAF each town, 0xB0, 0xB1, 0xAC, 0x7E, 0xAD, 0xB4, 0xB3) and
      queues 0xC4 (advance_time to 70 days later, and a week more each 5 years
      after 1300);
  queued after it: 0xA7 (the player), 0xC8 (the game speed), 0xAA and 0xAB
      (the player's names).
--stop says where to stop and write: `ops` (after 0x544E50), `advance`
(after the 0xC4) or `player` (after 0xA7, 0xAA and 0xAB; the default).

--probe OUT records, for tick_probe, the calls to --functions (by default
535760, each operation 0x544E50 runs) while 0x544E50 runs, as wide samples
(«T»: 24 u32 of the stack, where the operation is).

usage: python3 newgame_oracle.py <Patrician3.exe> DATOS <p2arch0_eng.cpr> <out.raw> [--seed N] [--year Y]
       [--home I] [--settings-from SAVE.raw] [--map FILE.gmf] [--stop ops|advance|player]
       [--ships N] [--female] [--first NAME] [--family NAME] [--probe OUT [--functions 535760,...]]
DATOS: a folder with navdata/ and scripts/ (re/emu/sesion6/run.sh makes /tmp/datos).
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from town_oracle import (ARCHIVE_FREE, ARCHIVE_READ, EMPTY_CSTRING, EXECUTOR_MAGIC, EXECUTOR_TRAMPOLINE,  # noqa: E402
                         EXPEDITIONS, INI_INT, INI_STRING, LOAD_NAMES, NAMES, OPERATION_SIZE, REST,
                         SETTINGS_POINTER, SETTINGS_SIZE, TOWN_TABLE, TOWN_TABLE_SIZE, WORLD, SENTINEL, Recorder,
                         TownOracle, lz_decompress, trampoline)
from unicorn import UC_HOOK_CODE  # noqa: E402
from write_oracle import write_files  # noqa: E402

NEW_GAME = 0x544E50  # thiscall on the setup object
SETUP_SIZE = 0x25C
SETUP_CONSTRUCTOR = 0x544830
SETUP_NAMES = 0x1BC  # 40 CStrings
TOWN_SLOTS = 40
# 0x544E50 is called through this (call; ret) so that it returns to a real
# instruction and the Recorder sees its end (at SENTINEL the emulator stops).
NEW_GAME_TRAMPOLINE = SENTINEL + 0x200
GAME = 0x6DF2F0  # the game object: [+0] 1 in a game of one player (0x54B230), | 0x10 once started
LOCAL_PLAYER = 0x6DF31C  # u32: the local player's number in the session (0 alone)


def parse_gmf(raw):
    """A decompressed .gmf as 0x5448A0 reads it: (n, efficient[n], inefficient[n],
    flags[n], numbers[n], templates[n], names[40])."""
    n = struct.unpack_from('<I', raw, 0)[0]
    p = 4
    if struct.unpack_from('<H', raw, p)[0] == 0xAFFE:
        length = struct.unpack_from('<H', raw, p + 2)[0]
        p += 4 + length
    if n > TOWN_SLOTS:
        raise RuntimeError(f'{n} towns in the map')
    efficient = struct.unpack_from(f'<{n}I', raw, p)
    p += 4 * n
    inefficient = struct.unpack_from(f'<{n}I', raw, p)
    p += 4 * n
    flags, numbers, templates = raw[p:p + n], raw[p + n:p + 2 * n], raw[p + 2 * n:p + 3 * n]
    p += 3 * n
    names = []
    for _ in range(TOWN_SLOTS):
        end = raw.index(b'\0', p)
        names.append(raw[p:end])
        p = end + 1
    return n, efficient, inefficient, bytes(flags), bytes(numbers), bytes(templates), names


class NewGameOracle(TownOracle):
    def __init__(self, exe, navdata, cpr, seed):
        # With the missions: the 0xA7 queues 0xC0, which starts them (missions_addon/, from the CPR).
        super().__init__(exe, b'\0' * 0x100, seed, navdata, cpr=cpr, rest=list(REST), statistics=True,
                         missions=True)

    def startup(self, settings):
        """What the game has before the new game window starts one: the settings,
        the names object with the towns' table (0x5121C0 at the game's start,
        0x51E440 from scripts/StadtDaten.ini) and the expeditions' points."""
        block = self.alloc(0x100)
        self.uc.mem_write(block, settings)
        self.write_u32(SETTINGS_POINTER, block)
        self.write_u32(GAME, 1)  # 0x54B230(1, 0, ...): one player
        hooks = [self.uc.hook_add(UC_HOOK_CODE, f, begin=a, end=a) for a, f in (
            (ARCHIVE_READ, self._archive_read), (ARCHIVE_FREE, self._archive_free), (INI_INT, self._ini_int),
            (INI_STRING, self._ini_string)) if a != INI_STRING or not self.missions]  # (missions: hooked already)
        for i in range(TOWN_SLOTS):
            self.write_u32(TOWN_TABLE + i * TOWN_TABLE_SIZE + 0x28, self.u32(EMPTY_CSTRING))
        self.call(LOAD_NAMES, [], ecx=NAMES)
        for h in hooks:
            self.uc.hook_del(h)
        expeditions = self.game_file('scripts/Expeditionen.dat')
        if expeditions is not None:
            table = self.alloc(len(expeditions))
            self.uc.mem_write(table, expeditions)
            self.write_u32(EXPEDITIONS, table)
            self.write_u32(EXPEDITIONS + 4, (len(expeditions) >> 2) & 0xFFFF)

    def cstring_object(self, text):
        """A CString (MFC 6) holding `text`: its data block {refs, length, size}
        and the characters, the pointer after the block."""
        block = self.alloc(12 + len(text) + 1)
        self.uc.mem_write(block, struct.pack('<iii', 1, len(text), len(text)) + text + b'\0')
        return block + 12

    def setup_object(self, gmf):
        n, efficient, inefficient, flags, numbers, templates, names = parse_gmf(gmf)
        setup = self.alloc(SETUP_SIZE)
        self.call(SETUP_CONSTRUCTOR, [], ecx=setup)
        self.write_u32(setup, n)
        self.uc.mem_write(setup + 0x4, struct.pack(f'<{n}I', *efficient))
        self.uc.mem_write(setup + 0xA4, struct.pack(f'<{n}I', *inefficient))
        self.uc.mem_write(setup + 0x144, flags)
        self.uc.mem_write(setup + 0x16C, numbers)
        self.uc.mem_write(setup + 0x194, templates)
        for i, name in enumerate(names):
            if name:
                self.write_u32(setup + SETUP_NAMES + 4 * i, self.cstring_object(name))
        return setup

    def rand(self):
        """rand() from Python, on the oracle's state (the window's own calls)."""
        self.rand_state = (self.rand_state * 0x343FD + 0x269EC3) & 0xFFFFFFFF
        return (self.rand_state >> 16) & 0x7FFF

    def execute(self, op):
        """An operation through the executor, as the oracle runs the queued ones."""
        buffer = self.alloc(OPERATION_SIZE)
        self.uc.mem_write(buffer, op)
        self.call(EXECUTOR_TRAMPOLINE, list(struct.unpack('<5I', op)) + [EXECUTOR_MAGIC], ecx=buffer)

    def window(self, settings, ships, male, first, family, stop):
        """What the window does after 0x544E50 (0x499600..0x49985C): the 0xA7 with
        two rand(), the queue (the 0xC4: advance_time to its target with the
        days' operations held, the money's cap; the 0xA7; the 0xC8 is the
        real-time loop's), the player's names (0xAA, 0xAB: 12 characters, then
        11 at a time) and the next frame's queue: the held operations, the 0xA7's
        and the names."""
        c4 = next(q for q in self.queued if struct.unpack_from('<I', q)[0] == 0xC4)
        self.tick_operations = []
        a7 = bytearray(OPERATION_SIZE)
        struct.pack_into('<II', a7, 0, 0xA7, self.u32(LOCAL_PLAYER))
        a7[8], a7[9], a7[0xA], a7[0xB] = settings[0xE], settings[0x1E], settings[0x9], ships
        a7[0xC] = 1 if male else 0
        a7[0xD] = (self.rand() % 0x16D) & 0xFF  # the remainder's low byte (dl)
        struct.pack_into('<H', a7, 0xE, self.rand())
        a7[0x10] = settings[0x11]
        self.write_u32(GAME, self.u32(GAME) | 0x10)
        target = struct.unpack_from('<I', c4, 8)[0]
        while self.u32(WORLD + 0x14) < target:
            self.advance_tick(defer=True)
        self.cap_money()
        if stop == 'advance':
            return
        self.execute(bytes(a7))
        player = self.u32(0x6DFC14)
        for code, name in ((0xAA, first), (0xAB, family)):
            q = bytearray(OPERATION_SIZE)
            struct.pack_into('<IH', q, 0, code, player)
            offset = 0
            for i, ch in enumerate(name):
                q[8 + i - offset] = ch
                if i % 11 == 0 and i != 0:
                    self.tick_operations.append(bytes(q))
                    offset = i + 1
                    struct.pack_into('<H', q, 6, offset)
            q[8 + len(name) - offset] = 0
            self.tick_operations.append(bytes(q))
        operations, self.tick_operations = self.tick_operations, []
        for op in operations:
            if struct.unpack_from('<I', op)[0] in self.operations | {0xAA, 0xAB}:
                self.execute(op)

    def new_game(self, setup):
        _, build = trampoline(NEW_GAME, 0)
        self.uc.mem_write(NEW_GAME_TRAMPOLINE, build(NEW_GAME_TRAMPOLINE))
        self.call(NEW_GAME_TRAMPOLINE, [], ecx=setup)


def main():
    p = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    p.add_argument('exe')
    p.add_argument('datos')
    p.add_argument('cpr')
    p.add_argument('out')
    p.add_argument('--seed', type=int, default=1, help="srand's seed (the game's: the time)")
    p.add_argument('--year', type=int, default=1300)
    p.add_argument('--home', type=int, default=None, help="the home town's index (settings [0x1E])")
    p.add_argument('--settings-from', help="a test save's settings (uncompressed .pat); by default 1.pat's")
    p.add_argument('--map', help='a .gmf (compressed); by default maps/Standard24.gmf of the CPR')
    p.add_argument('--stop', choices=('ops', 'advance', 'player'), default='player')
    p.add_argument('--ships', type=int, default=2, help="the player's ships (the 0xA7's [+0xB])")
    p.add_argument('--female', action='store_true')
    p.add_argument('--first', default='Johann', help="the player's first name")
    p.add_argument('--family', default='Kaufmann', help="the player's family name")
    p.add_argument('--probe', metavar='OUT', help="tick_probe's samples of the new game's calls")
    p.add_argument('--functions', default='535760', help='with --probe, the functions (hex) recorded')
    a = p.parse_args()
    o = NewGameOracle(a.exe, a.datos, a.cpr, a.seed)
    if a.settings_from:
        settings = bytearray(open(a.settings_from, 'rb').read()[4:4 + SETTINGS_SIZE])
    else:
        settings = bytearray.fromhex(
            '14050000050000000f00010c01030000000000000000000000010d0514050901012f50500301014cfc6265636b'
            '00000000000000000000000000008000000000000000000000000000000000')
    settings[0x3C:0x4C] = bytes(0x10)
    struct.pack_into('<I', settings, 0, a.year)
    if a.home is not None:
        settings[0x1E] = a.home
    o.startup(bytes(settings))
    blob = open(a.map, 'rb').read() if a.map else o.game_file('maps/Standard24.gmf')
    gmf = lz_decompress(blob, struct.unpack_from('<i', blob, 0)[0])
    setup = o.setup_object(gmf)

    def run():
        o.new_game(setup)
        towns, offices, merchants, ships = o.counts()
        print(f'after 0x544E50: {towns} towns, {offices} offices, {merchants} merchants, {ships} ships; '
              f'{len(o.queued)} queued: {[hex(struct.unpack_from("<I", q)[0]) for q in o.queued]}')
        if a.stop != 'ops':
            o.window(settings, a.ships, not a.female, a.first.encode('latin-1'), a.family.encode('latin-1'), a.stop)
            towns, offices, merchants, ships = o.counts()
            print(f'at the end: {towns} towns, {merchants} merchants, {ships} ships, tick {o.u32(WORLD + 0x14)}')
    if a.probe:
        with open(a.probe, 'wb') as f:
            f.write(b'P3PROBE1')
            rec = Recorder(o, [int(x, 16) for x in a.functions.split(',')], f, 1 << 30, wide=True)
            rec.active = True
            run()
            rec.active = False
            f.write(b'E')
        print(f'{rec.count} samples in {a.probe}')
    else:
        run()
    raw, _ = write_files(o, a.out)
    print(f'{len(raw)} bytes in {a.out}')


if __name__ == '__main__':
    main()
