// Visor de ciudades: dibuja las ciudades del juego original y permite
// recorrerlas, comerciar en su mercado y mandar los barcos del jugador de
// una ciudad a otra por el mapa del mar mientras pasa el tiempo. Es el
// primer paso del motor: de la simulación solo están el comercio y la
// navegación.

#include <SDL.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "p3data/aim.hpp"
#include "p3data/cpr.hpp"
#include "p3data/font.hpp"
#include "p3data/game_fixes.hpp"
#include "p3data/game_texts.hpp"
#include "p3data/hanse_tasks.hpp"
#include "p3data/image.hpp"
#include "p3data/letter_picture.hpp"
#include "p3data/letters.hpp"
#include "p3data/mail.hpp"
#include "p3data/navigation.hpp"
#include "p3data/player_actions.hpp"
#include "p3data/prices.hpp"
#include "p3data/sailing.hpp"
#include "p3data/simulation.hpp"
#include "p3data/savegame.hpp"
#include "p3data/seamap.hpp"
#include "p3data/town.hpp"
#include "p3data/town_buildings.hpp"
#include "p3data/trade.hpp"
#include "p3data/wares.hpp"
#include "debug_states.hpp"
#include "script.hpp"
#include "savegames.hpp"
#include "settings.hpp"

using namespace p3::data;
using p3::app::InputScript;

namespace {

// With --steps the viewer runs without a window and its time is the script's
// (one frame, 1/60 s), so that every run comes out the same.
const InputScript* g_script = nullptr;

Uint32 app_ticks() {
	return g_script ? g_script->now() : SDL_GetTicks();
}

// The game's interface texts (from its Patrician3.exe, see game_texts.hpp), or
// nullptr: then the viewer's own, in Spanish. Texts the game doesn't have
// (the Vita's buttons, the viewer's own messages) are always the viewer's.
const GameTexts* g_texts = nullptr;

std::string game_text(GameText id, const std::string& fallback) {
	return g_texts ? g_texts->get(id) : fallback;
}

// The language chosen in the options (settings.hpp): the viewer's own texts,
// those the game doesn't have, in Spanish or in English.
bool g_spanish = true;

std::string tr(const char* spanish, const char* english) {
	return g_spanish ? spanish : english;
}

constexpr int kScreenWidth = 960;
constexpr int kScreenHeight = 544;
constexpr char kTitle[] = "Patrician III";
constexpr int kPadSpeed = 12;  // Píxeles por fotograma con la cruceta.
constexpr int kStickSpeed = 16;  // Con el stick a fondo.
constexpr int kStickDeadZone = 6000;
// Partida que trae el juego con las 24 ciudades al empezar (año 1300).
constexpr char kStartSave[] = "missav/1.pat";
constexpr int kLuebeck = 14;
constexpr uint8_t kTownHallType = 0x20;
// Fuente del texto de la interfaz (Font4 de scripts/ttffonts.ini en la versión española).
constexpr char kUiFont[] = "fonts/Tiepolo_bold.ttf";
constexpr float kUiFontHeight = 23;
constexpr float kSmallFontHeight = 18;
constexpr int kBarHeight = 34;
constexpr int kBarMargin = 12;
constexpr int kTapDistance = 12;  // Píxeles que puede moverse un dedo en un toque.
constexpr SDL_Color kTextColor = {255, 236, 190, 255};
constexpr SDL_Color kDimColor = {200, 190, 170, 255};

#ifdef __vita__
const char* const kDataDirs[] = {"ux0:data/p3vita/"};
#else
const char* const kDataDirs[] = {"data/", "./"};
#endif
// El archivo inglés es la base de todas las versiones; el del idioma va encima.
constexpr char kBaseArchive[] = "p2arch0_eng.cpr";
constexpr char kLanguageArchive[] = "p2arch0_spa.cpr";

// The error on the console (on the PC) and, with a window, in a message box.
void show_error(SDL_Window* window, const std::string& message) {
#ifndef __vita__
	std::fprintf(stderr, "%s\n", message.c_str());
#endif
	if (!g_script) {
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, kTitle, message.c_str(), window);
	}
}

// `cpr_file` (--cpr): the English archive itself; for Spanish (`spanish`)
// the language one is mounted if it is in the same folder. `mounted_spanish`
// says whether the archive has the Spanish one (mounted, or alone).
std::unique_ptr<CprArchive> open_archive(const char* dir_override, const char* cpr_file, std::string& tried,
	bool spanish, bool& mounted_spanish) {
	mounted_spanish = false;
	if (cpr_file) {
		tried += std::string("\n") + cpr_file;
		auto archive = CprArchive::open(cpr_file);
		if (archive) {
			const std::string path = cpr_file;
			const size_t slash = path.find_last_of('/');
			const std::string dir = slash == std::string::npos ? "" : path.substr(0, slash + 1);
			if (path.substr(dir.size()) == kLanguageArchive) {
				mounted_spanish = true;
			} else if (spanish) {
				mounted_spanish = archive->mount(dir + kLanguageArchive);  // Optional.
			}
		}
		return archive;
	}
	std::vector<std::string> dirs;
	if (dir_override) {
		std::string dir = dir_override;
		if (!dir.empty() && dir.back() != '/') {
			dir += '/';
		}
		dirs.push_back(dir);
	} else {
		dirs.assign(std::begin(kDataDirs), std::end(kDataDirs));
	}

	for (const std::string& dir : dirs) {
		tried += "\n" + dir;
		if (auto archive = CprArchive::open(dir + kBaseArchive)) {
			if (spanish) {
				mounted_spanish = archive->mount(dir + kLanguageArchive);  // Opcional.
			}
			return archive;
		}
		// Sin la base, el archivo español solo también sirve para ver las ciudades.
		if (auto archive = CprArchive::open(dir + kLanguageArchive)) {
			mounted_spanish = true;
			return archive;
		}
	}
	return nullptr;
}

// The game's interface texts in a language (game_texts.hpp): from --exe (`exe`)
// or, in the data folder (or next to --cpr), Patrician3_spa.exe or
// Patrician3_eng.exe and Patrician3.exe: the first of that language's build
// (`matching`), or else the first that loads, or nullptr. Without --exe, the
// language's cache in the viewer's folder (`cache_dir`) first: made from the
// exe the first time, it saves reading the 3 MB of the exe at each start.
std::unique_ptr<GameTexts> load_game_texts(const char* exe, const char* cpr, const char* data_dir, bool spanish,
	bool& matching, const std::string& cache_dir) {
	const std::string cache = cache_dir + (spanish ? "p3vita-texts-es.bin" : "p3vita-texts-en.bin");
	if (!exe) {
		if (std::unique_ptr<GameTexts> cached = GameTexts::load_cache(cache); cached && cached->spanish() == spanish) {
			matching = true;
			return cached;
		}
	}
	std::vector<std::string> dirs;
	if (cpr) {
		const std::string path = cpr;
		const size_t slash = path.find_last_of('/');
		dirs.push_back(slash == std::string::npos ? "" : path.substr(0, slash + 1));
	} else if (data_dir) {
		const std::string dir = data_dir;
		dirs.push_back(dir + (dir.empty() || dir.back() == '/' ? "" : "/"));
	} else {
		dirs.assign(std::begin(kDataDirs), std::end(kDataDirs));
	}
	std::vector<std::string> paths;
	if (exe) {
		paths.push_back(exe);
	}
	for (const std::string& dir : dirs) {
		paths.push_back(dir + (spanish ? "Patrician3_spa.exe" : "Patrician3_eng.exe"));
		paths.push_back(dir + "Patrician3.exe");
		paths.push_back(dir + (spanish ? "Patrician3_eng.exe" : "Patrician3_spa.exe"));
	}
	matching = false;
	std::unique_ptr<GameTexts> first;
	for (const std::string& path : paths) {
		std::unique_ptr<GameTexts> texts = GameTexts::load(path);
		if (!texts) {
			continue;
		}
		if (texts->spanish() == spanish) {
			matching = true;
			if (!exe && !texts->save_cache(cache)) {
#ifndef __vita__
				std::fprintf(stderr, "can't write %s\n", cache.c_str());
#endif
			}
			return texts;
		}
		if (!first) {
			first = std::move(texts);
		}
	}
	return first;
}

// The Vita's buttons in the viewer's texts: a byte that TextRenderer draws as
// the button's symbol, in its colour (the game's font has none). Macros, to
// be joined to the literals ("\x01" followed by a letter would be one escape).
#define BTN_CROSS "\x01"
#define BTN_CIRCLE "\x02"
#define BTN_SQUARE "\x03"
#define BTN_TRIANGLE "\x04"

// The bytes TextRenderer draws as a symbol: the buttons above and the signs
// of the game's letters (letters.hpp: kLetterCoin, kLetterBarrel, kLetterLoad).
bool symbol_byte(char c) {
	return c >= 1 && c <= 7;
}

// The Vita's rear touchpad (an indirect touch device; the screen is direct) is
// of no use in this game: its touches are ignored. On the PC, the mouse's and
// the scripts' touches have no device and pass.
bool rear_touch(SDL_TouchID id) {
	const SDL_TouchDeviceType type = SDL_GetTouchDeviceType(id);
	return type == SDL_TOUCH_DEVICE_INDIRECT_ABSOLUTE || type == SDL_TOUCH_DEVICE_INDIRECT_RELATIVE;
}

// Texto de la interfaz con una fuente del juego subida como textura.
class TextRenderer {
public:
	TextRenderer(SDL_Renderer* renderer, std::unique_ptr<FontAtlas> font) : renderer_(renderer), font_(std::move(font)) {
		const Image& image = font_->image();
		texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, image.width,
			image.height);
		if (texture_) {
			SDL_UpdateTexture(texture_, nullptr, image.rgba.data(), image.width * 4);
			SDL_SetTextureBlendMode(texture_, SDL_BLENDMODE_BLEND);
		}
	}

	~TextRenderer() { SDL_DestroyTexture(texture_); }

	// The width of the text, the symbols included.
	int measure(const std::string& text) const {
		int width = 0;
		size_t start = 0;
		for (size_t i = 0; i <= text.size(); ++i) {
			if (i == text.size() || symbol_byte(text[i])) {
				if (i > start) {
					width += font_->measure(text.substr(start, i - start));
				}
				if (i < text.size()) {
					width += symbol_width(text[i]);
				}
				start = i + 1;
			}
		}
		return width;
	}
	int line_height() const { return font_->line_height(); }

	void draw(const std::string& text, int x, int y, SDL_Color color) {
		SDL_SetTextureColorMod(texture_, color.r, color.g, color.b);
		size_t start = 0;
		for (size_t i = 0; i <= text.size(); ++i) {
			if (i < text.size() && !symbol_byte(text[i])) {
				continue;
			}
			if (i > start) {
				const std::string part = text.substr(start, i - start);
				for (const FontAtlas::Quad& q : font_->layout(part, x, y)) {
					const SDL_Rect src = {q.src_x, q.src_y, q.width, q.height};
					const SDL_Rect dst = {q.dst_x, q.dst_y, q.width, q.height};
					SDL_RenderCopy(renderer_, texture_, &src, &dst);
				}
				x += font_->measure(part);
			}
			if (i < text.size()) {
				draw_symbol(text[i], x, y);
				x += symbol_width(text[i]);
			}
			start = i + 1;
		}
	}

private:
	// A button's symbol is about as wide as the line is high; a letter's sign, narrower.
	int symbol_width(char c) const { return c <= 4 ? font_->line_height() + 2 : font_->line_height() * 2 / 3 + 2; }

	// The symbol in a square of about the text's height: the buttons in the
	// PlayStation's colours, their lines two pixels thick; the letters' coin
	// (a gold disc), barrel and load (a sack), small and filled.
	void draw_symbol(char c, int x, int y) {
		const int h = font_->line_height();
		const int size = h * 3 / 5;
		const int left = x + (symbol_width(c) - size) / 2;
		const int top = y + (h - size) / 2;
		const int right = left + size - 1;
		const int bottom = top + size - 1;
		SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
		auto line = [&](int x1, int y1, int x2, int y2) {
			SDL_RenderDrawLine(renderer_, x1, y1, x2, y2);
			SDL_RenderDrawLine(renderer_, x1 + 1, y1, x2 + 1, y2);
		};
		if (c >= 5) {
			draw_sign(c, left, top, size);
			return;
		}
		switch (c) {
		case 1:  // Cross, blue.
			SDL_SetRenderDrawColor(renderer_, 124, 178, 232, 255);
			line(left, top, right - 1, bottom);
			line(right - 1, top, left, bottom);
			break;
		case 2: {  // Circle, red.
			SDL_SetRenderDrawColor(renderer_, 255, 102, 102, 255);
			const float cx = (left + right) / 2.0f;
			const float cy = (top + bottom) / 2.0f;
			for (const float r : {size / 2.0f, size / 2.0f - 1.0f}) {
				constexpr int kSteps = 24;
				for (int k = 0; k < kSteps; ++k) {
					const float a1 = 6.2831853f * float(k) / kSteps;
					const float a2 = 6.2831853f * float(k + 1) / kSteps;
					SDL_RenderDrawLine(renderer_, int(std::lround(cx + r * std::cos(a1))),
						int(std::lround(cy + r * std::sin(a1))), int(std::lround(cx + r * std::cos(a2))),
						int(std::lround(cy + r * std::sin(a2))));
				}
			}
			break;
		}
		case 3: {  // Square, pink.
			SDL_SetRenderDrawColor(renderer_, 255, 105, 248, 255);
			const SDL_Rect outer = {left, top, size, size};
			const SDL_Rect inner = {left + 1, top + 1, size - 2, size - 2};
			SDL_RenderDrawRect(renderer_, &outer);
			SDL_RenderDrawRect(renderer_, &inner);
			break;
		}
		default:  // Triangle, green.
			SDL_SetRenderDrawColor(renderer_, 64, 226, 160, 255);
			line((left + right) / 2, top, left, bottom);
			line((left + right) / 2, top, right - 1, bottom);
			line(left, bottom, right, bottom);
			SDL_RenderDrawLine(renderer_, left, bottom - 1, right, bottom - 1);
			break;
		}
	}

	// The game draws \C, \B and \L of its texts as small pictures.
	void draw_sign(char c, int left, int top, int size) {
		if (c == 5) {  // The coin: a gold disc with a darker rim.
			const float r = size / 2.0f;
			const float cx = left + r - 0.5f;
			for (int k = 0; k < size; ++k) {
				const float dy = float(k) - r + 0.5f;
				const int half = int(std::lround(std::sqrt(std::max(0.0f, r * r - dy * dy))));
				SDL_SetRenderDrawColor(renderer_, 236, 190, 60, 255);
				SDL_RenderDrawLine(renderer_, int(cx) - half + 1, top + k, int(cx) + half - 1, top + k);
				SDL_SetRenderDrawColor(renderer_, 150, 105, 25, 255);
				SDL_RenderDrawPoint(renderer_, int(cx) - half, top + k);
				SDL_RenderDrawPoint(renderer_, int(cx) + half, top + k);
			}
			return;
		}
		const int w = size * 3 / 4;
		const SDL_Rect body = {left + (size - w) / 2, top, w, size};
		if (c == 6) {  // A barrel: brown staves and two dark hoops.
			SDL_SetRenderDrawColor(renderer_, 160, 105, 55, 255);
			SDL_RenderFillRect(renderer_, &body);
			SDL_SetRenderDrawColor(renderer_, 70, 45, 25, 255);
			SDL_RenderDrawRect(renderer_, &body);
			SDL_RenderDrawLine(renderer_, body.x, top + size / 3, body.x + w - 1, top + size / 3);
			SDL_RenderDrawLine(renderer_, body.x, top + size * 2 / 3, body.x + w - 1, top + size * 2 / 3);
		} else {  // A load: a pale sack tied at the top.
			const SDL_Rect sack = {body.x, top + size / 4, w, size - size / 4};
			SDL_SetRenderDrawColor(renderer_, 205, 185, 140, 255);
			SDL_RenderFillRect(renderer_, &sack);
			SDL_SetRenderDrawColor(renderer_, 110, 90, 60, 255);
			SDL_RenderDrawRect(renderer_, &sack);
			SDL_RenderDrawLine(renderer_, body.x + w / 2, top, body.x + w / 2, top + size / 4);
			SDL_RenderDrawLine(renderer_, body.x + w / 4, top + size / 4, body.x + w * 3 / 4, top + size / 4);
		}
	}

	SDL_Renderer* renderer_;
	std::unique_ptr<FontAtlas> font_;
	SDL_Texture* texture_ = nullptr;
};

// Mapa de una ciudad listo para dibujar: los trozos de sprite con su textura.
// The town's buildings the player goes into, as in the game by clicking on
// them: the Market Hall (building type 0x23, «stadtkontor», .let type 0x00),
// the tavern (0x24, «kneipe», 0x02), the shipyard (0x2E, «werft», 0x01) and
// the repair dock (0x01, «repwerft», 0x1B) (savegame.md §8.1); their names,
// the game's (0x6A57C8 + 4 · the building's type).
enum class Place { None, MarketHall, Tavern, Shipyard, Ship, Office };

struct PlaceInfo {
	uint32_t let_type;
	Place place;
	GameText name;
	const char* spanish;
	const char* english;
};

constexpr PlaceInfo kPlaces[] = {
	{0x00, Place::MarketHall, GameText::BuildingMarketHall, "Lonja", "Market Hall"},
	{0x02, Place::Tavern, GameText::BuildingTavern, "Taberna", "Tavern"},
	{0x01, Place::Shipyard, GameText::BuildingShipyard, "Astillero", "Shipyard"},
	{0x1B, Place::Shipyard, GameText::BuildingRepairDock, "Muelle", "Repair Dock"},
	// The player's office («spielerkontor», building type 0x00, drawn only
	// where he has one): the market with the office (0x59AE40 opens the mode 2).
	{0x2C, Place::Office, GameText::Office, "Oficina", "Office"},
};

const PlaceInfo* place_of(uint32_t let_type) {
	for (const PlaceInfo& p : kPlaces) {
		if (p.let_type == let_type) {
			return &p;
		}
	}
	return nullptr;
}

class TownView {
public:
	TownView(SDL_Renderer* renderer, const CprArchive& archive)
		: renderer_(renderer), archive_(archive), tiles_(load_tileset(archive)) {
		for (const auto& [id, tile] : tiles_) {
			if (place_of(tile.type)) {
				places_by_atlas_[tile.atlas].push_back(&tile);
			}
		}
	}

	~TownView() {
		for (auto& [path, texture] : atlases_) {
			SDL_DestroyTexture(texture);
		}
	}

	const TileSet& tiles() const { return tiles_; }

	void show(const TownMap& town) {
		items_.clear();
		width_ = 0;
		height_ = 0;
		const std::vector<TownSprite> sprites = build_town_sprites(town, tiles_);
		items_.reserve(sprites.size());
		for (const TownSprite& sprite : sprites) {
			SDL_Texture* texture = tile_texture(sprite.tile);
			if (!texture) {
				continue;
			}
			items_.push_back({texture, {sprite.src_x, sprite.src_y, sprite.width, sprite.height},
				{sprite.dst_x, sprite.dst_y, sprite.width, sprite.height}, sprite.tile});
			width_ = std::max(width_, sprite.dst_x + sprite.width);
			height_ = std::max(height_, sprite.dst_y + sprite.height);
		}
	}

	// Dibuja lo que cae dentro de la pantalla con la cámara en (camera_x, camera_y).
	// The building of the .let type `highlight` (one of kPlaces) is drawn once
	// more on top, added, so that it glows (the one the cursor points at).
	void draw(int camera_x, int camera_y, int highlight = -1) {
		const SDL_Rect screen = {camera_x, camera_y, kScreenWidth, kScreenHeight};
		for (const Item& item : items_) {
			if (!SDL_HasIntersection(&item.dst, &screen)) {
				continue;
			}
			SDL_Rect dst = item.dst;
			dst.x -= camera_x;
			dst.y -= camera_y;
			SDL_RenderCopy(renderer_, item.texture, &item.src, &dst);
		}
		if (highlight < 0) {
			return;
		}
		for (const Item& item : items_) {
			if (!item.tile || int(item.tile->type) != highlight || !SDL_HasIntersection(&item.dst, &screen)) {
				continue;
			}
			SDL_Rect dst = item.dst;
			dst.x -= camera_x;
			dst.y -= camera_y;
			SDL_SetTextureBlendMode(item.texture, SDL_BLENDMODE_ADD);
			SDL_SetTextureColorMod(item.texture, 110, 95, 60);
			SDL_RenderCopy(renderer_, item.texture, &item.src, &dst);
			SDL_SetTextureColorMod(item.texture, 255, 255, 255);
			SDL_SetTextureBlendMode(item.texture, SDL_BLENDMODE_BLEND);
		}
	}

	// The .let type of the building of kPlaces under the town's pixel (x, y)
	// (the front one, where its picture isn't transparent), or -1.
	int place_at(int x, int y) const {
		for (auto it = items_.rbegin(); it != items_.rend(); ++it) {
			const Item& item = *it;
			if (!item.tile || x < item.dst.x || y < item.dst.y || x >= item.dst.x + item.dst.w ||
				y >= item.dst.y + item.dst.h) {
				continue;
			}
			const auto mask = masks_.find(item.tile);
			if (mask == masks_.end()) {
				continue;
			}
			const int mx = item.src.x - item.tile->src_x + (x - item.dst.x);
			const int my = item.src.y - item.tile->src_y + (y - item.dst.y);
			if (mx >= 0 && my >= 0 && mx < mask->second.width && my < mask->second.height &&
				mask->second.opaque[size_t(my) * size_t(mask->second.width) + size_t(mx)]) {
				return int(item.tile->type);
			}
		}
		return -1;
	}

	// Where the building of that .let type is drawn (in the town's pixels).
	SDL_Rect bounds(int let_type) const {
		SDL_Rect r{0, 0, 0, 0};
		for (const Item& item : items_) {
			if (item.tile && int(item.tile->type) == let_type) {
				if (r.w == 0) {
					r = item.dst;
				} else {
					SDL_UnionRect(&r, &item.dst, &r);
				}
			}
		}
		return r;
	}

	int width() const { return width_; }
	int height() const { return height_; }

private:
	struct Item {
		SDL_Texture* texture;
		SDL_Rect src;
		SDL_Rect dst;
		const TileDef* tile;
	};
	// Where a building of kPlaces is opaque (alpha ≥ 128), one byte per pixel
	// of its tile's rectangle in the atlas: made when its atlas is decoded.
	struct Mask {
		int width = 0;
		int height = 0;
		std::vector<uint8_t> opaque;
	};

	// A tile's atlas texture, kept by the tile (tiles_ doesn't change, so its
	// pointers stay): looking it up by the atlas's path for each sprite took
	// most of a town's preparation, about 0.2 s on the Vita with the rest.
	SDL_Texture* tile_texture(const TileDef* tile) {
		auto it = tile_textures_.find(tile);
		if (it != tile_textures_.end()) {
			return it->second;
		}
		return tile_textures_[tile] = atlas_texture(tile->atlas);
	}

	// Los atlas se decodifican y suben una sola vez, y se comparten entre ciudades.
	SDL_Texture* atlas_texture(const std::string& path) {
		auto it = atlases_.find(path);
		if (it != atlases_.end()) {
			return it->second;
		}
		SDL_Texture*& texture = atlases_[path];
		std::vector<uint8_t> data;
		Image image;
		const CprEntry* entry = archive_.find(path);
		if (!entry || !archive_.read(*entry, data) || !decode_aim(data.data(), data.size(), image)) {
			return texture = nullptr;
		}
		apply_magenta_key(image);
		if (const auto places = places_by_atlas_.find(path); places != places_by_atlas_.end()) {
			for (const TileDef* tile : places->second) {
				Mask& mask = masks_[tile];
				mask.width = tile->width;
				mask.height = tile->height;
				mask.opaque.assign(size_t(tile->width) * size_t(tile->height), 0);
				for (int y = 0; y < tile->height; ++y) {
					for (int x = 0; x < tile->width; ++x) {
						const int ax = tile->src_x + x;
						const int ay = tile->src_y + y;
						if (ax < image.width && ay < image.height) {
							mask.opaque[size_t(y) * size_t(tile->width) + size_t(x)] =
								image.rgba[(size_t(ay) * size_t(image.width) + size_t(ax)) * 4 + 3] >= 128;
						}
					}
				}
			}
		}
		texture = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, image.width,
			image.height);
		if (texture) {
			SDL_UpdateTexture(texture, nullptr, image.rgba.data(), image.width * 4);
			SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
		}
		return texture;
	}

	SDL_Renderer* renderer_;
	const CprArchive& archive_;
	TileSet tiles_;
	std::map<std::string, SDL_Texture*> atlases_;
	std::unordered_map<const TileDef*, SDL_Texture*> tile_textures_;
	std::map<std::string, std::vector<const TileDef*>> places_by_atlas_;  // The tiles of kPlaces, by atlas.
	std::unordered_map<const TileDef*, Mask> masks_;
	std::vector<Item> items_;
	int width_ = 0;
	int height_ = 0;
};

// Textura de una imagen estándar (BMP, TGA...) del archivo; nullptr si falla.
SDL_Texture* load_texture(SDL_Renderer* renderer, const CprArchive& archive, const char* path, int* width = nullptr,
	int* height = nullptr) {
	const CprEntry* entry = archive.find(path);
	std::vector<uint8_t> data;
	Image image;
	if (!entry || !archive.read(*entry, data) || !decode_standard_image(data.data(), data.size(), image)) {
		return nullptr;
	}
	SDL_Texture* texture =
		SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, image.width, image.height);
	if (texture) {
		SDL_UpdateTexture(texture, nullptr, image.rgba.data(), image.width * 4);
		SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
	}
	if (width) {
		*width = image.width;
	}
	if (height) {
		*height = image.height;
	}
	return texture;
}

// How many of the sea map's rows load a frame (SeaMapView::step) while the
// town waits for it with its notice.
constexpr int kSeaMapRowsPerFrame = 240;

// Mapa del mar: la imagen del juego con las ciudades y los barcos de la
// partida, en las mismas coordenadas que guarda la partida.
//
// The map's image (an 8-bit BMP of 2400 × 1800) goes into its texture some
// rows at a time (step): read from the archive and decoded through its
// palette straight into the texture (decode_bmp8), while the town stays on
// screen with a notice (all at once it was about a second of black screen on
// the Vita; loading it in the background while the player was in the town
// made the town lag). Another kind of image is loaded whole at the start
// (load_texture).
class SeaMapView {
public:
	SeaMapView(SDL_Renderer* renderer, const CprArchive& archive)
		: renderer_(renderer), archive_(archive), towns_(load_sea_map_towns(archive)) {
		start_map();
		symbols_ = load_texture(renderer, archive, kTownSymbols);
		marker_ = load_texture(renderer, archive, kShipMarker, &marker_width_, &marker_height_);
		for (size_t i = 0; i < ships_.size(); ++i) {
			ships_[i] = load_texture(renderer, archive, kShipSprites[i]);
		}
	}

	~SeaMapView() {
		for (SDL_Texture* texture : {map_, symbols_, marker_}) {
			SDL_DestroyTexture(texture);
		}
		for (SDL_Texture* texture : ships_) {
			SDL_DestroyTexture(texture);
		}
	}

	bool ok() const { return map_ && !towns_.empty(); }
	int width() const { return width_; }
	int height() const { return height_; }

	// Whether the map's image is all in its texture (or failed: ok() is false).
	bool ready() const { return !map_entry_ || next_row_ >= bmp_.height; }

	// Up to `rows` more rows of the map's image, in the file's order.
	void step(int rows) {
		if (ready()) {
			return;
		}
		const int n = std::min(rows, bmp_.height - next_row_);
		// The file's rows [next_row_, next_row_ + n): the image's last ones first
		// in a bottom-up BMP.
		const int y0 = bmp_.bottom_up ? bmp_.height - next_row_ - n : next_row_;
		const uint32_t offset = bmp_.pixels + uint32_t(next_row_) * bmp_.stride;
		const SDL_Rect rect = {0, y0, bmp_.width, n};
		void* pixels = nullptr;
		int pitch = 0;
		if (!archive_.read_range(*map_entry_, offset, uint32_t(n) * bmp_.stride, chunk_) ||
			SDL_LockTexture(map_, &rect, &pixels, &pitch) != 0) {
			SDL_DestroyTexture(map_);
			map_ = nullptr;
			map_entry_ = nullptr;
			return;
		}
		decode_bmp8(bmp_, chunk_.data(), offset, 0, y0, bmp_.width, n, static_cast<uint8_t*>(pixels), size_t(pitch));
		SDL_UnlockTexture(map_);
		next_row_ += n;
		if (ready()) {
			chunk_ = std::vector<uint8_t>();
		}
	}

