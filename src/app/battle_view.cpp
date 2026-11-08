#include "battle_view.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "p3data/aim.hpp"
#include "p3data/battle/battles.hpp"
#include "p3data/image.hpp"

namespace p3::app {

using p3::data::BattleGraph;
using p3::data::CprEntry;
using p3::data::Image;
using p3::data::LcrAnim;
using p3::data::LcrFrame;
using p3::data::SaveGame;
using BattleEffect = p3::data::Sailing::BattleEffect;

namespace {

constexpr int kWaterWidth = 102;  // The water's 3 × 3 tiles repeat every 102 × 66 pixels.
constexpr int kWaterHeight = 66;
constexpr uint32_t kWaterFrameMs = 150;  // The water's frames (a guess: the game's town view animates them).
constexpr uint32_t kCloudStepMs = 40;
constexpr int kCloudTile = 1200;  // The clouds repeat over the area every kCloudTile² pixels.
constexpr int kClouds = 8;
constexpr Uint8 kShadowAlpha = 110;
constexpr size_t kMaxEffects = 400;

// The effects' animations (the creatures of class 0x12 the battle code asks for).
constexpr char kSplash[] = "iso/lcr/fontain_a.lcr";  // 0x12/0, a ball landing in the water (0x603347).
constexpr char kPuff[] = "iso/lcr/rauch_a.lcr";  // 0x12/0x20, a ball hitting a ship (0x6036F4).
constexpr char kExplosion[] = "iso/lcr/waffenex.lcr";  // 0x12/0x11, a hit of result 2 (0x603656).
constexpr char kBurning[] = "iso/lcr/brandrauch.lcr";  // 0x12/0x24, a burning or sinking ship's smoke.
// 0x12/0x21, a gun's smoke, and 0x12/0x25, a heavy gun's fire (0x602D3B), in 16 directions; three of each.
constexpr const char* kGunSmoke[3] = {"iso/lcr/kanonenqualm.lcr", "iso/lcr/kanonenqualm_b.lcr",
	"iso/lcr/kanonenqualm_c.lcr"};
constexpr const char* kGunFire[3] = {"iso/lcr/KanoneFeuer.lcr", "iso/lcr/KanoneFeuer2.lcr", "iso/lcr/KanoneFeuer3.lcr"};

// A sinking ship's parts (0x12/4..7 the upper one with its shadow, 0x12/0x40..0x43 the lower one; by type), in
// 8 directions (the game's variant (d + 16) >> 5 & 7, plus 1: north, then clockwise).
std::string sinking_path(int type, bool upper, int dir8) {
	static constexpr const char* kDirs[8] = {"n", "no", "o", "so", "s", "sw", "w", "nw"};
	static constexpr const char* kUpper[4] = {"schniggeoben", "kraieroben", "koggesinktoben", "holksinktoben"};
	static constexpr const char* kLower[4] = {"schniggeunten", "kraierunten", "koggesinktunten", "holksinktunten"};
	return std::string("iso/lcr/") + (upper ? kUpper : kLower)[type & 3] + kDirs[dir8 & 7] + ".lcr";
}

// The ships of a battle's scene: [+0x4] their pointers, [+0x14] how many.
template <class F>
void for_each_ship(const BattleGraph& g, size_t b, F f) {
	const int list = g.ptr(b, 0x4);
	if (list < 0) {
		return;
	}
	const size_t n = g.get<uint16_t>(b, 0x14);
	for (size_t i = 0; i < n; ++i) {
		const int e = g.ptr(size_t(list), 4 * i);
		if (e >= 0) {
			f(size_t(e));
		}
	}
}

// 0x56E000: the hull state shown (with the town view's damage looks, [0x6C4010] 1): the hull in percent,
// one less unless 0, at most 99; 3 − that / 25.
int hull_state(const p3::data::SavedShip& ship) {
	const int32_t max = ship.max_health();
	int p = max > 0 ? int(int64_t(ship.health()) * 100 / max) : 0;
	if (p != 0) {
		--p;
	}
	p = std::clamp(p, 0, 99);
	return 3 - p / 25;
}

// The sine of a direction in 256ths of a turn, as a float.
float sine256(int d) {
	return float(std::sin(double(d & 0xFF) * 3.14159265358979 / 128.0));
}

}  // namespace

BattleView::BattleView(SDL_Renderer* renderer, const p3::data::CprArchive& archive, const p3::data::TileSet& tiles)
	: renderer_(renderer), archive_(archive), tiles_(tiles) {}

BattleView::~BattleView() {
	clear();
}

void BattleView::clear() {
	for (auto& [path, t] : textures_) {
		if (t) {
			SDL_DestroyTexture(t);
		}
	}
	textures_.clear();
	for (SDL_Texture*& t : water_) {
		if (t) {
			SDL_DestroyTexture(t);
		}
		t = nullptr;
	}
	anims_.clear();
	queue_.clear();
	ball_ = nullptr;
	arrow_ = nullptr;
	hits_.clear();
	order_marked_ = false;
	cloud_images_.fill(nullptr);
	clouds_.clear();
	effects_.clear();
	sinking_.clear();
	burning_.clear();
}

uint32_t BattleView::next_random() {
	random_ = random_ * 1103515245u + 12345u;
	return (random_ >> 16) & 0x7FFF;
}

// An image of the archive as a texture, kept by its path; `magenta`: FF00FF transparent (the creatures' key).
SDL_Texture* BattleView::texture(const std::string& path, bool magenta) {
	if (auto it = textures_.find(path); it != textures_.end()) {
		return it->second;
	}
	SDL_Texture*& t = textures_[path];
	const CprEntry* entry = archive_.find(path);
	std::vector<uint8_t> data;
	Image image;
	if (!entry || !archive_.read(*entry, data)) {
		return t = nullptr;
	}
	const bool aim = path.size() > 4 && (path.compare(path.size() - 4, 4, ".aim") == 0 ||
											path.compare(path.size() - 4, 4, ".AIM") == 0);
	if (!(aim ? p3::data::decode_aim(data.data(), data.size(), image)
			  : p3::data::decode_standard_image(data.data(), data.size(), image))) {
		return t = nullptr;
	}
	if (magenta) {
		p3::data::apply_magenta_key(image);
	}
	t = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, image.width, image.height);
	if (t) {
		SDL_UpdateTexture(t, nullptr, image.rgba.data(), image.width * 4);
		SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
	}
	return t;
}

