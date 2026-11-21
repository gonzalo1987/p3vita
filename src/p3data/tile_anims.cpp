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
	case 0x64: return 0xB;  // the sawmill (its own step, 0x556860)
	case 0x53: return 9;  // the grain farm (its own step, 0x553AB0)
	case 0x54: return 0x14;  // the hemp farm (its own step, 0x554560)
	case 0x60: return 0xE;  // the foundry (its own step, 0x555190)
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
	case 0x01:  // the shipyard (its own drawing: tile_anim_frames)
	case 0x1B:  // the repair yard (its own step and drawing)
	case 0x15:  // the lighthouse (its fire by day, 0x583A60)
		return -1;
	default: return -2;
	}
}

namespace {

constexpr uint8_t kIdle = 0x3F;  // A worker without a page.

// A worker's page started (as start(): the frame 0, its time its page's parameter 0 from now).
void start_worker(TileAnimGroup& w, size_t page, uint32_t now) {
	if (page < w.pages.size()) {
		start(w, page, now);
	}
}

// A worker's frames on (advance()); whether it ended.
bool worker_ended(TileAnimGroup& w, uint32_t now) {
	if ((w.flags & 0x3F) >= w.pages.size()) {
		return false;
	}
	const uint8_t mode = advance(w, now);
	return mode != 0 && (mode & 0x7F) == kEnded;
}

const TileSlice* frame_of(const TileVariant* page, size_t frame) {
	return page && frame < page->frames.size() ? &page->frames[frame] : nullptr;
}

// The repair yard (0x584260).
void step_repair_yard(TileAnims& a, uint32_t now, const TileAnimTown& town, uint32_t& rand_state) {
	std::vector<TileAnimGroup>& g = a.groups;
	if (g.size() < 5) {
		return;
	}
	a.busy = town.busy ? 1 : 0;
	step_tile_anim(g[1], now, town.active && a.busy, rand_state);  // the saw
	step_tile_anim(g[2], now, town.active && a.busy, rand_state);  // the hammer
	if (a.busy || g[4].frame != 0) {
		step_tile_anim(g[4], now, true, rand_state);  // the crane, to its rest
	}
	// Its ship's stage by the shipyard's employees: (n − 10) / 6 from 16 on, at most its pictures.
	uint32_t stage = 0;
	if (town.employees >= 0x10) {
		stage = uint32_t(town.employees - 10) / 6;
		const size_t count = g[3].pages.empty() ? 0 : g[3].pages[0]->frames.size();
		if (stage > count) {
			stage = uint32_t(count);
		}
	}
	a.stage = uint8_t(stage);
}

// The sawmill (0x556860): its woman now and then at one of her two works (the second only while the carpenter is at
// his page 5); the carpenter mostly sawing (page 2), then 4 again and again, at times 3 (then idle) or 5; both idle
// when it doesn't work.
void step_sawmill(TileAnims& a, uint32_t now, const TileAnimTown& town, uint32_t& rand_state) {
	if (a.pages.size() < 6 || !a.pages[0]) {
		return;
	}
	TileAnimGroup& woman = a.workers[0];
	TileAnimGroup& carpenter = a.workers[1];
	const uint8_t woman_page = woman.flags & 0x3F;
	const uint8_t carpenter_page = carpenter.flags & 0x3F;
	if (town.shown && town.active) {
		if (woman_page == kIdle && woman.time <= now) {
			const int r = game_rand(rand_state) % 20;
			if (r < 1) {
				start_worker(woman, 0, now);
			} else if (r < 2 && carpenter_page == 5) {
				start_worker(woman, 1, now);
			} else {
				woman.time = now + param(*a.pages[0], 0) * 5;
			}
		}
		if (carpenter_page == kIdle && carpenter.time <= now) {
			const int r = game_rand(rand_state) % 10;
			if (r < 8) {
				start_worker(carpenter, 2, now);
			} else if (a.pages[2]) {
				carpenter.time = now + param(*a.pages[2], 0) * 5;
			}
		}
	} else {
		woman.flags = kIdle;
		carpenter.flags = kIdle;
	}
	if (worker_ended(woman, now)) {
		woman.flags = kIdle;
	}
	if (worker_ended(carpenter, now)) {
		uint8_t next = kIdle;
		switch (carpenter.flags & 0x3F) {
		case 2: next = 4; break;
		case 3: next = kIdle; break;
		case 5: next = 4; break;
		case 4: {
			if ((woman.flags & 0x3F) == 1) {
				next = 3;
			} else {
				const int r = game_rand(rand_state) % 12;
				next = r < 1 ? 3 : r > 10 ? 5 : 4;
			}
			break;
		}
		default: break;
		}
		carpenter.flags = next;
		if (next != kIdle) {
			start_worker(carpenter, next, now);
		}
	}
}

// The foundry (0x555190): while it works, its smelter (a state of 7 parameters over its kind 4's pages), its kettle
// (boiling, page 0; pouring, page 1, while the smelter is at the first frame of its page 5) and its smoke; when it
// doesn't, the smelter (and so the rest) hidden.
void step_foundry(TileAnims& a, uint32_t now, const TileAnimTown& town, uint32_t& rand_state) {
	std::vector<TileAnimGroup>& g = a.groups;
	if (g.size() < 3 || g[0].pages.empty() || g[1].pages.empty() || g[2].pages.empty()) {
		return;
	}
	TileAnimGroup& smelter = g[1];
	TileAnimGroup& kettle = a.workers[0];
	TileAnimGroup& smoke = a.workers[1];
	if (!(town.shown && town.active)) {
		if (!(smelter.flags & kHidden)) {
			smelter.flags |= kStopped | kHidden;  // 0x551820
		}
		return;
	}
	step_tile_anim(smelter, now, true, rand_state);
	if ((smelter.flags & 0x3F) == 5 && smelter.frame == 0) {
		start_worker(kettle, 1, now);
	} else if (worker_ended(kettle, now)) {
		start_worker(kettle, 0, now);
	}
	if (worker_ended(smoke, now)) {
		start_worker(smoke, 0, now);
	}
}

// A 9 parameters' state as its init (0x5524E0): stopped, hidden, at its first place, one round.
void reset_person(TileAnimGroup& p) {
	p.time = 0;
	p.frame = 0;
	p.mode = 0;
	p.flags = kStopped | kHidden;
	p.place = 0;
	p.repeats = 1;
}

// A farm's first step: its stage and its time as its constructor (0x5535F0) sets them: a stage at random (none or one
// of its field's) and the next growing within its days.
void start_farm(TileAnims& a, const TileAnimTown& town, uint32_t& rand_state) {
	a.started = true;
	const uint32_t frames = uint32_t(a.field->frames.size());
	a.stage = uint8_t(uint32_t(game_rand(rand_state)) % (frames + 1) - 1);
	a.field_time = ((a.days ? uint32_t(game_rand(rand_state)) % a.days : 0) << 8) + town.tick;
}

// The farm's field one stage on (to none or back to the first past its last).
void grow(TileAnims& a, const TileAnimTown& town, bool to_none) {
	a.stage = uint8_t(a.stage + 1);
	if (int8_t(a.stage) >= int(a.field->frames.size())) {
		a.stage = to_none ? 0xFF : 0;
	}
	a.field_time = town.tick + (a.days << 8);
}

// The grain farm (0x553AB0): the farm itself, with no crop its farmer's wife sows (her pages' state; at her last but
// one page stopped, the field sown), it grows a stage every its days, and ripe (its last but one stage) the farmer
// reaps (at his last page, the last stage, then none); its fields grow round and round. Not working, all gone.
void step_grain_farm(TileAnims& a, uint32_t now, const TileAnimTown& town, uint32_t& rand_state) {
	if (!a.field || a.field->frames.empty() || (a.main_farm && (a.wife.empty() || a.farmer.empty()))) {
		return;
	}
	if (!a.started) {
		start_farm(a, town, rand_state);
	}
	a.working = town.shown && town.active;
	if (!a.working) {
		if (!(a.person.flags & kHidden) || a.stage != 0xFF) {
			a.person.flags |= kStopped | kHidden;
			a.stage = 0xFF;
		}
		return;
	}
	if (!a.main_farm) {
		if (a.stage == 0xFF || town.tick >= a.field_time) {
			grow(a, town, false);
		}
		return;
	}
	const int frames = int(a.field->frames.size());
	if (a.stage == 0xFF) {
		a.person.pages = a.wife;
		step_tile_anim(a.person, now, true, rand_state);
		if ((a.person.flags & kStopped) && int(a.person.flags & 0x3F) == int(a.wife.size()) - 2) {
			a.stage = 0;
			a.field_time = town.tick + (a.days << 8);
			reset_person(a.person);
		}
	} else if (int(int8_t(a.stage)) == frames - 2) {
		if (town.tick >= a.field_time) {
			a.person.pages = a.farmer;
			step_tile_anim(a.person, now, true, rand_state);
			if (int(a.person.flags & 0x3F) == int(a.farmer.size()) - 1) {
				a.stage = uint8_t(a.stage + 1);
				a.field_time = town.tick + (a.days << 8);
			}
		}
	} else {
		if (!(a.person.flags & kHidden)) {
			a.person.pages = a.farmer;
			step_tile_anim(a.person, now, true, rand_state);
			if (a.person.flags & kStopped) {
				reset_person(a.person);
			}
		}
		if (town.tick >= a.field_time) {
			grow(a, town, true);
		}
	}
}

// The hemp farm (0x554560): working, the farm's farmer at his pages and the field a stage on every its days, round
// and round; not working, the farmer hidden and the field bare.
void step_hemp_farm(TileAnims& a, uint32_t now, const TileAnimTown& town, uint32_t& rand_state) {
	if (!a.field || a.field->frames.empty()) {
		return;
	}
	if (!a.started) {
		start_farm(a, town, rand_state);
	}
	a.working = town.shown && town.active;
	if (!a.working) {
		if (!(a.person.flags & kHidden)) {
			a.person.flags |= kStopped | kHidden;
			a.stage = 0xFF;
		}
		return;
	}
	if (a.main_farm && !a.farmer.empty()) {
		a.person.pages = a.farmer;
		step_tile_anim(a.person, now, true, rand_state);
	}
	if (town.tick >= a.field_time || a.stage == 0xFF) {
		grow(a, town, false);
	}
}

}  // namespace

