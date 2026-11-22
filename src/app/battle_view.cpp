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
// A town battle's: a tower's fire (0x12/0xB0 at levels 1 and 2, 0x601BC6: `feuer_klein`, `feuer_mittel`; the game
// has a third, `feuer`, for the town's fires) and a tower falling (0x12/0x30, 0x6014EE).
constexpr const char* kTowerFire[2] = {"iso/lcr/feuer_klein.lcr", "iso/lcr/feuer_mittel.lcr"};
constexpr char kTowerFall[] = "iso/lcr/schiffsexplosion.lcr";
// A ship's pennant (class 0x1E, 0x56CA0B; 32 directions of 5 frames at 7 a second).
constexpr char kPennant[] = "iso/lcr/faehnchen.lcr";
// The splinters' pictures (class 0x13: `brocken_a.lcr`, `brocken_b.lcr`; their head has another layout, so the
// viewer takes the pictures' 8 cells of 16 · 16, a piece turning, at their 15 frames a second).
constexpr const char* kSplinterImages[2] = {"images/creatures/_X_p_brocken_a.aim", "images/creatures/_X_p_brocken_b.aim"};
constexpr int kSplinterSize = 16;
constexpr int kSplinterFrames = 8;
constexpr uint32_t kSplinterFps = 15;
constexpr size_t kMaxSplinters = 300;
// The ripples' pace and strengths (by eye: the game's map is of bytes, a drop 0xFF).
constexpr uint32_t kRippleStepMs = 30;
constexpr int kSinkDrop = 2400;
constexpr int kBowDrop = 900;

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
	: renderer_(renderer), archive_(archive), tiles_(tiles) {
	for (const auto& [id, tile] : tiles_) {
		tile_ids_[id] = &tile;
		if (tile.cols > 1 && (tile.type == 0x12 || tile.type == 0x13)) {
			(tile.type == 0x12 ? rock_tiles_ : islet_tiles_).push_back(&tile);
		}
	}
	auto by_id = [](const p3::data::TileDef* a, const p3::data::TileDef* b) { return a->id < b->id; };
	std::sort(rock_tiles_.begin(), rock_tiles_.end(), by_id);
	std::sort(islet_tiles_.begin(), islet_tiles_.end(), by_id);
}

BattleView::~BattleView() {
	clear();
}