	// The rest of the map's image at once.
	void finish() {
		while (!ready()) {
			step(kSeaMapRowsPerFrame);
		}
	}

	// Centro del símbolo de una ciudad de la partida.
	SDL_Point town_center(const SaveGame& save, size_t town) const {
		const SeaMapTown& t = towns_[size_t(save.towns[town].town_id())];
		return {t.symbol_x + kTownSymbolSize / 2, t.symbol_y + kTownSymbolSize / 2};
	}

	// Ciudad de la partida cuyo símbolo (o nombre, con margen) está en un punto del mapa; -1 si ninguna.
	int town_at(const SaveGame& save, int x, int y) const {
		for (size_t i = 0; i < save.towns.size(); ++i) {
			const SDL_Point c = town_center(save, i);
			if (std::abs(x - c.x) <= kTownSymbolSize && std::abs(y - c.y) <= kTownSymbolSize) {
				return int(i);
			}
		}
		return -1;
	}

	// `course`: barco del que se dibuja lo que le queda de ruta.
	void draw(const SaveGame& save, TextRenderer* names_text, const std::vector<std::string>& names, int camera_x,
		int camera_y, size_t selected, const SavedShip* course) {
		const SDL_Rect src = {camera_x, camera_y, std::min(kScreenWidth, width_ - camera_x),
			std::min(kScreenHeight, height_ - camera_y)};
		const SDL_Rect dst = {0, 0, src.w, src.h};
		SDL_RenderCopy(renderer_, map_, &src, &dst);

		// Ciudades: azules las que tienen oficina del jugador; la elegida, resaltada.
		for (size_t i = 0; i < save.towns.size(); ++i) {
			const int id = save.towns[i].town_id();
			const SeaMapTown& t = towns_[size_t(id)];
			bool kontor = false;
			for (const SavedOffice& office : save.offices) {
				kontor |= office.used() && office.owner() == save.player && office.town() == i;
			}
			const int frame = (kontor ? 3 : 0) + (i == selected ? 1 : 0);
			const SDL_Rect symbol_src = {(frame % 3) * kTownSymbolSize, (frame / 3) * kTownSymbolSize,
				kTownSymbolSize, kTownSymbolSize};
			const SDL_Rect symbol_dst = {t.symbol_x - camera_x, t.symbol_y - camera_y, kTownSymbolSize,
				kTownSymbolSize};
			SDL_RenderCopy(renderer_, symbols_, &symbol_src, &symbol_dst);
			if (names_text && id < int(names.size())) {
				const std::string& name = names[size_t(id)];
				const int x = t.name_right_aligned ? t.name_x - names_text->measure(name) : t.name_x;
				names_text->draw(name, x - camera_x, t.name_y - camera_y,
					i == selected ? SDL_Color{120, 20, 10, 255} : SDL_Color{40, 25, 10, 255});
			}
		}

		if (course && course->route && (course->status() == kShipSailing || course->status() == kShipLeaving)) {
			std::vector<SDL_Point> line = {{(course->x() >> 16) - camera_x, (course->y() >> 16) - camera_y}};
			for (size_t i = course->route_index(); i < course->route->size(); ++i) {
				const MapPoint p = (*course->route)[i];
				line.push_back({p.x - camera_x, p.y - camera_y});
			}
			// Dos líneas de un píxel, una al lado de la otra.
			SDL_SetRenderDrawColor(renderer_, 120, 30, 15, 255);
			SDL_RenderDrawLines(renderer_, line.data(), int(line.size()));
			for (SDL_Point& p : line) {
				++p.y;
			}
			SDL_RenderDrawLines(renderer_, line.data(), int(line.size()));
		}

		// Barcos: los que navegan, con su rumbo; los del jugador, con su marca
		// (la de los atracados, sobre su ciudad).
		for (const SavedShip& ship : save.ships) {
			if (!ship.in_use()) {
				continue;
			}
			int x = ship.x() >> 16;
			int y = ship.y() >> 16;
			const bool sailing = ship.status() == 0x0F || ship.status() == 0x12;
			if (sailing) {
				const int frame = ship.heading() & 15;
				const SDL_Rect ship_src = {(frame % 4) * kShipSpriteWidth, (frame / 4) * kShipSpriteHeight,
					kShipSpriteWidth, kShipSpriteHeight};
				const SDL_Rect ship_dst = {x - kShipSpriteWidth / 2 - camera_x, y - kShipSpriteHeight / 2 - camera_y,
					kShipSpriteWidth, kShipSpriteHeight};
				SDL_RenderCopy(renderer_, ships_[ship.type() & 3], &ship_src, &ship_dst);
			}
			if (ship.owner() != save.player || (!sailing && ship.town() >= save.towns.size())) {
				continue;
			}
			if (!sailing) {
				const SDL_Point c = town_center(save, ship.town());
				x = c.x;
				y = c.y + kShipSpriteHeight / 2 - kTownSymbolSize;
			}
			const SDL_Rect marker_dst = {x - marker_width_ / 2 - camera_x,
				y - kShipSpriteHeight / 2 - marker_height_ - camera_y, marker_width_, marker_height_};
			SDL_RenderCopy(renderer_, marker_, nullptr, &marker_dst);
		}
	}

private:
	// The map's texture, empty: an 8-bit BMP is filled by step; another image
	// is loaded whole now.
	void start_map() {
		map_entry_ = archive_.find(kSeaMapImage);
		std::vector<uint8_t> head;
		if (map_entry_ && archive_.read_range(*map_entry_, 0, std::min<uint32_t>(map_entry_->size, 2048), head) &&
			parse_bmp8(head.data(), head.size(), bmp_) &&
			map_entry_->size >= uint64_t(bmp_.pixels) + uint64_t(bmp_.stride) * uint64_t(bmp_.height)) {
			map_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, bmp_.width,
				bmp_.height);
			if (map_) {
				SDL_SetTextureBlendMode(map_, SDL_BLENDMODE_BLEND);
				width_ = bmp_.width;
				height_ = bmp_.height;
				return;
			}
		}
		map_entry_ = nullptr;
		map_ = load_texture(renderer_, archive_, kSeaMapImage, &width_, &height_);
	}

	SDL_Renderer* renderer_;
	const CprArchive& archive_;
	std::vector<SeaMapTown> towns_;
	const CprEntry* map_entry_ = nullptr;  // While the map's image loads by steps.
	Bmp8 bmp_;
	int next_row_ = 0;  // The file's next row to load.
	std::vector<uint8_t> chunk_;
	SDL_Texture* map_ = nullptr;
	SDL_Texture* symbols_ = nullptr;
	SDL_Texture* marker_ = nullptr;
	std::array<SDL_Texture*, 4> ships_{};
	int width_ = 0;
	int height_ = 0;
	int marker_width_ = 0;
	int marker_height_ = 0;
};

// Qué se ve: una ciudad de la partida de inicio, el mapa del mar, o una
// plantilla de mapa con todos los edificios del diseñador (.editor) o solo con
// el terreno (.town).
enum class Mode { Saved, Sea, Designer, Terrain };

struct Selection {
	Mode mode = Mode::Saved;
	size_t saved_index = 0;
	size_t template_index = 0;
};

// The saved town on screen, drawn from its current state (the town's memory,
// not what it was at load time): its template is loaded once, and its
// buildings are placed again whenever the simulation changes them.
class LiveTown {
public:
	bool load(const CprArchive& archive, const SaveGame& save, size_t index, const std::vector<int>& regions,
		std::string& error) {
		if (index >= save.towns.size()) {
			loaded_ = false;
			error = "the save has no town " + std::to_string(index);
			return false;
		}
		if (loaded_ && index == index_) {
			return true;  // Its template is already here (another game or language: a new LiveTown).
		}
		index_ = index;
		key_ = 0;
		drawn_ = false;
		loaded_ = load_saved_town_template(archive, save.towns[index], regions, base_, &error);
		return loaded_;
	}

	// Whether the town `index` was drawn last and its buildings haven't changed since.
	bool drawn(const SaveGame& save, size_t index) const {
		return loaded_ && drawn_ && index == index_ && !changed(save);
	}

	// The town as it is now.
	void draw(const SaveGame& save, const TileSet& tiles, TownMap& out) {
		place_saved_town(base_, save, index_, tiles, out);
		key_ = drawn_buildings_key(save, index_);
		drawn_ = true;
	}

	// The view shows something else (a template map): draw it again next time.
	void forget_drawn() { drawn_ = false; }

	// Whether its buildings have changed since the last draw.
	bool changed(const SaveGame& save) const {
		return loaded_ && drawn_buildings_key(save, index_) != key_;
	}

private:
	SavedTownTemplate base_;
	size_t index_ = 0;
	bool loaded_ = false;
	bool drawn_ = false;  // The view holds this town's drawing (TownView::show).
	uint64_t key_ = 0;
};

// A template map (Mode::Designer or Mode::Terrain).
bool load_template(const CprArchive& archive, const std::vector<int>& template_ids, const Selection& selection,
	TownMap& out, std::string& error) {
	const int id = template_ids[selection.template_index];
	// El mapa 30 no tiene .editor: se ve su terreno.
	return (selection.mode == Mode::Designer && load_town(archive, id, out, &error, true)) ||
		load_town(archive, id, out, &error);
}

// Punto en el que centrar la cámara: el ayuntamiento en las ciudades de la
// partida y el centro del mapa en las plantillas.
SDL_Point focus_point(const TownView& view, const SaveGame& save, const Selection& selection) {
	if (selection.mode == Mode::Saved) {
		for (const BuildingRecord& record : save.towns[selection.saved_index].records) {
			if (record.type == kTownHallType && record.x < 128 && record.y < 226) {
				return {cell_screen_x(record.x, record.y), cell_screen_y(record.y)};
			}
		}
	}
	return {view.width() / 2, view.height() / 2};
}

// Monedas con separador de miles: 30.000 (30,000 in English).
std::string format_money(int64_t value) {
	std::string digits = std::to_string(value < 0 ? -value : value);
	for (int i = int(digits.size()) - 3; i > 0; i -= 3) {
		digits.insert(size_t(i), g_spanish ? "." : ",");
	}
	return value < 0 ? "-" + digits : digits;
}

constexpr const char* kMonthNames[12] = {"enero", "febrero", "marzo", "abril", "mayo", "junio", "julio", "agosto",
	"septiembre", "octubre", "noviembre", "diciembre"};
constexpr const char* kEnglishMonthNames[12] = {"January", "February", "March", "April", "May", "June", "July",
	"August", "September", "October", "November", "December"};

std::string month_name(int month) {
	const int m = ((month % 12) + 12) % 12;
	return g_texts ? g_texts->month(month) : g_spanish ? kMonthNames[m] : kEnglishMonthNames[m];
}

// «13 de mayo», «13 May»
std::string day_and_month(int day, int month) {
	return std::to_string(day) + (g_spanish ? " de " : " ") + month_name(month);
}

// A date as the game writes it («13 de mayo de 1300», «13. May 1300»).
std::string game_date(int day, int month, int year) {
	if (!g_texts) {
		return day_and_month(day, month) + (g_spanish ? " de " : " ") + std::to_string(year);
	}
	std::string date = g_texts->format(GameText::Date, {std::to_string(day), month_name(month), std::to_string(year)});
	date.erase(std::remove(date.begin(), date.end(), '"'), date.end());  // The text ends in a quote.
	return date;
}

std::string town_name(const SaveGame& save, const std::vector<std::string>& names, int town) {
	const int id = save.towns[size_t(town)].town_id();
	return id < int(names.size()) ? names[size_t(id)] : tr("Ciudad ", "Town ") + std::to_string(id);
}

std::string bar_title(const SaveGame& save, const std::vector<std::string>& names,
	const std::vector<int>& template_ids, const Selection& selection) {
	if (selection.mode == Mode::Saved || selection.mode == Mode::Sea) {
		const std::string date = game_date(save.day(), save.month(), save.year());
		const std::string name = town_name(save, names, int(selection.saved_index));
		if (selection.mode == Mode::Sea) {
			return date + tr(" - Mapa del mar - ", " - Sea map - ") + name;
		}
		const std::string citizens = format_money(save.towns[selection.saved_index].citizens());
		return date + " - " + name + " - " +
			(g_texts ? g_texts->format(GameText::Inhabitants, {citizens}) : citizens + tr(" habitantes", " inhabitants"));
	}
	return tr("Plantilla ", "Template ") + std::to_string(template_ids[selection.template_index]) +
		(selection.mode == Mode::Designer ? tr(" - todos los edificios del diseñador", " - every building of the designer")
										  : tr(" - solo el terreno", " - the terrain only"));
}

// Barra de texto a lo ancho de la pantalla: el título a la izquierda y, a la
// derecha, el primero de los textos que quepa.
void draw_bar(SDL_Renderer* renderer, TextRenderer& text, int top, const std::string& title,
	const std::vector<std::string>& right_texts) {
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 160);
	const SDL_Rect bar = {0, top, kScreenWidth, kBarHeight};
	SDL_RenderFillRect(renderer, &bar);
	const int y = top + (kBarHeight - text.line_height()) / 2;
	text.draw(title, kBarMargin, y, kTextColor);
	const int room = kScreenWidth - 4 * kBarMargin - text.measure(title);
	for (const std::string& right : right_texts) {
		if (text.measure(right) <= room) {
			text.draw(right, kScreenWidth - kBarMargin - text.measure(right), y, kDimColor);
			break;
		}
	}
}

// Velocidades del juego (0x546620): milisegundos reales por tick, que son
// los segundos por día de la tabla 0x66DE7C · 1000 / 256, y ticks como
// máximo por fotograma; lo que pase de ahí se pierde, como en el juego.
struct GameSpeed {
	const char* name;
	const char* english;
	Uint32 ms_per_tick;
	int max_ticks;
};
constexpr GameSpeed kGameSpeeds[] = {
	{"ralentizada", "slowed down", 3515, 8},  // La de la tecla Pausa del juego.
	{"muy lenta", "very slow", 468, 8},
	{"lenta", "slow", 351, 8},
	{"normal", "normal", 234, 8},  // Un día por minuto.
	{"rápida", "fast", 117, 8},
	{"muy rápida", "very fast", 78, 8},
	{"avance rápido", "fast forward", 2, 256},  // F12 del juego.
};
constexpr int kGameSpeedCount = int(sizeof(kGameSpeeds) / sizeof(kGameSpeeds[0]));

// The speed's name: the game's six (its settings window, 0x69A934); fast
// forward (F12) has none.
std::string speed_name(int speed) {
	static constexpr GameText kNames[] = {GameText::SpeedSlowdown, GameText::SpeedVerySlow, GameText::SpeedSlow,
		GameText::SpeedNormal, GameText::SpeedFast, GameText::SpeedVeryFast};
	return g_texts && speed < 6 ? g_texts->get(kNames[speed]) : tr(kGameSpeeds[speed].name, kGameSpeeds[speed].english);
}
constexpr int kNormalSpeed = 3;

// Barcos del jugador que existen (también los que navegan).
std::vector<int> player_fleet(const SaveGame& save) {
	std::vector<int> fleet;
	for (size_t i = 0; i < save.ships.size(); ++i) {
		const SavedShip& ship = save.ships[i];
		if (ship.in_use() && ship.status() != kShipSunk && ship.owner() == save.player) {
			fleet.push_back(int(i));
		}
	}
	return fleet;
}

// Qué hace un barco: «saliendo de Lübeck hacia Visby».
std::string ship_activity(const SaveGame& save, const std::vector<std::string>& names, const SavedShip& ship) {
	const size_t count = save.towns.size();
	const std::string here = ship.town() < count ? town_name(save, names, ship.town()) : "";
	const std::string there = ship.destination() < count ? town_name(save, names, ship.destination()) : "";
	switch (ship.status()) {
	case kShipDocked:
		return tr("en el puerto de ", "in the port of ") + here;
	case kShipWaiting:
		return tr("esperando en ", "waiting in ") + here;
	case kShipLeaving:
		return tr("saliendo de ", "leaving ") + here + (there.empty() ? "" : tr(" hacia ", " for ") + there);
	case kShipArriving:
		return tr("entrando en el puerto de ", "entering the port of ") + here;
	case kShipSailing: {
		if (!ship.route) {
			return game_text(GameText::AtSea, tr("sin rumbo en el mar", "at sea"));
		}
		std::string text = there.empty() ? tr("navegando", "sailing") : tr("navegando hacia ", "sailing to ") + there;
		if (ship.arrival_tick() > save.tick()) {
			const GameDate date = date_from_tick(ship.arrival_tick());
			text += tr(", llegada prevista el ", ", expected on ") + day_and_month(date.day, date.month);
		}
		return text;
	}
	default:
		return "";
	}
}

// Cantidad en unidades del juego (barriles, cargas o piezas), redondeada hacia abajo.
int units(int32_t internal, WareUnit unit) {
	return internal / ware_scale(unit);
}

std::string unit_name(WareUnit unit, int count) {
	const bool one = count == 1;
	switch (unit) {
	case WareUnit::Barrel:
		return one ? tr("barril", "barrel") : tr("barriles", "barrels");
	case WareUnit::Load:
		return one ? tr("carga", "load") : tr("cargas", "loads");
	default:
		return one ? tr("pieza", "piece") : tr("piezas", "pieces");
	}
}

// «5 cargas», or the game's «%.0f load» («5 toneladas»).
std::string amount_label(int n, WareUnit unit) {
	if (!g_texts) {
		return std::to_string(n) + " " + unit_name(unit, n);
	}
	const GameText id = unit == WareUnit::Barrel ? GameText::AmountBarrels
		: unit == WareUnit::Load ? GameText::AmountLoads
		: GameText::AmountPieces;
	return g_texts->format(id, {std::to_string(n)});
}

std::string amount_text(int32_t internal, WareUnit unit) {
	return amount_label(units(internal, unit), unit);
}

// A ware's name: in lower case for the sentences, capitalized for the rows.
std::string ware_name(int ware) {
	if (g_texts && ware >= 0 && ware < 20) {
		return g_texts->get(GameText(int(GameText::WareGrain) + ware));
	}
	std::string name = kWares[size_t(ware)].name;
	name[0] = char(std::tolower(uint8_t(name[0])));
	return name;
}

std::string ware_label(int ware) {
	std::string name = ware_name(ware);
	name[0] = char(std::toupper(uint8_t(name[0])));  // All of them start with an ASCII letter.
	return name;
}

// What is bought or sold each time, in barrels or loads: the market window's
// 1, 5, 50 and all (0; kMarketSteps, player_actions.hpp).
constexpr int kTradeStepCount = int(kMarketSteps.size());
constexpr Uint32 kMessageTime = 4000;  // Milisegundos que se ve el resultado de una operación.

constexpr const char* kShipTypes[] = {"Snaikka", "Craier", "Kogge", "Holk"};

// The ship type's name (the Spanish game has Goleta, Carabela, Nao, Galeón).
std::string ship_type_name(int type) {
	return game_text(GameText(int(GameText::ShipSnaikka) + (type & 3)), kShipTypes[type & 3]);
}

// Estado del panel de mercado.
struct MarketUi {
	bool open = false;
	int ware = 0;  // Fila seleccionada.
	int step = 0;  // Index in kMarketSteps.
	size_t ship = 0;  // Entre los barcos del jugador atracados en la ciudad.
	// The market window's mode (player_actions.hpp): 1 a ship and the town; with
	// an office in the town, 2 the office and the town, 0 a ship and the
	// office, 5 the ship's weapons and artillery with the office.
	int mode = 1;
	std::string message;
	Uint32 message_until = 0;
};

// Barcos del jugador atracados en una ciudad.
std::vector<int> player_ships_in(const SaveGame& save, int town) {
	std::vector<int> ships;
	for (size_t i = 0; i < save.ships.size(); ++i) {
		const SavedShip& ship = save.ships[i];
		if (ship.docked() && ship.owner() == save.player && ship.town() == town) {
			ships.push_back(int(i));
		}
	}
	return ships;
}

// Barco con el que se comercia ahora, o -1.
int market_ship(const SaveGame& save, int town, const MarketUi& ui) {
	const std::vector<int> ships = player_ships_in(save, town);
	return ships.empty() ? -1 : ships[ui.ship % ships.size()];
}

// The operation the market window would send now (0x02 or 0x01), as the
// game's window computes its amount.
std::optional<OperationBytes> trade_operation(const SaveGame& save, int ship, int town, const MarketUi& ui,
	bool buying) {
	const uint32_t step = kMarketSteps[size_t(ui.step)];
	return buying ? market_buy_operation(save, ship, town, ui.ware, step)
				  : market_sell_operation(save, ship, town, ui.ware, step);
}

int32_t trade_amount(const SaveGame& save, int ship, int town, const MarketUi& ui, bool buying) {
	const std::optional<OperationBytes> op = trade_operation(save, ship, town, ui, buying);
	int32_t amount = 0;
	if (op) {
		std::memcpy(&amount, op->data() + 4, 4);
	}
	return amount;
}

std::string step_text(const MarketUi& ui) {
	const uint32_t step = kMarketSteps[size_t(ui.step)];
	if (ui.mode == 5) {
		return step > 0 ? std::to_string(step) : tr("todo", "all");
	}
	return step > 0 ? amount_label(int(step), kWares[size_t(ui.ware)].unit) : tr("todo", "all");
}

std::string capitalized(std::string s);

// --- The market with the player's office (the window's modes 2, 0 and 5) ---
// What the game's market window does with the office (player_actions.hpp):
// the office and the town (2: 0x59 buy, 0x58 sell), a ship and the office (0:
// 0x08) and the ship's weapons and artillery with the office (5: 0x08 for the
// weapons, 0x09 for the hand weapons and the pieces). L and R go through them
// in a town where the player has an office.

std::vector<int> market_modes(const SaveGame& save, int town) {
	return save.player >= 0 && player_office(save, town) >= 0 ? std::vector<int>{1, 2, 0, 5} : std::vector<int>{1};
}

int market_rows(const MarketUi& ui) {
	return ui.mode == 5 ? 11 : kTradeWareCount;  // The weapons, the hand weapons and the six pieces.
}

std::string market_mode_name(int mode) {
	const std::string ship = game_text(GameText::MarketShip, tr("Barco", "Ship"));
	const std::string office = game_text(GameText::Office, tr("Oficina", "Office"));
	switch (mode) {
	case 2:
		return office + " <-> " + game_text(GameText::MarketTown, tr("Ciudad", "Town"));
	case 0:
		return ship + " <-> " + office;
	case 5:
		return game_text(GameText::ShipWeapons, tr("Armas de barco", "Ship's weapons"));
	default:
		return ship + " <-> " + game_text(GameText::MarketTown, tr("Ciudad", "Town"));
	}
}

// The rows of the mode 5: the four weapons (the wares 0x14..0x17), the hand
// weapons and the six pieces (their names are pictures in the game; these
// are the viewer's, by their weight: 0x672CB4).
std::string arsenal_row_name(int row) {
	if (row < 4) {
		return capitalized(game_text(GameText(int(GameText::WareSword) + row),
			std::array<const char*, 4>{"sword", "bow", "crossbow", "carbine"}[size_t(row)]));
	}
	if (row == 4) {
		return game_text(GameText::HandWeapons, tr("Armas de mano", "Hand weapons"));
	}
	static constexpr bool kBig[6] = {false, false, true, true, true, false};
	const int type = row - 5;
	return (g_spanish ? "Pieza " + std::to_string(type + 1) + (kBig[type] ? " (grande)" : " (pequeña)")
					  : "Piece " + std::to_string(type + 1) + (kBig[type] ? " (big)" : " (small)"));
}

// What X (`first`) or Square sends in the modes 2, 0 and 5, as the game's
// window builds it, or nothing when there is nothing to move: then the viewer
// doesn't let the button be pressed (with the weapons the game's window would
// send a 0x09 of hand weapons; player_actions.hpp keeps it, behind
// fix_game_bugs(), but the viewer never asks for it).
std::optional<OperationBytes> office_operation(const SaveGame& save, int town, const MarketUi& ui, bool first) {
	const int office = player_office(save, town);
	const int ship = market_ship(save, town, ui);
	if (office < 0 || (ui.mode != 2 && ship < 0)) {
		return std::nullopt;
	}
	const uint32_t step = kMarketSteps[size_t(ui.step)];
	if (ui.mode == 2) {
		return first ? office_buy_operation(save, town, ui.ware, step) : office_sell_operation(save, town, ui.ware, step);
	}
	if (ui.mode == 0) {
		return first ? office_to_ship_operation(save, ship, ui.ware, step) : ship_to_office_operation(save, ship, ui.ware, step);
	}
	if (ui.ware >= 4) {
		return artillery_operation(save, ship, ui.ware - 5, step, !first);
	}
	const int ware = 0x14 + ui.ware;
	if (first && weapons_to_ship_pieces(step, market_office_units(save, office, ware),
					 save.ships[size_t(ship)].memory.s32(0x10), market_ship_used(save, ship)) == 0) {
		return std::nullopt;  // Nothing to move: the button can't be pressed.
	}
	return first ? weapons_to_ship_operation(save, ship, ware, step) : weapons_to_office_operation(save, ship, ware, step);
}

// X (`first`) and Square in the modes 2, 0 and 5: the operation the window
// sends, run at once; what it did, from the stocks before and after.
void office_trade(SaveGame& save, Sailing* sailing, int town, MarketUi& ui, bool first) {
	ui.message_until = app_ticks() + kMessageTime;
	const int office = player_office(save, town);
	const int ship = market_ship(save, town, ui);
	if (office < 0) {
		ui.message = tr("No tienes oficina en esta ciudad.", "You have no office in this town.");
		return;
	}
	if (ui.mode != 2 && ship < 0) {
		ui.message = tr("No tienes ningún barco en este puerto.", "You have no ship in this port.");
		return;
	}
	const std::optional<OperationBytes> op = office_operation(save, town, ui, first);
	if (!op) {
		ui.message = tr("No hay nada que pasar.", "There is nothing to move.");
		return;
	}
	// What can change: the ware or weapon in the office and the ship, the
	// ship's pieces and hand weapons, the money.
	auto snapshot = [&]() {
		std::array<int64_t, 4> v{};
		const OfficeMemory& o = save.offices[size_t(office)].memory;
		if (ui.mode == 5 && ui.ware >= 4) {
			v[0] = ship_artillery_count(save, ship, ui.ware - 5);
		} else {
			const int ware = ui.mode == 5 ? 0x14 + ui.ware : ui.ware;
			v[0] = o.s32(4 + 4 * size_t(ware));
			v[1] = ship >= 0 ? save.ships[size_t(ship)].cargo(ware) : 0;
		}
		v[2] = save.merchants[size_t(save.player)].money();
		return v;
	};
	const std::array<int64_t, 4> before = snapshot();
	if (sailing) {
		queue_operation(save, *op);
		run_queued_operations(save, *sailing);
	} else {
		run_operation(save, nullptr, op->data());
	}
	const std::array<int64_t, 4> after = snapshot();
	const std::string what = ui.mode == 5 ? arsenal_row_name(ui.ware) : ware_name(ui.ware);
	if (ui.mode == 2) {
		const int64_t moved = after[0] - before[0];
		const WareUnit unit = kWares[size_t(ui.ware)].unit;
		ui.message = moved == 0 ? tr("No se ha podido.", "It couldn't be done.")
			: g_spanish ? std::string(moved > 0 ? "La oficina compra " : "La oficina vende ") +
					amount_text(int32_t(std::abs(moved)), unit) + " de " + what + " por " +
					format_money(std::abs(after[2] - before[2])) + " monedas."
						: std::string(moved > 0 ? "The office buys " : "The office sells ") +
					amount_text(int32_t(std::abs(moved)), unit) + " of " + what + " for " +
					format_money(std::abs(after[2] - before[2])) + " coins.";
	} else if (ui.mode == 5 && ui.ware >= 4) {
		const int64_t moved = after[0] - before[0];
		ui.message = moved == 0 ? tr("No se ha podido.", "It couldn't be done.")
			: g_spanish ? what + (first ? ": al barco." : ": a la oficina.")
						: what + (first ? ": to the ship." : ": to the office.");
	} else {
		const int64_t moved = after[1] - before[1];
		const std::string amount = ui.mode == 5 ? std::to_string(std::abs(moved))
												: amount_text(int32_t(std::abs(moved)), kWares[size_t(ui.ware)].unit);
		ui.message = moved == 0 ? tr("No se ha podido.", "It couldn't be done.")
			: g_spanish ? (moved > 0 ? "Al barco: " : "A la oficina: ") + amount + " de " + what + "."
						: (moved > 0 ? "To the ship: " : "To the office: ") + amount + " of " + what + ".";
	}
}