// The town view's water (iso/let g_k.let, the 9 frames g of WASSER_neu.aim, each 3 × 3 tiles k placed as
// iso/let/wasser_neu_g.lgr says): each frame as one block of 102 × 66 pixels that tiles the plane. A cell
// (x, y) of the staggered grid is (u, v) = ((2x + (y & 1) + y) / 2, (y − 2x − (y & 1)) / 2) of the diamonds;
// the group's tile k by (u − 1) mod 3, (v + 1) mod 3.
void BattleView::make_water() {
	static constexpr int kTileOf[3][3] = {{1, 8, 7}, {2, 9, 6}, {3, 4, 5}};  // [a][b]
	std::map<std::string, const p3::data::TileDef*> by_name;
	for (const auto& [id, tile] : tiles_) {
		by_name[tile.name] = &tile;
	}
	std::string atlas_path;
	if (auto it = by_name.find("1_1"); it != by_name.end()) {
		atlas_path = it->second->atlas;
	}
	Image atlas;
	std::vector<uint8_t> data;
	const CprEntry* entry = atlas_path.empty() ? nullptr : archive_.find(atlas_path);
	const bool have = entry && archive_.read(*entry, data) && p3::data::decode_aim(data.data(), data.size(), atlas);
	if (have) {
		p3::data::apply_magenta_key(atlas);
	}
	for (int g = 0; g < 9; ++g) {
		std::vector<uint8_t> block(size_t(kWaterWidth) * kWaterHeight * 4);
		for (size_t i = 0; i < block.size(); i += 4) {  // Under the tiles' seams, the water's colour.
			block[i] = 74;
			block[i + 1] = 88;
			block[i + 2] = 92;
			block[i + 3] = 255;
		}
		for (int cy = 0; cy < 6 && have; ++cy) {
			for (int cx = 0; cx < 3; ++cx) {
				const int u = (2 * cx + (cy & 1) + cy) / 2;
				const int v = (cy - 2 * cx - (cy & 1)) / 2;
				const int a = ((u - 1) % 3 + 3) % 3;
				const int b = ((v + 1) % 3 + 3) % 3;
				const auto it = by_name.find(std::to_string(g + 1) + "_" + std::to_string(kTileOf[a][b]));
				if (it == by_name.end() || it->second->slices.empty()) {
					continue;
				}
				const p3::data::TileDef& tile = *it->second;
				const p3::data::TileSlice& front = tile.slices[std::min(tile.front_slice, tile.slices.size() - 1)];
				const int left = p3::data::cell_screen_x(cx, cy) - front.x;
				const int top = p3::data::cell_screen_y(cy) + p3::data::kTileHeight - front.height - front.y;
				for (int y = 0; y < tile.height; ++y) {
					for (int x = 0; x < tile.width; ++x) {
						const int sx = tile.src_x + x;
						const int sy = tile.src_y + y;
						if (sx < 0 || sy < 0 || sx >= atlas.width || sy >= atlas.height) {
							continue;
						}
						const uint8_t* p = &atlas.rgba[(size_t(sy) * size_t(atlas.width) + size_t(sx)) * 4];
						if (p[3] < 128) {
							continue;
						}
						const int bx = ((left + x) % kWaterWidth + kWaterWidth) % kWaterWidth;
						const int by = ((top + y) % kWaterHeight + kWaterHeight) % kWaterHeight;
						std::memcpy(&block[(size_t(by) * kWaterWidth + size_t(bx)) * 4], p, 3);
					}
				}
			}
		}
		water_[size_t(g)] =
			SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, kWaterWidth, kWaterHeight);
		if (water_[size_t(g)]) {
			SDL_UpdateTexture(water_[size_t(g)], nullptr, block.data(), kWaterWidth * 4);
		}
	}
}