bool BattleView::at_town(const SaveGame& save, size_t battle) {
	return save.battles.get<uint32_t>(battle, 0) == p3::data::Sailing::kTownBattleVtable;
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
	pinned_.clear();
	hits_.clear();
	order_marked_ = false;
	cloud_looks_.clear();
	cloud_images_.clear();
	clouds_.clear();
	effects_.clear();
	sinking_.clear();
	burning_.clear();
	towers_ = {};
	splinter_images_.fill(nullptr);
	splinters_.clear();
	if (ripple_texture_) {
		SDL_DestroyTexture(ripple_texture_);
		ripple_texture_ = nullptr;
	}
	ripple_w_ = 0;
	ripple_h_ = 0;
	ripple_now_.clear();
	ripple_last_.clear();
	ripple_water_.clear();
	ripple_pixels_.clear();
	ripple_masked_ = false;
	last_place_.clear();
	sink_frame_.clear();
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
	for (const char* path : {kSplash, kPuff, kExplosion, kBurning, kPennant}) {
		want(path);
	}
	for (int k = 0; k < 3; ++k) {
		want(kGunSmoke[k]);
		want(kGunFire[k]);
	}
	if (at_town(save, battle)) {
		want(kTowerFire[0]);
		want(kTowerFire[1]);
		want(kTowerFall);
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
				std::find(cloud_images_.begin(), cloud_images_.end(), t) != cloud_images_.end() ||
				std::find(splinter_images_.begin(), splinter_images_.end(), t) != splinter_images_.end() ||
				std::find(pinned_.begin(), pinned_.end(), t) != pinned_.end();
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
		// The clouds' looks (class 0x14: their reader 0x580290 keeps them in *0x6E2F2C in iso/lcr/files.lst's order).
		cloud_looks_.clear();
		cloud_images_.clear();
		std::vector<uint8_t> list;
		if (const CprEntry* entry = archive_.find("iso/lcr/files.lst"); entry && archive_.read(*entry, list)) {
			size_t begin = 0;
			for (size_t i = 0; i <= list.size(); ++i) {
				if (i < list.size() && list[i] != 0 && list[i] != '\r' && list[i] != '\n') {
					continue;
				}
				const std::string name(reinterpret_cast<const char*>(list.data()) + begin, i - begin);
				begin = i + 1;
				if (name.compare(0, 5, "wolke") != 0) {
					continue;
				}
				std::vector<uint8_t> data;
				LcrAnim look;
				const CprEntry* file = archive_.find("iso/lcr/" + name);
				if (file && archive_.read(*file, data) && p3::data::parse_lcr(data.data(), data.size(), look) &&
					look.kind == 0x14 && !look.directions.empty() && !look.directions[0].empty()) {
					cloud_images_.push_back(texture(look.image, look.key == 0xFFFF00FFu));
					cloud_looks_.push_back(std::move(look));
				}
			}
		}
		for (size_t i = 0; i < splinter_images_.size(); ++i) {
			splinter_images_[i] = texture(kSplinterImages[i], true);
		}
		// Where each type's pennant goes (0x56B052): the points of its look of hull state 0 and sails 0 less that
		// look's anchor in each direction; the same for every look of the type.
		for (int type = 0; type < 4; ++type) {
			pennant_offsets_[size_t(type)].clear();
			const CprEntry* entry = archive_.find(p3::data::lcr_ship_path(type, 0, 0));
			std::vector<uint8_t> data;
			LcrAnim look;
			if (!entry || !archive_.read(*entry, data) || !p3::data::parse_lcr(data.data(), data.size(), look)) {
				continue;
			}
			for (size_t d = 0; d < look.pennant.size() && d < look.directions.size(); ++d) {
				const LcrFrame* f = look.directions[d].empty() ? nullptr : &look.directions[d][0];
				pennant_offsets_[size_t(type)].push_back(
					f ? SDL_Point{look.pennant[d].x - f->anchor_x, look.pennant[d].y - f->anchor_y} : SDL_Point{0, 0});
			}
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
	uint32_t now, const BattleTownLayer* town) {
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
			// 0x60329B: on a cell of the water or a sand bank a splash, on anything else a puff (0x12/0x20).
			spawn(!town || !town->splash || town->splash(int(x), int(y)) ? kSplash : kPuff, 0, x, y, now);
			break;
		case BattleEffect::Hit:
			spawn(e.result == 2 ? kExplosion : kPuff, 0, x, y, now);
			add_splinters(x, y, e.puffs, now);
			break;
		case BattleEffect::TowerHit:
			if (e.x >= 0) {  // Off the area (0xFFFF) once the hit made it fall: the game's puff is out of sight.
				spawn(kPuff, 0, x, y, now);
				add_splinters(x, y, e.puffs, now);
			}
			break;
		case BattleEffect::TowerFall:
			spawn(kTowerFall, 0, x, y, now);
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

// A tile with its front cell (x, y) of the town view's grid, as the town view draws it.
void BattleView::draw_tile(const p3::data::TileDef* tile, int x, int y, int camera_x, int camera_y, int top) {
	if (!tile || tile->slices.empty()) {
		return;
	}
	SDL_Texture* t = texture(tile->atlas, true);
	if (!t) {
		return;
	}
	if (std::find(pinned_.begin(), pinned_.end(), t) == pinned_.end()) {
		pinned_.push_back(t);
	}
	const p3::data::TileSlice& front = tile->slices[std::min(tile->front_slice, tile->slices.size() - 1)];
	const SDL_Rect src = {tile->src_x, tile->src_y, tile->width, tile->height};
	const SDL_Rect dst = {p3::data::cell_screen_x(x, y) - front.x - camera_x,
		top + p3::data::cell_screen_y(y) + p3::data::kTileHeight - front.height - front.y - camera_y, tile->width,
		tile->height};
	SDL_RenderCopy(renderer_, t, &src, &dst);
}

void BattleView::draw_reefs(const SaveGame& save, size_t battle, int camera_x, int camera_y, int top, int view_width,
	int view_height) {
	const BattleGraph& g = save.battles;
	auto pick = [&](const std::vector<const p3::data::TileDef*>& list, int w, int h, int x, int y) {
		std::vector<const p3::data::TileDef*> fit;
		for (const p3::data::TileDef* t : list) {
			if (t->cols == w && t->rows == h) {
				fit.push_back(t);
			}
		}
		return fit.empty() ? nullptr : fit[size_t(uint32_t(x * y) % fit.size())];
	};
	auto visible = [&](int x, int y, int w, int h) {
		const int c = 2 * x + (y & 1);
		const int x0 = (c - w + 1) * 17 - camera_x, x1 = (c + h + 1) * 17 - camera_x;
		const int y0 = top + (y - w - h - 4) * 11 - camera_y, y1 = top + (y + 2) * 11 - camera_y;
		return x1 >= 0 && x0 <= view_width && y1 >= top && y0 <= top + view_height;
	};
	// The ground first (the sand banks, the reefs' feet), then the reefs.
	for (int pass = 0; pass < 3; ++pass) {
		const size_t base = pass == 0 ? 0x672 : 0x872;
		for (int k = 0; k < 0x40; ++k) {
			const int x = g.get<int16_t>(battle, base + 8 * size_t(k));
			if (x < 0) {
				continue;
			}
			const int y = g.get<int16_t>(battle, base + 8 * size_t(k) + 2);
			const int w = g.get<int16_t>(battle, base + 8 * size_t(k) + 4);
			const int h = g.get<int16_t>(battle, base + 8 * size_t(k) + 6);
			if (!visible(x, y, w, h)) {
				continue;
			}
			if (pass == 0) {
				draw_tile(pick(islet_tiles_, w, h, x, y), x, y, camera_x, camera_y, top);
				continue;
			}
			const p3::data::TileDef* rock = pick(rock_tiles_, w, h, x, y);
			if (pass == 1 && rock && rock->params.size() > 1) {
				const auto ground = tile_ids_.find(uint16_t(rock->params[1]));
				if (ground != tile_ids_.end()) {
					draw_tile(ground->second, x, y, camera_x, camera_y, top);
				}
			} else if (pass == 2) {
				draw_tile(rock, x, y, camera_x, camera_y, top);
			}
		}
	}
}

// The battle's minimap as the game's town minimap «CTownMap» (0x59C590): its image painted cell by cell from the town
// view's map (0x59CA00, the layers 0 then 1: the water 0xFF226192; with no town shown, a sea battle, a reef, the type
// 0x12 of the battle's list [+0x872] (0x610379 → 0x5944E0), grey 0xFF828282 and a sand bank, 0x13 of [+0x672]
// (0x594640), yellow 0xFFFFFF00; drawn here as one box each, by eye), a town battle's the town's minimap (its battle
// map's 128.aim..); the view's frame in grey 0xFF646464; each object a 3 · 3 dot of its colour [+0x34] (the ships': the
// local player's 0xFF0000FF, a pirate's in the state 0x12 0xFF282828, another human's 0xFFFF0000, else white:
// 0x56C75C..).
void BattleView::draw_minimap(const SaveGame& save, size_t battle, const SDL_Rect& rect, const SDL_Rect& camera,
	SDL_Texture* town) const {
	const BattleGraph& g = save.battles;
	const double sx = double(rect.w) / width(save, battle);
	const double sy = double(rect.h) / height(save, battle);
	SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(renderer_, 0x22, 0x61, 0x92, 255);
	SDL_RenderFillRect(renderer_, &rect);
	if (town) {
		SDL_RenderCopy(renderer_, town, nullptr, &rect);  // The battle map's (the town view's area, whole).
	}
	for (const size_t base : {size_t(0x672), size_t(0x872)}) {
		if (town) {
			break;
		}
		if (base == 0x672) {
			SDL_SetRenderDrawColor(renderer_, 0xFF, 0xFF, 0x00, 255);  // The sand banks.
		} else {
			SDL_SetRenderDrawColor(renderer_, 0x82, 0x82, 0x82, 255);  // The reefs.
		}
		for (int k = 0; k < 0x40; ++k) {
			const int x = g.get<int16_t>(battle, base + 8 * size_t(k));
			if (x < 0) {
				continue;
			}
			const int y = g.get<int16_t>(battle, base + 8 * size_t(k) + 2);
			const int w = g.get<int16_t>(battle, base + 8 * size_t(k) + 4);
			const int h = g.get<int16_t>(battle, base + 8 * size_t(k) + 6);
			const int c = 2 * x + (y & 1);
			const SDL_Rect r = {rect.x + int((c - w + 1) * 17 * sx), rect.y + int((y - w - h + 1) * 11 * sy),
				std::max(2, int((w + h) * 17 * sx)), std::max(2, int((w + h) * 11 * sy))};
			SDL_RenderFillRect(renderer_, &r);
		}
	}
	const SDL_Rect frame = {rect.x + int(camera.x * sx), rect.y + int(camera.y * sy), std::max(3, int(camera.w * sx)),
		std::max(3, int(camera.h * sy))};
	SDL_SetRenderDrawColor(renderer_, 0x64, 0x64, 0x64, 255);
	SDL_RenderDrawRect(renderer_, &frame);
	const int player = save.player;
	for_each_ship(g, battle, [&](size_t e) {
		if (g.get<uint8_t>(e, 0x12A) & 0x6) {
			return;
		}
		const uint32_t index = g.get<uint32_t>(e, 0x4);
		if (index >= save.ships.size()) {
			return;
		}
		const p3::data::SavedShip& ship = save.ships[index];
		SDL_Color c = {255, 255, 255, 255};
		if (ship.owner() == player) {
			c = {0, 0, 255, 255};
		} else if (ship.status() == 0x12) {
			c = {0x28, 0x28, 0x28, 255};
		} else if (ship.owner() < save.merchants.size() && save.merchants[ship.owner()].memory.u16(0x8) == 0) {
			c = {255, 0, 0, 255};  // Another human's (0x5303C0 [+0x8] 0), as the town view.
		}
		const int x = rect.x + int(g.get<int16_t>(e, 0xA) * sx);
		const int y = rect.y + int(screen_y(g.get<int16_t>(e, 0xE)) * sy);
		const SDL_Rect dot = {x - 1, y - 1, 3, 3};
		SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, 255);
		SDL_RenderFillRect(renderer_, &dot);
	});
}

bool BattleView::minimap_point(const SaveGame& save, size_t battle, const SDL_Rect& rect, int x, int y, SDL_Point& at) {
	if (x < rect.x || y < rect.y || x >= rect.x + rect.w || y >= rect.y + rect.h) {
		return false;
	}
	at = {int(double(x - rect.x) * width(save, battle) / rect.w), int(double(y - rect.y) * height(save, battle) / rect.h)};
	return true;
}

// 0x610D69: a town battle's method +0x28 is 1 (0x602980), and then «Leave», «Flee» and «Plunder» are left out
// (no fleeing a town nor leaving its battle).
uint16_t BattleView::panel_buttons(const SaveGame& save, size_t battle, int player) {
	const BattleGraph& g = save.battles;
	const size_t b = battle;
	const bool town = at_town(save, battle);
	auto side_merchant = [&](size_t e) { return g.get<uint16_t>(b, 0x35A + 2 * size_t(g.get<uint8_t>(e, 0x13D))); };
	const int sel = g.ptr(b, 0x66C);
	const size_t chosen = sel >= 0 ? g.get<uint16_t>(b, 0xAD0) : 0;
	auto chosen_entry = [&](size_t i) { return g.ptr(size_t(sel), 4 * i); };
	if (chosen > 0 && (chosen_entry(0) < 0 || side_merchant(size_t(chosen_entry(0))) != uint16_t(player))) {
		return 0;
	}
	uint32_t bits = town ? 0 : 0x8000;
	// «Leave»: every ship not his out, fled or sunk (0x27) and none grappling.
	const int list = g.ptr(b, 0x4);
	const size_t ships = size_t(g.get<uint16_t>(b, 0x65A)) + g.get<uint16_t>(b, 0x658);
	for (size_t i = 0; !town && list >= 0 && i < ships; ++i) {
		const int e = g.ptr(size_t(list), 4 * i);
		if (e < 0 || side_merchant(size_t(e)) == uint16_t(player)) {
			continue;
		}
		const uint8_t f = g.get<uint8_t>(size_t(e), 0x12A);
		if ((f & 0x8) || !(f & 0x27)) {
			bits &= 0x7FFF;
		}
	}
	if (chosen == 0) {
		return uint16_t(bits);
	}
	bits |= 0x3022;
	const size_t first = size_t(chosen_entry(0));
	const uint8_t side = g.get<uint8_t>(first, 0x13D);
	const uint8_t team = g.get<uint8_t>(b, 0x559 + side);
	// «Flee», as the key (0x60F309): his team isn't 0, [+0xACB] at most 3, and an enemy of the list [+0x10]
	// without 0x66 (out, sinking, fled, ...).
	if (!town && team != 0 && g.get<uint8_t>(b, 0xACB) <= 3) {
		uint16_t k = g.get<uint16_t>(b, 0x10);
		const int next = g.ptr(b, 0xC);
		while (k != 0xFFFF && list >= 0) {
			const int e = g.ptr(size_t(list), 4 * size_t(k));
			if (e >= 0 && !(g.get<uint8_t>(size_t(e), 0x12A) & 0x66)) {
				bits |= 0x1;
				break;
			}
			k = next >= 0 ? g.get<uint16_t>(size_t(next), 2 * size_t(k)) : 0xFFFF;
		}
	}
	// «Plunder» and its pair: a side of his whose team is in [+0xAD2].
	for (size_t i = 0; !town && i < g.get<uint8_t>(b, 0x558); ++i) {
		if (g.get<uint16_t>(b, 0x35A + 2 * i) == uint16_t(player) &&
			((1u << g.get<uint8_t>(b, 0x559 + i)) & g.get<uint8_t>(b, 0xAD2))) {
			bits |= 0x300;
		}
	}
	// «Capture»: the other team has ships.
	if ((team != 0 ? g.get<uint16_t>(b, 0x658) : g.get<uint16_t>(b, 0x65A)) != 0) {
		bits |= 0x10;
	}
	for (size_t i = 0; i < chosen; ++i) {
		const int e = chosen_entry(i);
		if (e < 0) {
			continue;
		}
		const uint8_t f = g.get<uint8_t>(size_t(e), 0x12A);
		bits = (f & 0x20) ? bits | 0x4 : bits & ~0x2u;
		bits = (f & 0x1) ? bits | 0x4000 : bits & ~0x2000u;
		if (g.get<uint8_t>(size_t(e), 0x13E) != 0) {
			bits = (f & 0x80) ? (bits & ~0x200u) | 0x40 : (bits & ~0x20u) | 0x400;
		} else {
			bits &= ~0x220u;
		}
	}
	if (bits & 0x2) {
		return 0x2;
	}
	if (bits & 0x20) {
		bits &= ~0x40u;
	}
	if (bits & 0x200) {
		bits &= ~0x400u;
	}
	if (bits & 0x2000) {
		bits &= ~0x4000u;
	}
	return uint16_t(bits);
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

int BattleView::tower_at(const SaveGame& save, size_t battle, int x, int y) {
	if (!at_town(save, battle)) {
		return -1;
	}
	const BattleGraph& g = save.battles;
	int best = -1;
	long nearest = 0;
	for (size_t k = 0; k < 4; ++k) {
		const int tx = g.get<uint16_t>(battle, 0x1E9E + 2 * k);
		if (tx == 0xFFFF || g.get<int32_t>(battle, 0x1EC4 + 4 * k) == 0) {
			continue;
		}
		const int ty = screen_y(g.get<uint16_t>(battle, 0x1EA6 + 2 * k));
		// Its picture about its place (the place is 2 rows above its cell, on its body): 40 each side, 70 above
		// and its cell below, a finger's width more.
		if (x < tx - 40 || x > tx + 40 || y < ty - 70 || y > ty + 2 * p3::data::kRowStep + p3::data::kTileHeight) {
			continue;
		}
		const long d = long(x - tx) * (x - tx) + long(y - ty) * (y - ty);
		if (best < 0 || d < nearest) {
			best = int(k);
			nearest = d;
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

int BattleView::scene_entry(const SaveGame& save, size_t battle, uint16_t place) {
	const BattleGraph& g = save.battles;
	int found = -1;
	for_each_ship(g, battle, [&](size_t e) {
		if (found < 0 && g.get<uint16_t>(e, 0x138) == place) {
			found = int(e);
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
	int view_height, uint32_t now, const std::vector<uint16_t>& selection, std::vector<BattleLabel>& labels,
	const BattleTownLayer* town) {
	const BattleGraph& g = save.battles;
	hits_.clear();
	missing_looks_ = false;
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

	if (town) {
		// The town's battle map over the water: its ground, then (below) its buildings among the ships.
		if (town->ground) {
			town->ground(camera_x, camera_y - top);
		}
	} else {
		draw_reefs(save, battle, camera_x, camera_y, top, view_width, view_height);
	}
	// The town's objects (its buildings, trees...) up to a row of its cells: those behind a ship before it.
	auto town_rows = [&](int last_row) {
		if (town && town->objects) {
			town->objects(camera_x, camera_y - top, last_row);
		}
	};

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
		if (!sinking && !(flags & 0x4)) {
			// 0x56DA6F: a ship that moves drops a ripple by its bow each frame, to one side and then the other (by
			// the clock's parity), farther out the faster it goes.
			const auto last = last_place_.find(e);
			if (last != last_place_.end()) {
				const float moved = std::hypot(ax - last->second.x, ay - last->second.y);
				if (moved > 0.05f) {
					const float fx = sine256(dir), fy = -sine256(dir + 0x40) * 11.0f / 17.0f;
					const float side = (bow_side_ ? 1.0f : -1.0f) * (5.0f + std::min(moved * 6.0f, 8.0f));
					ripple_drop(int(ax + fx * 22.0f - fy * side), int(ay + fy * 22.0f + fx * side), kBowDrop);
				}
			}
			last_place_[e] = {ax, ay};
		}
		const int sails = std::min<int>(g.get<uint8_t>(e, 0x26), 4);
		const Anim* look = ship_look(ship.type() & 3, hull_state(ship), sails);
		if (!look && !sinking) {
			missing_looks_ = true;
		}
		const int frames = look ? int(look->lcr.directions.size()) : 32;
		const int frame = ((dir * frames + 128) >> 8) & (frames - 1);
		ships.push_back({int(ax) - camera_x, top + int(ay) - camera_y, ax, ay, e, look, frame, sinking});
	});
	for (auto it = sinking_.begin(); it != sinking_.end();) {
		it = present.count(it->first) ? std::next(it) : sinking_.erase(it);
	}
	for (auto it = sink_frame_.begin(); it != sink_frame_.end();) {
		it = present.count(it->first) ? std::next(it) : sink_frame_.erase(it);
	}
	std::sort(ships.begin(), ships.end(), [](const Drawn& a, const Drawn& b) { return a.y < b.y; });
	bow_side_ = !bow_side_;
	for (auto it = last_place_.begin(); it != last_place_.end();) {
		it = present.count(it->first) ? std::next(it) : last_place_.erase(it);
	}
	ripple_step(camera_x, camera_y, view_width, view_height, now, town);
	ripple_draw(camera_x, camera_y, top);
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
		// The town's rows whose cells are wholly behind the ship's place (a cell's diamond is 21 high, rows 11
		// apart).
		town_rows((int(d.ay) - p3::data::kTileHeight) / p3::data::kRowStep);
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
				// 0x6203D1: each new frame of the upper part up to 0xC drops ripples: its middle first, then
				// circles of 0x40 points (the sine tables 0x67AF88, 0x67B388), wider each frame.
				auto drop_frame = sink_frame_.find(d.entry);
				if (part == &s.upper && frame <= 0xC &&
					(drop_frame == sink_frame_.end() || drop_frame->second != int(frame))) {
					sink_frame_[d.entry] = int(frame);
					if (frame == 0) {
						ripple_drop(int(d.ax), int(d.ay), kSinkDrop);
					} else {
						const float r = 4.0f + 4.0f * float(frame);
						for (int k = 0; k < 0x40; k += 2) {
							ripple_drop(int(d.ax + r * sine256(k * 4)), int(d.ay - r * sine256(k * 4 + 0x40) * 11.0f / 17.0f),
								kSinkDrop / 2);
						}
					}
				}
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
		int pennant_top = d.y;
		if (d.look) {
			draw_frame(*d.look, d.frame, 0, d.x, d.y, 255, false);
			// Its pennant (0x56CA0B at the look's point of that direction), turned to the wind ([+0x670]: the town
			// view's [+0x598]) and waving faster the stronger it blows (0x568EA0: a frame's time less its
			// [+0x671] / 20).
			const std::vector<SDL_Point>& offsets = pennant_offsets_[size_t(ship.type() & 3)];
			const Anim* pennant = anim(kPennant);
			if (pennant && size_t(d.frame) < offsets.size() && !(g.get<uint8_t>(d.entry, 0x12A) & 0x4)) {
				const int wind = g.get<uint8_t>(battle, 0x670);
				const int strength = std::min<int>(g.get<uint8_t>(battle, 0x671), 19);
				const int dirs = int(pennant->lcr.directions.size());
				const int dir = ((wind * dirs + 128) >> 8) & (dirs - 1);
				const uint32_t period = std::max<uint32_t>(30, (1000 / std::max<uint32_t>(pennant->lcr.fps, 1)) *
						uint32_t(20 - strength) / 20);
				const size_t n = std::max<size_t>(pennant->lcr.directions[size_t(dir)].size(), 1);
				// Each ship's own pace (its scene's block), as each game sprite keeps its own clock.
				const int frame = int((now / period + d.entry) % n);
				const int px = d.x + offsets[size_t(d.frame)].x, py = d.y + offsets[size_t(d.frame)].y;
				draw_frame(*pennant, dir, frame, px, py, 255, false);
				const std::vector<LcrFrame>& pf = pennant->lcr.directions[size_t(dir)];
				if (!pf.empty()) {
					const LcrFrame& p0 = pf[size_t(frame) % pf.size()];
					pennant_top = py - (p0.anchor_y - p0.body.y);  // The label goes over it, not on it.
				}
			}
			const LcrFrame& f = d.look->lcr.directions[size_t(d.frame)][0];
			const int body_top = d.y - (f.anchor_y - f.body.y);
			top_y = std::min(body_top, pennant_top);
			hit.rect = {d.x - (f.anchor_x - f.body.x), body_top, f.body.w, f.body.h};
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

	town_rows(1 << 20);  // The rest of the town.
	if (town) {
		draw_towers(save, battle, camera_x, camera_y, top, now);
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

	draw_splinters(camera_x, camera_y, top, now);

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
		// The game's ball (4 · 4) twice its size, as the viewer's other small pictures: on the Vita's screen it
		// was hardly seen; its shadow on the water under it.
		SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
		SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 110);
		const SDL_Rect shadow[2] = {{sx - 3, sy - 1, 7, 3}, {sx - 2, sy - 2, 5, 5}};
		SDL_RenderFillRects(renderer_, shadow, 2);
		const SDL_Rect dst = {sx - 4, sy - height - 4, 8, 8};
		SDL_RenderCopy(renderer_, ball_, nullptr, &dst);
	}

	draw_clouds(save, battle, camera_x, camera_y, top, view_width, view_height, now);
	SDL_RenderSetClipRect(renderer_, nullptr);
}

// 0x6018DB (a town battle's method +0x20, before the sea's): for each standing tower ([+0x1EC4 + 4k] its
// strength, [+0x1E9E + 2k], [+0x1EA6 + 2k] its place in the area), at 391000 / 3 or less a puff of the burning
// smoke (0x12/0x24) about it each 200 + 1200 · strength / 196000 ms (the game's first wait is a wrapped number
// that its next frame brings down to that), drifting with the wind; below 196000 / 3, a fire on it (0x12/0xB0)
// of level 2 − strength · 6 / 196000: 1 small, 2 middling (a fire of another level replaces it; none at 0).
void BattleView::draw_towers(const SaveGame& save, size_t battle, int camera_x, int camera_y, int top, uint32_t now) {
	const BattleGraph& g = save.battles;
	for (size_t k = 0; k < towers_.size(); ++k) {
		TowerFire& t = towers_[k];
		const int32_t strength = g.get<int32_t>(battle, 0x1EC4 + 4 * k);
		const int x = g.get<uint16_t>(battle, 0x1E9E + 2 * k);
		const int y = g.get<uint16_t>(battle, 0x1EA6 + 2 * k);
		if (strength == 0) {
			t.level = 0;
			continue;
		}
		const float ax = float(x);
		const float ay = float(screen_y(y));
		if (int64_t(strength) * 3 <= 0x5FB40) {
			if (int32_t(now - t.next_smoke) >= 0) {
				smoke(save, battle, ax, ay, now);
				t.next_smoke = now + 200 + uint32_t(int64_t(strength) * 1200 / 196000);
			}
		}
		const int level = int64_t(strength) * 3 < 0x2FDA0 ? 2 - int(int64_t(strength) * 6 / 196000) : 0;
		if (level != t.level) {
			t.level = level;
			t.start = now;
		}
		if (t.level == 0) {
			continue;
		}
		if (const Anim* a = anim(kTowerFire[t.level - 1])) {
			const uint32_t fps = std::max<uint32_t>(a->lcr.fps, 1);
			const size_t n = std::max<size_t>(a->lcr.directions[0].size(), 1);
			const size_t frame = size_t(uint64_t(now - t.start) * fps / 1000) % n;  // It burns on.
			draw_frame(*a, 0, int(frame), x - camera_x, top + int(ay) - camera_y, 255, false);
		}
	}
}

// A hit's splinters (0x603656.., 0x5FFB7F: each 0x590610 of class 0x13 with a rand()): its picture `_a` or `_b`
// and its first frame at random (0x5803B0), a speed of rand() % 64 − 32 and rand() % 44 − 22 tenths of a pixel a
// second (0x580520: position + speed · ms / 10000), shaken a little each frame; it floats 3 to 6 s (by eye; the
// game's life isn't known), fading at the end.
void BattleView::add_splinters(float x, float y, int count, uint32_t now) {
	for (int i = 0; i < count && splinters_.size() < kMaxSplinters; ++i) {
		Splinter p;
		p.x = x;
		p.y = y;
		p.vx = float(int(next_random() % 64) - 32) / 10.0f;
		p.vy = float(int(next_random() % 44) - 22) / 10.0f;
		p.start = now;
		p.life = 3000 + next_random() % 3000;
		p.image = int(next_random() % 2);
		p.frame = int(next_random() % kSplinterFrames);
		splinters_.push_back(p);
	}
}

void BattleView::draw_splinters(int camera_x, int camera_y, int top, uint32_t now) {
	for (auto it = splinters_.begin(); it != splinters_.end();) {
		const uint32_t age = now - it->start;
		if (age >= it->life) {
			it = splinters_.erase(it);
			continue;
		}
		SDL_Texture* t = splinter_images_[size_t(it->image)];
		if (t) {
			const float sec = float(age) / 1000.0f;
			const int x = int(it->x + it->vx * sec) - camera_x;
			const int y = top + int(it->y + it->vy * sec) - camera_y;
			const int frame = (it->frame + int(uint64_t(age) * kSplinterFps / 1000)) % kSplinterFrames;
			const SDL_Rect src = {0, frame * kSplinterSize, kSplinterSize, kSplinterSize};
			const SDL_Rect dst = {x - kSplinterSize / 2, y - kSplinterSize / 2, kSplinterSize, kSplinterSize};
			const uint32_t left = it->life - age;
			SDL_SetTextureAlphaMod(t, Uint8(left < 800 ? left * 255 / 800 : 255));
			SDL_RenderCopy(renderer_, t, &src, &dst);
			SDL_SetTextureAlphaMod(t, 255);
		}
		++it;
	}
}

void BattleView::ripple_drop(int x, int y, int strength) {
	const int cx = x / kRippleCell - ripple_x_;
	const int cy = y / kRippleCell - ripple_y_;
	if (cx < 1 || cy < 1 || cx >= ripple_w_ - 1 || cy >= ripple_h_ - 1) {
		return;
	}
	const size_t i = size_t(cy) * size_t(ripple_w_) + size_t(cx);
	if (!ripple_water_.empty() && !ripple_water_[i]) {
		return;
	}
	ripple_now_[i] = int16_t(std::clamp(int(ripple_now_[i]) + strength, -12000, 12000));
}

void BattleView::ripple_step(int camera_x, int camera_y, int view_width, int view_height, uint32_t now,
	const BattleTownLayer* town) {
	const int w = view_width / kRippleCell + 2 * kRippleMargin + 1;
	const int h = view_height / kRippleCell + 2 * kRippleMargin + 1;
	const int fx = camera_x / kRippleCell - kRippleMargin;
	const int fy = camera_y / kRippleCell - kRippleMargin;
	if (w != ripple_w_ || h != ripple_h_) {
		ripple_w_ = w;
		ripple_h_ = h;
		ripple_x_ = fx;
		ripple_y_ = fy;
		ripple_now_.assign(size_t(w) * size_t(h), 0);
		ripple_last_.assign(size_t(w) * size_t(h), 0);
		ripple_pixels_.assign(size_t(w) * size_t(h), 0);
		ripple_masked_ = false;
		if (ripple_texture_) {
			SDL_DestroyTexture(ripple_texture_);
		}
		ripple_texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, w, h);
		if (ripple_texture_) {
			SDL_SetTextureBlendMode(ripple_texture_, SDL_BLENDMODE_BLEND);
			SDL_SetTextureScaleMode(ripple_texture_, SDL_ScaleModeLinear);
		}
		ripple_time_ = now;
	}
	if (fx != ripple_x_ || fy != ripple_y_) {
		// The camera moved: the field goes with the area (what leaves it is lost).
		const int dx = fx - ripple_x_, dy = fy - ripple_y_;
		for (std::vector<int16_t>* f : {&ripple_now_, &ripple_last_}) {
			std::vector<int16_t> moved(f->size(), 0);
			for (int y = 0; y < h; ++y) {
				const int sy = y + dy;
				if (sy < 0 || sy >= h) {
					continue;
				}
				for (int x = 0; x < w; ++x) {
					const int sx = x + dx;
					if (sx >= 0 && sx < w) {
						moved[size_t(y) * size_t(w) + size_t(x)] = (*f)[size_t(sy) * size_t(w) + size_t(sx)];
					}
				}
			}
			f->swap(moved);
		}
		ripple_x_ = fx;
		ripple_y_ = fy;
		ripple_masked_ = false;
	}
	if (!ripple_masked_) {
		ripple_water_.clear();
		if (town && town->water) {
			ripple_water_.assign(size_t(w) * size_t(h), 0);
			for (int y = 0; y < h; ++y) {
				for (int x = 0; x < w; ++x) {
					ripple_water_[size_t(y) * size_t(w) + size_t(x)] = town->water(
						(ripple_x_ + x) * kRippleCell + kRippleCell / 2, (ripple_y_ + y) * kRippleCell + kRippleCell / 2)
						? 1
						: 0;
				}
			}
		}
		ripple_masked_ = true;
	}
	// The classic spread: each cell the half of its four neighbours' sum less its value of the step before, a
	// sixteenth lost each step; nothing on the land.
	int steps = 0;
	while (now - ripple_time_ >= kRippleStepMs && steps < 4) {
		ripple_time_ += kRippleStepMs;
		++steps;
		for (int y = 1; y < h - 1; ++y) {
			for (int x = 1; x < w - 1; ++x) {
				const size_t i = size_t(y) * size_t(w) + size_t(x);
				if (!ripple_water_.empty() && !ripple_water_[i]) {
					ripple_last_[i] = 0;
					continue;
				}
				int v = ((int(ripple_now_[i - 1]) + ripple_now_[i + 1] + ripple_now_[i - size_t(w)] +
							 ripple_now_[i + size_t(w)]) >>
							1) -
					ripple_last_[i];
				v -= v >> 4;
				ripple_last_[i] = int16_t(std::clamp(v, -12000, 12000));
			}
		}
		ripple_now_.swap(ripple_last_);
	}
	if (now - ripple_time_ > 4 * kRippleStepMs) {
		ripple_time_ = now;  // Far behind (a slow frame): it doesn't catch up.
	}
}

void BattleView::ripple_draw(int camera_x, int camera_y, int top) {
	if (!ripple_texture_ || ripple_w_ < 3 || ripple_h_ < 3) {
		return;
	}
	// The slope towards the light (up and left): light on a crest's near side, shade on its far one.
	const int w = ripple_w_, h = ripple_h_;
	bool any = false;
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			const size_t i = size_t(y) * size_t(w) + size_t(x);
			uint32_t px = 0;
			if (x > 0 && y > 0 && x < w - 1 && y < h - 1) {
				const int d = (int(ripple_now_[i - 1]) - ripple_now_[i + 1]) + (int(ripple_now_[i - size_t(w)]) - ripple_now_[i + size_t(w)]);
				if (d > 12) {
					px = uint32_t(std::min(170, d / 3)) << 24 | 0xFFFFFF;  // RGBA32 words: alpha high, white.
					any = true;
				} else if (d < -12) {
					px = uint32_t(std::min(90, -d / 5)) << 24 | 0x302010;
					any = true;
				}
			}
			ripple_pixels_[i] = px;
		}
	}
	if (!any) {
		return;
	}
	SDL_UpdateTexture(ripple_texture_, nullptr, ripple_pixels_.data(), w * 4);
	const SDL_Rect dst = {ripple_x_ * kRippleCell - camera_x + kRippleCell / 2, top + ripple_y_ * kRippleCell - camera_y +
		kRippleCell / 2, w * kRippleCell, h * kRippleCell};
	SDL_RenderCopy(renderer_, ripple_texture_, nullptr, &dst);
}

// The clouds (battle_clouds.hpp): made once for the battle (0x6107B9 fills its empty slots: 56 at a town, 224 at sea,
// the wind's of then their speed), each stepped every frame by the frame's time (0x580520, from the view's list of
// creatures) and drawn by 0x610709 after the rest (0x588C6C): first every cloud's shadow, its picture black at alpha 50
// down to 40 by its slot, 30 pixels right and 150 down (0x5802F0: P2.cfg's DETAILS 2 or more, the game's 3), then the
// clouds white at alpha 128 down to 100. Their places are the view's pixels, wrapped in the area and 200 more (so that at
// sea, whose view is 0x1E04 · 11 / 17 high, the clouds below it are out of sight). Their look by DETAILS (3: all
// `wolke3`).
void BattleView::draw_clouds(const SaveGame& save, size_t battle, int camera_x, int camera_y, int top,
	int view_width, int view_height, uint32_t now) {
	const BattleGraph& g = save.battles;
	if (cloud_looks_.empty()) {
		return;
	}
	constexpr int kDetails = 3;  // P2.cfg's DETAILS (the game's files').
	const p3::data::CloudRand rand = [this] { return int(next_random()); };
	if (clouds_.empty()) {
		std::vector<uint32_t> variants;
		for (const LcrAnim& look : cloud_looks_) {
			variants.push_back(look.variant);
		}
		clouds_ = p3::data::make_battle_clouds(at_town(save, battle), g.get<uint8_t>(battle, 0x670),
			g.get<uint8_t>(battle, 0x671), variants, kDetails, rand);
		clouds_time_ = now;
	}
	const uint32_t elapsed = now - clouds_time_;
	clouds_time_ = now;
	for (p3::data::BattleCloud& c : clouds_) {
		p3::data::battle_cloud_step(c, elapsed, rand);
	}
	for (int pass = kDetails >= 2 ? 0 : 1; pass < 2; ++pass) {
		for (size_t k = 0; k < clouds_.size(); ++k) {
			const p3::data::BattleCloud& c = clouds_[k];
			const size_t look = c.look < cloud_looks_.size() ? c.look : 0;
			SDL_Texture* image = cloud_images_[look];
			const LcrFrame& f = cloud_looks_[look].directions[0][0];
			if (!image) {
				continue;
			}
			const int x = (c.x >> 16) - (f.anchor_x - f.body.x) - camera_x + (pass == 0 ? 30 : 0);
			const int y = top + (c.y >> 16) - (f.anchor_y - f.body.y) - camera_y + (pass == 0 ? 150 : 0);
			if (x + f.body.w < 0 || x > view_width || y + f.body.h < top || y > top + view_height) {
				continue;
			}
			const SDL_Rect src = {f.body.x, f.body.y, f.body.w, f.body.h};
			const SDL_Rect dst = {x, y, f.body.w, f.body.h};
			const uint8_t grey = pass == 0 ? 0 : 255;
			SDL_SetTextureColorMod(image, grey, grey, grey);
			SDL_SetTextureAlphaMod(image, pass == 0 ? p3::data::battle_cloud_shadow_alpha(int(k)) :
													  p3::data::battle_cloud_alpha(int(k)));
			SDL_RenderCopy(renderer_, image, &src, &dst);
			SDL_SetTextureColorMod(image, 255, 255, 255);
			SDL_SetTextureAlphaMod(image, 255);
		}
	}
}

}  // namespace p3::app