TileAnims make_tile_anims(const TileDef& tile) {
	TileAnims a;
	a.type = tile.type;
	a.groups = tile_anim_groups(tile);
	if (tile.type == 0x64) {
		// 0x556780: each kind 3..8 into its slot (the last of a kind wins).
		static constexpr int kSlot[6] = {0, 1, 2, 4, 5, 3};
		a.pages.assign(6, nullptr);
		for (const TileVariant& v : tile.variants) {
			if (v.kind >= 3 && v.kind <= 8) {
				a.pages[size_t(kSlot[v.kind - 3])] = &v;
			}
		}
		for (TileAnimGroup& w : a.workers) {
			w.pages = a.pages;
			w.kind = 2;
			w.flags = kIdle;
		}
		if (std::find(a.pages.begin(), a.pages.end(), nullptr) != a.pages.end()) {
			a.pages.clear();
		}
	} else if (tile.type == 0x53 || tile.type == 0x54) {
		// Its reader (0x553DF0, 0x554700): kind 3 its field's page, 4 and 5 its people's (the class's lists); its
		// header's parameters (0x553EA0): the days a stage lasts, whether a field of the farm.
		for (const TileVariant& v : tile.variants) {
			if (v.kind == 3) {
				a.field = &v;
			} else if (v.kind == 4) {
				(tile.type == 0x53 ? a.wife : a.farmer).push_back(&v);
			} else if (v.kind == 5 && tile.type == 0x53) {
				a.farmer.push_back(&v);
			}
		}
		a.days = tile.params.size() > 1 ? tile.params[1] : 10;
		a.main_farm = tile.params.size() < 3 || tile.params[2] == 0;
		a.person.kind = 9;
		reset_person(a.person);
		a.stage = 0xFF;
	} else if (tile.type == 0x60 && a.groups.size() >= 3) {
		a.workers[0].pages = a.groups[0].pages;  // the kettle's pages (kind 3)
		a.workers[1].pages = {a.groups[2].pages.empty() ? nullptr : a.groups[2].pages.back()};  // the smoke (kind 5)
		for (TileAnimGroup& w : a.workers) {
			w.kind = 2;
			w.flags = 0;
			w.mode = 0;
		}
	}
	return a;
}