// Buys or sells as the game does: the market window queues the operation and
// the game runs its queue before the next tick, so the viewer runs it at once
// (without the sea, Sailing, only this operation). What it did is told by the
// ship's cargo and the owner's money before and after.
void trade(SaveGame& save, Sailing* sailing, int town, MarketUi& ui, bool buying) {
	if (ui.mode != 1) {
		office_trade(save, sailing, town, ui, buying);
		return;
	}
	const int ship = market_ship(save, town, ui);
	const WareInfo& ware = kWares[size_t(ui.ware)];
	const std::string name = ware_name(ui.ware);
	ui.message_until = app_ticks() + kMessageTime;
	if (ship < 0) {
		ui.message = tr("No tienes ningún barco en este puerto.", "You have no ship in this port.");
		return;
	}
	// Sin mercado los precios del juego son 0: no se deja comerciar.
	if (!save.towns[size_t(town)].has_market()) {
		ui.message = tr("Esta ciudad no tiene mercado.", "This town has no market.");
		return;
	}
	TradeResult result;
	if (const std::optional<OperationBytes> op = trade_operation(save, ship, town, ui, buying)) {
		const SavedShip& s = save.ships[size_t(ship)];
		const int32_t cargo = s.cargo(size_t(ui.ware));
		const int32_t money = save.merchants[s.owner()].money();
		if (sailing) {
			queue_operation(save, *op);
			run_queued_operations(save, *sailing);
		} else {
			run_operation(save, nullptr, op->data());
		}
		result.amount = buying ? s.cargo(size_t(ui.ware)) - cargo : cargo - s.cargo(size_t(ui.ware));
		result.price = buying ? money - save.merchants[s.owner()].money() : save.merchants[s.owner()].money() - money;
	}
	const SavedTown& market = save.towns[size_t(town)];
	if (result.amount > 0) {
		ui.message = g_spanish ? std::string(buying ? "Compra: " : "Venta: ") + amount_text(result.amount, ware.unit) +
				" de " + name + " por " + format_money(result.price) + " monedas."
							   : std::string(buying ? "Bought " : "Sold ") + amount_text(result.amount, ware.unit) +
				" of " + name + " for " + format_money(result.price) + " coins.";
	} else if (!buying) {
		ui.message = g_spanish ? "No llevas " + name + " en la bodega." : "There is no " + name + " in the hold.";
	} else if (int32_t(market.stock(size_t(ui.ware))) < ware_scale(ware.unit)) {
		ui.message = g_spanish ? "La ciudad no tiene " + name + "." : "The town has no " + name + ".";
	} else if (save.merchants[size_t(save.player)].money() <
		buy_price(ui.ware, market.market(ui.ware), ware_scale(ware.unit))) {
		ui.message = game_text(GameText::NotEnoughMoney, tr("No tienes dinero suficiente.", "Not enough money!"));
	} else {
		ui.message = tr("No queda sitio en la bodega.", "There is no room left in the hold.");
	}
}

// Disposición del panel, compartida por el dibujo y el táctil.
struct MarketLayout {
	static constexpr int kWidth = 560;
	static constexpr int kPadding = 10;
	static constexpr int kRowHeight = 19;
	static constexpr int kGap = 8;
	static constexpr int kColumns[4] = {250, 335, 410, 548};  // Bordes derechos de las columnas.
	SDL_Rect panel{};
	int header_y = 0;
	int titles_y = 0;
	int rows_y = 0;
	int footer_y = 0;
	// Los botones sirven también de leyenda de los controles.
	SDL_Rect step_button{};
	SDL_Rect buy_button{};
	SDL_Rect sell_button{};

	explicit MarketLayout(int line_height) {
		const int button_height = line_height + 4;
		const int height = kPadding * 2 + line_height * 3 + button_height + kRowHeight * kTradeWareCount + 16;
		panel = {kScreenWidth - kWidth - 8, kBarHeight + 4, kWidth, height};
		header_y = panel.y + kPadding;
		titles_y = header_y + line_height + 4;
		rows_y = titles_y + line_height + 2;
		footer_y = rows_y + kRowHeight * kTradeWareCount + 6;
		const int button_y = footer_y + line_height + 4;
		const int inner = kWidth - 2 * kPadding - 2 * kGap;
		step_button = {panel.x + kPadding, button_y, inner * 2 / 5, button_height};
		buy_button = {step_button.x + step_button.w + kGap, button_y, inner * 3 / 10, button_height};
		sell_button = {buy_button.x + buy_button.w + kGap, button_y, inner - step_button.w - buy_button.w,
			button_height};
	}

	// Fila de mercancía bajo un punto de la pantalla, o -1.
	int row_at(int x, int y) const {
		if (x < panel.x || x >= panel.x + panel.w || y < rows_y) {
			return -1;
		}
		const int row = (y - rows_y) / kRowHeight;
		return row < kTradeWareCount ? row : -1;
	}
};

