#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "p3data/cpr.hpp"
#include "p3data/savegame.hpp"

namespace p3::data {

// Writing a save the way Patrician3.exe does (docs/cloud/savegames.md). The
// game's save function is 0x5472F0: it expires the old notices of each
// merchant (0x4D6780), writes the .pat with 0x549CC0 (version 0x85) and
// compresses it with 0x54C410 (lz_compress, lz_compress.hpp).

// The .pat version the game writes.
constexpr int kWrittenSaveVersion = 0x85;

// What 0x5472F0 does to the state before writing: 0x4D6780 for each
// merchant (expire_merchant_mail).
void prepare_save(SaveGame& save);

// 0x4D6780 (thiscall on the mailbox 0x6DD730, with a merchant): takes out of
// the merchant's list the notices older than their type allows and puts
// them in the free list; their extras (SaveOpaque::mail_extras) go too.
void expire_merchant_mail(SaveGame& save, int merchant);

// The uncompressed .pat as 0x549CC0 writes it. Like the game, it changes
// SaveGame::settings first (date, hometown and its name, rank: see
// docs/cloud/savegames.md §1.2), the owner of the offices of the free list
// (0x52F870) and nothing else. The game's pointers that go into the file are
// written as 1 when they aren't null.
std::vector<uint8_t> write_savegame(SaveGame& save);

// prepare_save, write_savegame and lz_compress: the bytes of the .pat file.
std::vector<uint8_t> savegame_file(SaveGame& save);

// What load_game_scripts reads for the writer: the town names
// (SaveOpaque::town_names, scripts/NamenStaedte_eng.txt).
void load_save_scripts(const CprArchive& archive, SaveGame& save);

}  // namespace p3::data