void BattleView::want(const std::string& path) {
	if (!anims_.count(path) && std::find(queue_.begin(), queue_.end(), path) == queue_.end()) {
		queue_.push_back(path);
	}
}

void BattleView::need(const SaveGame& save, size_t battle) {
	for (const char* path : {kSplash, kPuff, kExplosion, kBurning}) {
		want(path);
	}
	for (int k = 0; k < 3; ++k) {
		want(kGunSmoke[k]);
		want(kGunFire[k]);
	}
	const BattleGraph& g = save.battles;
	std::vector<std::string> looks;
	for_each_ship(g, battle, [&](size_t e) {
		const uint32_t index = g.get<uint32_t>(e, 0x4);
		if (index >= save.ships.size()) {
			return;
		}
		const p3::data::SavedShip& ship = save.ships[index];
		for (int s = 0; s < 5; ++s) {
			looks.push_back(p3::data::lcr_ship_path(ship.type() & 3, hull_state(ship), s));
			want(looks.back());
		}
	});
	for (const auto& [e, s] : sinking_) {
		if (s.started) {
			want(s.upper);
			want(s.lower);
		}
	}
	// The looks no ship has any more (a hull state left behind) are freed, with the images only they used: the
	// Vita's memory holds a few of them (each type's 20 would be some 35 MB).
	bool freed = false;
	for (auto it = anims_.begin(); it != anims_.end();) {
		const bool ship_look = it->first.find("_schaden") != std::string::npos;
		if (ship_look && std::find(looks.begin(), looks.end(), it->first) == looks.end()) {
			it = anims_.erase(it);
			freed = true;
		} else {
			++it;
		}
	}
	if (freed) {
		for (auto it = textures_.begin(); it != textures_.end();) {
			SDL_Texture* t = it->second;
			bool used = t == ball_ || t == arrow_ ||
				std::find(cloud_images_.begin(), cloud_images_.end(), t) != cloud_images_.end();
			for (const auto& [path, a] : anims_) {
				used = used || a->body == t || a->shadow == t;
			}
			if (!used && t) {
				SDL_DestroyTexture(t);
				it = textures_.erase(it);
			} else {
				++it;
			}
		}
	}
}

void BattleView::load_step() {
	if (!water_[0]) {
		make_water();
		ball_ = texture("images/creatures/kanonenkugel01.bmp", true);
		arrow_ = texture("images/creatures/markerpfeil01.aim", true);
		if (arrow_) {
			SDL_SetTextureColorMod(arrow_, 90, 150, 255);  // The manual's blue arrow.
		}
		for (int i = 0; i < 6; ++i) {
			cloud_images_[size_t(i)] = texture("images/creatures/clouds_lokal0" + std::to_string(i + 3) + ".aim", false);
		}
		return;
	}
	if (queue_.empty()) {
		return;
	}
	const std::string path = queue_.front();
	queue_.erase(queue_.begin());
	auto a = std::make_unique<Anim>();
	const CprEntry* entry = archive_.find(path);
	std::vector<uint8_t> data;
	if (entry && archive_.read(*entry, data) && p3::data::parse_lcr(data.data(), data.size(), a->lcr)) {
		const bool magenta = a->lcr.key == 0xFFFF00FFu;
		a->body = texture(a->lcr.image, magenta);
		if (a->body && a->lcr.key == 0xFF000000u) {
			SDL_SetTextureBlendMode(a->body, SDL_BLENDMODE_ADD);  // Black is nothing: a fire's or explosion's light.
		}
		if (!a->lcr.shadow_image.empty()) {
			a->shadow = texture(a->lcr.shadow_image, magenta);
			if (a->shadow) {
				SDL_SetTextureColorMod(a->shadow, 0, 0, 0);
				SDL_SetTextureAlphaMod(a->shadow, kShadowAlpha);
			}
		}
	}
	anims_[path] = std::move(a);
}

const BattleView::Anim* BattleView::anim(const std::string& path) const {
	const auto it = anims_.find(path);
	return it != anims_.end() && it->second->body && !it->second->lcr.directions.empty() ? it->second.get() : nullptr;
}

