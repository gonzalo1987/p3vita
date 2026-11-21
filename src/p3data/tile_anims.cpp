#include "p3data/tile_anims.hpp"

#include <algorithm>

namespace p3::data {

namespace {

// The modes (the bytes 0x673D98..0x673D9B).
constexpr uint8_t kWaiting = 0;
constexpr uint8_t kStepped = 1;
constexpr uint8_t kEnded = 2;
constexpr uint8_t kStarted = 3;
constexpr uint8_t kBackwards = 0x80;
constexpr uint8_t kHidden = 0x40;
constexpr uint8_t kStopped = 0x80;

int game_rand(uint32_t& state) {
	state = state * 214013u + 2531011u;
	return int((state >> 16) & 0x7FFF);
}

uint32_t param(const TileVariant& page, size_t k) { return k < page.params.size() ? page.params[k] : 0; }

const TileVariant& page_of(const TileAnimGroup& g) {
	return *g.pages[std::min<size_t>(g.flags & 0x3F, g.pages.size() - 1)];
}

// A page started (its first frame now, the next its parameter 0 later).
void start(TileAnimGroup& g, size_t page, uint32_t now) {
	g.flags = uint8_t(page);
	g.frame = 0;
	g.time = now + param(*g.pages[page], 0);
	g.mode = kStarted;
}

// The steps' common part while running (0x5518CE..0x5519A2 and the same in each): the frames passed since its time,
// forward to the page's last (then backwards with its parameter 1, else ended) or backwards to the first (ended).
// The mode now.
uint8_t advance(TileAnimGroup& g, uint32_t now) {
	const TileVariant& page = page_of(g);
	if (g.mode == kStarted) {
		g.mode = kStepped;
	} else if (now < g.time) {
		g.mode = uint8_t((g.mode & kBackwards) | kWaiting);
	} else {
		const uint32_t ms = std::max<uint32_t>(param(page, 0), 1);
		const uint32_t passed = (now - g.time) / ms + 1;
		g.time = now + ms;
		if (g.mode & kBackwards) {
			if (int64_t(g.frame) - int64_t(passed) < 0) {
				g.frame = 0;
				g.mode = kEnded;
			} else {
				g.frame = uint8_t(g.frame - passed);
				g.mode = kStepped | kBackwards;
			}
		} else if (uint32_t(g.frame) + passed >= page.frames.size()) {
			g.frame = uint8_t(page.frames.size() - 1);
			g.mode = param(page, 1) ? kStepped | kBackwards : kEnded;
		} else {
			g.frame = uint8_t(g.frame + passed);
			g.mode = kStepped;
		}
	}
	return g.mode;
}

// The 7 and 9 parameters' pick (0x54EE81..0x54EED6): the `r`-th page (of those, `matching`) leaving the place where
// it is (their parameter 5).
size_t page_from_place(const TileAnimGroup& g, int r) {
	int seen = 0;
	size_t k = 0;
	for (; k < g.pages.size(); ++k) {
		if (param(*g.pages[k], 5) == g.place && seen++ == r) {
			break;
		}
	}
	return std::min(k, g.pages.size() - 1);
}

}  // namespace

const TileSlice* TileAnimGroup::shown(const TileVariant** page, bool even_hidden) const {
	if (kind == 0 || pages.empty() || ((flags & kHidden) && !even_hidden)) {
		return nullptr;
	}
	const size_t p = flags & 0x3F;
	if (p >= pages.size() || frame >= pages[p]->frames.size()) {
		return nullptr;
	}
	if (page) {
		*page = pages[p];
	}
	return &pages[p]->frames[frame];
}

std::vector<TileAnimGroup> tile_anim_groups(const TileDef& tile) {
	std::vector<TileAnimGroup> groups;
	for (const TileVariant& v : tile.variants) {
		if (v.kind < 3) {
			continue;
		}
		const size_t k = v.kind - 3;
		if (groups.size() <= k) {
			groups.resize(k + 1);
		}
		groups[k].pages.push_back(&v);
	}
	for (TileAnimGroup& g : groups) {
		const size_t count = g.pages.empty() ? 0 : g.pages[0]->params.size();
		g.kind = !g.pages.empty() && (count == 2 || count == 4 || count == 5 || count == 7 || count == 9) ? int(count) : 0;
		for (const TileVariant* p : g.pages) {
			if (p->frames.empty()) {
				g.kind = 0;  // A page without frames: not one to step (the cliffs' and towers' kinds of their own).
			}
		}
	}
	return groups;
}

void step_tile_anim(TileAnimGroup& g, uint32_t now, bool active, uint32_t& rand_state) {
	if (g.kind == 0 || g.pages.empty()) {
		return;
	}
	const size_t count = g.pages.size();
	if (g.flags & kStopped) {
		const TileVariant& page = page_of(g);
		switch (g.kind) {
		case 2:
			// 0x551878: started again while active.
			if (active) {
				start(g, g.flags & 0x3F, now);
			}
			return;
		case 4: {
			// 0x54EBE9: its parameter 2's page, or one at random one time in its parameter 3.
			if (!active) {
				return;
			}
			const uint32_t chance = param(page, 3);
			size_t next = param(page, 2) & 0xFF;
			if (chance && uint32_t(game_rand(rand_state)) % chance == 0) {
				next = size_t(game_rand(rand_state)) % count;
			}
			start(g, std::min(next, count - 1), now);
			return;
		}
		default: {
			// 0x552211, 0x54EE43, 0x552513: after its wait, one time in its parameter 3 a page starts (any, or one
			// leaving its place); else shown again and waiting its parameter 2 more.
			if (g.time > now || !active) {
				return;
			}
			const uint32_t chance = std::max<uint32_t>(param(page, 3), 1);
			const int r = game_rand(rand_state);
			if (uint32_t(r) % chance == 0) {
				size_t next;
				if (g.kind == 5) {
					next = size_t(game_rand(rand_state)) % count;
				} else {
					int matching = 0;
					for (const TileVariant* p : g.pages) {
						matching += param(*p, 5) == g.place;
					}
					if (matching == 0) {
						return;  // The game divides by it.
					}
					next = page_from_place(g, r % matching);
				}
				start(g, next, now);
				if (g.kind == 9) {
					const TileVariant& started = *g.pages[next];
					g.repeats = uint8_t(param(started, 7));
					if (param(started, 8)) {
						g.repeats = uint8_t(g.repeats + uint32_t(game_rand(rand_state)) % param(started, 8));
					}
				}
				return;
			}
			g.flags &= uint8_t(~kHidden);
			g.time = now + param(page, 2);
			return;
		}
		}
	}
	const uint8_t mode = advance(g, now);
	if (mode == 0) {
		return;
	}
	if (!active) {
		g.flags |= kStopped | kHidden;
		return;
	}
	if ((mode & 0x7F) != kEnded) {
		return;
	}
	const TileVariant& page = page_of(g);
	if (g.kind == 9 && --g.repeats != 0) {
		// 0x5527D7: the same page again.
		g.frame = 0;
		g.time = now + param(page, 0);
		g.mode = kStarted;
		return;
	}
	g.flags |= kStopped;
	if (g.kind >= 5) {
		// It waits its parameter 4 (and is at its page's parameter 6's place).
		if (g.kind >= 7) {
			g.place = uint8_t(param(page, 6));
		}
		g.time = now + param(page, 4);
	}
}

bool workshop_active(uint32_t producing, int facility, bool siege, uint8_t grid) {
	if (facility < 0 || facility > 31 || ((producing >> facility) & 1) == 0) {
		return false;
	}
	return !siege || grid > 0x7F;
}

int tile_anim_facility(uint32_t type) {
	switch (type) {
	case 0x5D: return 4;  // the hunting lodge (0x5561A0)
	case 0x58: return 6;  // the brewery (0x554D80)
	case 0x5B: return 7;  // the smithy (0x557200)
	case 0x59: return 8;  // the apiary (0x5560C0)
	case 0x56: return 0xA;  // the cattle farm (0x554A30)
	case 0x55: return 0xF;  // the sheep farm (0x5548F0)
	case 0x5C: return 0x10;  // the vineyard (0x557120)
	case 0x5A: return 0xC;  // the weaving mill (0x556FE0)
	case 0x5E: return 0xD;  // the saltworks (0x556E20)
	case 0x63: return 0x11;  // the pottery (0x556F00)
	case 0x62: return 0x12;  // the quarry (0x5572E0)
	case 0x61: return 0x13;  // the pitch maker (0x556280)
	case 0x02:  // the tavern (its smoke by day, 0x583D60)
	case 0x09:  // the bath house
	case 0x0B:  // the arsenal
	case 0x5F:  // the mine (its left or right .let's data, 0x554B70)
	case 0x01:  // the shipyard (its own drawing: tile_anim_drawing)
	case 0x15:  // the lighthouse (its fire by day, 0x583A60)
		return -1;
	default: return -2;
	}
}

std::vector<TileAnimDraw> tile_anim_drawing(uint32_t type, const std::vector<TileAnimGroup>& groups) {
	std::vector<TileAnimDraw> out;
	if (type == 0x01) {
		if (groups.empty() || !groups[0].shown()) {
			return out;
		}
		for (size_t k = 0; k < groups.size() && k < 3; ++k) {
			out.push_back({k, k > 0});
		}
		return out;
	}
	for (size_t k = 0; k < groups.size(); ++k) {
		out.push_back({k, false});
	}
	return out;
}

}  // namespace p3::data
