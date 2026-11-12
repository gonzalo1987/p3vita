#include "p3data/letter_window.hpp"

#include <cstring>

#include "p3data/game_fixes.hpp"
#include "p3data/game_memory.hpp"
#include "p3data/mail.hpp"
#include "p3data/town_tick/internal.hpp"

namespace p3::data {

namespace {

// 0x672D78: the days before each month, then what follows in the exe (the composer indexes it by the month's nibble).
constexpr uint16_t kMonthStart[16] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365, 0, 31, 28};
constexpr uint32_t kEventCount = 0x6DD748;  // u16 the event queue's records

const GameMemory<0x10>* letter_at(const SaveGame& save, uint16_t entry) {
	return entry < save.globals.u16(kMailCount) && entry < save.mail.size() ? &save.mail[entry] : nullptr;
}

uint32_t tick_of(const SaveGame& save) { return save.globals.u32(global::kWorldTick); }

// The day (from the year 0) of a letter's date, by the month's nibble masked with `mask`.
uint32_t letter_day(const GameMemory<0x10>& e, uint8_t mask) {
	const uint32_t month = e.u8(1) & mask;
	return (month < 16 ? kMonthStart[month] : 0u) + uint32_t(e.u16(2)) * 365 + e.u8(0);
}

// Whether the letter 0x79's event [+0xA] is the loan still to decide (0x485D2B..0x485D52).
bool loan_to_decide(const SaveGame& save, const GameMemory<0x10>& e) {
	const uint16_t event = e.u16(0xA);
	if (event >= save.globals.u16(kEventCount) || event >= save.events.size()) {
		return false;
	}
	const GameMemory<0x18>& loan = save.events[event];
	const uint16_t merchants = save.globals.u16(global::kWorldMerchantCount);
	return loan.u16(6) == 1 && loan.u8(0x11) >= merchants && loan.u8(0x10) < merchants && loan.u8(0x12) == 0;
}

void put32(OperationBytes& op, size_t at, uint32_t v) { std::memcpy(op.data() + at, &v, 4); }
void put16(OperationBytes& op, size_t at, uint16_t v) { std::memcpy(op.data() + at, &v, 2); }

OperationBytes new_operation(uint32_t code) {
	OperationBytes op{};
	put32(op, 0, code);
	return op;
}

}  // namespace

std::vector<std::string> letter_buttons(const SaveGame& save, const GameTexts* texts, uint16_t entry) {
	const GameMemory<0x10>* e = letter_at(save, entry);
	if (!e) {
		return {};
	}
	auto labels = [&](std::initializer_list<uint32_t> english) {
		std::vector<std::string> out;
		for (uint32_t a : english) {
			out.push_back(texts ? texts->raw_at(a) : std::string());
		}
		return out;
	};
	const uint32_t tick = tick_of(save);
	switch (e->u8(4)) {
	case 0x1E:
		return labels({0x6B503C});
	case 0x1F:
		return labels({0x6B5028, 0x6B502C});
	case 0x21:
		if (int32_t(tick) >= int32_t((uint32_t(e->u16(2)) * 365 + e->u16(0xE)) << 8)) {
			return {};
		}
		return labels({0x6B52A0});
	case 0x22:
		// Game bug: the letter's day (1..31) on the days before its month counts a day too many, so the button lasts
		// till the day's end once accepted (0x60 sets [+0xF] to 0) and the proposal can be accepted twice (two
		// weddings). Fixed behind fix_game_bugs() (the user's choice, 2026-11-12): the day counted from 0.
		if (((letter_day(*e, 0xF) - (fix_game_bugs() ? 1 : 0) + e->u8(0xF)) << 8) <= tick) {
			return {};
		}
		return labels({0x6B52A0});
	case 0x3D:
	case 0x3E:
	case 0x3F: {
		// The record [+0x8]: [+0x4] its deadline, [+0xB] the labels' count; the labels follow the title and the text
		// in the buffer (0x482296..0x48236B).
		if (entry >= save.mail_records.size() || !save.mail_records[entry] || entry >= save.mail_buffers.size()) {
			return {};
		}
		const GameMemory<0x10>& record = *save.mail_records[entry];
		if (tick >= record.u32(4) || record.u8(0xB) == 0) {
			return {};
		}
		const std::vector<uint8_t>& b = save.mail_buffers[entry];
		size_t at = 0;
		for (int skip = 0; skip < 2; ++skip) {
			while (at < b.size() && b[at] != 0) {
				++at;
			}
			++at;
		}
		std::vector<std::string> out;
		for (uint32_t k = 0; k < record.u8(0xB); ++k) {
			std::string label;
			while (at < b.size() && b[at] != 0) {
				label += char(b[at++]);
			}
			++at;
			out.push_back(label);
		}
		return out;
	}
	case 0x64:
		if (e->u8(0xF) == 0xFE || (tick & 0xFFFFFF00u) > ((letter_day(*e, 0x7F) + 0xD) << 8)) {
			return {};
		}
		return labels({0x6A9A04});
	case 0x79:
		if (!loan_to_decide(save, *e)) {
			return {};
		}
		return labels({0x6AEBFC, 0x6AEC00});
	default:
		return {};
	}
}