// The look to draw: the one wanted, else (while it loads) the same type's nearest hull state with those sails.
const BattleView::Anim* BattleView::ship_look(int type, int hull, int sails) const {
	for (int d = 0; d < 4; ++d) {
		for (int h : {hull - d, hull + d}) {
			if (h >= 0 && h <= 3) {
				if (const Anim* a = anim(p3::data::lcr_ship_path(type, h, sails))) {
					return a;
				}
			}
		}
	}
	return nullptr;
}

void BattleView::spawn(const std::string& path, int dir, float x, float y, uint32_t now, float vx, float vy, Uint8 alpha) {
	if (effects_.size() >= kMaxEffects) {
		return;
	}
	Effect e;
	e.anim = path;
	e.dir = dir;
	e.x = x;
	e.y = y;
	e.vx = vx;
	e.vy = vy;
	e.start = now;
	e.alpha = alpha;
	effects_.push_back(e);
}

// A puff of the burning smoke (0x12/0x24) about (x, y), within 10 each way (the game's by rand() % 20 − 10),
// drifting with the wind ([+0x670], [+0x671]: the game's [+0x40], [+0x44] of the sprite).
void BattleView::smoke(const SaveGame& save, size_t battle, float x, float y, uint32_t now) {
	const BattleGraph& g = save.battles;
	const int dir = g.get<uint8_t>(battle, 0x670);
	const float strength = float(g.get<uint8_t>(battle, 0x671));
	const float vx = sine256(dir) * strength / 16.0f;
	const float vy = -sine256(dir + 0x40) * strength / 16.0f * 11.0f / 17.0f - 6.0f;
	spawn(kBurning, 0, x + float(int(next_random() % 20) - 10), y + float(int(next_random() % 20) - 10) * 11.0f / 17.0f,
		now, vx, vy);
}

void BattleView::add_effects(const SaveGame& save, size_t battle, const std::vector<BattleEffect>& effects,
	uint32_t now) {
	const uint16_t slot = save.battles.get<uint16_t>(battle, 0x662);
	for (const BattleEffect& e : effects) {
		if (e.slot != slot) {
			continue;
		}
		const float x = float(e.x);
		const float y = float(screen_y(e.y));
		switch (e.kind) {
		case BattleEffect::Shot:
			spawn(e.heavy ? kGunFire[next_random() % 3] : kGunSmoke[next_random() % 3], ((e.dir + 8) & 0xF0) >> 4, x, y,
				now);
			break;
		case BattleEffect::Splash:
			spawn(kSplash, 0, x, y, now);
			break;
		case BattleEffect::Hit:
			spawn(e.result == 2 ? kExplosion : kPuff, 0, x, y, now);
			break;
		}
	}
}

// A frame of an animation with its anchor at (x, y) on the screen (and its shadow first, if asked).
void BattleView::draw_frame(const Anim& a, int dir, int frame, int x, int y, Uint8 alpha, bool shadow) const {
	const std::vector<LcrFrame>& frames = a.lcr.directions[size_t(dir) % a.lcr.directions.size()];
	if (frames.empty()) {
		return;
	}
	const LcrFrame& f = frames[size_t(frame) % frames.size()];
	if (shadow) {
		if (a.shadow && f.shadowed) {
			const SDL_Rect src = {f.shadow.x, f.shadow.y, f.shadow.w, f.shadow.h};
			const SDL_Rect dst = {x + f.shadow_x - f.anchor_x, y + f.shadow_y - f.anchor_y, f.shadow.w, f.shadow.h};
			SDL_RenderCopy(renderer_, a.shadow, &src, &dst);
		}
		return;
	}
	const SDL_Rect src = {f.body.x, f.body.y, f.body.w, f.body.h};
	const SDL_Rect dst = {x - (f.anchor_x - f.body.x), y - (f.anchor_y - f.body.y), f.body.w, f.body.h};
	if (alpha != 255) {
		SDL_SetTextureAlphaMod(a.body, alpha);
	}
	SDL_RenderCopy(renderer_, a.body, &src, &dst);
	if (alpha != 255) {
		SDL_SetTextureAlphaMod(a.body, 255);
	}
}

int BattleView::ship_at(int x, int y, bool& own) const {
	constexpr int kFinger = 12;  // Around the picture, for the finger.
	int best = -1;
	long nearest = 0;
	for (const Hit& h : hits_) {
		if (x < h.rect.x - kFinger || x >= h.rect.x + h.rect.w + kFinger || y < h.rect.y - kFinger ||
			y >= h.rect.y + h.rect.h + kFinger) {
			continue;
		}
		const long d = long(x - h.anchor.x) * (x - h.anchor.x) + long(y - h.anchor.y) * (y - h.anchor.y);
		if (best < 0 || d < nearest) {
			best = h.place;
			nearest = d;
			own = h.own;
		}
	}
	return best;
}