bool inside(const SDL_Rect& r, int x, int y) {
	return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

void draw_button(SDL_Renderer* renderer, TextRenderer& text, const SDL_Rect& r, const std::string& label) {
	SDL_SetRenderDrawColor(renderer, 90, 65, 35, 255);
	SDL_RenderFillRect(renderer, &r);
	SDL_SetRenderDrawColor(renderer, 190, 150, 90, 255);
	SDL_RenderDrawRect(renderer, &r);
	text.draw(label, r.x + (r.w - text.measure(label)) / 2, r.y + (r.h - text.line_height()) / 2, kTextColor);
}

// Mercado de la ciudad: existencias, precios de una unidad y carga del barco
// del jugador, con la mercancía seleccionada y lo que costaría comerciar.
void draw_office_market(SDL_Renderer* renderer, TextRenderer& text, const SaveGame& save, int town_index,
	const MarketUi& ui);

void draw_market(SDL_Renderer* renderer, TextRenderer& text, const SaveGame& save, int town_index,
	const MarketUi& ui) {
	if (ui.mode != 1) {
		draw_office_market(renderer, text, save, town_index, ui);
		return;
	}
	const SavedTown& town = save.towns[size_t(town_index)];
	const MarketLayout layout(text.line_height());
	const SDL_Rect& panel = layout.panel;
	const int left = panel.x + MarketLayout::kPadding;
	const int ship_index = market_ship(save, town_index, ui);
	const SavedShip* ship = ship_index >= 0 ? &save.ships[size_t(ship_index)] : nullptr;
	const WareUnit selected_unit = kWares[size_t(ui.ware)].unit;

	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(renderer, 30, 22, 14, 225);
	SDL_RenderFillRect(renderer, &panel);
	SDL_SetRenderDrawColor(renderer, 150, 120, 70, 255);
	SDL_RenderDrawRect(renderer, &panel);

	auto right = [&](const std::string& s, int column, int y, SDL_Color color) {
		text.draw(s, panel.x + MarketLayout::kColumns[column] - text.measure(s), y, color);
	};

	if (ship) {
		char used[32];
		char capacity[32];
		std::snprintf(used, sizeof(used), "%.1f", ship->used() / 2000.0);
		std::snprintf(capacity, sizeof(capacity), "%.1f", (ship->capacity() - ship->equipment()) / 2000.0);
		const std::string hold = g_texts
			? std::string(used) + " / " + g_texts->format(GameText::AmountLoadsDecimal, {capacity})
			: std::string(used) + tr(" de ", " of ") + capacity + tr(" cargas", " loads");
		text.draw(ship->name() + " (" + ship_type_name(ship->type()) + "): " + hold, left, layout.header_y, kTextColor);
	} else {
		text.draw(tr("No tienes ningún barco en este puerto.", "You have no ship in this port."), left, layout.header_y,
			kDimColor);
	}
	const std::string close = tr(BTN_CIRCLE " cerrar", BTN_CIRCLE " close");
	text.draw(close, panel.x + panel.w - MarketLayout::kPadding - text.measure(close), layout.header_y, kDimColor);

	text.draw(game_text(GameText::MarketGoods, tr("Mercancía", "Goods")), left, layout.titles_y, kDimColor);
	right(game_text(GameText::MarketTown, tr("Ciudad", "Town")), 0, layout.titles_y, kDimColor);
	right(game_text(GameText::MarketBuy, tr("Compra", "Buy")), 1, layout.titles_y, kDimColor);
	right(game_text(GameText::MarketSell, tr("Venta", "Sell")), 2, layout.titles_y, kDimColor);
	right(game_text(GameText::MarketShip, tr("Bodega", "Ship")), 3, layout.titles_y, kDimColor);
	for (int w = 0; w < kTradeWareCount; ++w) {
		const WareInfo& ware = kWares[size_t(w)];
		const int y = layout.rows_y + w * MarketLayout::kRowHeight;
		if (w == ui.ware) {
			const SDL_Rect row = {panel.x + 2, y - 1, panel.w - 4, MarketLayout::kRowHeight};
			SDL_SetRenderDrawColor(renderer, 110, 80, 40, 230);
			SDL_RenderFillRect(renderer, &row);
		}
		const int32_t unit = ware_scale(ware.unit);
		const int32_t buy = buy_price(w, town.market(w), unit);
		const int32_t sell = sell_price(w, town.market(w), unit, save.trade_difficulty);
		auto amount = [&](int n) {
			return g_texts ? amount_label(n, ware.unit)
						   : std::to_string(n) + (ware.unit == WareUnit::Barrel ? tr(" barr.", " barr.") : tr(" cargas", " loads"));
		};
		text.draw(ware_label(w), left, y, kTextColor);
		right(amount(units(int32_t(town.stock(size_t(w))), ware.unit)), 0, y, kTextColor);
		right(buy ? std::to_string(buy) : "-", 1, y, kTextColor);
		right(std::to_string(sell), 2, y, kTextColor);
		if (ship) {
			const int n = units(ship->cargo(size_t(w)), ware.unit);
			right(n ? amount(n) : "-", 3, y, n ? kTextColor : kDimColor);
		}
	}

	// El resultado de la última operación o, si no, lo que costaría comerciar
	// ahora con la cantidad elegida.
	const bool show_message = !ui.message.empty() && !SDL_TICKS_PASSED(app_ticks(), ui.message_until);
	std::string footer = show_message ? ui.message : "";
	if (!show_message && ship) {
		const TradeResult buy =
			quote_buy(save, ship_index, town_index, ui.ware, trade_amount(save, ship_index, town_index, ui, true));
		const TradeResult sell =
			quote_sell(save, ship_index, town_index, ui.ware, trade_amount(save, ship_index, town_index, ui, false),
				save.trade_difficulty);
		footer = buy.amount > 0 ? tr("Comprar ", "Buy ") + amount_text(buy.amount, selected_unit) + ": " +
				format_money(buy.price) + tr(" monedas", " coins")
								: tr("No se puede comprar", "Nothing to buy");
		if (sell.amount > 0) {
			footer += tr("    vender ", "    sell ") + std::to_string(units(sell.amount, selected_unit)) + ": " +
				format_money(sell.price);
		}
	}
	text.draw(footer, left, layout.footer_y, kTextColor);
	draw_button(renderer, text, layout.step_button, tr("Izq./Der.: ", "Left/Right: ") + step_text(ui));
	if (ship) {
		draw_button(renderer, text, layout.buy_button, tr(BTN_CROSS " comprar", BTN_CROSS " buy"));
		draw_button(renderer, text, layout.sell_button, tr(BTN_SQUARE " vender", BTN_SQUARE " sell"));
	}
}

// The market in the modes 2, 0 and 5: the rows with the town's stock and
// prices, the office's and the ship's.
void draw_office_market(SDL_Renderer* renderer, TextRenderer& text, const SaveGame& save, int town_index,
	const MarketUi& ui) {
	const SavedTown& town = save.towns[size_t(town_index)];
	const MarketLayout layout(text.line_height());
	const SDL_Rect& panel = layout.panel;
	const int left = panel.x + MarketLayout::kPadding;
	const int office = player_office(save, town_index);
	const int ship_index = market_ship(save, town_index, ui);
	const SavedShip* ship = ship_index >= 0 ? &save.ships[size_t(ship_index)] : nullptr;
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(renderer, 30, 22, 14, 225);
	SDL_RenderFillRect(renderer, &panel);
	SDL_SetRenderDrawColor(renderer, 150, 120, 70, 255);
	SDL_RenderDrawRect(renderer, &panel);
	auto right = [&](const std::string& s, int column, int y, SDL_Color color) {
		text.draw(s, panel.x + MarketLayout::kColumns[column] - text.measure(s), y, color);
	};
	std::string header = market_mode_name(ui.mode);
	if (ui.mode != 2) {
		header += ship ? ": " + ship->name() : tr(": ningún barco en el puerto", ": no ship in the port");
	}
	text.draw(header, left, layout.header_y, ship || ui.mode == 2 ? kTextColor : kDimColor);
	const std::string close = tr("L/R   " BTN_CIRCLE " cerrar", "L/R   " BTN_CIRCLE " close");
	text.draw(close, panel.x + panel.w - MarketLayout::kPadding - text.measure(close), layout.header_y, kDimColor);
	const std::string office_word = game_text(GameText::Office, tr("Oficina", "Office"));
	text.draw(game_text(GameText::MarketGoods, tr("Mercancía", "Goods")), left, layout.titles_y, kDimColor);
	if (ui.mode == 2) {
		right(game_text(GameText::MarketTown, tr("Ciudad", "Town")), 0, layout.titles_y, kDimColor);
		right(game_text(GameText::MarketBuy, tr("Compra", "Buy")), 1, layout.titles_y, kDimColor);
		right(game_text(GameText::MarketSell, tr("Venta", "Sell")), 2, layout.titles_y, kDimColor);
		right(office_word, 3, layout.titles_y, kDimColor);
	} else {
		right(office_word, 1, layout.titles_y, kDimColor);
		right(game_text(GameText::MarketShip, tr("Bodega", "Ship")), 3, layout.titles_y, kDimColor);
	}
	const OfficeMemory* o = office >= 0 ? &save.offices[size_t(office)].memory : nullptr;
	for (int r = 0; r < market_rows(ui); ++r) {
		const int y = layout.rows_y + r * MarketLayout::kRowHeight;
		if (r == ui.ware) {
			const SDL_Rect row = {panel.x + 2, y - 1, panel.w - 4, MarketLayout::kRowHeight};
			SDL_SetRenderDrawColor(renderer, 110, 80, 40, 230);
			SDL_RenderFillRect(renderer, &row);
		}
		auto count = [&](int64_t n, int column) {
			right(n ? std::to_string(n) : "-", column, y, n ? kTextColor : kDimColor);
		};
		if (ui.mode == 5) {
			text.draw(arsenal_row_name(r), left, y, kTextColor);
			if (r < 4) {
				count(o ? o->s32(4 + 4 * size_t(0x14 + r)) : 0, 1);
				count(ship ? ship->cargo(0x14 + r) : 0, 3);
			} else {
				count(o ? office_artillery_count(save, office, r - 5) : 0, 1);
				count(ship ? ship_artillery_count(save, ship_index, r - 5) : 0, 3);
			}
			continue;
		}
		const WareInfo& ware = kWares[size_t(r)];
		auto amount = [&](int32_t internal) {
			const int n = units(internal, ware.unit);
			return n ? amount_label(n, ware.unit) : std::string("-");
		};
		text.draw(ware_label(r), left, y, kTextColor);
		if (ui.mode == 2) {
			const int32_t unit = ware_scale(ware.unit);
			const int32_t buy = buy_price(r, town.market(r), unit);
			right(amount(int32_t(town.stock(size_t(r)))), 0, y, kTextColor);
			right(buy ? std::to_string(buy) : "-", 1, y, kTextColor);
			right(std::to_string(sell_price(r, town.market(r), unit, save.trade_difficulty)), 2, y, kTextColor);
			right(o ? amount(o->s32(4 + 4 * size_t(r))) : "-", 3, y, kTextColor);
		} else {
			right(o ? amount(o->s32(4 + 4 * size_t(r))) : "-", 1, y, kTextColor);
			right(ship ? amount(ship->cargo(r)) : "-", 3, y, kTextColor);
		}
	}
	if (!ui.message.empty() && !SDL_TICKS_PASSED(app_ticks(), ui.message_until)) {
		text.draw(ui.message, left, layout.footer_y, kTextColor);
	}
	draw_button(renderer, text, layout.step_button, tr("Izq./Der.: ", "Left/Right: ") + step_text(ui));
	// The buttons, dimmed when there is nothing to move (they do nothing then).
	auto button = [&](const SDL_Rect& r, const std::string& label, bool first) {
		draw_button(renderer, text, r, label);
		if (!office_operation(save, town_index, ui, first)) {
			SDL_SetRenderDrawColor(renderer, 30, 22, 14, 150);
			SDL_RenderFillRect(renderer, &r);
		}
	};
	if (ui.mode == 2) {
		button(layout.buy_button, tr(BTN_CROSS " comprar", BTN_CROSS " buy"), true);
		button(layout.sell_button, tr(BTN_SQUARE " vender", BTN_SQUARE " sell"), false);
	} else if (ship) {
		button(layout.buy_button, tr(BTN_CROSS " al barco", BTN_CROSS " to the ship"), true);
		button(layout.sell_button, tr(BTN_SQUARE " a la oficina", BTN_SQUARE " to the office"), false);
	}
}

// --- The windows of the town's places and of the ship ---
// The tavern (0x5CD540: hiring sailors, 0x5D4C10, and a captain, 0x5D4870),
// the shipyard's repairs (0x5F8F50) and the ship's window (0x493ED0:
// dismissing sailors and the captain), listing and sending what the game's
// windows do (player_actions.hpp), with the game's texts. The viewer runs
// the operation at once, as it does with the market.

struct PlaceUi {
	Place open = Place::None;
	int let_type = -1;  // The building that opened it: its name.
	int row = 0;
	int amount = 0;  // The sailors to hire or to dismiss.
	size_t ship = 0;  // Among the ships the row's list offers.
	float roll = 0;  // The tavern's roll when it opened (tavern_roll).
	std::string message;
	Uint32 message_until = 0;
};

// What a window shows now, made from the save each frame (also for the touch screen).
struct PlaceView {
	std::string title;
	std::vector<std::pair<std::string, SDL_Color>> lines;  // Of the selected row.
	std::vector<std::pair<std::string, bool>> rows;  // Label and whether it can be done.
	std::vector<uint16_t> ships;  // The selected row's list (Select: the next one).
	int ship = -1;  // The chosen one.
	int max_amount = -1;  // The amount goes from 0 to this (-1: the row has none).
	uint16_t captain = 0xFFFF;  // The tavern's captain.
	std::string act;  // The act button's label, if not the row's.
};

std::vector<std::string> wrap_text(const TextRenderer& text, const std::string& s, int width);

// The first letter in upper case (the game's «dismiss sailors» as a button).
std::string capitalized(std::string s) {
	if (!s.empty()) {
		s[0] = char(std::toupper(uint8_t(s[0])));  // ASCII only: the Spanish ones are capitalized already.
	}
	return s;
}

std::string place_name(int let_type) {
	const PlaceInfo* p = place_of(uint32_t(let_type));
	return p ? game_text(p->name, tr(p->spanish, p->english)) : "";
}

std::string ship_label(const SavedShip& ship) {
	return ship.name() + " (" + ship_type_name(ship.type()) + ")";
}

// The hull left, in % (the ship's window).
int hull_percent(const SavedShip& ship) {
	return ship.max_health() > 0 ? int(int64_t(ship.health()) * 100 / ship.max_health()) : 0;
}

std::string captain_name(const SaveGame& save, uint16_t captain) {
	const CaptainInfo c = captain_info(save, captain);
	return clean_game_text(game_first_name(save, c.first_name) + " " + game_family_name(save, c.family_name));
}

PlaceView place_view(const SaveGame& save, int town, PlaceUi& ui) {
	PlaceView v;
	const int player = save.player;
	auto choose = [&](std::vector<uint16_t> ships) {
		v.ships = std::move(ships);
		v.ship = v.ships.empty() ? -1 : int(v.ships[ui.ship % v.ships.size()]);
	};
	auto chosen_line = [&](const std::vector<std::string>& none) {
		if (v.ship >= 0) {
			v.lines.push_back({game_text(GameText::TavernChosenShip, tr("Barco elegido:", "Chosen ship:")) + " " +
					ship_label(save.ships[size_t(v.ship)]) + (v.ships.size() > 1 ? tr("   (Select: otro)", "   (Select: another)") : ""),
				kTextColor});
		} else {
			for (const std::string& line : none) {
				if (!line.empty()) {
					v.lines.push_back({line, kDimColor});
				}
			}
		}
	};
	switch (ui.open) {
	case Place::Tavern: {
		v.title = place_name(ui.let_type);
		v.rows = {{game_text(GameText::TavernHireSailors, tr("Contratar marineros", "Hire sailors")), true},
			{game_text(GameText::TavernHireCaptain, tr("Contratar capitán", "Hire captain")), true}};
		ui.row = std::clamp(ui.row, 0, 1);
		const std::string no_ship = game_text(GameText::TavernNoShip, tr("No tienes barcos adecuados en el puerto",
			"You have no ship in port"));
		if (ui.row == 0) {
			choose(window_ships(save, player, town, ShipListMode::Sailors));
			const uint32_t shown = tavern_sailors(save, player, town, ui.roll);
			chosen_line({no_ship, game_text(GameText::TavernNoShipForSailors, "")});
			v.lines.push_back({game_text(GameText::TavernThereAre, tr("Hay", "There are")) + " " + std::to_string(shown) + " " +
					game_text(GameText::TavernSailorsIn, tr("marineros en la taberna.", "sailors in the tavern.")),
				kTextColor});
			if (v.ship >= 0) {
				v.max_amount = int(shown);
				v.lines.push_back({game_text(GameText::TavernHowMany, tr("¿A cuántos quieres contratar?",
					"How many do you want to hire?")), kTextColor});
			}
			v.rows[0].second = v.ship >= 0 && shown > 0;
		} else {
			choose(window_ships(save, player, town, ShipListMode::Captains));
			v.captain = tavern_captain(save, town, uint16_t(player));
			if (v.captain != 0xFFFF) {
				const CaptainInfo c = captain_info(save, v.captain);
				v.lines.push_back({game_text(GameText::TavernCaptain, tr("Capitán", "Captain")) + " " +
						captain_name(save, v.captain) + " " +
						game_text(GameText::TavernOffersService, tr(": te ofrece sus servicios.", "offers his service to you.")),
					kTextColor});
				v.lines.push_back({game_text(GameText::TavernAge, tr("Edad:", "Age:")) + " " + std::to_string(c.age), kTextColor});
				// The game draws the three skills as pictures; these words are the viewer's.
				v.lines.push_back({tr("Comercio ", "Trading ") + std::to_string(c.trading) + tr(", navegación ", ", sailing ") +
						std::to_string(c.sailing) + tr(", combate ", ", fighting ") + std::to_string(c.fighting) + tr(" (de 5)", " (of 5)"),
					kTextColor});
				v.lines.push_back({game_text(GameText::TavernDemands, tr("Te pide", "He demands")) + " " +
						format_money(c.wage) + " " + game_text(GameText::TavernDailyWage, tr("como sueldo diario.", "as daily wage.")),
					kTextColor});
			} else {
				v.lines.push_back({tr("No hay ningún capitán en la taberna.", "There is no captain in the tavern."), kDimColor});
			}
			chosen_line({no_ship});
			v.rows[1].second = v.ship >= 0 && v.captain != 0xFFFF;
		}
		break;
	}
	case Place::Shipyard: {
		v.act = game_text(GameText::ShipyardRepair, tr("Reparar", "Repair"));
		v.title = place_name(ui.let_type) + " - " + v.act;
		const std::vector<uint16_t> ships = window_ships(save, player, town, ShipListMode::Repairs);
		v.lines.push_back({game_text(GameText::ShipyardRepairsHere, tr("Encargos de reparación en este astillero:",
			"For repairs at this shipyard")), kTextColor});
		for (const uint16_t s : ships) {
			const SavedShip& ship = save.ships[s];
			const bool convoy = ship.convoy() < save.globals.u16(global::kShips + 0xF6);
			v.rows.push_back({(convoy ? game_text(GameText::ShipyardConvoy, tr("Convoy", "Convoy")) + " " + ship.name()
									  : ship_label(ship)) + ": " + std::to_string(hull_percent(ship)) + " %",
				true});
		}
		ui.row = std::clamp(ui.row, 0, std::max(0, int(ships.size()) - 1));
		if (ships.empty()) {
			v.lines.push_back({game_text(GameText::ShipyardNoShips, tr("No hay barcos disponibles en el puerto.",
				"are no further ships available.")), kDimColor});
		} else {
			v.ship = ships[size_t(ui.row)];
			const std::string cost = format_money(repair_cost(save, town, v.ship));
			v.lines.push_back({g_texts ? g_texts->format(GameText::ShipyardRepairCost, {cost})
									   : tr("Los costes de reparación ascienden a ", "The cost of repairs amounts to ") + cost + ".",
				kTextColor});
		}
		break;
	}
	case Place::Ship: {
		std::vector<uint16_t> ships;
		for (const int s : player_ships_in(save, town)) {
			ships.push_back(uint16_t(s));
		}
		choose(std::move(ships));
		if (v.ship < 0) {
			v.title = tr("Barco", "Ship");
			v.lines.push_back({tr("No tienes ningún barco en este puerto.", "You have no ship in this port."), kDimColor});
			break;
		}
		const SavedShip& ship = save.ships[size_t(v.ship)];
		v.title = ship_label(ship) + (v.ships.size() > 1 ? tr("   (Select: otro)", "   (Select: another)") : "");
		const std::string captain_word = game_text(GameText::ShipCaptain, tr("Capitán", "Captain"));
		if (ship.captain() < save.globals.u16(global::kCaptainCount)) {
			v.lines.push_back({captain_word + ": " + captain_name(save, ship.captain()) + " (" +
					std::to_string(captain_info(save, ship.captain()).age) + tr(" años)", " years)"),
				kTextColor});
		} else {
			v.lines.push_back({captain_word + ": " + tr("ninguno", "none"), kDimColor});
		}
		v.lines.push_back({game_text(GameText::TavernSailors, tr("Marineros", "Sailors")) + ": " +
				std::to_string(ship.sailors()), kTextColor});
		v.lines.push_back({tr("Casco: ", "Hull: ") + std::to_string(hull_percent(ship)) + " %", kTextColor});
		const bool docked = ship_window_open(save, v.ship);
		if (!docked) {
			v.lines.push_back({tr("Solo se puede con el barco atracado.", "Only with the ship docked."), kDimColor});
		}
		v.rows = {{capitalized(game_text(GameText::ShipDismissSailors, tr("Despedir marineros", "Dismiss sailors"))),
					  docked && ship.sailors() > 0},
			{capitalized(game_text(GameText::ShipDismissCaptain, tr("Despedir capitán", "Dismiss captain"))),
				can_dismiss_captain(save, v.ship)}};
		ui.row = std::clamp(ui.row, 0, 1);
		if (ui.row == 0 && docked) {
			v.max_amount = ship.sailors();
		}
		break;
	}
	default:
		break;
	}
	if (v.max_amount >= 0) {
		ui.amount = std::clamp(ui.amount, 0, v.max_amount);  // As the windows keep it (0x5D4CF8, 0x494049).
	}
	return v;
}

// Disposición de las ventanas de los lugares, compartida por el dibujo y el táctil.
struct PlaceLayout {
	static constexpr int kWidth = 600;
	static constexpr int kPadding = 10;
	static constexpr int kRowHeight = 22;
	static constexpr int kMaxRows = 6;
	SDL_Rect panel{};
	int header_y = 0;
	int rows_y = 0;
	int footer_y = 0;
	SDL_Rect minus_button{};
	SDL_Rect plus_button{};
	SDL_Rect act_button{};

	explicit PlaceLayout(int line_height) {
		const int button_height = line_height + 6;
		panel = {kScreenWidth - kWidth - 8, kBarHeight + 4, kWidth, kScreenHeight - kBarHeight - 12};
		header_y = panel.y + kPadding;
		rows_y = header_y + line_height + 8;
		const int button_y = panel.y + panel.h - kPadding - button_height;
		footer_y = button_y - line_height - 6;
		const int inner = kWidth - 2 * kPadding;
		minus_button = {panel.x + kPadding, button_y, 60, button_height};
		plus_button = {minus_button.x + minus_button.w + 8 + 120 + 8, button_y, 60, button_height};
		act_button = {plus_button.x + plus_button.w + 8, button_y, panel.x + kPadding + inner - (plus_button.x + plus_button.w + 8),
			button_height};
	}

	// The amount's box, between − and +.
	SDL_Rect amount_box() const {
		return {minus_button.x + minus_button.w + 8, minus_button.y, 120, minus_button.h};
	}

	int row_at(int x, int y, int rows) const {
		if (x < panel.x || x >= panel.x + panel.w || y < rows_y || y >= rows_y + kMaxRows * kRowHeight) {
			return -1;
		}
		const int row = (y - rows_y) / kRowHeight;
		return row < rows ? row : -1;
	}
};

void draw_place(SDL_Renderer* renderer, TextRenderer& text, const PlaceUi& ui, const PlaceView& v) {
	const PlaceLayout layout(text.line_height());
	const SDL_Rect& panel = layout.panel;
	const int left = panel.x + PlaceLayout::kPadding;
	const int width = panel.w - 2 * PlaceLayout::kPadding;
	const int lh = text.line_height();
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(renderer, 30, 22, 14, 225);
	SDL_RenderFillRect(renderer, &panel);
	SDL_SetRenderDrawColor(renderer, 150, 120, 70, 255);
	SDL_RenderDrawRect(renderer, &panel);
	text.draw(v.title, left, layout.header_y, kTextColor);
	const std::string close = tr(BTN_CIRCLE " cerrar", BTN_CIRCLE " close");
	text.draw(close, panel.x + panel.w - PlaceLayout::kPadding - text.measure(close), layout.header_y, kDimColor);

	// The rows (the shipyard's ships scroll with the selection).
	const int first = std::max(0, ui.row - PlaceLayout::kMaxRows + 1);
	for (int i = first; i < int(v.rows.size()) && i < first + PlaceLayout::kMaxRows; ++i) {
		const int y = layout.rows_y + (i - first) * PlaceLayout::kRowHeight;
		if (i == ui.row) {
			const SDL_Rect box = {panel.x + 2, y - 1, panel.w - 4, PlaceLayout::kRowHeight};
			SDL_SetRenderDrawColor(renderer, 110, 80, 40, 230);
			SDL_RenderFillRect(renderer, &box);
		}
		text.draw(v.rows[size_t(i)].first, left, y, v.rows[size_t(i)].second ? kTextColor : kDimColor);
	}
	// What it says, right under its rows.
	int y = layout.rows_y + std::min(int(v.rows.size()), PlaceLayout::kMaxRows) * PlaceLayout::kRowHeight + 10;
	for (const auto& [line, color] : v.lines) {
		for (const std::string& part : wrap_text(text, line, width)) {
			if (y + lh > layout.footer_y) {
				break;
			}
			text.draw(part, left, y, color);
			y += lh;
		}
	}
	if (!ui.message.empty() && !SDL_TICKS_PASSED(app_ticks(), ui.message_until)) {
		text.draw(ui.message, left, layout.footer_y, kTextColor);
	}
	if (v.max_amount >= 0) {
		draw_button(renderer, text, layout.minus_button, "-");
		draw_button(renderer, text, layout.plus_button, "+");
		const SDL_Rect box = layout.amount_box();
		draw_button(renderer, text, box, std::to_string(ui.amount));
	}
	const bool can = ui.row < int(v.rows.size()) && v.rows[size_t(ui.row)].second && (v.max_amount < 0 || ui.amount > 0);
	if (ui.row < int(v.rows.size())) {
		draw_button(renderer, text, layout.act_button,
			std::string(BTN_CROSS " ") + (v.act.empty() ? v.rows[size_t(ui.row)].first : v.act));
		if (!can) {
			const SDL_Rect& r = layout.act_button;
			SDL_SetRenderDrawColor(renderer, 30, 22, 14, 150);
			SDL_RenderFillRect(renderer, &r);
		}
	}
}

// X or the act button: what the selected row sends, as the game's window
// builds it, run at once (the queue, as the market does).
void place_act(SaveGame& save, Sailing* sailing, int town, PlaceUi& ui, const PlaceView& v) {
	ui.message_until = app_ticks() + kMessageTime;
	if (ui.row >= int(v.rows.size()) || !v.rows[size_t(ui.row)].second || (v.max_amount >= 0 && ui.amount <= 0)) {
		ui.message = tr("Ahora no se puede.", "It can't be done now.");
		return;
	}
	std::optional<OperationBytes> op;
	const int ship = v.ship;
	const SavedShip* s = ship >= 0 ? &save.ships[size_t(ship)] : nullptr;
	const int sailors = s ? s->sailors() : 0;
	const uint16_t captain = s ? s->captain() : 0xFFFF;
	if (ui.open == Place::Tavern && ui.row == 0) {
		op = hire_sailors_operation(uint32_t(ship), uint32_t(ui.amount));
	} else if (ui.open == Place::Tavern) {
		op = hire_captain_operation(v.captain, uint32_t(ship), uint32_t(town));
	} else if (ui.open == Place::Shipyard) {
		op = repair_window_operation(save, ship);
	} else if (ui.open == Place::Ship && ui.row == 0) {
		op = dismiss_sailors_operation(uint32_t(ship), uint32_t(ui.amount));
	} else if (ui.open == Place::Ship) {
		op = dismiss_captain_operation(uint32_t(ship));
	}
	if (!op || !s) {
		return;
	}
	if (sailing) {
		queue_operation(save, *op);
		run_queued_operations(save, *sailing);
	} else {
		run_operation(save, nullptr, op->data());
	}
	const std::string name = s->name();
	if (ui.open == Place::Tavern && ui.row == 0) {
		const int n = s->sailors() - sailors;
		ui.message = g_spanish ? "Contratas " + std::to_string(n) + " marineros para el " + name + "."
							   : "You hire " + std::to_string(n) + " sailors for the " + name + ".";
		ui.amount = 0;
	} else if (ui.open == Place::Tavern) {
		ui.message = s->captain() != captain ? tr("El nuevo capitán sube a bordo del ", "The new captain boards the ") + name + "."
											 : tr("El capitán no ha subido a bordo.", "The captain hasn't come on board.");
	} else if (ui.open == Place::Shipyard) {
		ui.message = s->status() == 4 ? name + tr(" entra en el astillero.", " goes into the shipyard.")
									  : tr("No se ha podido reparar (mira el buzón).", "It couldn't be repaired (see the mailbox).");
	} else if (ui.row == 0) {
		const int n = sailors - s->sailors();
		ui.message = g_spanish ? "Despides a " + std::to_string(n) + " marineros." : "You dismiss " + std::to_string(n) + " sailors.";
		ui.amount = 0;  // The slider back to 0 (0x49419D).
	} else {
		ui.message = s->captain() != captain ? tr("Has despedido al capitán.", "You have dismissed the captain.")
											 : tr("El capitán sigue a bordo.", "The captain is still on board.");
	}
}

// The ship's card at the bottom left of a town, as the game's side panel: the
// player's ship in its port (tap it, or Select, for its window).
SDL_Rect ship_card_rect(TextRenderer& text, const std::string& label) {
	const int padding = 8;
	const int w = text.measure(label) + 2 * padding;
	const int h = text.line_height() + padding;
	return {8, kScreenHeight - h - 8, w, h};
}

std::string ship_card_label(const SaveGame& save, int town) {
	const std::vector<int> ships = player_ships_in(save, town);
	if (ships.empty()) {
		return "";
	}
	std::string label = tr("Barco: ", "Ship: ") + ship_label(save.ships[size_t(ships[0])]);
	if (ships.size() > 1) {
		label += " +" + std::to_string(ships.size() - 1);
	}
	return label;
}

// --- The Hanse panel: the path to Alderman («Gobernador») ---
// What the game shows in its Hanse task window (0x5E87D0) and its road
// window (0x5F2A20), and the operations they queue (0x4B and 0x3B, built in
// p3data/hanse_tasks.cpp); see docs/cloud/alderman-viewer.md. The game's own
// texts are in Patrician3.exe, not in the CPR, so these are the viewer's,
// with the names (towns, people, ships) from the CPR and the save.

struct HanseUi {
	bool open = false;
	int page = 0;  // 0 the Hanse tasks, 1 the election (Select).
	int row = 0;  // Selected task, material or candidate.
	int amount = 0;  // Index in kRoadAmounts.
	std::string message;
	Uint32 message_until = 0;
	bool briefing = false;  // The player has just taken a task: show what the Hanse says.
};

// What the panel reads besides the save: names, regions and the game's texts.
struct HanseContext {
	const std::vector<std::string>& town_names;
	const NameLists& names;
	const std::vector<int>& regions;
	const GameTexts* texts;  // nullptr: the viewer's own strings.

	// The game's text, or `fallback` without the executable.
	std::string text(GameText id, const std::string& fallback) const { return texts ? texts->get(id) : fallback; }
};

// What the panel shows, worked out from the save each frame.
struct HanseView {
	int level = 0;  // candidacy_level of the player.
	bool is_alderman = false;
	uint16_t task = 0xFFFF;  // The player's task, or 0xFFFF.
	std::vector<uint16_t> offered;  // Tasks on offer, for a candidate without a task.
	bool road_here = false;  // The player's road can take materials in this town.
	RoadMaterials road;
	std::array<int, 4> available{};
	// The election: today's (with its votes) or the next one's candidates.
	uint16_t election = 0xFFFF;  // Today's event 0x20, or 0xFFFF.
	bool can_vote = false;  // 0x4D9D40.
	std::array<ElectionCandidate, 4> candidates{};

	int rows(int page) const {
		if (page == 1) {
			return can_vote ? 4 : 0;
		}
		return task != 0xFFFF ? (road_here ? 4 : 0) : int(offered.size());
	}
};

HanseView hanse_view(const SaveGame& save, int town, int page) {
	HanseView v;
	const int player = save.player;
	v.level = candidacy_level(save, player);
	v.is_alderman = alderman(save) == player;
	v.task = merchant_hanse_task(save, player);
	if (v.task == 0xFFFF) {
		// The game's window offers the tasks to a candidate without one (0x4EC5F4).
		if (v.level >= 1 && v.level <= 3) {
			v.offered = offered_hanse_tasks(save);
		}
	} else if (road_materials(save, v.task, player, v.road)) {
		// The road window works in either of the two towns while sections are
		// left (what the executor of 0x3B, 0x53A3E0, accepts).
		const HanseTask t = hanse_task(save, v.task);
		v.road_here = (t.data[0] == town || t.data[1] == town) && t.data[2] > 0;
		v.available = available_road_materials(save, player, town);
	}
	if (page == 1) {
		v.election = election_today(save);
		v.can_vote = player_can_vote(save);
		v.candidates = v.election != 0xFFFF ? election_candidates(save, v.election) : next_election_candidates(save);
	}
	return v;
}

// Name of a town by its number in StadtDaten.ini (not its index in the save).
std::string numbered_town_name(const std::vector<std::string>& names, int number) {
	return number >= 0 && number < int(names.size()) ? names[size_t(number)]
													  : tr("la ciudad ", "town ") + std::to_string(number);
}

std::string full_date(uint32_t tick) {
	const GameDate date = date_from_tick(tick);
	return game_date(date.day, date.month, date.year);
}

std::string index_town_name(const SaveGame& save, const HanseContext& c, int town) {
	return town < int(save.towns.size()) ? town_name(save, c.town_names, town)
										 : tr("la ciudad ", "town ") + std::to_string(town);
}

// One line for a task: what to do.
std::string hanse_task_text(const SaveGame& save, const HanseTask& t, const HanseContext& c) {
	switch (HanseTaskType(t.type)) {
	case HanseTaskType::FoundTown:
		return tr("Fundar una ciudad nueva: ", "Found a new town: ") + numbered_town_name(c.town_names, t.data[4]);
	case HanseTaskType::Road:
		return tr("Carretera entre ", "Road between ") + index_town_name(save, c, t.data[0]) + tr(" y ", " and ") +
			index_town_name(save, c, t.data[1]);
	case HanseTaskType::PirateShip: {
		const size_t ship = size_t(t.data[0] | t.data[1] << 8);
		return ship < save.ships.size() ? tr("Cazar el barco pirata ", "Hunt the pirate ship ") + save.ships[ship].name()
										: tr("Cazar un barco pirata", "Hunt a pirate ship");
	}
	case HanseTaskType::PirateNest:
		return tr("Acabar con la guarida pirata de ", "Destroy the pirate hideaway near ") +
			numbered_town_name(c.town_names, t.data[1]);
	case HanseTaskType::GrowTown:
		return g_spanish ? "Que " + index_town_name(save, c, t.data[2]) + " llegue a " +
				format_money(t.data[0] | t.data[1] << 8) + " habitantes"
						 : "Let " + index_town_name(save, c, t.data[2]) + " reach " +
				format_money(t.data[0] | t.data[1] << 8) + " inhabitants";
	default:
		return tr("Encargo anulado", "Task cancelled");
	}
}

// The region of a town (its number in StadtDaten.ini) as the task window
// names it (0x5E6C19): by ranges of the number, not by the Region= of the ini.
std::string region_text(const HanseContext& c, int number) {
	if (!c.texts || number < 0) {
		return "";
	}
	const GameText region = number < 0x06 ? GameText::RegionWest
		: number < 0x0D ? GameText::RegionNorthSea
		: number < 0x10 ? GameText::RegionBaltic
		: number < 0x1C ? GameText::RegionNorth
		: number < 0x21 ? GameText::RegionBaltic
		: GameText::RegionEast;
	return c.texts->get(region);
}

// The game's description of a task on offer (with the region), or empty.
std::string hanse_task_description(const SaveGame& save, const HanseTask& t, const HanseContext& c) {
	if (!c.texts || t.type >= 5) {
		return "";
	}
	int number = -1;
	if (HanseTaskType(t.type) == HanseTaskType::FoundTown) {
		number = t.data[4];
	} else if (HanseTaskType(t.type) == HanseTaskType::Road && t.data[0] < save.towns.size()) {
		number = save.towns[t.data[0]].town_id();
	} else if (HanseTaskType(t.type) == HanseTaskType::GrowTown && t.data[2] < save.towns.size()) {
		number = save.towns[t.data[2]].town_id();
	}
	return c.texts->format(GameText(int(GameText::TaskFoundTown) + t.type), {region_text(c, number)});
}

// What the Hanse says when the player takes the task, or empty.
std::string hanse_thanks(const SaveGame& save, const HanseTask& t, const HanseContext& c) {
	if (!c.texts || t.type >= 5) {
		return "";
	}
	// The names, then the deadline (day, month, year); the town that grows
	// has them by position, with its inhabitants fifth.
	const GameDate date = date_from_tick(t.deadline);
	const std::vector<std::string> deadline = {std::to_string(date.day), c.texts->month(date.month),
		std::to_string(date.year)};
	std::vector<std::string> args;
	switch (HanseTaskType(t.type)) {
	case HanseTaskType::FoundTown: {
		const std::string name = numbered_town_name(c.town_names, t.data[4]);
		args = {name, name};
		break;
	}
	case HanseTaskType::Road:
		args = {index_town_name(save, c, t.data[0]), index_town_name(save, c, t.data[1])};
		break;
	case HanseTaskType::PirateShip: {
		// The captain's first name and family name ([+0x12], [+0x14] of the event).
		const size_t first = size_t(t.data[2] | t.data[3] << 8);
		const size_t family = size_t(t.data[4] | t.data[5] << 8);
		args = {first < c.names[0].size() ? c.names[0][first] : "", family < c.names[2].size() ? c.names[2][family] : ""};
		break;
	}
	case HanseTaskType::GrowTown:
		args = {index_town_name(save, c, t.data[2])};
		break;
	default:
		break;
	}
	args.insert(args.end(), deadline.begin(), deadline.end());
	if (HanseTaskType(t.type) == HanseTaskType::GrowTown) {
		args.push_back(format_money(t.data[0] | t.data[1] << 8));
	}
	return c.texts->format(GameText(int(GameText::ThanksFoundTown) + t.type), args);
}

// How the player's task is going.
std::string hanse_progress_text(const SaveGame& save, const HanseTask& t, const HanseView& v, const HanseContext& c) {
	switch (HanseTaskType(t.type)) {
	case HanseTaskType::FoundTown:
		return t.started && t.data[6] < save.towns.size()
			? (g_spanish ? "Se cumple cuando " + town_name(save, c.town_names, t.data[6]) + " tenga mercado."
						 : "Done when " + town_name(save, c.town_names, t.data[6]) + " has a market.")
			: tr("La ciudad se funda al empezar el encargo.", "The town is founded when the task starts.");
	case HanseTaskType::Road:
		return tr("Tramos hechos: ", "Sections built: ") + std::to_string(v.road.sections - v.road.sections_left) +
			tr(" de ", " of ") + std::to_string(v.road.sections) + " (" + std::to_string(v.road.percent()) + " %).";
	case HanseTaskType::PirateShip:
		return tr("Hunde el barco pirata (las batallas aún no están en p3vita).",
			"Sink the pirate ship (the battles aren't in p3vita yet).");
	case HanseTaskType::PirateNest:
		return t.data[2] == 0 ? tr("Acerca un barco a la guarida.", "Take a ship to the hideaway.")
							  : tr("Ataca la guarida (las batallas aún no están).",
								  "Attack the hideaway (the battles aren't there yet).");
	case HanseTaskType::GrowTown:
		return t.data[2] < save.towns.size()
			? tr("Habitantes ahora: ", "Inhabitants now: ") + format_money(save.towns[t.data[2]].citizens()) + "."
			: "";
	default:
		return "";
	}
}

// A candidate in the election: a merchant (with its home town) or the mayor of a town.
std::string candidate_name(const SaveGame& save, const HanseContext& c, const ElectionCandidate& candidate) {
	if (candidate.index < 0) {
		return "-";
	}
	if (candidate.merchant) {
		const SavedMerchant& m = save.merchants[size_t(candidate.index)];
		std::string name = merchant_name(save, c.names, candidate.index) + tr(" de ", " from ") +
			index_town_name(save, c, m.hometown());
		return candidate.index == save.player ? name + tr(" (tú)", " (you)") : name;
	}
	return tr("El alcalde de ", "The mayor of ") + index_town_name(save, c, candidate.index);
}

std::string road_amount_text(int amount) {
	const uint32_t n = kRoadAmounts[size_t(amount)];
	return n == 0xFFFFFFFF ? tr("todo", "all") : std::to_string(n);
}

// A text in lines that fit in `width` pixels (the game's texts are long and
// have line breaks).
std::vector<std::string> wrap_text(const TextRenderer& text, const std::string& s, int width) {
	std::vector<std::string> lines;
	size_t start = 0;
	while (start <= s.size()) {
		size_t end = s.find('\n', start);
		if (end == std::string::npos) {
			end = s.size();
		}
		const std::string paragraph = s.substr(start, end - start);
		std::string line;
		size_t pos = 0;
		while (pos < paragraph.size()) {
			size_t next = paragraph.find(' ', pos);
			if (next == std::string::npos) {
				next = paragraph.size();
			}
			const std::string word = paragraph.substr(pos, next - pos);
			const std::string candidate = line.empty() ? word : line + " " + word;
			if (!line.empty() && text.measure(candidate) > width) {
				lines.push_back(line);
				line = word;
			} else {
				line = candidate;
			}
			pos = next + 1;
		}
		if (!line.empty()) {
			lines.push_back(line);
		}
		start = end + 1;
	}
	return lines;
}

// Disposición del panel, compartida por el dibujo y el táctil.
struct HanseLayout {
	static constexpr int kWidth = 600;
	static constexpr int kPadding = 10;
	static constexpr int kRowHeight = 22;
	static constexpr int kMaxRows = 5;
	static constexpr int kInfoLines = 5;
	SDL_Rect panel{};
	int header_y = 0;
	int info_y = 0;
	int title_y = 0;
	int rows_y = 0;
	int detail_y = 0;
	int footer_y = 0;
	SDL_Rect amount_button{};
	SDL_Rect act_button{};

	explicit HanseLayout(int line_height) {
		const int button_height = line_height + 4;
		panel = {kScreenWidth - kWidth - 8, kBarHeight + 4, kWidth, kScreenHeight - kBarHeight - 12};
		header_y = panel.y + kPadding;
		info_y = header_y + line_height + 6;
		title_y = info_y + kInfoLines * line_height + 6;
		rows_y = title_y + line_height + 2;
		detail_y = rows_y + kMaxRows * kRowHeight + 6;
		const int button_y = panel.y + panel.h - kPadding - button_height;
		footer_y = button_y - line_height - 4;
		const int inner = kWidth - 2 * kPadding - 8;
		amount_button = {panel.x + kPadding, button_y, inner / 2, button_height};
		act_button = {amount_button.x + amount_button.w + 8, button_y, inner - amount_button.w, button_height};
	}

	int row_at(int x, int y, int rows) const {
		if (x < panel.x || x >= panel.x + panel.w || y < rows_y) {
			return -1;
		}
		const int row = (y - rows_y) / kRowHeight;
		return row < rows ? row : -1;
	}
};

// X or the act button: take the selected task (0x4B), give the selected
// material (0x3B) or vote for the selected candidate (0x3F), as the game's
// windows do. The executor runs it after the next tick, as the game runs its
// queue between two calls to advance_time.
void hanse_act(SaveGame& save, int town, HanseUi& ui, const HanseView& v, const HanseContext& c) {
	ui.message_until = app_ticks() + kMessageTime;
	if (ui.page == 1) {
		if (!v.can_vote) {
			ui.message = v.election != 0xFFFF ? tr("No puedes votar (o ya has votado).", "You can't vote (or you have voted).")
											  : tr("Hoy no hay elección.", "There is no election today.");
		} else if (ui.row < 4 && v.candidates[size_t(ui.row)].index >= 0) {
			queue_operation(save, vote_operation(uint32_t(save.player), uint32_t(ui.row)));
			std::string name = candidate_name(save, c, v.candidates[size_t(ui.row)]);
			if (name.compare(0, 3, "El ") == 0 || name.compare(0, 4, "The ") == 0) {
				name[0] = char(std::tolower(uint8_t(name[0])));
			}
			ui.message = tr("Has votado por ", "You voted for ") + name + ".";
		}
		return;
	}
	if (v.task == 0xFFFF && ui.row < int(v.offered.size())) {
		const HanseTask t = hanse_task(save, v.offered[size_t(ui.row)]);
		queue_operation(save, take_task_operation(uint32_t(save.player), t.event));
		ui.briefing = true;  // Once started (its deadline changes when it starts).
		ui.message = tr("Has aceptado el encargo.", "You have taken the task.");
		ui.row = 0;
		return;
	}
	if (v.road_here && ui.row < 4) {
		const int k = ui.row;
		const uint16_t needed = v.road.needed(k);
		if (needed == 0 || v.available[size_t(k)] <= 0) {
			ui.message = needed == 0 ? tr("Ya no hace falta más.", "No more is needed.")
									 : tr("No tienes en esta ciudad.", "You have none in this town.");
			return;
		}
		const uint16_t amount = road_window_amount(kRoadAmounts[size_t(ui.amount)], uint32_t(v.available[size_t(k)]),
			needed);
		queue_operation(save, road_materials_operation(v.task, amount, uint8_t(town), uint8_t(save.player),
			kRoadWares[size_t(k)]));
		const WareInfo& ware = kWares[kRoadWares[size_t(k)]];
		ui.message = tr("Entregas ", "You deliver ") + amount_label(amount, ware.unit) + tr(" de ", " of ") +
			ware_name(kRoadWares[size_t(k)]) + ".";
		return;
	}
	ui.message = v.level == 0 ? tr("Aún no eres candidato.", "You are not a candidate yet.")
							  : tr("No hay nada que hacer aquí.", "There is nothing to do here.");
}

// The Alderman: a merchant, or a citizen of the town [0x6DE52E].
std::string alderman_name(const SaveGame& save, const NameLists& names, const std::vector<std::string>& town_names) {
	const int chief = alderman(save);
	if (chief < save.merchant_count && chief < int(save.merchants.size())) {
		return merchant_name(save, names, chief);
	}
	const int town = save.globals.u8(0x6DE52E);
	return town < int(save.towns.size()) ? tr("un ciudadano de ", "a citizen of ") + town_name(save, town_names, town)
										 : tr("un ciudadano", "a citizen");
}

void draw_hanse(SDL_Renderer* renderer, TextRenderer& text, const SaveGame& save, const HanseUi& ui,
	const HanseView& v, const HanseContext& c) {
	const HanseLayout layout(text.line_height());
	const SDL_Rect& panel = layout.panel;
	const int left = panel.x + HanseLayout::kPadding;
	const int width = panel.w - 2 * HanseLayout::kPadding;
	const int lh = text.line_height();
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(renderer, 30, 22, 14, 225);
	SDL_RenderFillRect(renderer, &panel);
	SDL_SetRenderDrawColor(renderer, 150, 120, 70, 255);
	SDL_RenderDrawRect(renderer, &panel);

	text.draw(ui.page == 1 ? c.text(GameText::ElectionTitle, tr("Elección del Gobernador", "Election of Alderman"))
						   : tr("La Hansa", "The Hanse"),
		left, layout.header_y, kTextColor);
	const std::string close = (ui.page == 1 ? tr("Select: encargos", "Select: tasks") : tr("Select: elección", "Select: election")) +
		tr("   " BTN_CIRCLE " cerrar", "   " BTN_CIRCLE " close");
	text.draw(close, panel.x + panel.w - HanseLayout::kPadding - text.measure(close), layout.header_y, kDimColor);

	// The Alderman, the election and the player's candidacy.
	std::vector<std::pair<std::string, SDL_Color>> info;
	info.push_back({tr("Gobernador: ", "Alderman: ") + alderman_name(save, c.names, c.town_names), kTextColor});
	if (const uint32_t election = alderman_election_tick(save)) {
		info.push_back({(v.election != 0xFFFF ? tr("Elección: hoy, ", "Election: today, ")
											  : tr("Próxima elección: ", "Next election: ")) + full_date(election),
			kTextColor});
	}
	if (v.is_alderman) {
		info.push_back({tr("¡Eres Gobernador de la Hansa!", "You are the Alderman of the Hanseatic League!"), kTextColor});
	} else if (v.level == 0) {
		const std::vector<int> mayor = mayor_towns(save, save.player);
		info.push_back({tr("No eres candidato a Gobernador.", "You are not a candidate for Alderman."), kDimColor});
		info.push_back({mayor.empty() ? tr("Hace falta ser alcalde de una ciudad.", "You have to be the mayor of a town.")
									  : tr("Alcalde de ", "Mayor of ") + town_name(save, c.town_names, mayor[0]) + ".",
			kDimColor});
		info.push_back({tr("Reputación para optar: ", "Reputation to stand: ") +
				std::to_string(candidacy_reputation(save, save.player)) + tr(" de ", " of ") +
				std::to_string(kCandidacyReputation) + ".",
			kDimColor});
	} else if (v.level <= 3) {
		info.push_back({g_spanish ? "Candidato a Gobernador: " + std::to_string(v.level - 1) + " de 3 encargos cumplidos."
								  : "Candidate for Alderman: " + std::to_string(v.level - 1) + " of 3 tasks done.",
			kTextColor});
	} else {
		info.push_back({tr("Has cumplido los encargos: puedes presentarte a la elección.",
							"You have done the tasks: you can stand for election."),
			kTextColor});
	}
	for (size_t i = 0; i < info.size() && i < size_t(HanseLayout::kInfoLines); ++i) {
		text.draw(info[i].first, left, layout.info_y + int(i) * lh, info[i].second);
	}

	auto row_box = [&](int row) {
		const SDL_Rect box = {panel.x + 2, layout.rows_y + row * HanseLayout::kRowHeight - 1, panel.w - 4,
			HanseLayout::kRowHeight};
		SDL_SetRenderDrawColor(renderer, 110, 80, 40, 230);
		SDL_RenderFillRect(renderer, &box);
	};
	// Wrapped lines in the detail area, down to the footer.
	auto draw_lines = [&](const std::string& s, int y, SDL_Color color) {
		for (const std::string& line : wrap_text(text, s, width)) {
			if (y + lh > layout.footer_y) {
				break;
			}
			text.draw(line, left, y, color);
			y += lh;
		}
		return y;
	};

	if (ui.page == 1) {
		// The election: the candidates (with their votes on the day) and the vote.
		const bool today = v.election != 0xFFFF;
		std::string intro;
		if (v.can_vote) {
			intro = c.text(GameText::VotePrompt, tr("Elige un candidato y vota.", "Choose a candidate and cast your vote."));
		} else if (today) {
			intro = c.text(GameText::CandidatesIntro, tr("Se presentan a la elección:", "Standing for election:"));
		} else if (const uint32_t tick = alderman_election_tick(save)) {
			const GameDate date = date_from_tick(tick);
			const int place = save.globals.u8(0x6DE52E);
			intro = c.texts ? c.texts->format(GameText::Nominated, {std::to_string(date.day), c.texts->month(date.month),
								  std::to_string(date.year), index_town_name(save, c, place)})
							: tr("Candidatos para la próxima elección:", "Candidates for the next election:");
		}
		text.draw(today ? tr("Candidatos y votos de hoy:", "Candidates and today's votes:") : tr("Candidatos:", "Candidates:"),
			left, layout.title_y, kDimColor);
		for (size_t k = 0; k < v.candidates.size(); ++k) {
			const int y = layout.rows_y + int(k) * HanseLayout::kRowHeight;
			if (v.can_vote && int(k) == ui.row) {
				row_box(int(k));
			}
			text.draw(candidate_name(save, c, v.candidates[k]), left, y, kTextColor);
			if (today && v.candidates[k].index >= 0) {
				const std::string votes = std::to_string(v.candidates[k].votes) + tr(" votos", " votes");
				text.draw(votes, panel.x + panel.w - HanseLayout::kPadding - text.measure(votes), y, kDimColor);
			}
		}
		draw_lines(intro, layout.detail_y, kDimColor);
	} else if (v.task == 0xFFFF && !v.offered.empty()) {
		text.draw(c.text(GameText::TasksChoose, tr("Encargos de la Hansa (elige uno):", "The Hanse's tasks (choose one):")),
			left, layout.title_y, kDimColor);
		for (size_t i = 0; i < v.offered.size(); ++i) {
			const int y = layout.rows_y + int(i) * HanseLayout::kRowHeight;
			if (int(i) == ui.row) {
				row_box(int(i));
			}
			const HanseTask t = hanse_task(save, v.offered[i]);
			text.draw(hanse_task_text(save, t, c), left, y, kTextColor);
		}
		if (ui.row < int(v.offered.size())) {
			const HanseTask t = hanse_task(save, v.offered[size_t(ui.row)]);
			const int y = draw_lines(tr("Plazo: hasta el ", "Deadline: ") + full_date(t.deadline) + ".", layout.detail_y,
				kTextColor);
			draw_lines(hanse_task_description(save, t, c), y, kDimColor);
		}
	} else if (v.task != 0xFFFF) {
		const HanseTask t = hanse_task(save, v.task);
		text.draw(tr("Tu encargo: ", "Your task: ") + hanse_task_text(save, t, c), left, layout.title_y, kTextColor);
		if (v.road_here) {
			static constexpr int kColumns[2] = {390, 590};  // Right edges.
			auto right = [&](const std::string& s, int column, int y, SDL_Color color) {
				text.draw(s, panel.x + kColumns[column] - text.measure(s), y, color);
			};
			for (int k = 0; k < 4; ++k) {
				const int y = layout.rows_y + k * HanseLayout::kRowHeight;
				if (k == ui.row) {
					row_box(k);
				}
				const WareInfo& ware = kWares[kRoadWares[size_t(k)]];
				const uint16_t needed = v.road.needed(k);
				const bool enabled = needed != 0 && v.available[size_t(k)] > 0;
				text.draw(ware_label(kRoadWares[size_t(k)]), left, y, enabled ? kTextColor : kDimColor);
				auto amount = [&](int n) {
					return g_texts ? amount_label(n, ware.unit)
								   : std::to_string(n) + (ware.unit == WareUnit::Barrel ? tr(" barr.", " barr.") : tr(" cargas", " loads"));
				};
				right(tr("falta ", "needed ") + amount(needed), 0, y, enabled ? kTextColor : kDimColor);
				right(tr("tienes ", "you have ") + amount(v.available[size_t(k)]), 1, y, enabled ? kTextColor : kDimColor);
			}
		} else if (ui.briefing) {
			// No rows: what the Hanse says when the player takes it.
			int y = layout.rows_y;
			for (const std::string& line : wrap_text(text, hanse_thanks(save, t, c), width)) {
				if (y + lh > layout.detail_y) {
					break;
				}
				text.draw(line, left, y, kDimColor);
				y += lh;
			}
		}
		int y = draw_lines(tr("Plazo: hasta el ", "Deadline: ") + full_date(t.deadline) + ".", layout.detail_y, kTextColor);
		y = draw_lines(hanse_progress_text(save, t, v, c), y, kTextColor);
		if (HanseTaskType(t.type) == HanseTaskType::Road && !v.road_here && t.data[2] > 0) {
			draw_lines(tr("Lleva los materiales a ", "Take the materials to ") + town_name(save, c.town_names, t.data[0]) +
				tr(" o a ", " or to ") + town_name(save, c.town_names, t.data[1]) + ".", y, kDimColor);
		}
	} else if (v.level >= 1 && v.level <= 3 && c.texts) {
		draw_lines(c.texts->get(GameText::NoTasks), layout.title_y, kDimColor);
	}

	const bool show_message = !ui.message.empty() && !SDL_TICKS_PASSED(app_ticks(), ui.message_until);
	if (show_message) {
		text.draw(ui.message, left, layout.footer_y, kTextColor);
	}
	if (ui.page == 1) {
		if (v.can_vote) {
			draw_button(renderer, text, layout.act_button, tr(BTN_CROSS " votar", BTN_CROSS " vote"));
		}
	} else if (v.road_here) {
		draw_button(renderer, text, layout.amount_button, tr("Izq./Der.: ", "Left/Right: ") + road_amount_text(ui.amount));
		draw_button(renderer, text, layout.act_button, tr(BTN_CROSS " entregar", BTN_CROSS " deliver"));
	} else if (v.task == 0xFFFF && !v.offered.empty()) {
		draw_button(renderer, text, layout.act_button, tr(BTN_CROSS " aceptar el encargo", BTN_CROSS " take the task"));
	}
}

// The end: the window when the player is elected Alderman (the game also
// plays the video `eldermann` here: not yet in p3vita).
struct EndWindow {
	bool open = false;
	std::string text;
};

void draw_end_window(SDL_Renderer* renderer, TextRenderer& title, TextRenderer& text, const EndWindow& end,
	const HanseContext& c) {
	const int width = 620;
	const std::vector<std::string> lines = wrap_text(text, end.text, width - 40);
	const int height = 40 + title.line_height() + 16 + int(lines.size()) * text.line_height() + 24 +
		text.line_height() + 8;
	const SDL_Rect box = {(kScreenWidth - width) / 2, (kScreenHeight - height) / 2, width, height};
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(renderer, 30, 22, 14, 240);
	SDL_RenderFillRect(renderer, &box);
	SDL_SetRenderDrawColor(renderer, 190, 150, 90, 255);
	SDL_RenderDrawRect(renderer, &box);
	const std::string heading = c.text(GameText::AldermanOffice, tr("Gobernador de la Hansa", "Alderman's office"));
	int y = box.y + 20;
	title.draw(heading, box.x + (width - title.measure(heading)) / 2, y, kTextColor);
	y += title.line_height() + 16;
	for (const std::string& line : lines) {
		text.draw(line, box.x + (width - text.measure(line)) / 2, y, kTextColor);
		y += text.line_height();
	}
	const std::string hint = tr(BTN_CROSS " continuar", BTN_CROSS " continue");
	text.draw(hint, box.x + (width - text.measure(hint)) / 2, box.y + height - 8 - text.line_height(), kDimColor);
}

// The game's messages to the player in a window (Sailing::take_player_messages:
// the battle's question when a battle against the player starts, the
// boarding's message), one after another: the game's text, its first line
// («Batalla naval:», «Abordaje»...) as the title.
struct MessageWindow {
	bool open = false;
	std::string title;
	std::string text;
	std::deque<std::string> pending;  // Raw texts of the game, still to show.

	void next() {
		open = !pending.empty();
		if (!open) {
			return;
		}
		const std::string clean = clean_game_text(pending.front());
		pending.pop_front();
		const size_t line = clean.find('\n');
		title = clean.substr(0, line);
		text = line == std::string::npos ? std::string() : clean.substr(line + 1);
		const size_t first = text.find_first_not_of('\n');
		const size_t last = text.find_last_not_of('\n');
		text = first == std::string::npos ? std::string() : text.substr(first, last - first + 1);
	}
	void push(const std::string& raw) {
		pending.push_back(raw);
		if (!open) {
			next();
		}
	}
};

void draw_message_window(SDL_Renderer* renderer, TextRenderer& title, TextRenderer& text, const MessageWindow& w) {
	const int width = 620;
	const std::vector<std::string> lines = wrap_text(text, w.text, width - 40);
	const int height = 40 + title.line_height() + 16 + int(lines.size()) * text.line_height() + 24 +
		text.line_height() + 8;
	const SDL_Rect box = {(kScreenWidth - width) / 2, (kScreenHeight - height) / 2, width, height};
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(renderer, 30, 22, 14, 240);
	SDL_RenderFillRect(renderer, &box);
	SDL_SetRenderDrawColor(renderer, 190, 150, 90, 255);
	SDL_RenderDrawRect(renderer, &box);
	int y = box.y + 20;
	title.draw(w.title, box.x + (width - title.measure(w.title)) / 2, y, kTextColor);
	y += title.line_height() + 16;
	for (const std::string& line : lines) {
		text.draw(line, box.x + 20, y, kTextColor);
		y += text.line_height();
	}
	const std::string hint = w.pending.empty() ? tr(BTN_CROSS " continuar", BTN_CROSS " continue") : tr(BTN_CROSS " siguiente", BTN_CROSS " next");
	text.draw(hint, box.x + (width - text.measure(hint)) / 2, box.y + height - 8 - text.line_height(), kDimColor);
}

void draw_panel_box(SDL_Renderer* renderer, const SDL_Rect& box, Uint8 alpha = 235) {
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(renderer, 30, 22, 14, alpha);
	SDL_RenderFillRect(renderer, &box);
	SDL_SetRenderDrawColor(renderer, 150, 120, 70, 255);
	SDL_RenderDrawRect(renderer, &box);
}

// The mailbox (Circle in a town or on the sea map, or the menu): the game's
// letter window (0x47C520, 0x47D570, 0x47F3B0) with its four tabs: 0 the
// chronicle («Public news», 0x47DD40), 1..3 the letters of each class. The
// list on the left, the letter on the right. Picking a letter marks it read
// (operation 0x6E) and Square deletes it (0x6F); the executor runs them after
// the next tick, as in the game, so the screen hides a letter whose deletion
// is queued and shows as read one whose reading is.
struct MailUi {
	bool open = false;
	int tab = 1;
	int row = 0;  // The selected row of the list.
	int first = 0;  // The first row shown.
	int scroll = 0;  // The text's, in pixels.
	uint16_t selected = 0xFFFF;  // The entry of the selected row (the rows move when a letter comes).
	uint16_t shown = 0xFFFF;  // The entry whose text is in `lines`.
	int shown_tab = -1;
	std::vector<std::pair<LetterParagraph::Align, std::string>> lines;  // Wrapped, UTF-8 (kOrnament: a picture).
	bool stale = true;  // `lines` to be made again (another letter, language or font).
	uint16_t chronicle_head = 0xFFFF;  // The chronicle's newest news when it was made.
	// The picture the window makes for the letter shown (0x46, 0x6E: the map
	// of letter_map_picture), drawn at the line kPictureLine.
	std::shared_ptr<SDL_Texture> picture;
	int picture_width = 0;
	int picture_height = 0;
};

// A line of the mailbox that is one of the game's pictures (the chronicle's
// ornament between two news): drawn as a short rule.
constexpr char kOrnament = '\x1E';
// The line where the letter's own picture (MailUi::picture) starts; empty
// lines under it keep its height.
constexpr char kPictureLine = '\x1D';

// The picture of a letter 0x46 or 0x6E (letter_map_picture) as a texture.
std::shared_ptr<SDL_Texture> letter_picture_texture(SDL_Renderer* renderer, const CprArchive& archive,
	const GameMemory<0x10>& letter, int& width, int& height) {
	Image image;
	if (!letter_map_picture(archive, letter, image)) {
		return nullptr;
	}
	SDL_Texture* texture =
		SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, image.width, image.height);
	if (!texture) {
		return nullptr;
	}
	SDL_UpdateTexture(texture, nullptr, image.rgba.data(), image.width * 4);
	SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);  // The pieces not shown are transparent.
	width = image.width;
	height = image.height;
	return std::shared_ptr<SDL_Texture>(texture, SDL_DestroyTexture);
}