void step_tile_anims(TileAnims& a, uint32_t now, const TileAnimTown& town, uint32_t& rand_state) {
	switch (a.type) {
	case 0x1B: step_repair_yard(a, now, town, rand_state); return;
	case 0x64: step_sawmill(a, now, town, rand_state); return;
	case 0x60: step_foundry(a, now, town, rand_state); return;
	case 0x53: step_grain_farm(a, now, town, rand_state); return;
	case 0x54: step_hemp_farm(a, now, town, rand_state); return;
	default:
		for (TileAnimGroup& g : a.groups) {
			step_tile_anim(g, now, town.active, rand_state);
		}
		return;
	}
}

std::vector<TileAnimFrame> tile_anim_frames(const TileAnims& a) {
	std::vector<TileAnimFrame> out;
	auto add = [&](const TileVariant* page, size_t frame) {
		if (const TileSlice* f = frame_of(page, frame)) {
			out.push_back({page, f});
		}
	};
	auto add_shown = [&](const TileAnimGroup& g, bool even_hidden) {
		const TileVariant* page = nullptr;
		if (const TileSlice* f = g.shown(&page, even_hidden)) {
			out.push_back({page, f});
		}
	};
	const std::vector<TileAnimGroup>& g = a.groups;
	switch (a.type) {
	case 0x01:
		// The shipyard (0x584F00): its gate's gulls hidden, nothing; else they, its posts' and the flying ones even
		// hidden (those by day: the viewer has no night).
		if (!g.empty() && g[0].shown()) {
			for (size_t k = 0; k < g.size() && k < 3; ++k) {
				add_shown(g[k], k > 0);
			}
		}
		return out;
	case 0x1B: {
		// The repair yard (0x5844E0): with work its ship, its saw and hammer (not hidden); its ship's stage; its crane.
		if (g.size() < 5) {
			return out;
		}
		auto first = [](const TileAnimGroup& group) { return group.pages.empty() ? nullptr : group.pages[0]; };
		if (a.busy) {
			add(first(g[0]), 0);
			if (!(g[1].flags & kHidden)) {
				add(first(g[1]), g[1].frame);
			}
			if (!(g[2].flags & kHidden)) {
				add(first(g[2]), g[2].frame);
			}
		}
		if (a.stage) {
			add(first(g[3]), size_t(a.stage - 1));
		}
		add(first(g[4]), g[4].frame);
		return out;
	}
	case 0x64: {
		// The sawmill (0x5564F0): its woman at work, its carpenter at work or resting (its page 3's last frame).
		if (a.pages.size() < 6) {
			return out;
		}
		const TileAnimGroup& woman = a.workers[0];
		const TileAnimGroup& carpenter = a.workers[1];
		if ((woman.flags & 0x3F) != kIdle) {
			add(a.pages[woman.flags & 0x3F], woman.frame);
		}
		if ((carpenter.flags & 0x3F) != kIdle) {
			add(a.pages[carpenter.flags & 0x3F], carpenter.frame);
		} else if (!a.pages[3]->frames.empty()) {
			add(a.pages[3], a.pages[3]->frames.size() - 1);
		}
		return out;
	}
	case 0x53:
	case 0x54: {
		// A farm (0x5538D0, 0x5543A0): working, its field's stage and its people (the wife's pages with no crop).
		if (!a.working || !a.field) {
			return out;
		}
		if (a.stage != 0xFF) {
			add(a.field, a.stage);
		}
		if (!(a.person.flags & kHidden)) {
			const std::vector<const TileVariant*>& people = a.type == 0x53 && a.stage == 0xFF ? a.wife : a.farmer;
			if ((a.person.flags & 0x3F) < people.size()) {
				add(people[a.person.flags & 0x3F], a.person.frame);
			}
		}
		return out;
	}
	case 0x60: {
		// The foundry (0x555010): its kettle, its smelter and its smoke, while the smelter isn't hidden.
		if (g.size() < 3 || g[1].pages.empty() || (g[1].flags & kHidden)) {
			return out;
		}
		const TileAnimGroup& kettle = a.workers[0];
		const TileAnimGroup& smoke = a.workers[1];
		add(kettle.pages.empty() ? nullptr : kettle.pages[std::min<size_t>(kettle.flags & 0x3F, kettle.pages.size() - 1)],
			kettle.frame);
		add(g[1].pages[std::min<size_t>(g[1].flags & 0x3F, g[1].pages.size() - 1)], g[1].frame);
		add(smoke.pages.empty() ? nullptr : smoke.pages[0], smoke.frame);
		return out;
	}
	default:
		for (const TileAnimGroup& group : g) {
			add_shown(group, false);
		}
		return out;
	}
}

}  // namespace p3::data