void BattleView::mark_order(int x, int y, uint32_t now) {
	order_mark_ = {x, y};
	order_time_ = now;
	order_marked_ = true;
}

std::vector<uint16_t> BattleView::player_ships(const SaveGame& save, size_t battle, int player) {
	const BattleGraph& g = save.battles;
	std::vector<uint16_t> out;
	for_each_ship(g, battle, [&](size_t e) {
		const uint8_t side = g.get<uint8_t>(e, 0x13D);
		if (g.get<uint16_t>(battle, 0x35A + 2 * size_t(side)) == uint16_t(player) && !(g.get<uint8_t>(e, 0x12A) & 0x6)) {
			out.push_back(g.get<uint16_t>(e, 0x138));
		}
	});
	return out;
}

bool BattleView::ship_point(const SaveGame& save, size_t battle, uint16_t place, SDL_Point& point) {
	const BattleGraph& g = save.battles;
	bool found = false;
	for_each_ship(g, battle, [&](size_t e) {
		if (!found && g.get<uint16_t>(e, 0x138) == place) {
			point = {g.get<int16_t>(e, 0xA), screen_y(g.get<int16_t>(e, 0xE))};
			found = true;
		}
	});
	return found;
}

uint8_t BattleView::sails_step(const SaveGame& save, size_t battle, bool up) {
	const BattleGraph& g = save.battles;
	const int sel = g.ptr(battle, 0x66C);
	const size_t n = g.get<uint16_t>(battle, 0xAD0);
	uint8_t v = up ? 0 : 4;
	for (size_t i = 0; sel >= 0 && i < n; ++i) {
		const int e = g.ptr(size_t(sel), 4 * i);
		if (e < 0) {
			continue;
		}
		const uint8_t s = g.get<uint8_t>(size_t(e), 0x148);
		v = up ? std::max(v, s) : std::min(v, s);
	}
	if (up && v < 4) {
		++v;
	} else if (!up && v > 0) {
		--v;
	}
	return v;
}

bool BattleView::player_ships_centre(const SaveGame& save, size_t battle, int player, SDL_Point& centre) {
	const BattleGraph& g = save.battles;
	int64_t sx = 0;
	int64_t sy = 0;
	int n = 0;
	for_each_ship(g, battle, [&](size_t e) {
		const uint8_t side = g.get<uint8_t>(e, 0x13D);
		const int x = g.get<int16_t>(e, 0xA);
		const int y = g.get<int16_t>(e, 0xE);
		if (g.get<uint16_t>(battle, 0x35A + 2 * size_t(side)) != uint16_t(player) || (g.get<uint8_t>(e, 0x12A) & 0x2) ||
			(x == 0 && y == 0)) {
			return;  // Not his, out, or not placed yet (the formation, 0x6079D0, comes with the first step).
		}
		sx += x;
		sy += screen_y(y);
		++n;
	});
	if (n == 0) {
		return false;
	}
	centre = {int(sx / n), int(sy / n)};
	return true;
}

