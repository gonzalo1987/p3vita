#include "p3data/building_site.hpp"

namespace p3::data {

void change_site(BuildingSite& site, uint16_t cur, uint16_t total) {
	if (site.cur == cur && site.total == total) {
		return;
	}
	site.total = total;
	site.cur = cur;
	// (0x550660: with the new values; a tenth by 0x66666667, the u16s compared unsigned.)
	if (site.active()) {
		const uint16_t tenth = uint16_t(total / 10);
		site.cur = tenth < site.cur ? uint16_t(site.cur - tenth) : 0;
	}
}

namespace {

// The CRT's rand() (0x639C03), on the viewer's own state.
int site_rand(uint32_t& state) {
	state = state * 214013u + 2531011u;
	return int((state >> 16) & 0x7FFF);
}

// A frame of a flame's loop at random (0x5505F2, 0x550B10: rand() % (frames − loop) + loop, a byte).
int8_t loop_frame(const FlameTable& t, uint32_t& rand_state) {
	const uint32_t span = uint32_t(t.frames - t.loop);
	const uint32_t r = uint32_t(site_rand(rand_state));
	return int8_t(uint8_t((span ? r % span : 0) + uint32_t(t.loop)));
}

}  // namespace

void change_site_fire(BuildingSite& site, uint16_t cur, uint16_t total, const FlameTables& tables,
	uint32_t& rand_state) {
	if (site.cur == cur && site.total == total) {
		return;
	}
	site.total = total;
	site.cur = cur;
	if (site.flames[2] < -1) {
		if (cur >= 50) {
			site.flames = {0, 0, 0, 0};  // (0x5505BD: memset 0; 50 more taken away above it.)
			if (site.cur > 50) {
				site.cur = uint16_t(site.cur - 50);
			}
		} else {
			for (int k = 0; k < 4; ++k) {
				site.flames[std::size_t(k)] = loop_frame(flame_table(tables, k), rand_state);
			}
		}
	} else if (cur == 0) {
		site.flames = {-1, -1, -1, -1};  // (0x550626: burnt out.)
	}
}

bool step_site_flames(BuildingSite& site, uint32_t now, const FlameTables& tables, const std::array<bool, 4>& with_point,
	uint32_t& rand_state, std::vector<FlameSmoke>& smoke) {
	if (!site.active() || site.flames[2] < 0) {
		return false;
	}
	if (now <= site.next) {
		return true;
	}
	for (int k = 0; k < 4; ++k) {
		if (!with_point[std::size_t(k)]) {
			continue;
		}
		const FlameTable& t = flame_table(tables, k);
		int8_t& f = site.flames[std::size_t(k)];
		f = int8_t(f + 1);
		if (f == t.loop - 1) {
			f = loop_frame(t, rand_state);  // (0x550B10: its intro played, into the loop at random.)
		} else if (f >= t.frames) {
			f = int8_t(t.loop);
		}
		const int r = site_rand(rand_state);
		if (f % (13 - (r & 3)) == 0) {
			smoke.push_back({k, r});
		}
	}
	site.next = now + tables[0].ms;
	return true;
}

int site_rows(const BuildingSite& site, int height, int slice_y, int slice_h) {
	if (site.total == 0) {
		return slice_h;  // (Never: the game would divide by 0.)
	}
	// (0x5507B3: imul, cdq, idiv by [+0x6]; then less the picture's top and height, plus the slice's top and height,
	// the slice's rows taken in the picture's own place as p3data keeps them: TileDef::slices.)
	return int(site.cur) * height / int(site.total) + slice_y + slice_h - height;
}

std::vector<uint8_t> site_fade(uint8_t alpha) {
	std::vector<uint8_t> rows;
	// (0x5508E2..0x5509B1: fild, fdiv by 22.0f at 0x673E60, compared with 1.0f at 0x66BC80, each value stored as a
	// float; the alpha by _ftol, a truncation.)
	const float step = float(alpha) / 22.0f;
	for (float a = float(alpha) - step; !(a < 1.0f); a -= step) {
		rows.push_back(uint8_t(int(a)));
	}
	return rows;
}

}  // namespace p3::data
