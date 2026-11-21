#include "town_sounds.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <SDL.h>

#include "audio.hpp"
#include "p3data/sound.hpp"

namespace p3::app {

bool tile_offers_sound(uint32_t type) {
	switch (type) {
	// The default step 0x552C10 (re/emu/sesion51/soundclasses.py: each class's tables from the factory 0x54F7E0).
	case 0x01: case 0x02: case 0x03: case 0x04: case 0x05: case 0x07: case 0x08: case 0x09: case 0x0B: case 0x15:
	case 0x29: case 0x2A: case 0x2C: case 0x2D: case 0x2E: case 0x33: case 0x50: case 0x51: case 0x55: case 0x56:
	case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x5E: case 0x5F: case 0x61: case 0x62:
	case 0x63: case 0x65: case 0x66: case 0x67: case 0x68: case 0x69:
	// Their own steps that offer it too: the fisherman's hut, the foundry, the sawmill.
	case 0x57: case 0x60: case 0x64:
		return true;
	default:
		return false;
	}
}

void TownSounds::configure(const std::string& iso_ini) {
	const char* const s = "LIGHTNINGVIEW0";
	list_base_ = p3::data::ini_int(iso_ini, s, "GebaeudeSoundListBase", 0);
	per_type_ = p3::data::ini_int(iso_ini, s, "SoundsProGebaeude", 0);
	min_distance_ = p3::data::ini_int(iso_ini, s, "GebaeudeMinDist", 0);
	pan_distance_ = p3::data::ini_int(iso_ini, s, "PanDist", 0);
	volume_distance_ = p3::data::ini_int(iso_ini, s, "VolDist", 0);
	type_wait_ = uint32_t(p3::data::ini_int(iso_ini, s, "GebaeudeTypLockTime", 0));
	slot_wait_ = uint32_t(p3::data::ini_int(iso_ini, s, "GebaeudeKanalLockTime", 0));
	collapse_list_ = p3::data::ini_int(iso_ini, s, "BuildingCollapseList", 0);
	ship_select_ = p3::data::ini_int(iso_ini, s, "ShipSelSoundID", 0);
	ship_in_out_list_ = p3::data::ini_int(iso_ini, s, "ShipInOutSoundList", 0);
	church_list_ = p3::data::ini_int(iso_ini, s, "ChurchSoundList", 0);
	seed_ = SDL_GetTicks() | 1;
}

void TownSounds::load_lists() {
	sound_.load_list(ship_in_out_list_);
	sound_.load_list(church_list_);
	for (int type = 0; type < kTypes; ++type) {
		sound_.load_list(list_base_ + per_type_ * type);
	}
	sound_.load_list(collapse_list_);
	for (int list = 60000; list <= 60100; list += 10) {
		sound_.load_list(list);
	}
	for (int list = 61000; list <= 61100; list += 10) {
		sound_.load_list(list);
	}
}

int TownSounds::rand15() {
	seed_ = seed_ * 214013u + 2531011u;
	return int((seed_ >> 16) & 0x7FFF);
}

void TownSounds::set_view(int x, int y, int width, int height) {
	view_x_ = x;
	view_y_ = y;
	view_w_ = width;
	view_h_ = height;
}

void TownSounds::volume_and_pan(int x, int y, int& volume, int& pan) const {
	const int dx = x - view_w_ / 2 - view_x_;
	const int dy = y - view_h_ / 2 - view_y_;
	const uint32_t d2 = uint32_t(dy * dy + dx * dx);
	const uint32_t far = uint32_t(volume_distance_ * volume_distance_);
	const uint32_t lost = far != 0 ? (d2 * 127u) / far : 127;
	volume = lost >= 127 ? 0 : int(127 - lost);
	pan = std::clamp(pan_distance_ != 0 ? dx * 64 / pan_distance_ + 64 : 64, 0, 127);
}

void TownSounds::offer(int x, int y, int type, uint32_t now) {
	if (type < 0 || type >= kTypes || sound_.list_variants(list_base_ + per_type_ * type) == 0) {
		return;
	}
	// ([+0x56C] with a type other than 0 keeps the last slot for the fire's: not set in the viewer.)
	const int usable = kBuildingSlots;
	if (type_until_[type] > now) {
		return;
	}
	int free = 0;
	while (free < usable && slots_[free].taken) {
		++free;
	}
	if (free == kBuildingSlots) {
		return;
	}
	const int dx = std::abs(x - (view_w_ / 2 + view_x_));
	const int dy = std::abs(y - (view_h_ / 2 + view_y_));
	const uint32_t d2 = uint32_t(dy * dy + dx * dx);
	if (d2 > uint32_t(min_distance_ * min_distance_)) {
		return;
	}
	auto take = [&](Slot& s) {
		s.distance = d2;
		s.x = x;
		s.y = y;
		s.type = type;
		s.taken = true;
	};
	if (type != 0) {
		// An offer waiting to start: the nearer of the two.
		for (int k = 0; k < usable; ++k) {
			Slot& s = slots_[k];
			if (s.channel < 0 && s.taken) {
				if (s.distance > d2) {
					take(s);
				}
				return;
			}
		}
	}
	for (int k = 0; k < usable; ++k) {
		if (slots_[k].channel < 0 && !slots_[k].taken) {
			take(slots_[k]);
			return;
		}
	}
	for (int k = 0; k < usable; ++k) {
		if (slots_[k].channel < 0 && slots_[k].distance > d2) {
			take(slots_[k]);
			return;
		}
	}
}

void TownSounds::start(uint32_t now) {
	for (int k = 0; k < kBuildingSlots; ++k) {
		Slot& s = slots_[k];
		if (!s.taken || s.channel >= 0) {
			continue;
		}
		const int list = list_base_ + per_type_ * s.type;
		const int variants = sound_.list_variants(list);
		const int variant = variants > 1 ? rand15() % variants : 0;
		int volume = 0;
		int pan = 64;
		volume_and_pan(s.x, s.y, volume, pan);
		s.channel = sound_.play_sample(list + variant, true, true, volume, pan, false, false);
		if (s.channel < 0) {
			s = Slot{};
			continue;
		}
		const uint32_t end = now + sound_.sample_ms(s.channel);
		s.end = end;
		if (s.type != 0) {
			// One random number for both waits: the slot's and the type's, each ± its half.
			const uint32_t r = uint32_t(rand15());
			s.end += r % slot_wait_ + slot_wait_ / 2;
			type_until_[s.type] = end + r % type_wait_ + type_wait_ / 2;
		}
	}
}

void TownSounds::update(uint32_t now) {
	for (Slot& s : slots_) {
		if (s.taken && now > s.end && s.channel >= 0 && sound_.sample_done(s.channel)) {
			sound_.release_sample(s.channel);
			s = Slot{};
		}
	}
	if (view_x_ == moved_x_ && view_y_ == moved_y_) {
		return;
	}
	moved_x_ = view_x_;
	moved_y_ = view_y_;
	for (const Slot& s : slots_) {
		if (s.channel >= 0) {
			int volume = 0;
			int pan = 64;
			volume_and_pan(s.x, s.y, volume, pan);
			sound_.set_sample_volume(s.channel, volume);
			sound_.set_sample_pan(s.channel, pan);
		}
	}
}

void TownSounds::ship_in_out(int x, int y, bool leaving, uint32_t now) {
	int volume = 0;
	int pan = 64;
	volume_and_pan(x, y, volume, pan);
	if (volume == 0) {
		return;
	}
	const int kind = leaving ? 1 : 0;
	int free = -1;
	for (int k = 14; k < kSlots; ++k) {
		const Slot& s = slots_[k];
		if (!s.taken) {
			free = k;  // (The last free one.)
		} else if (s.type == kind && s.end == now + sound_.sample_ms(s.channel)) {
			return;  // The same one, started just now.
		}
	}
	if (free < 0) {
		return;
	}
	Slot& s = slots_[free];
	s.channel = sound_.play_sample(ship_in_out_list_ + kind, true, true, volume, pan, false, false);
	if (s.channel < 0) {
		s.channel = -1;
		return;
	}
	s.end = now + sound_.sample_ms(s.channel);
	s.x = x;
	s.y = y;
	s.taken = true;
	s.type = kind;
}

void TownSounds::bells() {
	const std::time_t t = std::time(nullptr);
	const std::tm* local = std::localtime(&t);
	if (!local) {
		return;
	}
	int hour = local->tm_hour;
	int minute = local->tm_min;
	// (On the PC, P3VITA_SOUND_CLOCK=HH:MM in the environment stands for the clock, to check the bells.)
	if (const char* clock = SDL_getenv("P3VITA_SOUND_CLOCK")) {
		hour = std::atoi(clock);
		const char* colon = std::strchr(clock, ':');
		minute = colon ? std::atoi(colon + 1) : 0;
	}
	if (minute == 30) {
		if (half_hour_ != hour) {
			sound_.play_sample(church_list_ + 1, false, false, -1, 0x40, false, false);
			half_hour_ = hour;
		}
	} else if (minute == 0 && hour_ != hour) {
		hour_ = hour;
		strikes_ = hour > 12 ? hour - 12 : hour;
	}
	if (strikes_ > 0) {
		if (bell_channel_ < 0 || sound_.sample_done(bell_channel_)) {
			sound_.release_sample(bell_channel_);
			bell_channel_ = sound_.play_sample(church_list_, false, true, -1, 0x40, false, false);
			--strikes_;
		}
	} else if (bell_channel_ >= 0) {
		sound_.release_sample(bell_channel_);
	}
}

void TownSounds::reset() {
	std::fill(std::begin(type_until_), std::end(type_until_), 0u);
	for (Slot& s : slots_) {
		s = Slot{};
	}
}

}  // namespace p3::app