void BattleView::draw(const SaveGame& save, size_t battle, int camera_x, int camera_y, int top, int view_width,
	int view_height, uint32_t now, const std::vector<uint16_t>& selection, std::vector<BattleLabel>& labels) {
	const BattleGraph& g = save.battles;
	hits_.clear();
	const SDL_Rect clip = {0, top, view_width, view_height};
	SDL_RenderSetClipRect(renderer_, &clip);

	// The water.
	if (SDL_Texture* water = water_[(now / kWaterFrameMs) % 9]) {
		const int x0 = -(((camera_x % kWaterWidth) + kWaterWidth) % kWaterWidth);
		const int y0 = top - (((camera_y % kWaterHeight) + kWaterHeight) % kWaterHeight);
		for (int y = y0; y < top + view_height; y += kWaterHeight) {
			for (int x = x0; x < view_width; x += kWaterWidth) {
				const SDL_Rect dst = {x, y, kWaterWidth, kWaterHeight};
				SDL_RenderCopy(renderer_, water, nullptr, &dst);
			}
		}
	}

	// The ships, back to front: each one's shadow, then its picture; a sinking one's two parts.
	struct Drawn {
		int x;  // On the screen.
		int y;
		float ax;  // In the area's screen pixels.
		float ay;
		size_t entry;
		const Anim* look;
		int frame;
		const Sinking* sinking;
	};
	std::vector<Drawn> ships;
	const int player = save.player;
	std::map<size_t, bool> present;
	for_each_ship(g, battle, [&](size_t e) {
		const uint8_t flags = g.get<uint8_t>(e, 0x12A);
		if ((flags & 0x2) && !(flags & 0x4)) {
			return;  // Out of the battle (0x620AED: its sprite removed).
		}
		const uint32_t index = g.get<uint32_t>(e, 0x4);
		if (index >= save.ships.size()) {
			return;
		}
		present[e] = true;
		const p3::data::SavedShip& ship = save.ships[index];
		const uint8_t dir = g.get<uint8_t>(e, 0x11);
		const float ax = float(g.get<int16_t>(e, 0xA));
		const float ay = float(screen_y(g.get<int16_t>(e, 0xE)));
		const Sinking* sinking = nullptr;
		if (flags & 0x4) {
			// 0x6201A2: it sinks once its course is near one of 8 ((d − 16) & 0x18 none); the viewer doesn't
			// wait more than 3 s for it.
			Sinking& s = sinking_[e];
			if (!s.started) {
				if (s.start == 0) {
					s.start = now | 1;
				}
				if (((dir - 16) & 0x18) == 0 || now - s.start > 3000) {
					const int dir8 = ((dir + 16) >> 5) & 7;
					s.started = true;
					s.start = now;
					s.upper = sinking_path(ship.type() & 3, true, dir8);
					s.lower = sinking_path(ship.type() & 3, false, dir8);
					want(s.upper);
					want(s.lower);
				}
			}
			if (s.started) {
				sinking = &s;
			}
		} else if (3 * int64_t(ship.health()) <= 2 * int64_t(ship.max_health()) && ship.max_health() > 0) {
			// 0x620CDE: a ship at two thirds of its hull or less smokes, a puff each 1200 · hull / max + 200 ms.
			uint32_t& next = burning_[e];
			if (int32_t(now - next) >= 0) {
				smoke(save, battle, ax, ay, now);
				next = now + uint32_t(1200 * int64_t(std::max(ship.health(), 0)) / ship.max_health()) + 200;
			}
		}
		const int sails = std::min<int>(g.get<uint8_t>(e, 0x26), 4);
		const Anim* look = ship_look(ship.type() & 3, hull_state(ship), sails);
		const int frames = look ? int(look->lcr.directions.size()) : 32;
		const int frame = ((dir * frames + 128) >> 8) & (frames - 1);
		ships.push_back({int(ax) - camera_x, top + int(ay) - camera_y, ax, ay, e, look, frame, sinking});
	});
	for (auto it = sinking_.begin(); it != sinking_.end();) {
		it = present.count(it->first) ? std::next(it) : sinking_.erase(it);
	}
	std::sort(ships.begin(), ships.end(), [](const Drawn& a, const Drawn& b) { return a.y < b.y; });
	for (const Drawn& d : ships) {
		if (d.sinking) {
			if (const Anim* upper = anim(d.sinking->upper)) {
				const uint32_t fps = std::max<uint32_t>(upper->lcr.fps, 1);
				const size_t n = upper->lcr.directions[0].size();
				const size_t frame = size_t((now - d.sinking->start) * fps / 1000);
				if (frame < n) {
					draw_frame(*upper, 0, int(frame), d.x, d.y, 255, true);
				}
			}
		} else if (d.look) {
			draw_frame(*d.look, d.frame, 0, d.x, d.y, 255, true);
		}
	}
	for (const Drawn& d : ships) {
		const uint32_t index = g.get<uint32_t>(d.entry, 0x4);
		const p3::data::SavedShip& ship = save.ships[index];
		if (d.sinking) {
			// Its lower part, then its upper part, each once at its frames per second; while the upper one's
			// first 10 frames play, two puffs of smoke (0x620944; a frame of the viewer each 50 ms at most).
			Sinking& s = sinking_[d.entry];
			for (const std::string* part : {&s.lower, &s.upper}) {
				const Anim* a = anim(*part);
				if (!a) {
					continue;
				}
				const uint32_t fps = std::max<uint32_t>(a->lcr.fps, 1);
				const size_t frame = size_t((now - s.start) * fps / 1000);
				if (frame < a->lcr.directions[0].size()) {
					draw_frame(*a, 0, int(frame), d.x, d.y, 255, false);
				}
				if (part == &s.upper && frame < 10 && now - s.smoke >= 50) {
					s.smoke = now;
					smoke(save, battle, d.ax, d.ay, now);
					smoke(save, battle, d.ax, d.ay, now);
				}
			}
			continue;
		}
		int top_y = d.y - 60;
		const uint8_t side = g.get<uint8_t>(d.entry, 0x13D);
		const bool own = int(g.get<uint16_t>(battle, 0x35A + 2 * size_t(side))) == player;
		const uint16_t place = g.get<uint16_t>(d.entry, 0x138);
		Hit hit;
		hit.place = place;
		hit.own = own;
		hit.anchor = {d.x, d.y};
		hit.rect = {d.x - 30, d.y - 80, 60, 100};
		if (d.look) {
			draw_frame(*d.look, d.frame, 0, d.x, d.y, 255, false);
			const LcrFrame& f = d.look->lcr.directions[size_t(d.frame)][0];
			top_y = d.y - (f.anchor_y - f.body.y);
			hit.rect = {d.x - (f.anchor_x - f.body.x), top_y, f.body.w, f.body.h};
		}
		if (g.get<uint8_t>(d.entry, 0x12A) & 0x4) {
			continue;  // Sinking: no label, no touch.
		}
		hits_.push_back(hit);
		BattleLabel label;
		label.x = d.x;
		label.y = top_y - 2;
		label.name = ship.name();
		label.sailors = ship.sailors();
		label.hull = ship.max_health() > 0 ? int(int64_t(ship.health()) * 100 / ship.max_health()) : 0;
		label.firepower = ship.memory.s32(0x120);
		label.own = own;
		label.selected = own && std::find(selection.begin(), selection.end(), place) != selection.end();
		labels.push_back(label);
	}

	// The order lines (0x56DD20, from method +0x20 while [+0xAE0] isn't −1): from where each chosen ship of
	// the player was when the order to go went ([+0xAE0 + 8 · i], [+0xAE4 + 8 · i], y · 22 / 34) to its point
	// ([+0x12D8], [+0x12DC]); the game clears them after a frame once the order is let go ([+0x12E0] 0: the
	// viewer, clear_order_lines). And the marker of the last order (mark_order).
	const int sel = g.ptr(battle, 0x66C);
	if (g.get<int32_t>(battle, 0xAE0) != -1 && sel >= 0) {
		const int tx = int(g.get<uint32_t>(battle, 0x12D8)) - camera_x;
		const int ty = top + g.get<int32_t>(battle, 0x12DC) - camera_y;
		SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
		for (uint16_t i = 0; i < g.get<uint16_t>(battle, 0xAD0) && i < 0xF0; ++i) {
			const int32_t fx = g.get<int32_t>(battle, 0xAE0 + 8 * size_t(i));
			if (fx == -1) {
				continue;
			}
			const int x0 = fx - camera_x;
			const int y0 = top + g.get<int32_t>(battle, 0xAE4 + 8 * size_t(i)) - camera_y;
			SDL_SetRenderDrawColor(renderer_, 255, 245, 200, 160);
			const int steps = std::max(1, int(std::hypot(double(tx - x0), double(ty - y0)) / 12));
			for (int k = 0; k < steps; k += 2) {  // Dashes of 12 pixels.
				SDL_RenderDrawLine(renderer_, x0 + (tx - x0) * k / steps, y0 + (ty - y0) * k / steps,
					x0 + (tx - x0) * (k + 1) / steps, y0 + (ty - y0) * (k + 1) / steps);
			}
		}
	}
	if (order_marked_ && now - order_time_ < 900) {
		const Uint8 a = Uint8(255 - (now - order_time_) * 255 / 900);
		const int x = order_mark_.x - camera_x;
		const int y = top + order_mark_.y - camera_y;
		const int r = 6 + int(now - order_time_) / 60;
		SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
		SDL_SetRenderDrawColor(renderer_, 255, 245, 200, a);
		for (int k = 0; k < 24; ++k) {  // A ring that widens and fades, flattened as the water.
			const double t0 = k * 6.2831853 / 24, t1 = (k + 1) * 6.2831853 / 24;
			SDL_RenderDrawLine(renderer_, x + int(r * std::cos(t0)), y + int(r * 0.6 * std::sin(t0)),
				x + int(r * std::cos(t1)), y + int(r * 0.6 * std::sin(t1)));
		}
	} else {
		order_marked_ = false;
	}

	// The effects: the guns' smoke and fire, the splashes, the hits, the smoke; each once.
	for (auto it = effects_.begin(); it != effects_.end();) {
		const Anim* a = anim(it->anim);
		const auto known = anims_.find(it->anim);
		const uint32_t age = now - it->start;
		if (known != anims_.end() && !a) {
			it = effects_.erase(it);  // Its animation didn't load.
			continue;
		}
		if (a) {
			const uint32_t fps = std::max<uint32_t>(a->lcr.fps, 1);
			const size_t frame = size_t(uint64_t(age) * fps / 1000);
			const std::vector<LcrFrame>& frames = a->lcr.directions[size_t(it->dir) % a->lcr.directions.size()];
			if (frame >= frames.size()) {
				it = effects_.erase(it);
				continue;
			}
			const int x = int(it->x + it->vx * float(age) / 1000.0f) - camera_x;
			const int y = top + int(it->y + it->vy * float(age) / 1000.0f) - camera_y;
			draw_frame(*a, it->dir, int(frame), x, y, it->alpha, false);
		} else if (age > 5000) {
			it = effects_.erase(it);  // Never loaded in time.
			continue;
		}
		++it;
	}

	// The cannon balls (0x6038C0): drawn ahead of their place by their speed, raised by their arc.
	const int list = g.ptr(battle, 0x668);
	const size_t slots = g.get<uint16_t>(battle, 0xACC);
	for (size_t k = 0; k < slots && list >= 0 && ball_; ++k) {
		const int ball = g.ptr(size_t(list), 4 * k);
		if (ball < 0) {
			continue;
		}
		const size_t bb = size_t(ball);
		const int tx = g.get<int16_t>(bb, 0x0);
		const int ty = g.get<int16_t>(bb, 0x2);
		const int bx = g.get<int16_t>(bb, 0x6);
		const int by = g.get<int16_t>(bb, 0xA);
		const int32_t flight = g.get<int32_t>(bb, 0x1C);
		if (flight <= 0) {
			continue;
		}
		const int t = g.get<int16_t>(bb, 0x32);
		const double dd = std::sqrt(double((tx - bx) * (tx - bx) + (ty - by) * (ty - by)));
		const int dist = int(dd);
		const int lead = t * 512 / flight - 0x6F;
		const int x = bx + (g.get<int32_t>(bb, 0xC) / 256) * lead / 1328;
		const int y = by + (g.get<int32_t>(bb, 0x10) / 256) * lead / 1328;
		const int frac = dist * 256 / flight;
		const int off = frac > 128 ? frac - 128 : 128 - frac;
		const int height = t - ((off * off * t) >> 14) + int(g.get<uint16_t>(bb, 0x34)) * dist / flight;
		const int sx = x - camera_x;
		const int sy = top + screen_y(y) - camera_y;
		const SDL_Rect shadow = {sx - 2, sy - 1, 4, 3};
		SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
		SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 90);
		SDL_RenderFillRect(renderer_, &shadow);
		const SDL_Rect dst = {sx - 2, sy - height - 2, 4, 4};
		SDL_RenderCopy(renderer_, ball_, nullptr, &dst);
	}

	draw_clouds(save, battle, camera_x, camera_y, top, view_width, view_height, now);
	SDL_RenderSetClipRect(renderer_, nullptr);
}