// Where everything goes, shared by the drawing and the touch screen.
struct MailLayout {
	static constexpr int kPadding = 8;
	static constexpr int kListWidth = 360;
	SDL_Rect panel{};
	std::array<SDL_Rect, 4> tabs{};
	SDL_Rect list{};
	int row_height = 0;
	int rows = 0;  // Rows in a page.
	SDL_Rect text{};  // The letter (the whole width in the chronicle).
	SDL_Rect delete_button{};
	SDL_Rect close_button{};

	MailLayout(int line_height, bool chronicle) {
		panel = {kPadding, kBarHeight + 4, kScreenWidth - 2 * kPadding, kScreenHeight - kBarHeight - 12};
		const int tab_height = line_height + 10;
		const int tab_width = (panel.w - 5 * kPadding) / 4;
		for (int k = 0; k < 4; ++k) {
			tabs[size_t(k)] = {panel.x + kPadding + k * (tab_width + kPadding), panel.y + kPadding, tab_width, tab_height};
		}
		const int top = panel.y + kPadding + tab_height + 6;
		const int button_height = line_height + 10;
		const int bottom = panel.y + panel.h - kPadding;
		const int buttons_y = bottom - button_height;
		row_height = 2 * line_height + 6;
		list = {panel.x + kPadding, top, kListWidth, buttons_y - 6 - top};
		rows = std::max(1, list.h / row_height);
		const int button_width = (kListWidth - kPadding) / 2;
		delete_button = {list.x, buttons_y, button_width, button_height};
		close_button = {list.x + button_width + kPadding, buttons_y, kListWidth - button_width - kPadding, button_height};
		const int text_x = chronicle ? panel.x + kPadding : list.x + kListWidth + 12;
		text = {text_x, top, panel.x + panel.w - kPadding - text_x, bottom - top};
		if (chronicle) {
			text.h = buttons_y - 6 - top;
			close_button.x = panel.x + panel.w - kPadding - close_button.w;
		}
	}

	int row_at(int x, int y) const {
		if (!inside(list, x, y)) {
			return -1;
		}
		const int row = (y - list.y) / row_height;
		return row < rows ? row : -1;
	}
	int tab_at(int x, int y) const {
		for (int k = 0; k < 4; ++k) {
			if (inside(tabs[size_t(k)], x, y)) {
				return k;
			}
		}
		return -1;
	}
};

// A paragraph of a letter (letter_paragraphs) in lines that fit in `width`
// pixels: at the spaces, or inside a word at one of its soft hyphens, with a
// '-', as the game's text box does; the soft hyphens taken out. An empty
// paragraph is an empty line.
std::vector<std::string> wrap_letter(const TextRenderer& text, const std::string& s, int width) {
	auto plain = [](std::string w) {
		w.erase(std::remove(w.begin(), w.end(), kLetterSoftHyphen), w.end());
		return w;
	};
	std::vector<std::string> lines;
	std::string line;
	bool empty_line = true;  // No word on `line` yet.
	size_t pos = 0;
	while (pos <= s.size()) {
		size_t next = s.find(' ', pos);
		if (next == std::string::npos) {
			next = s.size();
		}
		std::string word = s.substr(pos, next - pos);
		pos = next + 1;
		while (true) {
			const std::string joined = empty_line ? plain(word) : line + " " + plain(word);
			if (text.measure(joined) <= width) {
				line = joined;
				empty_line = false;
				break;
			}
			// The longest start of the word up to a soft hyphen that fits with its '-'.
			size_t cut = std::string::npos;
			for (size_t k = word.find(kLetterSoftHyphen); k != std::string::npos;
				 k = word.find(kLetterSoftHyphen, k + 1)) {
				const std::string head = plain(word.substr(0, k)) + "-";
				if (text.measure(empty_line ? head : line + " " + head) <= width) {
					cut = k;
				}
			}
			if (cut != std::string::npos) {
				lines.push_back((empty_line ? "" : line + " ") + plain(word.substr(0, cut)) + "-");
				word = word.substr(cut + 1);
			} else if (!empty_line) {
				lines.push_back(line);
			} else {
				line = plain(word);  // Too long for a line: as it is.
				empty_line = false;
				break;
			}
			line.clear();
			empty_line = true;
		}
	}
	lines.push_back(line);
	return lines;
}

// The tab's name, from the game. The Spanish exe repeats «Notificaciones
// oficiales» for the chronicle's tab (a bug of its text, fixed with
// fix_game_bugs()).
std::string mail_tab_name(int tab) {
	if (!g_texts) {
		static const char* const kSpanish[4] = {"Noticias públicas", "Escritos oficiales", "Cartas personales",
			"Información comercial"};
		static const char* const kEnglish[4] = {"Public news", "Official letters", "Personal letters", "Trade information"};
		return tr(kSpanish[tab], kEnglish[tab]);
	}
	if (tab == 0 && fix_game_bugs() && letter_tab_name(*g_texts, 0) == letter_tab_name(*g_texts, 1)) {
		return tr("Noticias públicas", "Public news");
	}
	return clean_game_text(letter_tab_name(*g_texts, uint8_t(tab)));
}

// Whether the operation `op` for the player's letter `entry` is still in the queue.
bool queued_letter_operation(const SaveGame& save, uint32_t op, uint16_t entry) {
	for (const std::vector<uint8_t>& q : save.queued_operations) {
		uint32_t f[3] = {};
		std::memcpy(f, q.data(), std::min(q.size(), sizeof(f)));
		if (f[0] == op && int32_t(f[1]) == save.player && f[2] == entry) {
			return true;
		}
	}
	return false;
}

OperationBytes letter_operation(uint32_t op, int32_t merchant, uint16_t entry) {
	OperationBytes bytes{};
	const uint32_t f[3] = {op, uint32_t(merchant), entry};
	std::memcpy(bytes.data(), f, sizeof(f));
	return bytes;
}

// The rows of a tab of letters: those whose deletion isn't queued.
std::vector<uint16_t> mail_rows(const SaveGame& save, int tab) {
	std::vector<uint16_t> rows = letter_rows(save, uint8_t(tab));
	rows.erase(std::remove_if(rows.begin(), rows.end(),
				   [&](uint16_t e) { return queued_letter_operation(save, 0x6F, e); }),
		rows.end());
	return rows;
}

void draw_mailbox(SDL_Renderer* renderer, TextRenderer& text, const SaveGame& save, const MailUi& ui,
	const std::vector<uint16_t>& rows) {
	const bool chronicle = ui.tab == 0;
	const MailLayout layout(text.line_height(), chronicle);
	draw_panel_box(renderer, layout.panel, 250);
	for (int k = 0; k < 4; ++k) {
		const SDL_Rect& r = layout.tabs[size_t(k)];
		SDL_SetRenderDrawColor(renderer, k == ui.tab ? 120 : 55, k == ui.tab ? 90 : 42, k == ui.tab ? 45 : 26, 230);
		SDL_RenderFillRect(renderer, &r);
		SDL_SetRenderDrawColor(renderer, 150, 120, 70, 255);
		SDL_RenderDrawRect(renderer, &r);
		const std::string name = mail_tab_name(k);
		text.draw(name, r.x + (r.w - text.measure(name)) / 2, r.y + 5, k == ui.tab ? kTextColor : kDimColor);
	}
	if (!chronicle) {
		if (rows.empty()) {
			text.draw(tr("No hay cartas.", "No letters."), layout.list.x + 4, layout.list.y + 4, kDimColor);
		}
		for (int i = 0; i < layout.rows && ui.first + i < int(rows.size()); ++i) {
			const int index = ui.first + i;
			const uint16_t entry = rows[size_t(index)];
			const int y = layout.list.y + i * layout.row_height;
			if (index == ui.row) {
				const SDL_Rect selected = {layout.list.x - 2, y, layout.list.w + 4, layout.row_height - 4};
				SDL_SetRenderDrawColor(renderer, 120, 90, 45, 160);
				SDL_RenderFillRect(renderer, &selected);
			}
			const LetterRow row = g_texts ? letter_row(save, *g_texts, entry) : LetterRow();
			const bool unread = (save.mail[entry].u8(1) & 0x80) && !queued_letter_operation(save, 0x6E, entry);
			// The unread ones in another colour, as the game (0x47D8E0): bright, the read ones dimmer.
			const SDL_Color title_color = unread ? SDL_Color{255, 214, 120, 255} : kDimColor;
			std::string title = clean_game_text(row.title);
			while (!title.empty() && text.measure(title) > layout.list.w - 8) {
				title.pop_back();
				while (!title.empty() && (uint8_t(title.back()) & 0xC0) == 0x80) {
					title.pop_back();  // Never half a UTF-8 character.
				}
			}
			text.draw(title, layout.list.x + 4, y + 2, title_color);
			const std::string detail = row.date + "  " + clean_game_text(row.town);
			text.draw(detail, layout.list.x + 12, y + 2 + text.line_height(), unread ? kTextColor : SDL_Color{150, 140, 120, 255});
			if (unread) {
				const SDL_Rect dot = {layout.list.x + layout.list.w - 10, y + 8, 6, 6};
				SDL_SetRenderDrawColor(renderer, 255, 214, 120, 255);
				SDL_RenderFillRect(renderer, &dot);
			}
		}
		if (int(rows.size()) > layout.rows) {
			const int pages = (int(rows.size()) + layout.rows - 1) / layout.rows;
			const std::string page = std::to_string(ui.first / layout.rows + 1) + "/" + std::to_string(pages);
			text.draw(page, layout.list.x + layout.list.w - text.measure(page), layout.delete_button.y - text.line_height() - 4,
				kDimColor);
		}
		draw_button(renderer, text, layout.delete_button, tr(BTN_SQUARE " Borrar", BTN_SQUARE " Delete"));
	}
	draw_button(renderer, text, layout.close_button, tr(BTN_CIRCLE " Cerrar", BTN_CIRCLE " Close"));
	// The letter (or the chronicle), clipped to its box and scrolled.
	SDL_Rect clip = layout.text;
	SDL_RenderSetClipRect(renderer, &clip);
	int y = layout.text.y + 2 - ui.scroll;
	for (const auto& [align, line] : ui.lines) {
		if (line.size() == 1 && line[0] == kPictureLine) {
			if (ui.picture) {
				const int x = align == LetterParagraph::Right ? layout.text.x + layout.text.w - 4 - ui.picture_width
					: align == LetterParagraph::Center ? layout.text.x + (layout.text.w - ui.picture_width) / 2
					: layout.text.x + 4;
				const SDL_Rect where = {x, y, ui.picture_width, ui.picture_height};
				SDL_RenderCopy(renderer, ui.picture.get(), nullptr, &where);
			}
		} else if (line.size() == 1 && line[0] == kOrnament) {
			// The ornament: a rule with a diamond in the middle.
			const int cx = layout.text.x + layout.text.w / 2;
			const int cy = y + text.line_height() / 2;
			SDL_SetRenderDrawColor(renderer, 150, 120, 70, 255);
			SDL_RenderDrawLine(renderer, cx - 90, cy, cx - 8, cy);
			SDL_RenderDrawLine(renderer, cx + 8, cy, cx + 90, cy);
			for (int k = 0; k <= 4; ++k) {
				SDL_RenderDrawLine(renderer, cx - 4 + k, cy - k, cx + 4 - k, cy - k);
				SDL_RenderDrawLine(renderer, cx - 4 + k, cy + k, cx + 4 - k, cy + k);
			}
		} else if (y + text.line_height() >= layout.text.y && y < layout.text.y + layout.text.h) {
			const int w = text.measure(line);
			const int x = align == LetterParagraph::Right ? layout.text.x + layout.text.w - 4 - w
				: align == LetterParagraph::Center ? layout.text.x + (layout.text.w - w) / 2
				: layout.text.x + 4;
			text.draw(line, x, y, kTextColor);
		}
		y += text.line_height();
	}
	SDL_RenderSetClipRect(renderer, nullptr);
	const int height = int(ui.lines.size()) * text.line_height() + 4;
	if (height > layout.text.h) {
		// Where in the text: a thin bar on the right.
		const int bar = std::max(20, layout.text.h * layout.text.h / height);
		const int at = layout.text.y + (layout.text.h - bar) * ui.scroll / std::max(1, height - layout.text.h);
		const SDL_Rect thumb = {layout.text.x + layout.text.w - 3, at, 3, bar};
		SDL_SetRenderDrawColor(renderer, 150, 120, 70, 255);
		SDL_RenderFillRect(renderer, &thumb);
	}
}

int stick_speed(Sint16 value) {
	if (std::abs(value) < kStickDeadZone) {
		return 0;
	}
	return value * kStickSpeed / 32767;
}

void show_loading(SDL_Renderer* renderer) {
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_RenderClear(renderer);
	SDL_RenderPresent(renderer);
}

// The menu (Start on the Vita, Escape on the PC), with the game's texts: go
// on, save and load the game (the slots of savegames.hpp), the options (the
// fixes to the game's bugs) and quitting. The game stops while it is open.
enum class MenuScreen { Closed, Main, Save, Load, Options };
// A question over the menu: overwrite a slot, delete it, quit.
enum class MenuAsk { None, Overwrite, Delete, Quit };

struct MenuUi {
	MenuScreen screen = MenuScreen::Closed;
	int row = 0;
	MenuAsk ask = MenuAsk::None;
	std::array<p3::app::SlotInfo, p3::app::kSaveSlots> slots{};

	bool open() const { return screen != MenuScreen::Closed; }
	bool slot_screen() const { return screen == MenuScreen::Save || screen == MenuScreen::Load; }
};

// The rows of the main screen, in order.
enum MainRow { kRowContinue, kRowMailbox, kRowSave, kRowLoad, kRowOptions, kRowQuit, kMainRows };
enum OptionRow { kRowFixGameBugs, kRowLanguage, kOptionRows };

int menu_rows(MenuScreen screen) {
	switch (screen) {
	case MenuScreen::Main:
		return kMainRows;
	case MenuScreen::Save:
	case MenuScreen::Load:
		return p3::app::kSaveSlots;
	case MenuScreen::Options:
		return kOptionRows;
	default:
		return 0;
	}
}

// Where everything goes: the panel, its rows, the buttons at its foot (for
// the touch screen: delete and back in the slots' screens, back in the
// options) and the question's box with its two buttons.
struct MenuLayout {
	static constexpr int kPadding = 10;
	SDL_Rect panel{};
	int header_y = 0;
	int rows_y = 0;
	int row_height = 0;
	int rows = 0;
	SDL_Rect delete_button{};
	SDL_Rect back_button{};
	SDL_Rect ask_box{};
	SDL_Rect ok_button{};
	SDL_Rect cancel_button{};