std::optional<OperationBytes> letter_answer(SaveGame& save, uint16_t entry, uint32_t answer) {
	const GameMemory<0x10>* e = letter_at(save, entry);
	if (!e) {
		return std::nullopt;
	}
	GlobalMemory& g = save.globals;
	const uint8_t player = uint8_t(g.u32(global::kPlayer));
	const uint32_t towns = g.u32(global::kWorldTownCount);
	switch (e->u8(4)) {
	case 0x1E:
	case 0x1F: {
		if (e->u8(4) == 0x1E && answer != 0) {
			return std::nullopt;
		}
		OperationBytes op = new_operation(0x75);
		put32(op, 4, e->u32(8));
		put32(op, 8, e->u16(0xC));
		put32(op, 0xC, e->u16(0xE));
		put32(op, 0x10, e->u8(4) == 0x1E ? 0xFFFFFFFFu : answer);
		return op;
	}
	case 0x21: {
		OperationBytes op = new_operation(0x72);
		put16(op, 4, e->u16(0xC));
		put16(op, 6, entry);
		for (size_t k = 0; k < 3; ++k) {
			put16(op, 8 + 2 * k, uint16_t(tick::game_rand(save)));
		}
		op[0xE] = uint8_t((tick::game_rand(save) & 0xFFFF) % towns);
		op[0xF] = player;
		op[0x10] = e->u8(5);
		op[0x11] = e->u8(8);
		op[0x12] = e->u8(0xA);
		op[0x13] = (((uint32_t(e->u16(2)) * 365 + e->u16(0xE)) << 8) | 0xFF) >= g.u32(global::kWorldTick) ? 1 : 0;
		return op;
	}
	case 0x22: {
		OperationBytes op = new_operation(0x60);
		const uint32_t tick = g.u32(global::kWorldTick);
		put32(op, 4, (((tick::game_rand(save) & 7) + 0xE) << 8) + tick);
		put16(op, 8, uint16_t((tick::game_rand(save) & 0x1FFF) + 0x13F6));
		op[0xA] = uint8_t(tick::game_rand(save));
		op[0xB] = e->u8(0xE);
		op[0xC] = e->u8(0xC);
		op[0xD] = e->u8(0xD);
		op[0xE] = uint8_t(e->u8(0xB) % towns);
		op[0xF] = e->u8(0xA);
		op[0x11] = player;
		op[0x10] = uint8_t(tick::game_rand(save) & 3);
		return op;
	}
	case 0x3C:
	case 0x3D:
	case 0x3E:
	case 0x3F: {
		if (entry >= save.mail_records.size() || !save.mail_records[entry]) {
			return std::nullopt;  // The game reads the record's pointer without looking.
		}
		const GameMemory<0x10>& record = *save.mail_records[entry];
		OperationBytes op = new_operation(0x74);
		put32(op, 4, record.u16(8));
		put32(op, 8, record.u8(0xA));
		put32(op, 0xC, answer);
		put32(op, 0x10, entry);
		return op;
	}
	case 0x64: {
		OperationBytes op = new_operation(0x8C);
		put32(op, 4, e->u8(5));
		put32(op, 8, g.u32(global::kPlayer));
		put32(op, 0xC, e->u8(0xF));
		put32(op, 0x10, entry);
		return op;
	}
	case 0x79: {
		OperationBytes op = new_operation(0x77);
		put32(op, 4, e->u16(0xA));
		put32(op, 8, entry);
		put32(op, 0xC, answer);
		return op;
	}
	default:
		return std::nullopt;
	}
}

std::optional<OperationBytes> letter_shown_operation(const SaveGame& save, uint16_t entry) {
	const GameMemory<0x10>* e = letter_at(save, entry);
	if (!e || e->u8(4) != 0x79 || e->u16(0xA) >= save.globals.u16(kEventCount) || loan_to_decide(save, *e)) {
		return std::nullopt;
	}
	OperationBytes op = new_operation(0x77);
	put32(op, 4, 0xFFFF);
	put32(op, 8, entry);
	put32(op, 0xC, 1);
	return op;
}

}  // namespace p3::data