// The clouds of the wind (the battle's [+0x670] direction, [+0x671] strength; the game makes them with
// 0x6107B9, white and half see-through): a few over a tile of the area that repeats, drifting with the wind.
void BattleView::draw_clouds(const SaveGame& save, size_t battle, int camera_x, int camera_y, int top,
	int view_width, int view_height, uint32_t now) {
	const BattleGraph& g = save.battles;
	if (clouds_.empty()) {
		for (int i = 0; i < kClouds; ++i) {
			Cloud c;
			c.x = float(next_random() % kCloudTile);
			c.y = float(next_random() % kCloudTile);
			c.image = int(next_random() % 6);
			c.alpha = Uint8(80 + next_random() % 30);
			clouds_.push_back(c);
		}
		clouds_time_ = now;
	}
	const int dir = g.get<uint8_t>(battle, 0x670);
	const float strength = float(g.get<uint8_t>(battle, 0x671));
	const float vx = sine256(dir) * strength / 64.0f;
	const float vy = -sine256(dir + 0x40) * strength / 64.0f * 11.0f / 17.0f;
	while (now - clouds_time_ >= kCloudStepMs) {
		clouds_time_ += kCloudStepMs;
		for (Cloud& c : clouds_) {
			c.x = std::fmod(c.x + vx + kCloudTile, float(kCloudTile));
			c.y = std::fmod(c.y + vy + kCloudTile, float(kCloudTile));
		}
	}
	for (const Cloud& c : clouds_) {
		SDL_Texture* image = cloud_images_[size_t(c.image)];
		if (!image) {
			continue;
		}
		int w = 0;
		int h = 0;
		SDL_QueryTexture(image, nullptr, nullptr, &w, &h);
		SDL_SetTextureAlphaMod(image, c.alpha);
		// Each copy of the tile that the screen part reaches.
		const int first_x = int(std::floor(float(camera_x - w) / kCloudTile));
		const int first_y = int(std::floor(float(camera_y - h) / kCloudTile));
		for (int ty = first_y; ty * kCloudTile < camera_y + view_height + h; ++ty) {
			for (int tx = first_x; tx * kCloudTile < camera_x + view_width + w; ++tx) {
				const int x = int(c.x) + tx * kCloudTile - camera_x - w / 2;
				const int y = top + int(c.y) + ty * kCloudTile - camera_y - h / 2;
				if (x + w < 0 || x > view_width || y + h < top || y > top + view_height) {
					continue;
				}
				const SDL_Rect dst = {x, y, w, h};
				SDL_RenderCopy(renderer_, image, nullptr, &dst);
			}
		}
	}
}

}  // namespace p3::app