	MenuLayout(int line_height, MenuScreen screen) {
		rows = menu_rows(screen);
		row_height = screen == MenuScreen::Options ? 2 * line_height + 8 : line_height + 10;
		const bool buttons = screen != MenuScreen::Main;
		const int width = screen == MenuScreen::Main ? 360 : 600;
		const int button_height = line_height + 10;
		const int height = kPadding * 3 + line_height + rows * row_height + (buttons ? button_height + kPadding : 0);
		panel = {(kScreenWidth - width) / 2, (kScreenHeight - height) / 2, width, height};
		header_y = panel.y + kPadding;
		rows_y = header_y + line_height + kPadding;
		const int buttons_y = rows_y + rows * row_height + kPadding;
		const int button_width = (width - 3 * kPadding) / 2;
		back_button = {panel.x + panel.w - kPadding - button_width, buttons_y, button_width, button_height};
		delete_button = {panel.x + kPadding, buttons_y, button_width, button_height};
		const int ask_width = 560;
		const int ask_height = 4 * line_height + 4 * kPadding + button_height;
		ask_box = {(kScreenWidth - ask_width) / 2, (kScreenHeight - ask_height) / 2, ask_width, ask_height};
		const int ask_buttons_y = ask_box.y + ask_box.h - kPadding - button_height;
		const int ask_button_width = (ask_width - 3 * kPadding) / 2;
		ok_button = {ask_box.x + kPadding, ask_buttons_y, ask_button_width, button_height};
		cancel_button = {ask_box.x + 2 * kPadding + ask_button_width, ask_buttons_y, ask_button_width, button_height};
	}

	int row_at(int x, int y) const {
		if (x < panel.x || x >= panel.x + panel.w || y < rows_y) {
			return -1;
		}
		const int row = (y - rows_y) / row_height;
		return row < rows ? row : -1;
	}
};

// «13 de mayo de 1300 - Lübeck»: what a slot holds.
std::string slot_description(const p3::app::SlotInfo& slot, const std::vector<std::string>& town_names) {
	if (slot.damaged) {
		return game_text(GameText::LoadDamaged, tr("No se puede cargar la partida.", "This save game appears to be damaged"));
	}
	std::string text = game_date(slot.day, slot.month, slot.year);
	if (slot.town_number >= 0 && slot.town_number < int(town_names.size())) {
		text += " - " + town_names[size_t(slot.town_number)];
	}
	return text;
}

std::string slot_label(const MenuUi& menu, int slot, const std::vector<std::string>& town_names) {
	const p3::app::SlotInfo& info = menu.slots[size_t(slot)];
	return std::to_string(slot + 1) + ". " + (info.used ? slot_description(info, town_names) : tr("(vacía)", "(empty)"));
}

// The question's two lines, as the game has them.
std::array<std::string, 2> menu_question(const MenuUi& menu, const std::vector<std::string>& town_names) {
	switch (menu.ask) {
	case MenuAsk::Overwrite: {
		const std::string slot = slot_description(menu.slots[size_t(menu.row)], town_names);
		return {game_text(GameText::OverwriteAsk, tr("¿Seguro que deseas sobrescribir", "Do you really want to overwrite")),
			g_texts ? g_texts->format(GameText::OverwriteAskSavegame, {slot})
					: tr("la partida guardada '", "the savegame '") + slot + "'?"};
	}
	case MenuAsk::Delete:
		return {game_text(GameText::DeleteAsk, tr("¿Seguro que quieres", "Do you really want to")),
			game_text(GameText::DeleteAskSavegame, tr("borrar esta partida?", "delete this savegame?"))};
	case MenuAsk::Quit:
		return {game_text(GameText::QuitAsk, tr("¿Seguro que deseas", "Do you really want")),
			game_text(GameText::QuitAskGame, tr("abandonar la partida?", "to quit the game?"))};
	default:
		return {"", ""};
	}
}

void draw_menu(SDL_Renderer* renderer, TextRenderer& text, const MenuUi& menu, const p3::app::AppSettings& settings,
	const std::vector<std::string>& town_names) {
	const MenuLayout layout(text.line_height(), menu.screen);
	const SDL_Rect& panel = layout.panel;
	const int left = panel.x + MenuLayout::kPadding;
	draw_panel_box(renderer, panel);
	const std::string title = menu.screen == MenuScreen::Save ? game_text(GameText::MenuSaveGame, tr("Guardar partida", "Save game"))
		: menu.screen == MenuScreen::Load ? game_text(GameText::MenuLoadGame, tr("Cargar partida", "Load game"))
		: menu.screen == MenuScreen::Options ? game_text(GameText::MenuOptions, tr("Opciones", "Options"))
		: std::string(kTitle);
	text.draw(title, left, layout.header_y, kTextColor);
	const std::string hint = menu.screen == MenuScreen::Main ? tr(BTN_CIRCLE " cerrar", BTN_CIRCLE " close")
															 : tr(BTN_CIRCLE " atrás", BTN_CIRCLE " back");
	text.draw(hint, panel.x + panel.w - MenuLayout::kPadding - text.measure(hint), layout.header_y, kDimColor);

	for (int i = 0; i < layout.rows; ++i) {
		const int y = layout.rows_y + i * layout.row_height;
		if (i == menu.row) {
			const SDL_Rect selected = {panel.x + 4, y - 2, panel.w - 8, layout.row_height - 4};
			SDL_SetRenderDrawColor(renderer, 120, 90, 45, 160);
			SDL_RenderFillRect(renderer, &selected);
		}
		std::string label;
		std::string detail;
		switch (menu.screen) {
		case MenuScreen::Main: {
			static const GameText kTexts[kMainRows] = {GameText::ButtonContinue, GameText::Count, GameText::MenuSaveGame,
				GameText::MenuLoadGame, GameText::MenuOptions, GameText::QuitGame};
			static const char* const kFallbacks[kMainRows] = {"Continuar", "Buzón", "Guardar partida", "Cargar partida",
				"Opciones", "Salir del juego"};
			static const char* const kEnglishFallbacks[kMainRows] = {"Continue", "Mailbox", "Save game", "Load game", "Options",
				"Quit"};
			// The mailbox is the viewer's own entry (the game opens it from its panel).
			label = kTexts[i] == GameText::Count ? tr(kFallbacks[i], kEnglishFallbacks[i])
												 : game_text(kTexts[i], tr(kFallbacks[i], kEnglishFallbacks[i]));
			break;
		}
		case MenuScreen::Save:
		case MenuScreen::Load:
			label = slot_label(menu, i, town_names);
			break;
		case MenuScreen::Options:
			if (i == kRowFixGameBugs) {
				label = tr("Corregir los fallos del juego: ", "Fix the game's bugs: ") +
					(settings.fix_game_bugs ? game_text(GameText::SettingOn, tr("Sí", "on"))
											: game_text(GameText::SettingOff, tr("No", "off")));
				detail = settings.fix_game_bugs
					? tr("Con «No», el juego hace lo mismo que el original, fallos incluidos.",
						"Off, the game does as the original, bugs included.")
					: tr("Ahora se comporta como el original, fallos incluidos.", "Now it behaves like the original, bugs included.");
			} else {
				label = tr("Idioma: ", "Language: ") + (settings.spanish ? "Español" : "English");
				detail = tr("Los textos y los nombres del juego en ese idioma.", "The game's texts and names in that language.");
			}
			break;
		default:
			break;
		}
		const bool empty_slot = menu.slot_screen() && !menu.slots[size_t(i)].used;
		text.draw(label, left, y, empty_slot ? kDimColor : kTextColor);
		if (!detail.empty()) {
			text.draw(detail, left + 12, y + text.line_height(), kDimColor);
		}
	}
	if (menu.slot_screen()) {
		draw_button(renderer, text, layout.delete_button,
			tr(BTN_SQUARE " ", BTN_SQUARE " ") + game_text(GameText::DeleteSavegame, tr("Borrar partida", "Delete savegame")));
	}
	if (menu.screen != MenuScreen::Main) {
		draw_button(renderer, text, layout.back_button,
			tr(BTN_CIRCLE " ", BTN_CIRCLE " ") + game_text(GameText::ButtonBack, tr("Atrás", "Back")));
	}

	if (menu.ask != MenuAsk::None) {
		const std::array<std::string, 2> question = menu_question(menu, town_names);
		draw_panel_box(renderer, layout.ask_box, 255);
		int y = layout.ask_box.y + 2 * MenuLayout::kPadding;
		for (const std::string& line : question) {
			text.draw(line, layout.ask_box.x + (layout.ask_box.w - text.measure(line)) / 2, y, kTextColor);
			y += text.line_height();
		}
		draw_button(renderer, text, layout.ok_button, std::string(BTN_CROSS " ") + game_text(GameText::ButtonOk, "OK"));
		draw_button(renderer, text, layout.cancel_button,
			tr(BTN_CIRCLE " ", BTN_CIRCLE " ") + game_text(GameText::ButtonCancel, tr("Cancelar", "Cancel")));
	}
}

// A black screen with a line in the middle (loading a game).
void show_loading_text(SDL_Renderer* renderer, TextRenderer* text, const std::string& line) {
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_RenderClear(renderer);
	if (text) {
		text->draw(line, (kScreenWidth - text->measure(line)) / 2, kScreenHeight / 2, kTextColor);
	}
	SDL_RenderPresent(renderer);
}

// The command line (only on the PC: the Vita starts the viewer without one).
struct Options {
	const char* data_dir = nullptr;
	const char* cpr = nullptr;
	const char* steps = nullptr;
	const char* screenshot = nullptr;
	const char* debug_state = nullptr;
	const char* fix_game_bugs = nullptr;  // on|off: overrides the settings file
	const char* exe = nullptr;
	const char* save = nullptr;  // The save to start from (in the archive), instead of kStartSave.
};

void print_usage() {
	std::printf(
		"usage: p3vita [DATA_DIR] [--cpr FILE] [--steps STEPS] [--screenshot FILE.png]\n"
		"              [--debug-state STATES] [--exe Patrician3.exe] [--fix-game-bugs on|off]\n"
		"              [--save missav/N.pat]\n"
		"\n"
		"Shows the towns of Patrician III. DATA_DIR is the folder with p2arch0_eng.cpr\n"
		"(and p2arch0_spa.cpr for the Spanish version, optional); by default data/ and\n"
		"the current folder. --cpr gives the English archive itself. The interface\n"
		"texts come from the game's Patrician3.exe (English GOG 1.1 or Spanish), next\n"
		"to the archive or given with --exe; without it, the viewer's own. The\n"
		"language (an option: Spanish by default) chooses the Spanish archive mounted\n"
		"or not, and the exe of that language among Patrician3_spa.exe or\n"
		"Patrician3_eng.exe and Patrician3.exe.\n"
		"\n"
		"The menu (Start, or Escape on the PC) saves and loads the game in 8 slots\n"
		"(savegames/ in the data folder: slotN.pat and slotN.pst, as the game writes\n"
		"them) and has the options. --fix-game-bugs on|off overrides the option of the\n"
		"same name (the fixes to the original game's bugs; on by default), which is\n"
		"kept in p3vita-settings.txt in the data folder.\n"
		"\n"
		"With --steps or --screenshot it runs without a window: it plays the steps,\n"
		"saves the screen into FILE.png at the end and quits.\n"
		"\n"
		"%s"
		"\n"
		"%s"
		"\n"
		"Example: p3vita --cpr p2arch0_eng.cpr --steps \"Triangle,R,shot:sea.png,X,days:5\"\n"
		"         --screenshot sailing.png\n",
		p3::app::kStepsHelp, p3::app::kDebugStatesHelp);
}

// -1: go on; otherwise the exit code (0 after --help, 2 for a wrong command line).
int parse_options(int argc, char** argv, Options& options) {
	for (int i = 1; i < argc; ++i) {
		const std::string arg = argv[i];
		const char** value = arg == "--cpr" ? &options.cpr
			: arg == "--steps" ? &options.steps
			: arg == "--screenshot" ? &options.screenshot
			: arg == "--debug-state" ? &options.debug_state
			: arg == "--fix-game-bugs" ? &options.fix_game_bugs
			: arg == "--exe" ? &options.exe
			: arg == "--save" ? &options.save
			: nullptr;
		if (arg == "--help" || arg == "-h") {
			print_usage();
			return 0;
		}
		if (value) {
			if (i + 1 >= argc) {
				std::fprintf(stderr, "%s needs a value (see --help)\n", argv[i]);
				return 2;
			}
			*value = argv[++i];
			if (value == &options.fix_game_bugs && std::string(*value) != "on" && std::string(*value) != "off") {
				std::fprintf(stderr, "--fix-game-bugs takes on or off\n");
				return 2;
			}
		} else if (arg.compare(0, 1, "-") == 0 || options.data_dir) {
			std::fprintf(stderr, "unknown argument: %s (see --help)\n", argv[i]);
			return 2;
		} else {
			options.data_dir = argv[i];
		}
	}
	return -1;
}

}  // namespace

int main(int argc, char** argv) {
	Options options;
	if (const int code = parse_options(argc, argv, options); code >= 0) {
		return code;
	}
	// Without a window: the steps of the command line and a screenshot at the end.
	std::unique_ptr<InputScript> script;
	if (options.steps || options.screenshot) {
		std::string error;
		std::string steps = options.steps ? options.steps : "";
		if (options.screenshot) {
			steps += std::string(",shot:") + options.screenshot;
		}
		script = InputScript::parse(steps, error);
		if (!script) {
			std::fprintf(stderr, "--steps: %s\n", error.c_str());
			return 2;
		}
		g_script = script.get();
		// The dummy video driver, unless SDL_VIDEODRIVER already chooses one.
		// The environment variable works with every SDL 2; the hint
		// SDL_HINT_VIDEODRIVER only exists from SDL 2.0.22 (Ubuntu 22.04 has
		// 2.0.20).
		SDL_setenv("SDL_VIDEODRIVER", "dummy", 0);
	}
#ifndef __vita__
	SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "1");  // On the PC the mouse is the touch screen.
#endif
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
		show_error(nullptr, SDL_GetError());
		return 1;
	}
	SDL_Window* window = nullptr;
	SDL_Surface* screen = nullptr;  // Without a window the renderer draws into it.
	SDL_Renderer* renderer = nullptr;
	if (script) {
		screen = SDL_CreateRGBSurfaceWithFormat(0, kScreenWidth, kScreenHeight, 32, SDL_PIXELFORMAT_ARGB8888);
		renderer = screen ? SDL_CreateSoftwareRenderer(screen) : nullptr;
	} else {
		window = SDL_CreateWindow(kTitle, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, kScreenWidth,
			kScreenHeight, 0);
		renderer =
			window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : nullptr;
		if (window && !renderer) {
			renderer = SDL_CreateRenderer(window, -1, 0);  // Any (for example with SDL_VIDEODRIVER=offscreen).
		}
	}
	if (!renderer) {
		show_error(window, SDL_GetError());
		SDL_Quit();
		return 1;
	}
	show_loading(renderer);

	// The settings: the file, then the command line; scripted runs don't save them.
	const std::string settings_file = p3::app::settings_path(options.data_dir);
	const std::string data_dir = p3::app::data_folder(options.data_dir);  // The viewer's own files.
	p3::app::AppSettings settings = p3::app::load_settings(settings_file);
	if (options.fix_game_bugs) {
		settings.fix_game_bugs = std::string(options.fix_game_bugs) == "on";
	}
	p3::data::set_fix_game_bugs(settings.fix_game_bugs);
	g_spanish = settings.spanish;

	std::string tried;
	bool spanish_mounted = false;
	auto archive = open_archive(options.data_dir, options.cpr, tried, settings.spanish, spanish_mounted);
	const std::vector<int> template_ids = archive ? list_town_ids(*archive) : std::vector<int>{};
	if (template_ids.empty()) {
		const std::string message = archive
			? tr("El archivo CPR no contiene mapas de ciudad.", "The CPR archive has no town maps.")
			: tr("No se encuentran los datos del juego. Copia p2arch0_eng.cpr (y p2arch0_spa.cpr para la "
				 "versión española) en:",
				  "The game's data can't be found. Copy p2arch0_eng.cpr (and p2arch0_spa.cpr for the Spanish "
				  "version) to:") + tried;
		show_error(window, message);
		SDL_Quit();
		return 1;
	}
	bool screenshot_failed = false;

	auto view = std::make_unique<TownView>(renderer, *archive);
	std::unique_ptr<SeaMapView> sea;  // Se carga la primera vez que se abre.
	std::vector<std::string> town_names = load_town_names(*archive);
	std::unique_ptr<TextRenderer> text;
	std::unique_ptr<TextRenderer> small_text;
	if (auto font = FontAtlas::load(*archive, kUiFont, kUiFontHeight)) {
		text = std::make_unique<TextRenderer>(renderer, std::move(font));
	}
	if (auto font = FontAtlas::load(*archive, kUiFont, kSmallFontHeight)) {
		small_text = std::make_unique<TextRenderer>(renderer, std::move(font));
	}
	MarketUi market;
	HanseUi hanse;  // The Hanse panel (Square in a town).
	PlaceUi place;  // The town's places (the tavern, the shipyard) and the ship's window.
	SaveGame save;
	bool has_save = load_savegame(*archive, options.save ? options.save : kStartSave, save) && !save.towns.empty();
	if (options.debug_state) {
		std::string error;
		if (!has_save || !p3::app::apply_debug_states(save, options.debug_state, error)) {
			show_error(window, "--debug-state: " + (has_save ? error : std::string("no save")));
			SDL_Quit();
			return 2;
		}
	}
	// To tell the player the result of each election of the Alderman.
	int last_alderman = alderman(save);
	uint32_t last_election = alderman_election_tick(save);
	std::vector<int> regions = load_town_regions(*archive);
	NameLists names = load_name_lists(*archive);
	// The game's interface texts, from the executable of the language (--exe,
	// or in the data folder: see load_game_texts).
	bool texts_matching = false;
	std::unique_ptr<GameTexts> game_texts =
		load_game_texts(options.exe, options.cpr, options.data_dir, settings.spanish, texts_matching, data_dir);
#ifndef __vita__
	if (!game_texts && options.exe) {
		std::fprintf(stderr, "--exe: can't read %s; the viewer's own texts\n", options.exe);
	}
#endif
	HanseContext hanse_context{town_names, names, regions, game_texts.get()};
	g_texts = game_texts.get();
	EndWindow end_window;  // When the player is elected Alderman.
	MessageWindow messages;  // The game's messages to the player (a battle...).
	Selection selection;
	if (has_save) {
		for (size_t i = 0; i < save.towns.size(); ++i) {
			if (save.towns[i].town_id() == kLuebeck) {
				selection.saved_index = i;
			}
		}
	} else {
		selection.mode = Mode::Designer;
	}
	// Navegación: sin los datos de navdata/ los barcos no se mueven.
	std::unique_ptr<NavData> nav = has_save ? NavData::load(*archive) : nullptr;
	std::unique_ptr<Sailing> sailing;
	if (nav) {
		sailing = std::make_unique<Sailing>(save, *nav, load_sea_map_towns(*archive));
		sailing->assign_loaded_routes();
		sailing->set_texts(game_texts.get());  // The battles' messages in the installation's language.
	}
	size_t fleet_index = 0;  // Barco del jugador al que se dan las órdenes en el mapa del mar.
	int game_speed = kNormalSpeed;
	Uint32 clock = app_ticks();  // Hasta dónde se ha convertido el tiempo real en ticks.
	// The notices under the top bar, each for a few seconds: the viewer's own
	// (an order, a ship that arrives) and the game's lines (a new letter); the
	// last kNotices, the newest at the bottom.
	constexpr size_t kNotices = 4;
	std::deque<std::pair<std::string, Uint32>> notices;
	auto notify = [&](const std::string& message) {
		if (!notices.empty() && notices.back().first == message) {
			notices.pop_back();  // The same again: only once, for longer.
		}
		notices.emplace_back(message, app_ticks() + kMessageTime);
		while (notices.size() > kNotices) {
			notices.pop_front();
		}
	};
	MailUi mail;  // The mailbox.
	// Triangle in a town before the sea map is loaded: the town stays, with a
	// notice, while it loads (from the frame after the notice is on screen).
	bool sea_wanted = false;
	bool sea_notice_shown = false;
	std::string sea_notice;  // Its notice, as shown.
	auto fleet_ship = [&]() -> int {
		const std::vector<int> fleet = player_fleet(save);
		return fleet.empty() ? -1 : fleet[fleet_index % fleet.size()];
	};
	// X en el mapa del mar: el barco elegido zarpa hacia la ciudad elegida (operación 0x00).
	auto order_sail = [&]() {
		const int index = fleet_ship();
		if (!sailing || index < 0) {
			notify(tr("No tienes ningún barco.", "You have no ship."));
			return;
		}
		const SavedShip& ship = save.ships[size_t(index)];
		const int town = int(selection.saved_index);
		const std::string there = town_name(save, town_names, town);
		const Sailing::SailCheck check = ship.status() < 4 ? sailing->sail_check(ship) : Sailing::SailCheck::Ok;
		if ((ship.status() == kShipDocked || ship.status() == kShipWaiting) && ship.town() == town) {
			notify(ship.name() + tr(" ya está en ", " is already in ") + there + ".");
		} else if (check == Sailing::SailCheck::FewSailors) {
			notify(game_text(GameText::SailNoCrew, tr("Faltan marineros para zarpar.", "Not enough sailors to set sail.")));
		} else if (check == Sailing::SailCheck::Tired) {
			notify(game_text(GameText::SailMutiny, tr("La tripulación necesita descansar.", "The crew needs a rest.")));
		} else if (check == Sailing::SailCheck::Overloaded) {
			notify(game_text(GameText::SailOverloaded,
				tr("Lleva más carga de la que cabe en la bodega.", "The ship is overloaded.")));
		} else if (check == Sailing::SailCheck::Damaged) {
			notify(game_text(GameText::SailDamaged, tr("El casco está demasiado dañado para zarpar sin capitán.",
				"The hull is too damaged to sail without a captain.")));
		} else if (check == Sailing::SailCheck::PortClosed) {
			notify(tr("El puerto está bloqueado.", "The port is blockaded."));
		} else if (check != Sailing::SailCheck::Ok) {
			notify(ship.name() + tr(" no puede zarpar ahora.", " can't set sail now."));
		} else {
			const std::string verb = ship.status() == kShipSailing ? tr(" pone rumbo a ", " heads for ")
				: ship.status() == kShipLeaving && ship.town() == town ? tr(" vuelve a ", " goes back to ")
				: tr(" zarpa hacia ", " sets sail for ");
			if (!sailing->set_sail(index, town)) {
				notify(ship.name() + tr(" no puede zarpar ahora.", " can't set sail now."));
			} else {
				notify(ship.name() + verb + there + ".");
			}
		}
	};

	SDL_GameController* controller = nullptr;
	bool reload = true;
	bool keep_camera = false;
	LiveTown live;  // The saved town on screen (Mode::Saved).
	bool running = true;
	MenuUi menu;  // Start: the menu (saving and loading, the options, quitting).
	const std::string saves_dir = p3::app::savegames_dir(options.data_dir);  // The player's saved games.
	// Saves the game in play in the slot (as the game: it expires the old
	// letters and updates the settings first).
	auto save_game = [&](int slot) {
		std::string error;
		if (!p3::app::save_slot(saves_dir, slot, save, error)) {
#ifndef __vita__
			std::fprintf(stderr, "%s\n", error.c_str());
#endif
			notify(tr("No se puede guardar la partida.", "The game can't be saved."));
			return false;
		}
		notify(tr("Partida guardada.", "Game saved."));
		return true;
	};
	// Replaces the game in play with the slot's: a new Sailing for it, and
	// what the viewer keeps of the old one forgotten; the town on screen is
	// the player's home town. On a failure the game in play stays.
	auto load_game = [&](int slot) {
		show_loading_text(renderer, text.get(), game_text(GameText::LoadingGame, tr("Iniciando partida...", "Loading game...")));
		SaveGame loaded;
		std::string error;
		if (!p3::app::load_slot(*archive, saves_dir, slot, loaded, error) || loaded.towns.empty()) {
#ifndef __vita__
			std::fprintf(stderr, "%s\n", error.c_str());
#endif
			notify(game_text(GameText::LoadDamaged, tr("No se puede cargar la partida.", "This save game appears to be damaged")) +
				" " + game_text(GameText::LoadDamagedCannot, tr("El fichero está dañado.", "and can't be loaded.")));
			return false;
		}
		sailing.reset();  // It refers to the game it was made for.
		save = std::move(loaded);
		has_save = true;
		if (!nav) {
			nav = NavData::load(*archive);
		}
		if (nav) {
			sailing = std::make_unique<Sailing>(save, *nav, load_sea_map_towns(*archive));
			sailing->assign_loaded_routes();
			sailing->set_texts(game_texts.get());
			use_sailing(*sailing);
		}
		last_alderman = alderman(save);
		last_election = alderman_election_tick(save);
		market = MarketUi();
		hanse = HanseUi();
		place = PlaceUi();
		messages = MessageWindow();
		end_window = EndWindow();
		fleet_index = 0;
		live = LiveTown();
		selection.mode = Mode::Saved;
		const int home = save.player >= 0 ? save.merchants[size_t(save.player)].hometown() : 0;
		selection.saved_index = size_t(home) < save.towns.size() ? size_t(home) : 0;
		reload = true;
		keep_camera = false;
		clock = app_ticks();
		notices.clear();
		mail = MailUi();
		sea_wanted = false;
		sea_notice_shown = false;
		return true;
	};
	// Options: another language. The archive (with or without the Spanish
	// one), the fonts, the names and the game's texts again, as at the start;
	// the game in play stays as it is. Not if that language's files are
	// missing (its archive, or its exe when the game's texts are in use).
	int language_change = -1;  // 1 Spanish, 0 English, at the start of the next frame.
	auto switch_language = [&](bool spanish) {
		std::string tried_again;
		bool mounted = false;
		std::unique_ptr<CprArchive> next = open_archive(options.data_dir, options.cpr, tried_again, spanish, mounted);
		if (!next || mounted != spanish) {
			notify(spanish ? "Para el español falta p2arch0_spa.cpr en la carpeta de datos."
						   : "Para el inglés falta p2arch0_eng.cpr en la carpeta de datos.");
			return false;
		}
		bool matching = false;
		std::unique_ptr<GameTexts> texts =
			load_game_texts(options.exe, options.cpr, options.data_dir, spanish, matching, data_dir);
		if (game_texts && !matching) {
			notify(spanish ? "Para el español falta su Patrician3.exe (o Patrician3_spa.exe) en la carpeta de datos."
						   : "Para el inglés falta su Patrician3.exe, como Patrician3_eng.exe, en la carpeta de datos.");
			return false;
		}
		show_loading(renderer);
		view.reset();  // It reads from the old archive.
		sea.reset();
		archive = std::move(next);
		view = std::make_unique<TownView>(renderer, *archive);
		town_names = load_town_names(*archive);
		regions = load_town_regions(*archive);
		names = load_name_lists(*archive);
		if (auto font = FontAtlas::load(*archive, kUiFont, kUiFontHeight)) {
			text = std::make_unique<TextRenderer>(renderer, std::move(font));
		}
		if (auto font = FontAtlas::load(*archive, kUiFont, kSmallFontHeight)) {
			small_text = std::make_unique<TextRenderer>(renderer, std::move(font));
		}
		game_texts = std::move(texts);
		g_texts = game_texts.get();
		g_spanish = spanish;
		hanse_context.texts = game_texts.get();
		if (sailing) {
			sailing->set_texts(game_texts.get());
		}
		live = LiveTown();
		reload = true;
		keep_camera = true;
		mail.stale = true;  // The letter again, in the new language.
		return true;
	};
	// The mailbox opens (as 0x47C520 fills its list: the player's old letters
	// go first) on the tab it had, at its first row.
	auto open_mailbox = [&]() {
		if (!has_save || save.player < 0 || !small_text) {
			return;
		}
		expire_letters(save, save.player);
		mail.open = true;
		mail.row = 0;
		mail.selected = 0xFFFF;
		mail.first = 0;
		mail.stale = true;
		market.open = false;
		hanse.open = false;
		place.open = Place::None;
	};
	auto mail_tab = [&](int tab) {
		expire_letters(save, save.player);
		mail.tab = (tab + 4) % 4;
		mail.row = 0;
		mail.selected = 0xFFFF;
		mail.first = 0;
		mail.stale = true;
	};
	auto open_menu = [&](MenuScreen screen, int row) {
		menu.screen = screen;
		menu.row = row;
		menu.ask = MenuAsk::None;
		if (menu.slot_screen()) {
			for (int i = 0; i < p3::app::kSaveSlots; ++i) {
				menu.slots[size_t(i)] = p3::app::read_slot_info(saves_dir, i);
			}
		}
	};
	// Circle: from a screen to the main one (on its row), from the main one out.
	auto menu_back = [&]() {
		if (menu.screen == MenuScreen::Main) {
			menu.screen = MenuScreen::Closed;
			return;
		}
		open_menu(MenuScreen::Main, menu.screen == MenuScreen::Save ? kRowSave
				: menu.screen == MenuScreen::Load                    ? kRowLoad
																	 : kRowOptions);
	};
	// X: the selected row, or the question's OK.
	auto menu_act = [&]() {
		const int slot = std::clamp(menu.row, 0, p3::app::kSaveSlots - 1);
		const bool used = menu.slots[size_t(slot)].used;
		const MenuAsk ask = menu.ask;
		menu.ask = MenuAsk::None;
		if (ask == MenuAsk::Overwrite) {
			if (save_game(slot)) {
				menu.screen = MenuScreen::Closed;
			}
			return;
		}
		if (ask == MenuAsk::Delete) {
			p3::app::delete_slot(saves_dir, slot);
			open_menu(menu.screen, slot);  // The list again.
			return;
		}
		if (ask == MenuAsk::Quit) {
			running = false;
			return;
		}
		switch (menu.screen) {
		case MenuScreen::Main:
			if (menu.row == kRowContinue) {
				menu.screen = MenuScreen::Closed;
			} else if (menu.row == kRowMailbox) {
				if (has_save && save.player >= 0) {
					menu.screen = MenuScreen::Closed;
					open_mailbox();
				}
			} else if (menu.row == kRowSave) {
				if (has_save && save.player >= 0) {
					open_menu(MenuScreen::Save, 0);
				}
			} else if (menu.row == kRowLoad) {
				open_menu(MenuScreen::Load, 0);
			} else if (menu.row == kRowOptions) {
				open_menu(MenuScreen::Options, 0);
			} else {
				menu.ask = MenuAsk::Quit;
			}
			break;
		case MenuScreen::Save:
			if (used) {
				menu.ask = MenuAsk::Overwrite;
			} else if (save_game(slot)) {
				menu.screen = MenuScreen::Closed;
			}
			break;
		case MenuScreen::Load:
			if (used && load_game(slot)) {
				menu.screen = MenuScreen::Closed;
			}
			break;
		case MenuScreen::Options:
			if (menu.row == kRowFixGameBugs) {
				settings.fix_game_bugs = !settings.fix_game_bugs;
				p3::data::set_fix_game_bugs(settings.fix_game_bugs);
				if (!script && !p3::app::save_settings(settings_file, settings)) {
					std::fprintf(stderr, "can't save %s\n", settings_file.c_str());
				}
			} else {
				language_change = settings.spanish ? 0 : 1;
			}
			break;
		default:
			break;
		}
	};
	// Square in the slots' screens: delete the selected one (after asking).
	auto menu_delete = [&]() {
		if (menu.slot_screen() && menu.slots[size_t(std::clamp(menu.row, 0, p3::app::kSaveSlots - 1))].used) {
			menu.ask = MenuAsk::Delete;
		}
	};
	// With the menu open, every button is its own.
	auto menu_button = [&](Uint8 button) {
		if (menu.ask != MenuAsk::None) {
			if (button == SDL_CONTROLLER_BUTTON_A) {
				menu_act();
			} else if (button == SDL_CONTROLLER_BUTTON_B || button == SDL_CONTROLLER_BUTTON_START) {
				menu.ask = MenuAsk::None;
			}
			return;
		}
		const int rows = menu_rows(menu.screen);
		switch (button) {
		case SDL_CONTROLLER_BUTTON_DPAD_UP:
		case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
			menu.row = (menu.row + rows + (button == SDL_CONTROLLER_BUTTON_DPAD_UP ? -1 : 1)) % rows;
			break;
		case SDL_CONTROLLER_BUTTON_A:
			menu_act();
			break;
		case SDL_CONTROLLER_BUTTON_X:  // Square.
			menu_delete();
			break;
		case SDL_CONTROLLER_BUTTON_B:
			menu_back();
			break;
		case SDL_CONTROLLER_BUTTON_START:
			menu.screen = MenuScreen::Closed;
			break;
		default:
			break;
		}
	};
	int camera_x = 0;
	int camera_y = 0;
	// Toque en curso: dónde empezó y cuánto se ha movido el dedo.
	SDL_Point touch_start = {0, 0};
	int touch_moved = 0;
	bool touch_on_panel = false;
	while (running) {
		if (script) {
			script->start_frame();
			if (script->finished()) {
				break;
			}
		}
		if (language_change >= 0) {
			const bool spanish = language_change == 1;
			language_change = -1;
			if (switch_language(spanish)) {
				settings.spanish = spanish;
				if (!script && !p3::app::save_settings(settings_file, settings)) {
					std::fprintf(stderr, "can't save %s\n", settings_file.c_str());
				}
			}
		}
		// El mercado solo existe en las ciudades de la partida, con el jugador cargado.
		const bool trading = market.open && selection.mode == Mode::Saved && save.player >= 0 && small_text;
		const int town_index = int(selection.saved_index);
		// The Hanse panel, in the save's towns like the market (never both open).
		const bool in_hanse = hanse.open && !trading && selection.mode == Mode::Saved && save.player >= 0 && small_text;
		const HanseView hanse_now = in_hanse ? hanse_view(save, town_index, hanse.page) : HanseView();
		// The window of a place or of the ship, in the save's towns (never with
		// the market or the Hanse panel).
		const bool in_place = place.open != Place::None && !trading && !in_hanse && selection.mode == Mode::Saved &&
			save.player >= 0 && small_text;
		const PlaceView place_now = in_place ? place_view(save, town_index, place) : PlaceView();
		const bool in_menu = menu.open() && small_text;
		// The mailbox, over the town or the sea map.
		const bool in_mail = mail.open && has_save && save.player >= 0 && small_text &&
			(selection.mode == Mode::Saved || selection.mode == Mode::Sea);
		// The building in the middle of the screen, the buttons' cursor (as the
		// game's mouse pointer): X goes into it.
		const bool town_free = selection.mode == Mode::Saved && has_save && save.player >= 0 && !trading && !in_hanse &&
			!in_place && !in_mail && !in_menu;
		const int hover = town_free ? view->place_at(camera_x + kScreenWidth / 2, camera_y + kScreenHeight / 2) : -1;
		// Goes into a place: the Market Hall is the market; the tavern rolls its
		// rand() as the game's window does each time it opens (0x5CC479).
		auto open_place = [&](Place p, int let_type) {
			if (p == Place::MarketHall || p == Place::Office) {
				// The Market Hall: a ship and the town; the office: the office and
				// the town (0x59AE40 without a ship chosen).
				market.mode = p == Place::Office && player_office(save, town_index) >= 0 ? 2 : 1;
				market.ware = std::min(market.ware, market_rows(market) - 1);
				market.open = true;
				hanse.open = false;
				place.open = Place::None;
				return;
			}
			place = PlaceUi();
			place.open = p;
			place.let_type = let_type;
			if (p == Place::Tavern) {
				place.roll = tavern_roll(save);
			}
			market.open = false;
			hanse.open = false;
		};
		// With a window open the pad, L, R, X, Circle and Select are its own.
		auto place_button = [&](Uint8 button) {
			const int rows = int(place_now.rows.size());
			auto add_amount = [&](int step) {
				place.amount = std::clamp(place.amount + step, 0, std::max(0, place_now.max_amount));
			};
			switch (button) {
			case SDL_CONTROLLER_BUTTON_DPAD_UP:
			case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
				if (rows > 0) {
					place.row = (place.row + rows + (button == SDL_CONTROLLER_BUTTON_DPAD_UP ? -1 : 1)) % rows;
				}
				place.amount = 0;
				if (place.open == Place::Tavern) {
					place.ship = 0;  // The other row's list.
				}
				return true;
			case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
			case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
				add_amount(button == SDL_CONTROLLER_BUTTON_DPAD_LEFT ? -1 : 1);
				return true;
			case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
			case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
				add_amount(button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER ? -10 : 10);
				return true;
			case SDL_CONTROLLER_BUTTON_A:
				place_act(save, sailing.get(), town_index, place, place_now);
				return true;
			case SDL_CONTROLLER_BUTTON_B:
				place.open = Place::None;
				return true;
			case SDL_CONTROLLER_BUTTON_BACK:  // Select: the next ship of the list.
				place.ship++;
				place.amount = 0;
				return true;
			case SDL_CONTROLLER_BUTTON_Y:
			case SDL_CONTROLLER_BUTTON_START:
				return false;  // The sea map and the menu, as always.
			default:
				return true;
			}
		};
		const MailLayout mail_layout(small_text ? small_text->line_height() : 20, mail.tab == 0);
		const std::vector<uint16_t> mail_list = in_mail && mail.tab != 0 ? mail_rows(save, mail.tab) : std::vector<uint16_t>();
		auto mail_select = [&](int row) {
			if (mail_list.empty()) {
				return;
			}
			mail.row = std::clamp(row, 0, int(mail_list.size()) - 1);
			mail.selected = mail_list[size_t(mail.row)];
		};
		auto mail_scroll = [&](int pixels) { mail.scroll += pixels; };  // Kept in range when drawn.
		// Square: delete the letter shown (operation 0x6F, as the game's window).
		auto mail_delete = [&]() {
			if (mail.tab != 0 && mail.row < int(mail_list.size())) {
				queue_operation(save, letter_operation(0x6F, save.player, mail_list[size_t(mail.row)]));
				mail.selected = 0xFFFF;  // The same row: the next letter.
			}
		};
		// With the mailbox open every button but Start is its own.
		auto mail_button = [&](Uint8 button) {
			const int line = small_text ? small_text->line_height() : 20;
			switch (button) {
			case SDL_CONTROLLER_BUTTON_DPAD_UP:
			case SDL_CONTROLLER_BUTTON_DPAD_DOWN: {
				const int step = button == SDL_CONTROLLER_BUTTON_DPAD_UP ? -1 : 1;
				if (mail.tab == 0) {
					mail_scroll(step * line);
				} else {
					mail_select(mail.row + step);
				}
				return true;
			}
			case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
			case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: {
				const int step = button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER ? -1 : 1;
				if (mail.tab == 0) {
					mail_scroll(step * (mail_layout.text.h - line));
				} else {
					mail.first = std::max(0, mail.first + step * mail_layout.rows);
					mail_select(mail.row + step * mail_layout.rows);
				}
				return true;
			}
			case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
			case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
				mail_tab(mail.tab + (button == SDL_CONTROLLER_BUTTON_DPAD_LEFT ? -1 : 1));
				return true;
			case SDL_CONTROLLER_BUTTON_BACK:  // Select: the next tab.
				mail_tab(mail.tab + 1);
				return true;
			case SDL_CONTROLLER_BUTTON_X:  // Square.
				mail_delete();
				return true;
			case SDL_CONTROLLER_BUTTON_B:  // Circle.
				mail.open = false;
				return true;
			case SDL_CONTROLLER_BUTTON_START:
				return false;
			default:
				return true;
			}
		};
		auto hanse_select = [&](int step) {
			const int rows = hanse_now.rows(hanse.page);
			hanse.row = rows > 0 ? (hanse.row + rows + step) % rows : 0;
		};
		auto change_amount = [&](int step) {
			const int count = int(kRoadAmounts.size());
			hanse.amount = (hanse.amount + count + step) % count;
		};
		auto open_hanse = [&](bool open) {
			hanse.open = open;
			hanse.row = 0;
			hanse.briefing = false;
			if (open) {
				market.open = false;
			}
		};
		// With the panel open the pad, X, Circle and Square are its own; the rest work as usual.
		auto hanse_button = [&](Uint8 button) {
			switch (button) {
			case SDL_CONTROLLER_BUTTON_DPAD_UP:
			case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
				hanse_select(button == SDL_CONTROLLER_BUTTON_DPAD_UP ? -1 : 1);
				return true;
			case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
			case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
				change_amount(button == SDL_CONTROLLER_BUTTON_DPAD_LEFT ? -1 : 1);
				return true;
			case SDL_CONTROLLER_BUTTON_A:
				hanse_act(save, town_index, hanse, hanse_now, hanse_context);
				return true;
			case SDL_CONTROLLER_BUTTON_B:
			case SDL_CONTROLLER_BUTTON_X:
				open_hanse(false);
				return true;
			case SDL_CONTROLLER_BUTTON_BACK:  // Select: the tasks or the election.
				hanse.page ^= 1;
				hanse.row = 0;
				return true;
			default:
				return false;
			}
		};
		auto select_ware = [&](int step) {
			const int rows = market_rows(market);
			market.ware = (market.ware + rows + step) % rows;
		};
		// L and R in the market: the next mode, where the player has an office.
		auto market_mode_step = [&](int step) {
			const std::vector<int> modes = market_modes(save, town_index);
			if (modes.size() < 2) {
				return false;
			}
			const auto at = std::find(modes.begin(), modes.end(), market.mode);
			const int k = at == modes.end() ? 0 : int(at - modes.begin());
			market.mode = modes[size_t((k + int(modes.size()) + step) % int(modes.size()))];
			market.ware = std::min(market.ware, market_rows(market) - 1);
			return true;
		};
		auto change_step = [&](int step) { market.step = (market.step + kTradeStepCount + step) % kTradeStepCount; };
		// Triangle asked for the sea map: it loads now, some rows a frame
		// (SeaMapView::step), the town and the notice on screen; then to the sea.
		if (sea_wanted && sea_notice_shown) {
			if (!sea) {
				sea = std::make_unique<SeaMapView>(renderer, *archive);
			} else if (!sea->ready()) {
				sea->step(kSeaMapRowsPerFrame);
			}
		}
		sea_notice_shown = sea_wanted;
		if (sea_wanted && sea && sea->ready()) {
			sea_wanted = false;
			sea_notice_shown = false;
			notices.erase(std::remove_if(notices.begin(), notices.end(),
							  [&](const std::pair<std::string, Uint32>& n) { return n.first == sea_notice; }),
				notices.end());
			if (selection.mode == Mode::Saved) {
				selection.mode = Mode::Sea;
				reload = true;
			}
		}
		if (reload && selection.mode == Mode::Sea) {
			if (!sea) {
				sea = std::make_unique<SeaMapView>(renderer, *archive);
			}
			if (!sea->ready()) {
				show_loading(renderer);
				sea->finish();
			}
			if (!sea->ok()) {
				show_error(window, tr("No se encuentra el mapa del mar.", "The sea map can't be found."));
				break;
			}
			const SDL_Point focus = sea->town_center(save, selection.saved_index);
			camera_x = focus.x - kScreenWidth / 2;
			camera_y = focus.y - kScreenHeight / 2;
			reload = false;
		}
		if (reload) {
			// A town of the game takes a few milliseconds (its graphics are
			// decoded once): the last screen stays meanwhile, no black one; the
			// same town unchanged is already drawn.
			if (selection.mode != Mode::Saved) {
				show_loading(renderer);
			}
			TownMap town;
			std::string error;
			bool loaded = false;
			bool drawn = false;
			if (selection.mode == Mode::Saved) {
				drawn = live.drawn(save, selection.saved_index);
				loaded = drawn || live.load(*archive, save, selection.saved_index, regions, error);
				if (loaded && !drawn) {
					live.draw(save, view->tiles(), town);
				}
			} else {
				loaded = load_template(*archive, template_ids, selection, town, error);
				live.forget_drawn();
			}
			if (!loaded) {
				show_error(window, error);
				break;
			}
			if (!drawn) {
				view->show(town);
			}
			if (!keep_camera) {
				const SDL_Point focus = focus_point(*view, save, selection);
				camera_x = focus.x - kScreenWidth / 2;
				camera_y = focus.y - kScreenHeight / 2;
			}
			reload = false;
			keep_camera = false;
		}

		// Pasa el tiempo: los ticks que caben en el tiempo real transcurrido.
		int ticks_run = 0;
		if (sailing && in_menu) {
			clock = app_ticks();  // The game stops while the menu is open.
		} else if (sailing) {
			const GameSpeed& speed = kGameSpeeds[game_speed];
			const Uint32 now = app_ticks();
			const Uint32 elapsed = now - clock;
			clock = now - elapsed % speed.ms_per_tick;
			const int ticks =
				std::min(int(elapsed / speed.ms_per_tick), speed.max_ticks) + (script ? script->extra_ticks() : 0);
			ticks_run = ticks;
			for (int i = 0; i < ticks; ++i) {
				advance_game_tick(save, *sailing);
				for (const std::string& message : sailing->take_player_messages()) {
					messages.push(message);
				}
				for (const ShipEvent& event : sailing->events()) {
					const SavedShip& ship = save.ships[size_t(event.ship)];
					if (ship.owner() != save.player) {
						continue;
					}
					// With the game's texts the game's own ticker lines tell these
					// (docked, at its destination, sunk: take_ticker below); without
					// them, the viewer's.
					if (event.kind == ShipEvent::Arrived) {
						if (!g_texts) {
							notify(ship.name() + tr(" ha llegado a ", " has reached ") + town_name(save, town_names, event.town) + ".");
						}
						// En el mapa del mar queda elegida la ciudad: Triángulo entra en ella.
						if (selection.mode == Mode::Sea && event.ship == fleet_ship()) {
							selection.saved_index = size_t(event.town);
						}
					} else if (!g_texts) {
						notify(event.kind == ShipEvent::Stopped ? ship.name() + tr(" se ha detenido en el mar.", " has stopped at sea.")
															   : ship.name() + tr(" se ha hundido.", " has sunk."));
					}
				}
			}
			// The new letters (0x4D7B10): «Carta personal: Botadura»... as the
			// game's line, for the classes the player's settings ([+0x26]) want.
			for (const uint16_t entry : take_new_letters(save)) {
				if (g_texts && letter_notice_wanted(save, entry)) {
					notify(clean_game_text(letter_notice(save, *g_texts, entry)));
				}
			}
			// The game's ticker lines (0x548CA0 → 0x5483D0): «Tu barco X se ha
			// hundido.»..., with the game's texts; an empty one isn't shown.
			for (const SaveGame::TickerLine& line : take_ticker(save)) {
				if (!g_texts) {
					continue;
				}
				if (const std::optional<std::string> ticker = compose_ticker(save, *g_texts, line)) {
					const std::string shown = clean_game_text(*ticker);
					if (!shown.empty()) {
						notify(shown);
					}
				}
			}
			// The election of the Alderman (event 0x20): its result. The game also
			// shows a window and, for the player, the video `eldermann` (pending).
			const uint32_t election = alderman_election_tick(save);
			if (alderman(save) != last_alderman || (election != last_election && last_election != 0 &&
				(election & ~0xFFu) != (last_election & ~0xFFu))) {
				const bool same = alderman(save) == last_alderman;
				last_alderman = alderman(save);
				if (last_alderman == save.player) {
					notify(same ? tr("Sigues siendo Gobernador de la Hansa.", "You are still the Alderman of the Hanseatic League.")
								: tr("¡Has sido elegido Gobernador de la Hansa!", "You have been elected Alderman of the Hanseatic League!"));
					if (!same) {
						// The end window, with the game's text (0x5299F0 gives title and
						// rank too; here only the name) and the player's votes.
						int votes = 0;
						for (const ElectionCandidate& c : election_candidates(save, election_event(save))) {
							if (c.merchant && c.index == save.player) {
								votes = c.votes;
							}
						}
						const std::string home = town_name(save, town_names, save.merchants[size_t(save.player)].hometown());
						end_window.text = game_texts
							? game_texts->format(GameText::Elected, {"", "", merchant_name(save, names, save.player), "",
								  home, std::to_string(votes)})
							: g_spanish ? "¡Has sido elegido Gobernador de la Hansa por " + std::to_string(votes) + " votos!"
										: "You have been elected Alderman of the Hanseatic League by " + std::to_string(votes) +
									" votes!";
						end_window.open = true;
					}
				} else {
					notify((same ? tr("Elección de Gobernador: sigue ", "Election of Alderman: still ")
								 : tr("Nuevo Gobernador de la Hansa: ", "New Alderman of the Hanseatic League: ")) +
						alderman_name(save, names, town_names) + ".");
				}
			}
			last_election = election;
		}

		// The living town: when the simulation changes the buildings of the town
		// on screen, it is drawn again, with the camera where it was.
		if (ticks_run > 0 && selection.mode == Mode::Saved && live.changed(save)) {
			TownMap town;
			live.draw(save, view->tiles(), town);
			view->show(town);
		}

		int town_step = 0;
		bool next_mode = false;  // Triángulo: entre la ciudad y el mapa del mar.
		bool next_debug_view = false;  // Select: vistas de las plantillas de ciudad.
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			if ((event.type == SDL_FINGERDOWN || event.type == SDL_FINGERMOTION || event.type == SDL_FINGERUP) &&
				rear_touch(event.tfinger.touchId)) {
				continue;
			}
			switch (event.type) {
			case SDL_QUIT:
				running = false;
				break;
			case SDL_CONTROLLERDEVICEADDED:
				if (!controller) {
					controller = SDL_GameControllerOpen(event.cdevice.which);
				}
				break;
			case SDL_CONTROLLERBUTTONDOWN:
				if (in_menu) {
					menu_button(event.cbutton.button);
					break;
				}
				if (end_window.open) {
					// The end window takes X and Circle.
					if (event.cbutton.button == SDL_CONTROLLER_BUTTON_A || event.cbutton.button == SDL_CONTROLLER_BUTTON_B) {
						end_window.open = false;
						break;
					}
				}
				if (messages.open) {
					// So does the window of the game's messages: the next one, or closed.
					if (event.cbutton.button == SDL_CONTROLLER_BUTTON_A || event.cbutton.button == SDL_CONTROLLER_BUTTON_B) {
						messages.next();
						break;
					}
				}
				if (in_mail && mail_button(event.cbutton.button)) {
					break;
				}
				if (in_hanse && hanse_button(event.cbutton.button)) {
					break;
				}
				if (in_place && place_button(event.cbutton.button)) {
					break;
				}
				if (trading &&
					(event.cbutton.button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER ||
						event.cbutton.button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) &&
					market_mode_step(event.cbutton.button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER ? -1 : 1)) {
					break;
				}
				switch (event.cbutton.button) {
				case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
					town_step = -1;
					break;
				case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
					town_step = 1;
					break;
				case SDL_CONTROLLER_BUTTON_Y:  // Triángulo en la Vita.
					next_mode = true;
					break;
				case SDL_CONTROLLER_BUTTON_A:
					// X en la Vita: en el mercado, compra; en la ciudad, entra en el
					// edificio del centro de la pantalla (o el mercado); en el mar, zarpar.
					if (trading) {
						trade(save, sailing.get(), town_index, market, true);
					} else if (selection.mode == Mode::Saved) {
						const PlaceInfo* p = hover >= 0 ? place_of(uint32_t(hover)) : nullptr;
						open_place(p ? p->place : Place::MarketHall, hover);
					} else if (selection.mode == Mode::Sea) {
						order_sail();
					}
					break;
				case SDL_CONTROLLER_BUTTON_X:
					// Cuadrado: vende; en el mar, el siguiente barco del jugador; en la ciudad, the Hanse panel.
					if (trading) {
						trade(save, sailing.get(), town_index, market, false);
					} else if (selection.mode == Mode::Sea) {
						fleet_index++;
					} else if (selection.mode == Mode::Saved) {
						open_hanse(true);
					}
					break;
				case SDL_CONTROLLER_BUTTON_B:  // Circle: closes the market; otherwise, in a town or at sea, the mailbox.
					if (market.open) {
						market.open = false;
					} else if (selection.mode == Mode::Saved || selection.mode == Mode::Sea) {
						open_mailbox();
					}
					break;
				case SDL_CONTROLLER_BUTTON_BACK:
					// Select: siguiente barco del puerto, velocidad del juego en el mar,
					// en la ciudad la ventana del barco del jugador (si tiene uno en
					// su puerto) o vista de plantilla.
					if (trading) {
						market.ship++;
					} else if (selection.mode == Mode::Sea) {
						game_speed = (game_speed + 1) % kGameSpeedCount;
					} else if (selection.mode == Mode::Saved && has_save && save.player >= 0 &&
						!player_ships_in(save, town_index).empty()) {
						open_place(Place::Ship, -1);
					} else {
						next_debug_view = true;
					}
					break;
				case SDL_CONTROLLER_BUTTON_DPAD_UP:
				case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
					if (trading) {
						select_ware(event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP ? -1 : 1);
					}
					break;
				case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
				case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
					if (trading) {
						change_step(event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_LEFT ? -1 : 1);
					}
					break;
				case SDL_CONTROLLER_BUTTON_START:  // The menu.
					open_menu(MenuScreen::Main, kRowContinue);
					break;
				}
				break;
			case SDL_KEYDOWN:
				if (in_menu) {
					const SDL_Keycode key = event.key.keysym.sym;
					menu_button(key == SDLK_UP ? SDL_CONTROLLER_BUTTON_DPAD_UP
						: key == SDLK_DOWN ? SDL_CONTROLLER_BUTTON_DPAD_DOWN
						: key == SDLK_RETURN || key == SDLK_c ? SDL_CONTROLLER_BUTTON_A
						: key == SDLK_DELETE || key == SDLK_d ? SDL_CONTROLLER_BUTTON_X
						: key == SDLK_ESCAPE ? SDL_CONTROLLER_BUTTON_B
						: SDL_CONTROLLER_BUTTON_INVALID);
					break;
				}
				if (end_window.open && (event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_ESCAPE)) {
					end_window.open = false;
					break;
				}
				if (messages.open && (event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_ESCAPE)) {
					messages.next();
					break;
				}
				if (in_hanse) {
					// Keyboard: the arrows, C or Enter to act, H or Escape to close.
					const SDL_Keycode key = event.key.keysym.sym;
					const Uint8 button = key == SDLK_UP ? SDL_CONTROLLER_BUTTON_DPAD_UP
						: key == SDLK_DOWN ? SDL_CONTROLLER_BUTTON_DPAD_DOWN
						: key == SDLK_LEFT ? SDL_CONTROLLER_BUTTON_DPAD_LEFT
						: key == SDLK_RIGHT ? SDL_CONTROLLER_BUTTON_DPAD_RIGHT
						: key == SDLK_c || key == SDLK_RETURN ? SDL_CONTROLLER_BUTTON_A
						: key == SDLK_h || key == SDLK_ESCAPE ? SDL_CONTROLLER_BUTTON_B
						: key == SDLK_TAB ? SDL_CONTROLLER_BUTTON_BACK
						: SDL_CONTROLLER_BUTTON_INVALID;
					if (button != Uint8(SDL_CONTROLLER_BUTTON_INVALID) && hanse_button(button)) {
						break;
					}
				}
				if (in_place) {
					// Keyboard: the arrows, Page Up/Down ±10, C or Enter to act, Tab the next ship, Escape to close.
					const SDL_Keycode key = event.key.keysym.sym;
					const Uint8 button = key == SDLK_UP ? SDL_CONTROLLER_BUTTON_DPAD_UP
						: key == SDLK_DOWN ? SDL_CONTROLLER_BUTTON_DPAD_DOWN
						: key == SDLK_LEFT ? SDL_CONTROLLER_BUTTON_DPAD_LEFT
						: key == SDLK_RIGHT ? SDL_CONTROLLER_BUTTON_DPAD_RIGHT
						: key == SDLK_PAGEUP ? SDL_CONTROLLER_BUTTON_LEFTSHOULDER
						: key == SDLK_PAGEDOWN ? SDL_CONTROLLER_BUTTON_RIGHTSHOULDER
						: key == SDLK_c || key == SDLK_RETURN ? SDL_CONTROLLER_BUTTON_A
						: key == SDLK_ESCAPE ? SDL_CONTROLLER_BUTTON_B
						: key == SDLK_TAB ? SDL_CONTROLLER_BUTTON_BACK
						: SDL_CONTROLLER_BUTTON_INVALID;
					if (button != Uint8(SDL_CONTROLLER_BUTTON_INVALID) && place_button(button)) {
						break;
					}
				}
				if (in_mail) {
					// Keyboard: the arrows, Page Up/Down, Tab the next tab, Delete or D, B or Escape to close.
					const SDL_Keycode key = event.key.keysym.sym;
					const Uint8 button = key == SDLK_UP ? SDL_CONTROLLER_BUTTON_DPAD_UP
						: key == SDLK_DOWN ? SDL_CONTROLLER_BUTTON_DPAD_DOWN
						: key == SDLK_LEFT ? SDL_CONTROLLER_BUTTON_DPAD_LEFT
						: key == SDLK_RIGHT ? SDL_CONTROLLER_BUTTON_DPAD_RIGHT
						: key == SDLK_PAGEUP ? SDL_CONTROLLER_BUTTON_LEFTSHOULDER
						: key == SDLK_PAGEDOWN ? SDL_CONTROLLER_BUTTON_RIGHTSHOULDER
						: key == SDLK_TAB ? SDL_CONTROLLER_BUTTON_BACK
						: key == SDLK_DELETE || key == SDLK_d ? SDL_CONTROLLER_BUTTON_X
						: key == SDLK_b || key == SDLK_ESCAPE ? SDL_CONTROLLER_BUTTON_B
						: SDL_CONTROLLER_BUTTON_INVALID;
					if (button != Uint8(SDL_CONTROLLER_BUTTON_INVALID)) {
						mail_button(button);
					}
					break;
				}
				switch (event.key.keysym.sym) {
				case SDLK_h:
					if (selection.mode == Mode::Saved) {
						open_hanse(true);
					}
					break;
				case SDLK_b:  // The mailbox.
					if (selection.mode == Mode::Saved || selection.mode == Mode::Sea) {
						open_mailbox();
					}
					break;
				case SDLK_PAGEUP:
				case SDLK_PAGEDOWN:
					if (!(trading && market_mode_step(event.key.keysym.sym == SDLK_PAGEUP ? -1 : 1))) {
						town_step = event.key.keysym.sym == SDLK_PAGEUP ? -1 : 1;
					}
					break;
				case SDLK_e:
					next_mode = true;
					break;
				case SDLK_f:
					next_debug_view = true;
					break;
				case SDLK_m:
					market.open = !market.open;
					hanse.open = false;
					break;
				case SDLK_c:
				case SDLK_v:
					if (trading) {
						trade(save, sailing.get(), town_index, market, event.key.keysym.sym == SDLK_c);
					}
					break;
				case SDLK_TAB:
					market.ship++;
					fleet_index++;
					break;
				case SDLK_RETURN:
					if (selection.mode == Mode::Sea) {
						order_sail();
					} else if (selection.mode == Mode::Saved && !trading && has_save) {
						// As X: the building in the middle of the screen, or the market.
						const PlaceInfo* p = hover >= 0 ? place_of(uint32_t(hover)) : nullptr;
						open_place(p ? p->place : Place::MarketHall, hover);
					}
					break;
				case SDLK_PLUS:  // Como en el juego: + más rápido, - más lento.
				case SDLK_KP_PLUS:
					game_speed = std::min(game_speed + 1, kGameSpeedCount - 1);
					break;
				case SDLK_MINUS:
				case SDLK_KP_MINUS:
					game_speed = std::max(game_speed - 1, 0);
					break;
				case SDLK_UP:
				case SDLK_DOWN:
					if (trading) {
						select_ware(event.key.keysym.sym == SDLK_UP ? -1 : 1);
					}
					break;
				case SDLK_LEFT:
				case SDLK_RIGHT:
					if (trading) {
						change_step(event.key.keysym.sym == SDLK_LEFT ? -1 : 1);
					}
					break;
				case SDLK_ESCAPE:
					if (market.open) {
						market.open = false;
					} else {
						open_menu(MenuScreen::Main, kRowContinue);
					}
					break;
				}
				break;
			case SDL_FINGERDOWN:
				touch_start = {int(event.tfinger.x * kScreenWidth), int(event.tfinger.y * kScreenHeight)};
				touch_moved = 0;
				touch_on_panel =
					(trading && inside(MarketLayout(small_text->line_height()).panel, touch_start.x, touch_start.y)) ||
					(in_hanse && inside(HanseLayout(small_text->line_height()).panel, touch_start.x, touch_start.y)) ||
					(in_place && inside(PlaceLayout(small_text->line_height()).panel, touch_start.x, touch_start.y)) ||
					(in_menu && (inside(MenuLayout(small_text->line_height(), menu.screen).panel, touch_start.x, touch_start.y) ||
						menu.ask != MenuAsk::None)) ||
					(in_mail && inside(mail_layout.panel, touch_start.x, touch_start.y));
				break;
			case SDL_FINGERMOTION:
				touch_moved += std::abs(int(event.tfinger.dx * kScreenWidth)) +
					std::abs(int(event.tfinger.dy * kScreenHeight));
				// Arrastrar desplaza la ciudad con el dedo, salvo sobre el mercado.
				if (!touch_on_panel) {
					camera_x -= int(event.tfinger.dx * kScreenWidth);
					camera_y -= int(event.tfinger.dy * kScreenHeight);
				} else if (in_mail && !in_menu && inside(mail_layout.text, touch_start.x, touch_start.y)) {
					mail_scroll(-int(event.tfinger.dy * kScreenHeight));  // Dragging the letter moves it.
				}
				break;
			case SDL_FINGERUP:
				if (in_mail && !in_menu && touch_on_panel && touch_moved < kTapDistance) {
					const int x = touch_start.x;
					const int y = touch_start.y;
					const int tab = mail_layout.tab_at(x, y);
					const int row = mail.tab != 0 ? mail_layout.row_at(x, y) : -1;
					if (tab >= 0) {
						mail_tab(tab);
					} else if (inside(mail_layout.close_button, x, y)) {
						mail.open = false;
					} else if (mail.tab != 0 && inside(mail_layout.delete_button, x, y)) {
						mail_delete();
					} else if (row >= 0 && mail.first + row < int(mail_list.size())) {
						mail_select(mail.first + row);
					}
					touch_on_panel = false;
					break;
				}
				// En el mapa del mar, tocar una ciudad la elige.
				if (selection.mode == Mode::Sea && sea && touch_moved < kTapDistance && !(in_mail && touch_on_panel)) {
					const int town = sea->town_at(save, camera_x + touch_start.x, camera_y + touch_start.y);
					if (town >= 0) {
						selection.saved_index = size_t(town);
					}
				}
				if (in_menu && touch_moved < kTapDistance) {
					const MenuLayout layout(small_text->line_height(), menu.screen);
					const int x = touch_start.x;
					const int y = touch_start.y;
					const int row = layout.row_at(x, y);
					if (menu.ask != MenuAsk::None) {
						if (inside(layout.ok_button, x, y)) {
							menu_act();
						} else if (inside(layout.cancel_button, x, y)) {
							menu.ask = MenuAsk::None;
						}
					} else if (menu.screen != MenuScreen::Main && inside(layout.back_button, x, y)) {
						menu_back();
					} else if (menu.slot_screen() && inside(layout.delete_button, x, y)) {
						menu_delete();
					} else if (row >= 0) {
						// In the slots' screens the first tap chooses and the second acts.
						const bool act = !menu.slot_screen() || row == menu.row;
						menu.row = row;
						if (act) {
							menu_act();
						}
					} else if (!inside(layout.panel, x, y)) {
						menu_back();  // A tap outside goes back.
					}
				} else if (touch_on_panel && in_hanse && touch_moved < kTapDistance) {
					const HanseLayout layout(small_text->line_height());
					const int row = layout.row_at(touch_start.x, touch_start.y, hanse_now.rows(hanse.page));
					if (hanse_now.road_here && inside(layout.amount_button, touch_start.x, touch_start.y)) {
						change_amount(1);
					} else if (inside(layout.act_button, touch_start.x, touch_start.y)) {
						hanse_act(save, town_index, hanse, hanse_now, hanse_context);
					} else if (row >= 0) {
						hanse.row = row;
					}
				} else if (touch_on_panel && in_place && touch_moved < kTapDistance) {
					const PlaceLayout layout(small_text->line_height());
					const int x = touch_start.x;
					const int y = touch_start.y;
					const int first = std::max(0, place.row - PlaceLayout::kMaxRows + 1);
					const int row = layout.row_at(x, y, int(place_now.rows.size()) - first);
					if (place_now.max_amount >= 0 && inside(layout.minus_button, x, y)) {
						place.amount = std::max(0, place.amount - 1);
					} else if (place_now.max_amount >= 0 && inside(layout.plus_button, x, y)) {
						place.amount = std::min(place_now.max_amount, place.amount + 1);
					} else if (inside(layout.act_button, x, y)) {
						place_act(save, sailing.get(), town_index, place, place_now);
					} else if (row >= 0 && first + row != place.row) {
						place.row = first + row;
						place.amount = 0;
						if (place.open == Place::Tavern) {
							place.ship = 0;
						}
					}
				} else if (!touch_on_panel && touch_moved < kTapDistance && selection.mode == Mode::Saved && has_save &&
					save.player >= 0 && small_text && !in_menu && !in_mail && !messages.open && !end_window.open) {
					// A tap in the town: the ship's card, or a building to go into, as in the game.
					const std::string card = ship_card_label(save, town_index);
					const int building = view->place_at(camera_x + touch_start.x, camera_y + touch_start.y);
					if (!card.empty() && inside(ship_card_rect(*small_text, card), touch_start.x, touch_start.y)) {
						open_place(Place::Ship, -1);
					} else if (const PlaceInfo* p = building >= 0 ? place_of(uint32_t(building)) : nullptr) {
						open_place(p->place, building);
					}
				} else if (touch_on_panel && trading && touch_moved < kTapDistance) {
					const MarketLayout layout(small_text->line_height());
					const int row = layout.row_at(touch_start.x, touch_start.y);
					if (inside(layout.step_button, touch_start.x, touch_start.y)) {
						change_step(1);
					} else if (inside(layout.buy_button, touch_start.x, touch_start.y)) {
						trade(save, sailing.get(), town_index, market, true);
					} else if (inside(layout.sell_button, touch_start.x, touch_start.y)) {
						trade(save, sailing.get(), town_index, market, false);
					} else if (row >= 0 && row < market_rows(market)) {
						market.ware = row;
					}
				}
				touch_on_panel = false;
				break;
			}
		}
		if (town_step != 0) {
			if (selection.mode == Mode::Saved || selection.mode == Mode::Sea) {
				const size_t count = save.towns.size();
				selection.saved_index = (selection.saved_index + count + size_t(town_step)) % count;
			} else {
				const size_t count = template_ids.size();
				selection.template_index = (selection.template_index + count + size_t(town_step)) % count;
			}
			market.ship = 0;
			place.open = Place::None;
			reload = true;
			continue;
		}
		if (next_mode && has_save) {
			place.open = Place::None;
			if (selection.mode == Mode::Saved && (!sea || !sea->ready())) {
				// The sea map isn't loaded yet: the town stays while it loads.
				if (!sea_wanted) {
					sea_notice = tr("Cargando el mapa del mar...", "Loading the sea map...");
					notify(sea_notice);
				}
				sea_wanted = true;
				continue;
			}
			// Entre la ciudad elegida y el mapa del mar; desde una plantilla, a la ciudad.
			selection.mode = selection.mode == Mode::Saved ? Mode::Sea : Mode::Saved;
			reload = true;
			continue;
		}
		if (next_debug_view) {
			// Al pasar de la partida a las plantillas se sigue en el mismo mapa,
			// con la cámara donde estaba.
			if (selection.mode == Mode::Saved || selection.mode == Mode::Sea) {
				const int map = save.towns[selection.saved_index].template_map();
				auto it = std::find(template_ids.begin(), template_ids.end(), map);
				selection.template_index = it == template_ids.end() ? 0 : size_t(it - template_ids.begin());
				keep_camera = selection.mode == Mode::Saved;
				selection.mode = Mode::Designer;
			} else if (selection.mode == Mode::Designer) {
				selection.mode = Mode::Terrain;
				keep_camera = true;
			} else {
				selection.mode = has_save ? Mode::Saved : Mode::Designer;
				keep_camera = true;
			}
			reload = true;
			continue;
		}

		// Con el mercado abierto, la cruceta y las flechas son suyas; el stick sigue moviendo la cámara.
		// The pad is the controller's or the script's.
		auto pad = [&](SDL_GameControllerButton button) {
			return int((controller && SDL_GameControllerGetButton(controller, button)) ||
				(script && script->held(button)));
		};
		if (controller && in_mail) {
			// In the mailbox both sticks move the letter.
			mail_scroll(stick_speed(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY)) +
				stick_speed(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTY)));
		} else if (controller) {
			camera_x += stick_speed(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX));
			camera_y += stick_speed(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY));
		}
		if (!trading && !in_hanse && !in_mail && !in_place) {
			camera_x += kPadSpeed * (pad(SDL_CONTROLLER_BUTTON_DPAD_RIGHT) - pad(SDL_CONTROLLER_BUTTON_DPAD_LEFT));
			camera_y += kPadSpeed * (pad(SDL_CONTROLLER_BUTTON_DPAD_DOWN) - pad(SDL_CONTROLLER_BUTTON_DPAD_UP));
		}
		if (!trading && !in_hanse && !in_mail && !in_place) {
			const Uint8* keys = SDL_GetKeyboardState(nullptr);
			camera_x += kPadSpeed * (keys[SDL_SCANCODE_RIGHT] - keys[SDL_SCANCODE_LEFT]);
			camera_y += kPadSpeed * (keys[SDL_SCANCODE_DOWN] - keys[SDL_SCANCODE_UP]);
		}
		const bool at_sea = selection.mode == Mode::Sea && sea;
		int look_x = 0;
		int look_y = 0;
		if (script && !at_sea && script->look_at(look_x, look_y)) {
			camera_x = cell_screen_x(look_x, look_y) - kScreenWidth / 2;
			camera_y = cell_screen_y(look_y) - kScreenHeight / 2;
		}
		const int view_width = at_sea ? sea->width() : view->width();
		const int view_height = at_sea ? sea->height() : view->height();
		camera_x = std::clamp(camera_x, 0, std::max(0, view_width - kScreenWidth));
		camera_y = std::clamp(camera_y, 0, std::max(0, view_height - kScreenHeight));

		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
		SDL_RenderClear(renderer);
		const int course_ship = fleet_ship();
		if (at_sea) {
			sea->draw(save, small_text.get(), town_names, camera_x, camera_y, selection.saved_index,
				course_ship >= 0 ? &save.ships[size_t(course_ship)] : nullptr);
		} else {
			// The building under the cursor now (after this frame's camera): glowing, with its name.
			const bool free_now = selection.mode == Mode::Saved && has_save && save.player >= 0 && !market.open &&
				!hanse.open && place.open == Place::None && !mail.open && !menu.open();
			const int pointed = free_now ? view->place_at(camera_x + kScreenWidth / 2, camera_y + kScreenHeight / 2) : -1;
			view->draw(camera_x, camera_y, pointed);
			if (free_now && small_text) {
				// The cursor: a small cross in the middle of the screen, light on dark.
				const int cx = kScreenWidth / 2;
				const int cy = kScreenHeight / 2;
				SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
				SDL_SetRenderDrawColor(renderer, 30, 20, 10, 255);
				const SDL_Rect dark[] = {{cx - 8, cy - 2, 17, 5}, {cx - 2, cy - 8, 5, 17}};
				SDL_RenderFillRects(renderer, dark, 2);
				SDL_SetRenderDrawColor(renderer, 255, 236, 190, 255);
				const SDL_Rect light[] = {{cx - 7, cy - 1, 15, 3}, {cx - 1, cy - 7, 3, 15}};
				SDL_RenderFillRects(renderer, light, 2);
				SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
			}
			if (pointed >= 0 && small_text) {
				const SDL_Rect b = view->bounds(pointed);
				const std::string name = place_name(pointed);
				const int padding = 6;
				const int w = small_text->measure(name) + 2 * padding;
				const int h = small_text->line_height() + padding;
				const SDL_Rect box = {std::clamp(b.x + b.w / 2 - camera_x - w / 2, 4, kScreenWidth - w - 4),
					std::clamp(b.y - camera_y - h - 2, kBarHeight + 2, kScreenHeight - h - 4), w, h};
				draw_panel_box(renderer, box);
				small_text->draw(name, box.x + padding, box.y + padding / 2, kTextColor);
			}
			const std::string card = selection.mode == Mode::Saved && has_save && save.player >= 0 && !in_mail &&
					!in_menu && small_text
				? ship_card_label(save, town_index)
				: "";
			if (!card.empty()) {
				const SDL_Rect box = ship_card_rect(*small_text, card);
				draw_panel_box(renderer, box);
				small_text->draw(card, box.x + 8, box.y + 4, kTextColor);
			}
		}
		if (text) {
			std::vector<std::string> right_texts;
			if (in_mail) {
				right_texts = mail.tab == 0
					? std::vector<std::string>{tr("L/R: página   Select: pestaña   " BTN_CIRCLE " cerrar",
												   "L/R: page   Select: tab   " BTN_CIRCLE " close"),
						  tr("Select: pestaña   " BTN_CIRCLE " cerrar", "Select: tab   " BTN_CIRCLE " close")}
					: std::vector<std::string>{tr("L/R: página   Select: pestaña   " BTN_SQUARE " borrar   " BTN_CIRCLE " cerrar",
												   "L/R: page   Select: tab   " BTN_SQUARE " delete   " BTN_CIRCLE " close"),
						  tr("Select: pestaña   " BTN_SQUARE " borrar   " BTN_CIRCLE " cerrar",
							  "Select: tab   " BTN_SQUARE " delete   " BTN_CIRCLE " close"),
						  tr(BTN_SQUARE " borrar   " BTN_CIRCLE " cerrar", BTN_SQUARE " delete   " BTN_CIRCLE " close")};
			} else if (trading || in_hanse || place.open != Place::None) {
				const std::string money = format_money(save.merchants[size_t(save.player)].money()) + tr(" monedas", " coins");
				right_texts = {merchant_name(save, names, save.player) + ": " + money, money};
			} else if (at_sea) {
				right_texts = {tr("L/R: ciudad   " BTN_CROSS " zarpar   " BTN_TRIANGLE " entrar   " BTN_CIRCLE " buzón",
								   "L/R: town   " BTN_CROSS " set sail   " BTN_TRIANGLE " enter   " BTN_CIRCLE " mailbox"),
					tr(BTN_CROSS " zarpar   " BTN_TRIANGLE " entrar   " BTN_CIRCLE " buzón",
						BTN_CROSS " set sail   " BTN_TRIANGLE " enter   " BTN_CIRCLE " mailbox"),
					tr("L/R: ciudad   " BTN_CROSS " zarpar   " BTN_TRIANGLE " entrar", "L/R: town   " BTN_CROSS " set sail   " BTN_TRIANGLE " enter"),
					tr(BTN_CROSS " zarpar   " BTN_TRIANGLE " entrar", BTN_CROSS " set sail   " BTN_TRIANGLE " enter")};
			} else if (selection.mode == Mode::Saved) {
				// X: the building under the cursor, or the market.
				const int pointed = view->place_at(camera_x + kScreenWidth / 2, camera_y + kScreenHeight / 2);
				const std::string x = BTN_CROSS " " + (pointed >= 0 ? place_name(pointed) : tr("mercado", "market"));
				const std::string ship = !ship_card_label(save, town_index).empty() ? tr("   Select: barco", "   Select: ship") : "";
				const std::string l = tr("L/R: ciudad   ", "L/R: town   ");
				const std::string map = tr(BTN_TRIANGLE " mapa   ", BTN_TRIANGLE " map   ");
				const std::string hanse_mail = tr("   " BTN_SQUARE " Hansa   " BTN_CIRCLE " buzón", "   " BTN_SQUARE " Hanse   " BTN_CIRCLE " mailbox");
				const std::string hanse_only = tr("   " BTN_SQUARE " Hansa", "   " BTN_SQUARE " Hanse");
				right_texts = {l + map + x + hanse_mail + ship, map + x + hanse_mail + ship, map + x + hanse_mail,
					map + x + hanse_only, x + hanse_only, map + x, x};
			} else {
				right_texts = {tr("L/R: ciudad   " BTN_TRIANGLE " mapa   " BTN_CROSS " mercado", "L/R: town   " BTN_TRIANGLE " map   " BTN_CROSS " market"),
					tr(BTN_TRIANGLE " mapa   " BTN_CROSS " mercado", BTN_TRIANGLE " map   " BTN_CROSS " market")};
			}
			draw_bar(renderer, *text, 0, bar_title(save, town_names, template_ids, selection), right_texts);
		}
		// En el mapa del mar, abajo: el barco al que se dan las órdenes y la velocidad del juego.
		if (at_sea && small_text) {
			std::string ship_text = tr("No tienes ningún barco.", "You have no ship.");
			if (course_ship >= 0) {
				const SavedShip& ship = save.ships[size_t(course_ship)];
				ship_text = ship.name() + " (" + ship_type_name(ship.type()) + "): " + ship_activity(save, town_names, ship);
			}
			const std::string speed = g_texts ? g_texts->get(GameText::GameSpeed) + ": " + speed_name(game_speed)
											  : tr("velocidad ", "speed ") + speed_name(game_speed);
			draw_bar(renderer, *small_text, kScreenHeight - kBarHeight, ship_text,
				{tr(BTN_SQUARE " barco   Select: ", BTN_SQUARE " ship   Select: ") + speed, "Select: " + speed, speed});
		}
		if (in_hanse) {
			// After this frame's ticks and operations: what the panel shows now.
			const HanseView view_now = hanse_view(save, town_index, hanse.page);
			hanse.row = std::min(hanse.row, std::max(0, view_now.rows(hanse.page) - 1));
			draw_hanse(renderer, *small_text, save, hanse, view_now, hanse_context);
		}
		if (trading) {
			draw_market(renderer, *small_text, save, town_index, market);
		}
		if (place.open != Place::None && selection.mode == Mode::Saved && has_save && save.player >= 0 && small_text &&
			!market.open && !hanse.open) {
			// After this frame's ticks and operations: what the window shows now.
			const PlaceView view_now = place_view(save, town_index, place);
			draw_place(renderer, *small_text, place, view_now);
		}
		if (in_mail && mail.open) {
			// After this frame's ticks and operations: the rows now, the selected
			// letter kept by its entry, and its text made once (the game's
			// composer, which may change the save: letter_viewed) and marked
			// read (0x6E) when it is shown.
			const std::vector<uint16_t> rows = mail.tab != 0 ? mail_rows(save, mail.tab) : std::vector<uint16_t>();
			const MailLayout layout(small_text->line_height(), mail.tab == 0);
			const auto at = std::find(rows.begin(), rows.end(), mail.selected);
			mail.row = at != rows.end() ? int(at - rows.begin()) : std::clamp(mail.row, 0, std::max(0, int(rows.size()) - 1));
			mail.selected = rows.empty() ? 0xFFFF : rows[size_t(mail.row)];
			mail.first = std::clamp(mail.first, std::max(0, mail.row - layout.rows + 1), mail.row);
			const uint16_t entry = mail.tab == 0 ? 0xFFFE : mail.selected;
			// The chronicle again when a news comes (a new head of its list).
			const uint16_t news = save.globals.u16(kMailChronicle);
			if (mail.tab == 0 && news != mail.chronicle_head) {
				mail.stale = true;
			}
			if (mail.stale || entry != mail.shown || mail.tab != mail.shown_tab) {
				if (entry != mail.shown || mail.tab != mail.shown_tab) {
					mail.scroll = 0;
				}
				mail.lines.clear();
				mail.picture.reset();
				mail.chronicle_head = news;
				std::string raw;
				bool from_game = false;  // The game's text (Windows-1252, codes); else the viewer's (UTF-8).
				if (!g_texts && (mail.tab == 0 || entry != 0xFFFF)) {
					raw = tr("Para leer las cartas hace falta el Patrician3.exe del juego en la carpeta de datos.",
						"The letters need the game's Patrician3.exe in the data folder.");
				} else if (mail.tab == 0) {
					raw = compose_chronicle(save, *g_texts);
					chronicle_viewed(save, *g_texts);
					from_game = true;
				} else if (entry == 0xFFFF) {
					raw.clear();
				} else if (std::optional<std::string> letter = compose_letter(save, *g_texts, entry)) {
					raw = *letter;
					letter_viewed(save, entry);
					from_game = true;
				} else {
					raw = tr("(Esta carta aún no se puede mostrar.)", "(This letter can't be shown yet.)");
				}
				for (const LetterParagraph& p : letter_paragraphs(raw)) {
					if (from_game && p.made_picture && p.picture == kLetterMapPicture && entry < save.mail.size()) {
						// The picture the game's window makes for this letter (0x51AC60),
						// and empty lines of its height.
						if (!mail.picture) {
							mail.picture = letter_picture_texture(renderer, *archive, save.mail[entry], mail.picture_width,
								mail.picture_height);
						}
						if (mail.picture) {
							mail.lines.emplace_back(p.align, std::string(1, kPictureLine));
							for (int h = small_text->line_height(); h < mail.picture_height; h += small_text->line_height()) {
								mail.lines.emplace_back(p.align, std::string());
							}
							continue;
						}
					}
					if (from_game && p.picture >= 0 && p.text.find_first_not_of(' ') == std::string::npos) {
						mail.lines.emplace_back(p.align, std::string(1, kOrnament));  // The game's picture.
						continue;
					}
					const std::string& s = from_game ? p.text : raw;  // The viewer's own texts are UTF-8 already.
					for (const std::string& line : wrap_letter(*small_text, s, layout.text.w - 12)) {
						mail.lines.emplace_back(p.align, line);
					}
					if (!from_game) {
						break;
					}
				}
				if (entry < save.mail.size() && (save.mail[entry].u8(1) & 0x80) &&
					!queued_letter_operation(save, 0x6E, entry)) {
					queue_operation(save, letter_operation(0x6E, save.player, entry));
				}
				mail.shown = entry;
				mail.shown_tab = mail.tab;
				mail.stale = false;
			}
			const int height = int(mail.lines.size()) * small_text->line_height() + 4;
			mail.scroll = std::clamp(mail.scroll, 0, std::max(0, height - layout.text.h));
			draw_mailbox(renderer, *small_text, save, mail, rows);
		}
		if (small_text) {
			// The notices still in time, one under another (with the mailbox
			// open, at the bottom on the right, under its letter).
			const int padding = 8;
			const int box_height = small_text->line_height() + padding;
			while (!notices.empty() && SDL_TICKS_PASSED(app_ticks(), notices.front().second)) {
				notices.pop_front();
			}
			int y = in_mail && mail.open ? kScreenHeight - 18 - int(notices.size()) * (box_height + 2) : kBarHeight + 6;
			for (const auto& [line, until] : notices) {
				const int width = small_text->measure(line) + 2 * padding;
				const SDL_Rect box = {in_mail && mail.open ? kScreenWidth - 18 - width : 8, y, width, box_height};
				SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
				SDL_SetRenderDrawColor(renderer, 30, 22, 14, 225);
				SDL_RenderFillRect(renderer, &box);
				SDL_SetRenderDrawColor(renderer, 150, 120, 70, 255);
				SDL_RenderDrawRect(renderer, &box);
				small_text->draw(line, box.x + padding, box.y + padding / 2, kTextColor);
				y += box.h + 2;
			}
		}
		if (messages.open && text && small_text) {
			draw_message_window(renderer, *text, *small_text, messages);
		}
		if (end_window.open && text && small_text) {
			draw_end_window(renderer, *text, *small_text, end_window, hanse_context);
		}
		if (in_menu) {  // The menu, on top of everything.
			draw_menu(renderer, *small_text, menu, settings, town_names);
		}
		if (script && !script->screenshot().empty()) {
			std::string error;
			if (p3::app::save_screenshot(renderer, kScreenWidth, kScreenHeight, script->screenshot(), error)) {
				std::printf("%s\n", script->screenshot().c_str());
			} else {
				std::fprintf(stderr, "%s\n", error.c_str());
				screenshot_failed = true;
			}
		}
		SDL_RenderPresent(renderer);
	}

	// Las texturas tienen que liberarse antes de cerrar SDL.
	small_text.reset();
	text.reset();
	sea.reset();
	view.reset();
	if (controller) {
		SDL_GameControllerClose(controller);
	}
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_FreeSurface(screen);
	SDL_Quit();
	return screenshot_failed ? 1 : 0;
}
