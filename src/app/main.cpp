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
#include "p3data/statistics.hpp"
#include "p3data/statistics_window.hpp"
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
// The towns' names of the archive (load_town_names), for the windows.
const std::vector<std::string>* g_town_names = nullptr;
// Where the town's part of the screen ends: the side panel takes the rest
// (its windows go inside the town's part).
int g_town_right = 960;

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
enum class Place { None, MarketHall, Tavern, Shipyard, Ship, Office, Lender, Building };

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
	// The moneylender (building type 0x25, «darlehensgeber»): loans and company shares.
	{0x05, Place::Lender, GameText::BuildingLender, "Prestamista", "Lender's House"},
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
			tiles_by_atlas_[tile.atlas].push_back(&tile);
		}
	}

	~TownView() {
		for (auto& [path, texture] : atlases_) {
			SDL_DestroyTexture(texture);
		}
		if (minimap_) {
			SDL_DestroyTexture(minimap_);
		}
	}

	// The town's minimap (the side panel's, as the game's town map): one row
	// of pixels per row of cells, two pixels per cell (odd rows half a cell
	// on), each cell the average colour of what covers it (a building, a tree,
	// or the ground); made by show().
	SDL_Texture* minimap() const { return minimap_; }
	int map_columns() const { return map_columns_; }
	int map_rows() const { return map_rows_; }

	// The cell under the town's pixel (x, y): the one whose diamond holds it
	// (34 · 21, rows every 11 pixels, odd rows half a cell on).
	static SDL_Point cell_at(int x, int y) {
		SDL_Point best = {-1, -1};
		double nearest = 1e9;
		const int row = y / kRowStep;
		for (int cy = row - 2; cy <= row + 1; ++cy) {
			if (cy < 0) {
				continue;
			}
			const int column = (x - (cy & 1) * (kTileWidth / 2)) / kTileWidth;
			for (int cx = column - 1; cx <= column + 1; ++cx) {
				if (cx < 0) {
					continue;
				}
				const double dx = std::abs(x - (cell_screen_x(cx, cy) + kTileWidth / 2.0)) / (kTileWidth / 2.0);
				const double dy = std::abs(y - (cell_screen_y(cy) + kTileHeight / 2.0)) / (kTileHeight / 2.0);
				if (dx + dy < nearest) {
					nearest = dx + dy;
					best = {cx, cy};
				}
			}
		}
		return best;
	}

	// A building's picture to place: its .let type (the building's picture,
	// 0x672FC0), for the houses the orientation `param` and the first variant,
	// of the town's region where there is one (as the drawing of the town:
	// town_buildings.cpp), otherwise the first of its type.
	const TileDef* building_tile(uint32_t catalog, uint8_t param, int region) const {
		const TileDef* found = nullptr;
		int best = -1;
		for (const auto& [id, tile] : tiles_) {
			if (tile.type != catalog) {
				continue;
			}
			const std::vector<uint32_t>& p = tile.params;
			int score = 0;
			if (catalog == 0x2A || catalog == 0x50 || catalog == 0x51) {
				if (p.size() < 4 || p[1] != 0 || p[2] != param) {
					continue;
				}
				score = p[3] == uint32_t(region) ? 3 : p[3] == 2 ? 2 : 1;
			} else {
				score = p.size() < 3 || p[2] == 0 ? 2 : 1;
			}
			if (score > best || (score == best && found && tile.name < found->name)) {
				best = score;
				found = &tile;
			}
		}
		return found;
	}

	// The picture standing on the cell (x, y) as the town draws it (its front
	// strip on the cell, bottoms aligned), tinted (r, g, b) and see-through.
	void draw_tile(const TileDef* tile, int x, int y, int camera_x, int camera_y, Uint8 r, Uint8 g, Uint8 b) {
		if (!tile || tile->slices.empty()) {
			return;
		}
		SDL_Texture* texture = tile_texture(tile);
		if (!texture) {
			return;
		}
		const TileSlice& front = tile->slices[std::min(tile->front_slice, tile->slices.size() - 1)];
		const SDL_Rect src = {tile->src_x, tile->src_y, tile->width, tile->height};
		const SDL_Rect dst = {cell_screen_x(x, y) - front.x - camera_x,
			cell_screen_y(y) + kTileHeight - front.height - front.y - camera_y, tile->width, tile->height};
		SDL_SetTextureColorMod(texture, r, g, b);
		SDL_SetTextureAlphaMod(texture, 190);
		SDL_RenderCopy(renderer_, texture, &src, &dst);
		SDL_SetTextureAlphaMod(texture, 255);
		SDL_SetTextureColorMod(texture, 255, 255, 255);
	}

	// The cells marked 1 in `marks` (y · width + x, build_marks) as green
	// diamonds over the town, those on the screen's part [0, right).
	void draw_marks(const std::vector<uint8_t>& marks, int width, int camera_x, int camera_y, int right) const {
		if (width <= 0) {
			return;
		}
		std::vector<SDL_Vertex> vertices;
		const SDL_Color colour = {60, 230, 60, 95};
		for (size_t i = 0; i < marks.size(); ++i) {
			if (!marks[i]) {
				continue;
			}
			const int x = int(i % size_t(width));
			const int y = int(i / size_t(width));
			const float left = float(cell_screen_x(x, y) - camera_x);
			const float top = float(cell_screen_y(y) - camera_y);
			if (left > right || left + kTileWidth < 0 || top > kScreenHeight || top + kTileHeight < 0) {
				continue;
			}
			const SDL_FPoint n = {left + kTileWidth / 2.0f, top};
			const SDL_FPoint e = {left + kTileWidth, top + kTileHeight / 2.0f};
			const SDL_FPoint s = {left + kTileWidth / 2.0f, top + kTileHeight};
			const SDL_FPoint w = {left, top + kTileHeight / 2.0f};
			for (const SDL_FPoint& p : {n, e, s, n, s, w}) {
				vertices.push_back({p, colour, {0, 0}});
			}
		}
		if (!vertices.empty()) {
			SDL_RenderGeometry(renderer_, nullptr, vertices.data(), int(vertices.size()), nullptr, 0);
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
		make_minimap(town);
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
		if (const auto all = tiles_by_atlas_.find(path); all != tiles_by_atlas_.end()) {
			// Each tile's average colour (its opaque pixels), for the minimap.
			for (const TileDef* tile : all->second) {
				uint64_t sum[3] = {0, 0, 0};
				uint64_t count = 0;
				for (int y = tile->src_y; y < tile->src_y + tile->height && y < image.height; ++y) {
					for (int x = tile->src_x; x < tile->src_x + tile->width && x < image.width; ++x) {
						const uint8_t* p = &image.rgba[(size_t(y) * size_t(image.width) + size_t(x)) * 4];
						if (p[3] >= 128) {
							sum[0] += p[0];
							sum[1] += p[1];
							sum[2] += p[2];
							++count;
						}
					}
				}
				if (count) {
					tile_colors_[tile] = 0xFF000000u | uint32_t(sum[0] / count) | (uint32_t(sum[1] / count) << 8) |
						(uint32_t(sum[2] / count) << 16);
				}
			}
		}
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

	// The minimap's pixels (RGBA32, little-endian ABGR words) from the town's
	// cells: the object's colour where there is one, the ground's otherwise.
	void make_minimap(const TownMap& town) {
		if (minimap_) {
			SDL_DestroyTexture(minimap_);
			minimap_ = nullptr;
		}
		map_columns_ = town.width;
		map_rows_ = town.height;
		if (town.width <= 0 || town.height <= 0) {
			return;
		}
		const int width = 2 * town.width + 1;
		std::vector<uint32_t> pixels(size_t(width) * size_t(town.height), 0xFF203040u);
		auto colour_of = [&](const TownCell& cell, uint32_t& out) {
			const auto it = tiles_.find(cell.tile);
			if (cell.tile == 0 || it == tiles_.end() || it->second.is_editor_marker()) {
				return false;
			}
			const auto c = tile_colors_.find(&it->second);
			if (c == tile_colors_.end()) {
				return false;
			}
			out = c->second;
			return true;
		};
		for (int y = 0; y < town.height; ++y) {
			for (int x = 0; x < town.width; ++x) {
				const size_t i = size_t(y) * size_t(town.width) + size_t(x);
				uint32_t colour = 0;
				if ((i < town.objects.size() && colour_of(town.objects[i], colour)) ||
					(i < town.ground.size() && colour_of(town.ground[i], colour))) {
					const size_t at = size_t(y) * size_t(width) + size_t(2 * x + (y & 1));
					pixels[at] = colour;
					pixels[at + 1] = colour;
				}
			}
		}
		minimap_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, width, town.height);
		if (minimap_) {
			SDL_UpdateTexture(minimap_, nullptr, pixels.data(), width * 4);
		}
	}

	SDL_Renderer* renderer_;
	const CprArchive& archive_;
	TileSet tiles_;
	std::map<std::string, std::vector<const TileDef*>> tiles_by_atlas_;  // Every tile, by atlas.
	std::unordered_map<const TileDef*, uint32_t> tile_colors_;  // Their average colours (RGBA32 words).
	SDL_Texture* minimap_ = nullptr;
	int map_columns_ = 0;
	int map_rows_ = 0;
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
	SDL_Texture* texture() const { return map_; }  // The whole map (the side panel's minimap of the sea).

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

	// The town's region (its buildings' style).
	int region() const { return base_.region; }

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

// A town's name in the windows (by its index in the save).
std::string window_town_name(const SaveGame& save, int town) {
	static const std::vector<std::string> none;
	return town_name(save, g_town_names ? *g_town_names : none, town);
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
	const std::vector<std::string>& right_texts, int width = kScreenWidth) {
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 160);
	const SDL_Rect bar = {0, top, width, kBarHeight};
	SDL_RenderFillRect(renderer, &bar);
	const int y = top + (kBarHeight - text.line_height()) / 2;
	text.draw(title, kBarMargin, y, kTextColor);
	const int room = width - 4 * kBarMargin - text.measure(title);
	for (const std::string& right : right_texts) {
		if (text.measure(right) <= room) {
			text.draw(right, width - kBarMargin - text.measure(right), y, kDimColor);
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

// What the market trades with, as the game's side panel selects it (0x492430):
// one of the player's ships in port out of a convoy, or a convoy in the town
// with ships of his (the market window's modes 4 and 3: 0x4AA4A0 counts only
// his ships). Select goes through them.
struct MarketEntry {
	int ship = -1;  // A ship alone, or the convoy's guide.
	int convoy = -1;
};

std::vector<MarketEntry> market_entries(const SaveGame& save, int town) {
	std::vector<MarketEntry> entries;
	const uint16_t convoys = save.globals.u16(global::kShips + 0xF6);
	for (const int s : player_ships_in(save, town)) {
		const uint16_t convoy = save.ships[size_t(s)].convoy();
		if (convoy >= convoys || convoy >= save.convoys.size()) {
			entries.push_back({s, -1});
		} else if (std::none_of(entries.begin(), entries.end(), [&](const MarketEntry& e) { return e.convoy == convoy; })) {
			entries.push_back({int(save.convoys[convoy].u16(0x10)), int(convoy)});
		}
	}
	return entries;
}

MarketEntry market_entry(const SaveGame& save, int town, const MarketUi& ui) {
	const std::vector<MarketEntry> entries = market_entries(save, town);
	return entries.empty() ? MarketEntry() : entries[ui.ship % entries.size()];
}

// Barco con el que se comercia ahora (solo, fuera de un convoy), o -1.
int market_ship(const SaveGame& save, int town, const MarketUi& ui) {
	const MarketEntry e = market_entry(save, town, ui);
	return e.convoy < 0 ? e.ship : -1;
}

// The convoy the market trades with now, or -1.
int market_convoy(const SaveGame& save, int town, const MarketUi& ui) {
	return market_entry(save, town, ui).convoy;
}

// A convoy's name, as the game's: «Convoy» and its guide's name.
std::string convoy_label(const SaveGame& save, int convoy) {
	const uint16_t guide = save.convoys[size_t(convoy)].u16(0x10);
	return game_text(GameText::ShipyardConvoy, tr("Convoy", "Convoy")) + " " +
		(guide < save.ships.size() ? save.ships[guide].name() : std::string("?"));
}

// What the player's ships of a convoy carry of a ware (internal units).
int64_t convoy_cargo(const SaveGame& save, int convoy, int ware) {
	int64_t sum = 0;
	const uint16_t ships = save.globals.u16(global::kShipCount);
	for (uint32_t i = save.convoys[size_t(convoy)].u16(0xA); i < ships && i < save.ships.size();
		 i = save.ships[i].memory.u16(0x6)) {
		if (save.ships[i].owner() == save.player) {
			sum += save.ships[i].cargo(size_t(ware));
		}
	}
	return sum;
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

// With a convoy chosen: 4 (the convoy and the town) and, with an office, 2
// and 3 (the convoy and the office); there are no ship's weapons for a convoy.
std::vector<int> market_modes(const SaveGame& save, int town, const MarketUi& ui) {
	const bool office = save.player >= 0 && player_office(save, town) >= 0;
	if (market_convoy(save, town, ui) >= 0) {
		return office ? std::vector<int>{4, 2, 3} : std::vector<int>{4};
	}
	return office ? std::vector<int>{1, 2, 0, 5} : std::vector<int>{1};
}

// The mode that fits what is chosen: a convoy's for a convoy (1 and 5 → 4,
// 0 → 3) and a ship's for a ship (4 → 1, 3 → 0).
void fit_market_mode(const SaveGame& save, int town, MarketUi& ui) {
	const bool convoy = market_convoy(save, town, ui) >= 0;
	if (convoy && (ui.mode == 1 || ui.mode == 5)) {
		ui.mode = 4;
	} else if (convoy && ui.mode == 0) {
		ui.mode = 3;
	} else if (!convoy && ui.mode == 4) {
		ui.mode = 1;
	} else if (!convoy && ui.mode == 3) {
		ui.mode = 0;
	}
}

int market_rows(const MarketUi& ui) {
	return ui.mode == 5 ? 11 : kTradeWareCount;  // The weapons, the hand weapons and the six pieces.
}

std::string market_mode_name(int mode) {
	const std::string ship = game_text(GameText::MarketShip, tr("Barco", "Ship"));
	const std::string office = game_text(GameText::Office, tr("Oficina", "Office"));
	const std::string convoy = game_text(GameText::ShipyardConvoy, tr("Convoy", "Convoy"));
	switch (mode) {
	case 2:
		return office + " <-> " + game_text(GameText::MarketTown, tr("Ciudad", "Town"));
	case 0:
		return ship + " <-> " + office;
	case 4:
		return convoy + " <-> " + game_text(GameText::MarketTown, tr("Ciudad", "Town"));
	case 3:
		return convoy + " <-> " + office;
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
	const uint32_t step = kMarketSteps[size_t(ui.step)];
	if (office >= 0 && ui.mode == 3) {
		const int convoy = market_convoy(save, town, ui);
		if (convoy < 0) {
			return std::nullopt;
		}
		return first ? office_to_convoy_operation(save, convoy, ui.ware, step)
					 : convoy_to_office_operation(save, convoy, ui.ware, step);
	}
	if (office < 0 || (ui.mode != 2 && ship < 0)) {
		return std::nullopt;
	}
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
	const int convoy = market_convoy(save, town, ui);
	if (ui.mode != 2 && ship < 0 && convoy < 0) {
		ui.message = tr("No tienes ningún barco en este puerto.", "You have no ship in this port.");
		return;
	}
	const std::optional<OperationBytes> op = office_operation(save, town, ui, first);
	if (!op) {
		ui.message = tr("No hay nada que pasar.", "There is nothing to move.");
		return;
	}
	// What can change: the ware or weapon in the office and the ship (or the
	// convoy's ships of the player), the ship's pieces and hand weapons, the money.
	auto snapshot = [&]() {
		std::array<int64_t, 4> v{};
		const OfficeMemory& o = save.offices[size_t(office)].memory;
		if (ui.mode == 3) {
			v[0] = o.s32(4 + 4 * size_t(ui.ware));
			v[1] = convoy_cargo(save, convoy, ui.ware);
		} else if (ui.mode == 5 && ui.ware >= 4) {
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
		const bool to_convoy = ui.mode == 3;
		ui.message = moved == 0 ? tr("No se ha podido.", "It couldn't be done.")
			: g_spanish ? (moved > 0 ? (to_convoy ? "Al convoy: " : "Al barco: ") : "A la oficina: ") + amount + " de " + what + "."
						: (moved > 0 ? (to_convoy ? "To the convoy: " : "To the ship: ") : "To the office: ") + amount + " of " +
					what + ".";
	}
}

// The market in the mode 4 (a convoy and the town): 0x1A buying and 0x19
// selling, as the game's window builds them from what it keeps of the convoy
// (0x4AA4A0); what it did, from the convoy's cargo and the money.
std::optional<OperationBytes> convoy_market_operation(const SaveGame& save, int town, const MarketUi& ui, bool buying) {
	const int convoy = market_convoy(save, town, ui);
	if (convoy < 0) {
		return std::nullopt;
	}
	const uint32_t step = kMarketSteps[size_t(ui.step)];
	return buying ? convoy_buy_operation(save, convoy, ui.ware, step) : convoy_sell_operation(save, convoy, ui.ware, step);
}

void convoy_trade(SaveGame& save, Sailing* sailing, int town, MarketUi& ui, bool buying) {
	ui.message_until = app_ticks() + kMessageTime;
	const int convoy = market_convoy(save, town, ui);
	const std::string name = ware_name(ui.ware);
	if (convoy < 0) {
		ui.message = tr("No tienes ningún barco en este puerto.", "You have no ship in this port.");
		return;
	}
	if (!save.towns[size_t(town)].has_market()) {
		ui.message = tr("Esta ciudad no tiene mercado.", "This town has no market.");
		return;
	}
	const std::optional<OperationBytes> op = convoy_market_operation(save, town, ui, buying);
	if (!op) {
		ui.message = buying ? tr("No se puede comprar.", "Nothing to buy.")
							: (g_spanish ? "El convoy no lleva " + name + "." : "The convoy carries no " + name + ".");
		return;
	}
	const int64_t cargo = convoy_cargo(save, convoy, ui.ware);
	const int32_t money = save.merchants[size_t(save.player)].money();
	if (sailing) {
		queue_operation(save, *op);
		run_queued_operations(save, *sailing);
	} else {
		run_operation(save, nullptr, op->data());
	}
	const int32_t amount = int32_t(std::abs(convoy_cargo(save, convoy, ui.ware) - cargo));
	const int32_t price = std::abs(save.merchants[size_t(save.player)].money() - money);
	const WareUnit unit = kWares[size_t(ui.ware)].unit;
	if (amount == 0) {
		ui.message = buying && save.merchants[size_t(save.player)].money() <= 0
			? game_text(GameText::NotEnoughMoney, tr("No tienes dinero suficiente.", "Not enough money!"))
			: tr("No se ha podido.", "It couldn't be done.");
	} else {
		ui.message = g_spanish ? std::string(buying ? "Compra: " : "Venta: ") + amount_text(amount, unit) + " de " + name +
				" por " + format_money(price) + " monedas."
							   : std::string(buying ? "Bought " : "Sold ") + amount_text(amount, unit) + " of " + name +
				" for " + format_money(price) + " coins.";
	}
}

// Buys or sells as the game does: the market window queues the operation and
// the game runs its queue before the next tick, so the viewer runs it at once
// (without the sea, Sailing, only this operation). What it did is told by the
// ship's cargo and the owner's money before and after.
void trade(SaveGame& save, Sailing* sailing, int town, MarketUi& ui, bool buying) {
	if (ui.mode == 4) {
		convoy_trade(save, sailing, town, ui, buying);
		return;
	}
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
		panel = {g_town_right - kWidth - 8, kBarHeight + 4, kWidth, height};
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
	if (ui.mode != 1 && ui.mode != 4) {
		draw_office_market(renderer, text, save, town_index, ui);
		return;
	}
	const SavedTown& town = save.towns[size_t(town_index)];
	const MarketLayout layout(text.line_height());
	const SDL_Rect& panel = layout.panel;
	const int left = panel.x + MarketLayout::kPadding;
	const int ship_index = market_ship(save, town_index, ui);
	const int convoy = ui.mode == 4 ? market_convoy(save, town_index, ui) : -1;
	const SavedShip* ship = ship_index >= 0 && convoy < 0 ? &save.ships[size_t(ship_index)] : nullptr;
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
	} else if (convoy >= 0) {
		// What the window keeps of the convoy (0x4AA4A0): the player's ships' hold.
		char used[32];
		char capacity[32];
		std::snprintf(used, sizeof(used), "%.1f", market_convoy_used(save, convoy) / 10.0);
		std::snprintf(capacity, sizeof(capacity), "%.1f", market_convoy_capacity(save, convoy) / 2000.0);
		const std::string hold = g_texts
			? std::string(used) + " / " + g_texts->format(GameText::AmountLoadsDecimal, {capacity})
			: std::string(used) + tr(" de ", " of ") + capacity + tr(" cargas", " loads");
		text.draw(convoy_label(save, convoy) + ": " + hold, left, layout.header_y, kTextColor);
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
		if (ship || convoy >= 0) {
			const int n = units(int32_t(ship ? ship->cargo(size_t(w)) : convoy_cargo(save, convoy, w)), ware.unit);
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
	} else if (!show_message && convoy >= 0) {
		// What the window would send (its price is the game's when it runs).
		auto amount_of = [&](bool buying) {
			int32_t n = 0;
			if (const std::optional<OperationBytes> op = convoy_market_operation(save, town_index, ui, buying)) {
				std::memcpy(&n, op->data() + 4, 4);
			}
			return n;
		};
		const int32_t buy = amount_of(true);
		const int32_t sell = amount_of(false);
		footer = buy > 0 ? tr("Comprar ", "Buy ") + amount_text(buy, selected_unit) : tr("No se puede comprar", "Nothing to buy");
		if (sell > 0) {
			footer += tr("    vender ", "    sell ") + amount_text(sell, selected_unit);
		}
	}
	text.draw(footer, left, layout.footer_y, kTextColor);
	draw_button(renderer, text, layout.step_button, tr("Izq./Der.: ", "Left/Right: ") + step_text(ui));
	if (ship) {
		draw_button(renderer, text, layout.buy_button, tr(BTN_CROSS " comprar", BTN_CROSS " buy"));
		draw_button(renderer, text, layout.sell_button, tr(BTN_SQUARE " vender", BTN_SQUARE " sell"));
	} else if (convoy >= 0) {
		// The buttons, dimmed when the window sends nothing.
		for (const bool buying : {true, false}) {
			const SDL_Rect& r = buying ? layout.buy_button : layout.sell_button;
			draw_button(renderer, text, r, buying ? tr(BTN_CROSS " comprar", BTN_CROSS " buy") : tr(BTN_SQUARE " vender", BTN_SQUARE " sell"));
			if (!convoy_market_operation(save, town_index, ui, buying)) {
				SDL_SetRenderDrawColor(renderer, 30, 22, 14, 150);
				SDL_RenderFillRect(renderer, &r);
			}
		}
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
	const int convoy = ui.mode == 3 ? market_convoy(save, town_index, ui) : -1;
	const SavedShip* ship = ship_index >= 0 && ui.mode != 3 ? &save.ships[size_t(ship_index)] : nullptr;
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(renderer, 30, 22, 14, 225);
	SDL_RenderFillRect(renderer, &panel);
	SDL_SetRenderDrawColor(renderer, 150, 120, 70, 255);
	SDL_RenderDrawRect(renderer, &panel);
	auto right = [&](const std::string& s, int column, int y, SDL_Color color) {
		text.draw(s, panel.x + MarketLayout::kColumns[column] - text.measure(s), y, color);
	};
	std::string header = market_mode_name(ui.mode);
	if (ui.mode == 3) {
		header += convoy >= 0 ? ": " + convoy_label(save, convoy) : "";
	} else if (ui.mode != 2) {
		header += ship ? ": " + ship->name() : tr(": ningún barco en el puerto", ": no ship in the port");
	}
	text.draw(header, left, layout.header_y, ship || convoy >= 0 || ui.mode == 2 ? kTextColor : kDimColor);
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
		right(ui.mode == 3 ? game_text(GameText::ShipyardConvoy, tr("Convoy", "Convoy"))
						   : game_text(GameText::MarketShip, tr("Bodega", "Ship")),
			3, layout.titles_y, kDimColor);
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
			if (convoy >= 0) {
				right(amount(int32_t(convoy_cargo(save, convoy, r))), 3, y, kTextColor);
			} else {
				right(ship ? amount(ship->cargo(r)) : "-", 3, y, kTextColor);
			}
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
	} else if (convoy >= 0) {
		button(layout.buy_button, tr(BTN_CROSS " al convoy", BTN_CROSS " to the convoy"), true);
		button(layout.sell_button, tr(BTN_SQUARE " a la oficina", BTN_SQUARE " to the office"), false);
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
	// The shipyard's page (the repair dock has only the first): 0 repair, 1
	// order a ship, 2 cancel an order, 3 upgrade, 4 rename.
	int page = 0;
	// The name being written (Latin-1) on the rename page, and whether the
	// text input is on (Square: the Vita's keyboard, the PC's).
	std::string name;
	bool typing = false;
	std::string message;
	Uint32 message_until = 0;
	// The moneylender's window: the loans the player owes when it opened plus
	// those taken there (its [+0xFE4]: more than 2, no more loans); whether he
	// paid one off (the window drops its list then: one per visit); the
	// interest's kind to grant a loan (0 very low .. 4 very high, 2 at first).
	int lender_debts = 0;
	bool paid_off = false;
	int lender_kind = 2;
	// The player's own building touched in the town (its record): demolish or auction it.
	int record = -1;
};

constexpr int kShipyardPages = 5;
constexpr int kLenderPages = 6;

// How many pages the window has (L/R, or touching its title): the shipyard's
// and the moneylender's; 0 for the others (the repair dock only repairs).
int place_pages(const PlaceUi& ui) {
	if (ui.open == Place::Shipyard && ui.let_type == 0x01) {
		return kShipyardPages;
	}
	return ui.open == Place::Lender ? kLenderPages : 0;
}

// The next or the previous page, from its first row.
void turn_page(PlaceUi& ui, int step) {
	const int pages = place_pages(ui);
	ui.page = ((ui.page + step) % pages + pages) % pages;
	ui.row = 0;
	ui.amount = 0;
	ui.name.clear();
}

// What a window shows now, made from the save each frame (also for the touch screen).
struct PlaceView {
	std::string title;
	std::vector<std::pair<std::string, SDL_Color>> lines;  // Of the selected row.
	std::vector<std::pair<std::string, bool>> rows;  // Label and whether it can be done.
	std::vector<uint16_t> ships;  // The selected row's list (Select: the next one).
	int ship = -1;  // The chosen one.
	int max_amount = -1;  // The amount goes from 0 to this (-1: the row has none).
	// The amount is a choice (a destination, a level, a name), 0 too, shown
	// as `amount_label`.
	bool amount_choice = false;
	std::string amount_label;
	uint16_t captain = 0xFFFF;  // The tavern's captain.
	std::string act;  // The act button's label, if not the row's.
	// What each row does, where the rows aren't fixed: the convoy window's
	// choices (kPrivateConvoy, kPublicConvoy, a convoy to join), kLeaveConvoy,
	// kDisbandConvoy; the shipyard's pages' items (a ship type, a ship).
	std::vector<int32_t> codes;
};

constexpr int32_t kLeaveConvoy = -3;
constexpr int32_t kDisbandConvoy = -4;
constexpr int32_t kNoConvoy = -5;

// A game's text in lines, without the empty ones (its codes are cleaned).
std::vector<std::string> text_lines(const std::string& s) {
	std::vector<std::string> lines;
	size_t at = 0;
	while (at <= s.size()) {
		const size_t end = std::min(s.find('\n', at), s.size());
		std::string line = s.substr(at, end - at);
		const size_t first = line.find_first_not_of(" \t");
		if (first != std::string::npos) {
			lines.push_back(line.substr(first));
		}
		at = end + 1;
	}
	return lines;
}

// A game's text formatted, or the fallback.
std::string game_format(GameText id, const std::vector<std::string>& args, const std::string& fallback) {
	return g_texts ? g_texts->format(id, args) : fallback;
}

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

std::string full_date(uint32_t tick);

// The shipyard's pages 1 to 4 (0x5F8F50): order a ship (0x5F9570, 0x2C),
// cancel an order (0x5F9B00, 0x2F), upgrade a ship (0x5FA320, 0x0C) and
// rename one (0x5FABE0, 0x2D/0x2E), with what each lists and costs
// (player_actions.hpp).
void shipyard_page_view(const SaveGame& save, int town, PlaceUi& ui, PlaceView& v) {
	const int player = save.player;
	static const char* kPages[kShipyardPages][2] = {{"", ""}, {"Construir barco", "Build a ship"}, {"", ""}, {"", ""}, {"", ""}};
	const GameText titles[kShipyardPages] = {GameText::ShipyardRepair, GameText::ShipyardRepair,
		GameText::ShipyardCancelContract, GameText::ShipyardUpgradeShip, GameText::ShipyardChangeName};
	const std::string page = ui.page == 1 ? tr(kPages[1][0], kPages[1][1])
		: game_text(titles[ui.page], ui.page == 2 ? tr("Cancelar contrato", "Cancel contract")
								   : ui.page == 3 ? tr("Mejorar barco", "Upgrade ship") : tr("Cambiar nombre", "Change name"));
	v.title = place_name(ui.let_type) + " - " + page + "   (L/R)";
	auto clamp_row = [&]() { ui.row = std::clamp(ui.row, 0, std::max(0, int(v.rows.size()) - 1)); };
	switch (ui.page) {
	case 1: {  // Order a ship: the four types, what each costs (0x52B2C0).
		v.act = game_text(GameText::ShipyardExecute, tr("Ejecutar", "Execute"));
		for (uint32_t type = 0; type < 4; ++type) {
			int32_t cost = 0;
			const bool ok = ship_order_cost(save, town, type, player, cost);
			v.rows.push_back({ship_type_name(int(type)) + (cost > 0 ? ": " + format_money(cost) : std::string()), ok});
			v.codes.push_back(int32_t(type));
		}
		clamp_row();
		int32_t cost = 0;
		ship_order_cost(save, town, uint32_t(ui.row), player, cost);
		v.lines.push_back({game_format(GameText::ShipyardTotalCost, {format_money(cost)},
							   tr("El coste total asciende a ", "The total cost amounts to ") + format_money(cost) + "."),
			v.rows[size_t(ui.row)].second ? kTextColor : kDimColor});
		break;
	}
	case 2: {  // The orders of the shipyard (its list [+0x81C]).
		v.act = game_text(GameText::ShipyardCancelContract, tr("Cancelar contrato", "Cancel contract"));
		for (const uint16_t s : shipyard_orders(save, town)) {
			const SavedShip& ship = save.ships[s];
			std::string label = ship_type_name(ship.type()) + " " + ship.name();
			if (ship.owner() == player) {
				label += tr(" (tuyo)", " (yours)");
			}
			v.rows.push_back({label, can_cancel_order(save, s)});
			v.codes.push_back(int32_t(s));
		}
		clamp_row();
		if (v.rows.empty()) {
			v.lines.push_back({game_text(GameText::ShipyardNoOrders, tr("No se encargaron barcos en este astillero.",
				"No ships were commissioned at this shipyard.")), kDimColor});
			break;
		}
		const SavedShip& ship = save.ships[size_t(v.codes[size_t(ui.row)])];
		// [+0x4C]: when it was ordered (the letter 0x14's «Your order of ...»).
		v.lines.push_back({tr("Encargado el ", "Ordered on ") + full_date(ship.memory.u32(0x4C)), kTextColor});
		if (ship.owner() == player) {
			const GameText note = v.rows[size_t(ui.row)].second ? GameText::ShipyardCancelAsk : GameText::ShipyardWorkStarted;
			for (const std::string& line : text_lines(game_text(note, ""))) {
				v.lines.push_back({line, note == GameText::ShipyardCancelAsk ? kTextColor : kDimColor});
			}
		}
		break;
	}
	case 3: {  // Upgrade: the ships of 0x504AC0's mode 2, the level to reach and its cost (0x52B670).
		v.act = game_text(GameText::ShipyardUpgradeShip, tr("Mejorar barco", "Upgrade ship"));
		std::vector<uint16_t> ships = window_ships(save, player, town, ShipListMode::Upgrades);
		for (const uint16_t s : ships) {
			const SavedShip& ship = save.ships[s];
			v.rows.push_back({ship_label(ship) + " - " + game_text(GameText::ShipyardLevel, tr("Nivel", "Level")) + " " +
					std::to_string(ship.memory.u8(0xF)),
				true});
			v.codes.push_back(int32_t(s));
		}
		clamp_row();
		if (v.rows.empty()) {
			v.lines.push_back({game_text(GameText::ShipyardNoShips, tr("No hay barcos disponibles en el puerto.",
				"are no further ships available.")), kDimColor});
			break;
		}
		const int ship = v.codes[size_t(ui.row)];
		const int level = save.ships[size_t(ship)].memory.u8(0xF);
		v.max_amount = std::max(0, 1 - level);  // To the level + 1 .. 2.
		v.amount_choice = true;
		const int target = level + 1 + std::clamp(ui.amount, 0, v.max_amount);
		v.amount_label = game_text(GameText::ShipyardNewLevel, tr("Nivel", "Level")) + " " + std::to_string(target);
		ShipUpgrade cost;
		const bool ok = upgrade_cost(save, town, ship, target, cost);
		v.rows[size_t(ui.row)].second = ok;
		v.lines.push_back({game_text(GameText::ShipyardMaterials, tr("Material necesario", "Raw material requirement")) +
				" (" + game_text(GameText::ShipyardInStock, tr("existencias", "in stock")) + " / " +
				game_text(GameText::ShipyardBuy, tr("comprar", "buy")) + "):",
			kTextColor});
		static constexpr int kWaresUsed[3] = {0xB, 0xF, 0xC};
		for (size_t k = 0; k < 3; ++k) {
			v.lines.push_back({ware_name(kWaresUsed[k]) + ": " + std::to_string(cost.s32(0xC + 4 * k)) + " / " +
					std::to_string(cost.s32(0x18 + 4 * k)) + " (" + format_money(cost.s32(0x24 + 4 * k)) + ")",
				kTextColor});
		}
		v.lines.push_back({game_format(GameText::ShipyardTotalCost, {format_money(cost.s32(0x30))},
							   tr("El coste total asciende a ", "The total cost amounts to ") + format_money(cost.s32(0x30)) + ".") +
				"  " + std::to_string(cost.s32(0x34)) + tr(" días", " days"),
			ok ? kTextColor : kDimColor});
		break;
	}
	default: {  // Rename: the ships of 0x504AC0's mode 0; the name typed or one of the game's.
		v.act = game_text(GameText::ShipyardChangeName, tr("Cambiar nombre", "Change name"));
		for (const uint16_t s : window_ships(save, player, town, ShipListMode::Sailors)) {
			v.rows.push_back({ship_label(save.ships[s]), true});
			v.codes.push_back(int32_t(s));
		}
		clamp_row();
		if (v.rows.empty()) {
			v.lines.push_back({game_text(GameText::ShipyardNoShips, tr("No hay barcos disponibles en el puerto.",
				"are no further ships available.")), kDimColor});
			break;
		}
		v.max_amount = std::max(0, int(save.ship_names.size()) - 1);
		v.amount_choice = true;
		const std::string suggested =
			save.ship_names.empty() ? std::string() : save.ship_names[size_t(std::clamp(ui.amount, 0, v.max_amount))];
		v.amount_label = "<>";
		const std::string name = ui.name.empty() ? suggested : ui.name;
		v.lines.push_back({game_text(GameText::ShipyardNewName, tr("Nuevo nombre", "New name")) + ": " +
				clean_game_text(name) + (ui.typing ? "_" : ""),
			kTextColor});
		v.lines.push_back({ui.typing ? tr("Escribe el nombre; X para acabar.", "Type the name; X to finish.")
									 : tr(BTN_SQUARE " escribir otro;  Izq./Der.: los del juego", BTN_SQUARE " type another;  Left/Right: the game's"),
			kDimColor});
		v.rows[size_t(ui.row)].second = !name.empty();
		break;
	}
	}
}

// --- The moneylender (its window 0x5B2B00; what it lists and sends in
// player_actions.hpp) ---

// One of its texts, by its address in the English exe (game_text_addresses.inc).
std::string lender_text(uint32_t english, const char* spanish, const char* fallback) {
	if (g_texts) {
		const std::string& raw = g_texts->raw_at(english);
		if (!raw.empty()) {
			return clean_game_text(raw);
		}
	}
	return tr(spanish, fallback);
}

// Two pieces of a game's sentence, with one space between them.
std::string joined(const std::string& a, const std::string& b) {
	return a.empty() || a.back() == ' ' ? a + b : a + " " + b;
}

// The first letter in lower case (a game's word inside a sentence; ASCII).
std::string lowered(std::string s) {
	if (!s.empty()) {
		s[0] = char(std::tolower(uint8_t(s[0])));
	}
	return s;
}

// A game's text in one line (its line breaks as spaces).
std::string one_line(const std::string& s) {
	std::string out;
	for (const std::string& line : text_lines(s)) {
		out += (out.empty() ? "" : " ") + line;
	}
	return out;
}

std::string merchant_name(const SaveGame& save, int merchant) {
	if (merchant < 0 || size_t(merchant) >= save.merchants.size()) {
		return "?";
	}
	const MerchantMemory& m = save.merchants[size_t(merchant)].memory;
	return clean_game_text(game_first_name(save, m.u8(0x1C)) + " " + game_family_name(save, m.u8(0x1D)));
}

// A merchant's or townsman's rank (0x6A0608, «shopkeeper» .. «Alderman»).
std::string rank_name(int rank) {
	return lender_text(0x6A0608 + 4 * uint32_t(rank & 7), "", "");
}

// «2.9»: the weekly interest in tenths of %.
std::string interest_text(uint8_t tenths) {
	return std::to_string(tenths / 10) + (g_spanish ? "," : ".") + std::to_string(tenths % 10);
}

// The interest's kinds to grant a loan («very low» .. «very high»).
std::string interest_kind(int kind) {
	static const char* kKinds[5][2] = {{"Muy bajo", "very low"}, {"Bajo", "low"}, {"Normal", "normal"}, {"Alto", "high"},
		{"Muy alto", "very high"}};
	const int k = std::clamp(kind, 0, 4);
	return capitalized(lender_text(0x69C0EC + 4 * uint32_t(k), kKinds[k][0], kKinds[k][1]));
}

// Its pages: 0 take out a loan, 1 pay one off, 2 grant one, 3 the loans
// granted, 4 buy company shares, 5 sell them.
void lender_view(SaveGame& save, int town, PlaceUi& ui, PlaceView& v) {
	static const uint32_t kTitles[kLenderPages] = {0x69BFE8, 0x69BFEC, 0x69BFF0, 0x69C118, 0x69BFF4, 0x69BFF8};
	static const char* kFallback[kLenderPages][2] = {{"Contratar préstamo", "Take out loan"},
		{"Devolver préstamo", "Pay off loan"}, {"Conceder préstamo", "Grant loan"},
		{"Préstamos concedidos", "Loans granted"}, {"Comprar acciones", "Buy company shares"},
		{"Vender acciones", "Sell company shares"}};
	ui.page = ((ui.page % kLenderPages) + kLenderPages) % kLenderPages;
	v.title = place_name(ui.let_type) + " - " + lender_text(kTitles[ui.page], kFallback[ui.page][0], kFallback[ui.page][1]) +
		"   (L/R)";
	const int player = save.player;
	const int32_t money = save.merchants[size_t(player)].money();
	auto clamp_row = [&]() { ui.row = std::clamp(ui.row, 0, std::max(0, int(v.rows.size()) - 1)); };
	auto dim = [&](const std::string& line) {
		for (const std::string& part : text_lines(line)) {
			v.lines.push_back({part, kDimColor});
		}
	};
	switch (ui.page) {
	case 0: {  // Take out a loan (0x51CE80; 0x5B5A00 sends it).
		v.act = lender_text(0x69BFFC, "Contratar", "Take out");
		std::array<LoanTerms, 3> offers{};
		const int32_t n = loan_offers(save, town, offers);
		if (n == -1) {
			dim(lender_text(0x69C0A0, "\"Por desgracia no puedo ofrecerte un préstamo debido",
					"\"Unfortunately, I cannot offer you a loan because") + " " +
				lender_text(0x69C0A4, "a tu mala fama de pagador.\"", "of your bad payment history.\""));
			break;
		}
		if (n == -2) {
			const MerchantMemory& m = save.merchants[size_t(player)].memory;
			const std::string text = lender_text(0x69C0A8, "\"Créeme, me gustaría ayudarte, pero con tus deudas es demasiado arriesgado.\"",
				"\"Believe me, I would like to help you, but with your debts it is too great a risk.\"");
			const std::vector<std::string> names = {clean_game_text(game_first_name(save, m.u8(0x1C))),
				clean_game_text(game_family_name(save, m.u8(0x1D))), window_town_name(save, m.u8(0x19))};
			dim(one_line(g_texts ? GameTexts::format(text, names) : text));
			break;
		}
		if (n == 0) {
			dim(lender_text(0x69C114, "\"En este momento no te puedo ofrecer ningún préstamo.\"",
				"\"At present, I cannot offer you a loan.\""));
			break;
		}
		v.lines.push_back({lender_text(0x69C010, "Están disponibles los siguientes préstamos:", "The following loans are available"),
			kTextColor});
		const bool room = ui.lender_debts <= 2;
		for (int32_t k = 0; k < n; ++k) {
			const LoanTerms& o = offers[size_t(k)];
			uint16_t amount;
			std::memcpy(&amount, o.data() + 8, 2);
			v.rows.push_back({format_money(amount) + " " + lender_text(0x69C014, "durante un período de", "at") + " " +
					std::to_string(o[0xA]) + " " + lender_text(0x69C048, "semanas", "weeks"),
				room});
			v.codes.push_back(k);
		}
		clamp_row();
		const LoanTerms& o = offers[size_t(ui.row)];
		uint32_t repay;
		std::memcpy(&repay, o.data(), 4);
		v.lines.push_back({capitalized(lender_text(0x69C01C, "La devolución asciende a", "The repayment amounts to")) + " " +
				format_money(int64_t(repay)) + ".  " + capitalized(lender_text(0x69C020, "Interés semanal", "Weekly interest")) +
				": " + interest_text(o[0x4]) + " %",
			kTextColor});
		if (!room) {
			dim(lender_text(0x69C108, "\"Deberías devolver este préstamo", "\"You should pay off a loan first") + " " +
				lender_text(0x69C10C, "antes de solicitar uno nuevo.\"", "before asking for a new one.\""));
		}
		break;
	}
	case 1: {  // Pay one off (0x5B5B90: 0x2A).
		v.act = lender_text(0x69C000, "Liquidar", "Pay off");
		const std::vector<uint16_t> debts = loans_owed(save, player);
		if (debts.empty()) {
			dim(lender_text(0x69C0B4, "Roguemos al Señor para que nos conceda una vida libre de deudas.",
				"Praise God for a debt free life"));
			break;
		}
		v.lines.push_back({lender_text(0x69C024, "Tienes pendientes las siguientes deudas", "You have the following debts outstanding"),
			kTextColor});
		for (const uint16_t d : debts) {
			const GameMemory<0x18>& e = save.events[d];
			const bool can = !ui.paid_off && can_pay_off_here(save, town, d) && uint32_t(money) >= e.u32(8);
			v.rows.push_back({format_money(int64_t(e.u32(8))) + " " + lender_text(0x69C028, "en", "in") + " " +
					window_town_name(save, e.u8(0x14)) + ", " + lender_text(0x69C02C, "a pagar el", "to be paid by") + " " +
					full_date(e.u32(0)),
				can});
			v.codes.push_back(d);
		}
		clamp_row();
		const uint16_t d = uint16_t(v.codes[size_t(ui.row)]);
		if (ui.paid_off) {
			dim(tr("Ya has liquidado un préstamo en esta visita.", "You have paid off a loan on this visit already."));
		} else if (!can_pay_off_here(save, town, d)) {
			dim(tr("Solo se puede liquidar aquí, donde tengas oficina o un barco en el puerto.",
				"Only here or where you have an office or a ship in port."));
		} else if (uint32_t(money) < save.events[d].u32(8)) {
			dim(lender_text(0x69C0C4, "\"No tienes el oro suficiente para devolver el préstamo.\"",
				"\"You do not enough cash to pay off the loan.\""));
		}
		break;
	}
	case 2: {  // Grant one (0x51D160; 0x5B6000 sends it with the interest's kind).
		v.act = lender_text(0x69C004, "Ofrecer préstamo", "Offer loan");
		if (player_office(save, town) < 0) {
			dim(lender_text(0x69C078, "Tienes que residir en esta ciudad", "You have to be resident in this town") + " " +
				lender_text(0x69C07C, "para poder conceder un préstamo", "in order to be able to grant a loan"));
			break;
		}
		std::array<LoanTerms, 3> requests{};
		const int32_t n = loan_requests(save, town, requests, nullptr);
		if (n == 0) {
			dim(lender_text(0x69C0E4, "Nadie en esta ciudad necesita un préstamo.", "Nobody in this town wants to take a loan."));
			break;
		}
		v.lines.push_back({lender_text(0x69C030, "Puedo contactar con las siguientes personas que desean",
								"I can arrange for the following parties to be interested") +
				" " + lender_text(0x69C034, "obtener un préstamo de ti:", "in a loan for you"),
			kTextColor});
		for (int32_t k = 0; k < n; ++k) {
			const LoanTerms& o = requests[size_t(k)];
			uint16_t amount;
			std::memcpy(&amount, o.data() + 8, 2);
			v.rows.push_back({capitalized(rank_name(o[0x5])) + " " + clean_game_text(game_first_name(save, o[0x6])) + " " +
					clean_game_text(game_family_name(save, o[0x7])) + ": " + format_money(amount) + ", " +
					std::to_string(o[0xA]) + " " + lender_text(0x69C048, "semanas", "weeks"),
				int32_t(amount) <= money});
			v.codes.push_back(k);
		}
		clamp_row();
		LoanTerms o = requests[size_t(ui.row)];
		o[0xF] = uint8_t((o[0xF] & 0xF0) | uint8_t(ui.lender_kind));
		fn_51d270(o, nullptr);  // What the window shows; the roll is when it is granted.
		v.max_amount = 4;
		v.amount_choice = true;
		v.amount_label = interest_kind(ui.lender_kind);
		uint16_t amount;
		std::memcpy(&amount, o.data() + 8, 2);
		uint32_t repay;
		std::memcpy(&repay, o.data(), 4);
		v.lines.push_back({lender_text(0x69C04C, "¿Cuál debería ser el tipo de interés?", "How high should the interest be?") +
				"  " + tr("(Izq./Der.)", "(Left/Right)"),
			kTextColor});
		auto with = [](std::string text, const std::string& value) {
			const size_t at = text.find("%1.1f");
			if (at != std::string::npos) {
				text.replace(at, 5, value);
			}
			return GameTexts::format(text, {});
		};
		v.lines.push_back({GameTexts::format(lender_text(0x69C050, "Cantidad prestada: %i", "Amount of loan %i"), {format_money(amount)}) +
				GameTexts::format(lender_text(0x69C054, ". Plazo de devolución %i semanas", ", term %i weeks"), {std::to_string(o[0xA])}),
			kTextColor});
		v.lines.push_back({capitalized(with(lender_text(0x69C058, "Interés: %1.1f%%", "interest: %1.1f%%"), interest_text(o[0x4]))) +
				GameTexts::format(lender_text(0x69C05C, ". Total a devolver %i", ", repayment sum: %i"), {format_money(int64_t(repay))}),
			kTextColor});
		if (int32_t(amount) > money) {
			dim(lender_text(0x69C080, "Tendrás que pedir tú mismo un préstamo", "Perhaps you should take out a loan yourself") + " " +
				lender_text(0x69C084, "para poder ofrecer una cantidad así.", "before offering an amount of this size."));
		}
		break;
	}
	case 3: {  // The loans granted (0x5B62E0, 0x4D9210): nothing to send.
		const std::vector<uint16_t> granted = loans_granted(save, player);
		if (granted.empty()) {
			dim(lender_text(0x69C124, "No has concedido ningún préstamo.", "You have granted no loan."));
			break;
		}
		v.lines.push_back({lender_text(0x69C11C, "Has concedido los siguientes préstamos:", "You have granted the following loans."),
			kTextColor});
		for (const uint16_t g : granted) {
			const GameMemory<0x18>& e = save.events[g];
			const uint8_t borrower = e.u8(0x11);
			const std::string who = borrower < save.merchants.size()
				? merchant_name(save, borrower)
				: rank_name(e.u8(0x15)) + " " + clean_game_text(game_first_name(save, e.u8(0x16))) + " " +
					clean_game_text(game_family_name(save, e.u8(0x17)));
			v.lines.push_back({who + " (" + window_town_name(save, e.u8(0x14)) + "): " + format_money(int64_t(e.u32(8))) + ", " +
					full_date(e.u32(0)),
				kTextColor});
		}
		break;
	}
	case 4: {  // Buy company shares (0x4D9320, every frame; 0x5B6400 sends 0x2B).
		v.act = lender_text(0x69C008, "Comprar acciones", "Buy shares");
		const std::vector<ShareEntry> list = shares_on_sale(save);
		for (size_t k = 0; k < list.size(); ++k) {
			const ShareEntry& e = list[k];
			if (e[1] == 0) {
				continue;  // An entry the game never fills (a dropped pool).
			}
			uint32_t price, owns;
			std::memcpy(&price, e.data() + 4, 4);
			std::memcpy(&owns, e.data() + 8, 4);
			std::string label = int(e[0]) == player
				? one_line(GameTexts::format(lender_text(0x69C06C, "%i%% de tu empresa por %i", "%i%% of your own company for %i"),
					  {std::to_string(e[1]), format_money(int64_t(price))}))
				: std::to_string(e[1]) + " % " + merchant_name(save, e[0]) + ", " + lowered(lender_text(0x69C070, "por", "for")) + " " +
					format_money(int64_t(price));
			if (owns > 0 && int(e[0]) != player) {
				label += " " + GameTexts::format(lender_text(0x69C0E8, "(posees el %i%%)", "(owns %i%%)"), {std::to_string(owns)});
			}
			v.rows.push_back({label, can_buy_shares(save, e)});
			v.codes.push_back(int32_t(k));
		}
		if (v.rows.empty()) {
			dim(lender_text(0x69C0B8, "No hay oferta de acciones por el momento.", "No shares are on offer at the moment."));
			break;
		}
		v.lines.push_back({joined(lender_text(0x69C064, "Las acciones de los siguientes negocios están",
								  "Shares of the following companies are currently"),
							   lender_text(0x69C068, "actualmente en oferta", "being offered for sale")),
			kTextColor});
		clamp_row();
		break;
	}
	default: {  // Sell company shares (0x4D9620; 0x5B66C0 sends 0x2B).
		v.act = lender_text(0x69C00C, "Vender acciones", "Sell shares");
		const std::vector<ShareEntry> list = shares_to_sell(save);
		for (size_t k = 0; k < list.size(); ++k) {
			const ShareEntry& e = list[k];
			uint32_t price, owns;
			std::memcpy(&price, e.data() + 4, 4);
			std::memcpy(&owns, e.data() + 8, 4);
			if (k == 0) {
				v.lines.push_back({lender_text(0x69C088, "Acciones de tu empresa", "Shares of your own company") + ": " +
						std::to_string(int32_t(owns)) + " %",
					kTextColor});
				if (e[1] == 0) {
					dim(one_line(lender_text(0x69C128, "De momento, a nadie le interesa comprar acciones de tu empresa.",
						"Nobody is interested in buying shares of your company at the moment.")));
					continue;
				}
				v.rows.push_back({GameTexts::format(lender_text(0x69C0AC, "%i%% de tu propia empresa", "%i%% of your own company"),
									  {std::to_string(e[1])}) +
						", " + lowered(lender_text(0x69C070, "por", "for")) + " " + format_money(int64_t(price)),
					true});
			} else {
				// A holding: what he owns and what the moneylender would buy of it.
				std::string label = merchant_name(save, e[0]) + " " +
					GameTexts::format(lender_text(0x69C0E8, "(posees el %i%%)", "(owns %i%%)"), {std::to_string(owns)});
				if (e[1] > 0) {
					label += ": " + std::to_string(e[1]) + " %, " + lowered(lender_text(0x69C070, "por", "for")) + " " +
						format_money(int64_t(price));
				} else if (ui.row == int(v.rows.size())) {
					const MerchantMemory& m = save.merchants[e[0] < save.merchants.size() ? e[0] : 0].memory;
					dim(one_line(GameTexts::format(lender_text(0x69C12C, "A nadie le interesa comprar acciones de la empresa de %s %s.",
														   "Nobody is interested in buying shares of the company of %s %s at the moment."),
						{clean_game_text(game_first_name(save, m.u8(0x1C))), clean_game_text(game_family_name(save, m.u8(0x1D)))})));
				}
				v.rows.push_back({label, e[1] > 0});
			}
			v.codes.push_back(int32_t(k));
		}
		if (list.size() > 1) {
			v.lines.push_back({lender_text(0x69C098, "Estaría interesado en las siguientes", "I would be interested in the following shares") +
					" " + lender_text(0x69C09C, "acciones:", "in your possession"),
				kTextColor});
		}
		clamp_row();
		break;
	}
	}
}

void run_operations(SaveGame& save, Sailing* sailing, const std::vector<OperationBytes>& ops);

// What the selected row sends, run at once.
void lender_act(SaveGame& save, Sailing* sailing, int town, PlaceUi& ui, const PlaceView& v) {
	const int player = save.player;
	const int k = v.codes[size_t(ui.row)];
	switch (ui.page) {
	case 0: {
		std::array<LoanTerms, 3> offers{};
		if (loan_offers(save, town, offers) <= k) {
			return;
		}
		const size_t before = loans_owed(save, player).size();
		run_operations(save, sailing, {loan_operation(offers[size_t(k)])});
		if (loans_owed(save, player).size() > before) {
			++ui.lender_debts;
			ui.message = one_line(lender_text(0x69C110, "\"Has recibido el préstamo.\"", "\"You have received the loan.\""));
		} else {
			ui.message = tr("No se ha podido.", "It couldn't be done.");
		}
		break;
	}
	case 1: {
		const uint16_t d = uint16_t(k);
		const uint32_t owed = save.events[d].u32(8);
		const uint8_t where = save.events[d].u8(0x14);
		run_operations(save, sailing, {pay_off_operation(uint32_t(player), d)});
		ui.paid_off = true;
		ui.message = GameTexts::format(lender_text(0x69C0BC, "El préstamo de %i", "The loan of %i"), {format_money(int64_t(owed))}) +
			" " + GameTexts::format(lender_text(0x69C0C0, "ha sido devuelto en %s.", "was paid off in %s."), {window_town_name(save, where)});
		break;
	}
	case 2: {
		std::array<LoanTerms, 3> requests{};
		if (loan_requests(save, town, requests, nullptr) <= k) {
			return;
		}
		const size_t before = loans_granted(save, player).size();
		const OperationBytes op = grant_loan_operation(save, requests[size_t(k)], uint32_t(ui.lender_kind));
		run_operations(save, sailing, {op});
		ui.message = loans_granted(save, player).size() > before
			? tr("Has concedido el préstamo.", "You have granted the loan.")
			: tr("No se ha podido.", "It couldn't be done.");
		ui.row = 0;
		break;
	}
	case 4: {
		const std::vector<ShareEntry> list = shares_on_sale(save);
		if (size_t(k) >= list.size()) {
			return;
		}
		const ShareEntry e = list[size_t(k)];
		run_operations(save, sailing, {shares_operation(e)});
		if (int(e[0]) == player) {
			const std::vector<ShareEntry> mine = shares_to_sell(save);
			uint32_t own = 0;
			if (!mine.empty()) {
				std::memcpy(&own, mine[0].data() + 8, 4);
			}
			const std::string all = own >= 100 ? lender_text(0x69C0DC, "todas", "all") : std::to_string(own) + " %";
			ui.message = one_line(GameTexts::format(lender_text(0x69C0D4, "Acabas de adquirir %i%% acciones de tu propia empresa. Ahora posees %s las acciones.",
														"You have just bought back %i%% shares of your own company. You now own %s shares."),
				{std::to_string(e[1]), all}));
		} else {
			const GameDate date = date_from_tick(next_dividend_tick(save));
			const MerchantMemory& m = save.merchants[e[0]].memory;
			ui.message = one_line(GameTexts::format(lender_text(0x69C0C8,
				"Acabas de comprar %i%% acciones de la empresa de %s %s %s. El siguiente dividendo se pagará el %i de %s de %i.",
				"You have just bought %i%% shares of the company of %s %s %s. The next dividend will be paid on %i. %s %i."),
				{std::to_string(e[1]), rank_name(m.u8(0x39C + m.u8(0x19))), clean_game_text(game_first_name(save, m.u8(0x1C))),
					clean_game_text(game_family_name(save, m.u8(0x1D))), std::to_string(date.day),
					g_texts ? g_texts->month(date.month) : std::to_string(date.month + 1), std::to_string(date.year)}));
		}
		break;
	}
	case 5: {
		const std::vector<ShareEntry> list = shares_to_sell(save);
		if (size_t(k) >= list.size()) {
			return;
		}
		const ShareEntry e = list[size_t(k)];
		const int32_t money = save.merchants[size_t(player)].money();
		run_operations(save, sailing, {shares_operation(e)});
		ui.message = (g_spanish ? "Vendes un " + std::to_string(e[1]) + " % por " : "You sell " + std::to_string(e[1]) + " % for ") +
			format_money(save.merchants[size_t(player)].money() - money) + tr(" monedas.", " coins.");
		ui.row = 0;
		break;
	}
	default:
		break;
	}
}

std::string building_name(int type);
std::string exe_text(uint32_t english, const char* spanish, const char* fallback);

PlaceView place_view(SaveGame& save, int town, PlaceUi& ui) {
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
		// The shipyard's pages (the repair dock only repairs): L and R.
		ui.page = ui.let_type == 0x01 ? (ui.page % kShipyardPages + kShipyardPages) % kShipyardPages : 0;
		if (ui.page != 0) {
			shipyard_page_view(save, town, ui, v);
			break;
		}
		v.act = game_text(GameText::ShipyardRepair, tr("Reparar", "Repair"));
		v.title = place_name(ui.let_type) + " - " + v.act + (ui.let_type == 0x01 ? "   (L/R)" : "");
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
		// The side panel's ship menu (its crew and convoy pages): `ui.ship` is
		// the ship itself. Rows 0 and 1 the crew page's (dismiss sailors, the
		// captain), from 2 on the convoy's.
		if (ui.ship >= save.ships.size() || save.player < 0 || save.ships[ui.ship].owner() != save.player ||
			!save.ships[ui.ship].in_use()) {
			break;
		}
		v.ship = int(ui.ship);
		v.ships = {uint16_t(ui.ship)};
		const SavedShip& ship = save.ships[size_t(v.ship)];
		v.title = ship_label(ship);
		const bool docked = ship_window_open(save, v.ship);
		v.rows = {{capitalized(game_text(GameText::ShipDismissSailors, tr("Despedir marineros", "Dismiss sailors"))),
					  docked && ship.sailors() > 0},
			{capitalized(game_text(GameText::ShipDismissCaptain, tr("Despedir capitán", "Dismiss captain"))),
				can_dismiss_captain(save, v.ship)}};
		v.codes = {0, 0};
		// The convoys (the ship's panel: the convoy window 0x40C170, 0x494DC0),
		// the ship alone selected.
		const uint16_t convoy = ship.convoy();
		if (convoy < save.globals.u16(global::kShips + 0xF6) && convoy < save.convoys.size()) {
			const bool guide = save.convoys[convoy].u16(0x10) == v.ship;
			// The %s: the guide's name (0x494EDE) or the ship's.
			const uint16_t guide_ship = save.convoys[convoy].u16(0x10);
			const std::vector<std::string> ask = text_lines(game_format(guide ? GameText::ConvoyDisbandAsk : GameText::ConvoyLeaveAsk,
				{guide_ship < save.ships.size() && guide ? save.ships[guide_ship].name() : ship.name()},
				guide ? tr("Deshacer convoy\n¿Quieres separar los barcos del convoy?", "Disband convoy\nShould the convoy be disbanded?")
					  : tr("Abandonar convoy\n¿Quieres que tu barco abandone el convoy?", "Leave convoy\nShould the ship leave the convoy?")));
			v.rows.push_back({ask.empty() ? std::string() : ask[0], true});
			v.codes.push_back(guide ? kDisbandConvoy : kLeaveConvoy);
			if (ui.row == 2) {
				for (size_t k = 1; k < ask.size(); ++k) {
					v.lines.push_back({ask[k], kTextColor});
				}
			}
		} else {
			const std::vector<int32_t> choices = convoy_choices(save, {uint16_t(v.ship)});
			for (const int32_t c : choices) {
				std::string label;
				if (c == kPrivateConvoy) {
					label = game_text(GameText::ConvoyFormPrivate, tr("Formar convoy privado", "Form private convoy"));
				} else if (c == kPublicConvoy) {
					label = game_text(GameText::ConvoyFormPublic, tr("Formar convoy público", "Form public convoy"));
				} else {
					const uint16_t guide = save.convoys[size_t(c)].u16(0x10);
					const std::string name = guide < save.ships.size() ? save.ships[guide].name() : std::string("?");
					label = game_format(GameText::ConvoyJoin, {name}, tr("Unirse al convoy ", "Join convoy ") + name);
				}
				v.rows.push_back({label, true});
				v.codes.push_back(c);
			}
			if (choices.empty()) {
				v.rows.push_back({game_text(GameText::ConvoyFormPrivate, tr("Formar convoy privado", "Form private convoy")), false});
				v.codes.push_back(kNoConvoy);
				if (ui.row == 2) {
					const std::vector<std::string> why = text_lines(game_text(GameText::ConvoyCannotForm,
						tr("No es posible formar un convoy\nNo has seleccionado una nave capitana apropiada.",
							"Convoy cannot be formed\nYou have not chosen a suitable convoy leader.")));
					for (const std::string& line : why) {
						v.lines.push_back({line, kDimColor});
					}
				}
			}
		}
		ui.row = std::clamp(ui.row, 0, int(v.rows.size()) - 1);
		if (ui.row == 0 && docked) {
			v.max_amount = ship.sailors();
		}
		if (v.codes[size_t(ui.row)] == kPublicConvoy) {
			// Its destination (0x40CA8E): the towns but this one (and, with a big
			// ship, those that take them).
			const std::vector<uint8_t> towns = public_convoy_towns(save, {uint16_t(v.ship)});
			v.max_amount = std::max(0, int(towns.size()) - 1);
			v.amount_choice = true;
			const int k = std::clamp(ui.amount, 0, v.max_amount);
			v.amount_label = towns.empty() ? "-" : window_town_name(save, towns[size_t(k)]);
			v.lines.push_back({tr("Destino: ", "Destination: ") + v.amount_label, kTextColor});
		}
		break;
	}
	case Place::Lender:
		lender_view(save, town, ui, v);
		break;
	case Place::Building: {
		// The player's building (the game's windows 0x5AF5C0 and 0x5A2xxx, with
		// their two buttons, which the game draws as pictures): demolish it,
		// after the game's question, or auction it with its reserve price.
		const SavedTown& st = save.towns[size_t(town)];
		if (ui.record < 0 || size_t(ui.record) >= st.records.size()) {
			v.title = tr("Edificio", "Building");
			break;
		}
		const BuildingRecord& r = st.records[size_t(ui.record)];
		v.title = building_name(r.type);
		const uint32_t player = uint32_t(save.player);
		v.rows.push_back({tr("Derribar", "Demolish"), can_demolish(save, uint32_t(town), uint32_t(ui.record))});
		v.rows.push_back({tr("Subastar", "Auction"), can_auction(save, uint32_t(town), player, uint32_t(ui.record))});
		if (r.pending != 0) {
			v.lines.push_back({tr("En obras.", "Under construction."), kDimColor});
		}
		const std::string question = ui.row == 0
			? exe_text(0x6A246C, "¿De verdad deseas derribar este edificio?", "Do you really want to demolish this building?")
			: GameTexts::format(exe_text(0x6AF364, "¿Quieres subastar este edificio con un precio de salida de %d?",
									"Do you really want to auction this building with a reserve price of %d?"),
				  {format_money(auction_reserve_price(save, uint32_t(town), uint32_t(ui.record)))});
		v.lines.push_back({question, kTextColor});
		v.act = v.rows[size_t(std::min(ui.row, 1))].first;
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
		panel = {g_town_right - kWidth - 8, kBarHeight + 4, kWidth, kScreenHeight - kBarHeight - 12};
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
		// A long message (the moneylender's) goes up from the footer.
		const std::vector<std::string> parts = wrap_text(text, ui.message, width);
		int at = layout.footer_y - lh * (int(parts.size()) - 1);
		for (const std::string& part : parts) {
			text.draw(part, left, at, kTextColor);
			at += lh;
		}
	}
	if (v.max_amount >= 0) {
		draw_button(renderer, text, layout.minus_button, "-");
		draw_button(renderer, text, layout.plus_button, "+");
		const SDL_Rect box = layout.amount_box();
		draw_button(renderer, text, box, v.amount_choice ? v.amount_label : std::to_string(ui.amount));
	}
	const bool can = ui.row < int(v.rows.size()) && v.rows[size_t(ui.row)].second &&
		(v.max_amount < 0 || v.amount_choice || ui.amount > 0);
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

// The operations a window sends, queued in their order and run at once (as
// the game runs its queue before the next tick).
void run_operations(SaveGame& save, Sailing* sailing, const std::vector<OperationBytes>& ops) {
	for (const OperationBytes& op : ops) {
		if (sailing) {
			queue_operation(save, op);
		} else {
			run_operation(save, nullptr, op.data());
		}
	}
	if (sailing) {
		run_queued_operations(save, *sailing);
	}
}

// The ship's window's convoy rows (the convoy window's choices, or leaving
// and disbanding), with the ship alone selected.
void ship_convoy_act(SaveGame& save, Sailing* sailing, PlaceUi& ui, const PlaceView& v) {
	const int32_t code = v.codes[size_t(ui.row)];
	const uint16_t ship = uint16_t(v.ship);
	std::vector<OperationBytes> ops;
	uint8_t destination = 0;
	if (code == kPrivateConvoy || code == kPublicConvoy) {
		if (code == kPublicConvoy) {
			const std::vector<uint8_t> towns = public_convoy_towns(save, {ship});
			if (towns.empty()) {
				return;
			}
			destination = towns[size_t(std::clamp(ui.amount, 0, int(towns.size()) - 1))];
		}
		ops = create_convoy_operations(save, {ship}, true, code == kPublicConvoy, destination);
	} else if (code >= 0) {
		ops = join_convoy_operations(save, uint32_t(code), {ship});
	} else if (code == kLeaveConvoy || code == kDisbandConvoy) {
		ops = {leave_convoy_operation(save, ship, false)};
	}
	run_operations(save, sailing, ops);
	const uint16_t convoy = save.ships[ship].convoy();
	const bool in_convoy = convoy < save.globals.u16(global::kShips + 0xF6);
	const std::string name = save.ships[ship].name();
	if (code == kPublicConvoy && in_convoy) {
		std::string text;
		for (const std::string& line : text_lines(game_format(GameText::ConvoyPublicLeaving,
				 {convoy_label(save, convoy), "5", window_town_name(save, destination)},
				 tr("El convoy público zarpará en 5 días hacia ", "The public convoy will leave in 5 days for ") +
					 window_town_name(save, destination) + "."))) {
			text += (text.empty() ? "" : " ") + line;
		}
		ui.message = text;
	} else if ((code == kPrivateConvoy || code >= 0) && in_convoy) {
		ui.message = g_spanish ? "El " + name + " va ahora en convoy." : "The " + name + " is in a convoy now.";
	} else if ((code == kLeaveConvoy || code == kDisbandConvoy) && !in_convoy) {
		ui.message = code == kDisbandConvoy ? tr("El convoy se ha deshecho.", "The convoy is disbanded.")
											: (g_spanish ? "El " + name + " ha dejado el convoy." : "The " + name + " has left the convoy.");
	} else {
		ui.message = tr("No se ha podido.", "It couldn't be done.");
	}
	ui.amount = 0;
}

// The shipyard's pages 1 to 4: what the selected row sends.
void shipyard_act(SaveGame& save, Sailing* sailing, int town, PlaceUi& ui, const PlaceView& v) {
	const int32_t code = v.codes[size_t(ui.row)];
	const int32_t money = save.merchants[size_t(save.player)].money();
	switch (ui.page) {
	case 1: {
		const size_t orders = shipyard_orders(save, town).size();
		run_operations(save, sailing, {order_ship_operation(uint32_t(code), uint32_t(save.player), uint32_t(town))});
		ui.message = shipyard_orders(save, town).size() > orders
			? ship_type_name(code) + game_text(GameText::ShipyardOrdered, tr(": contrato de construcción aceptado.", " was ordered."))
			: tr("No se ha podido encargar.", "It couldn't be ordered.");
		break;
	}
	case 2: {
		const size_t orders = shipyard_orders(save, town).size();
		run_operations(save, sailing, {cancel_order_operation(uint32_t(save.player), uint32_t(town), uint32_t(code))});
		ui.message = shipyard_orders(save, town).size() < orders
			? (g_spanish ? "Contrato cancelado: te devuelven " : "Contract cancelled: you get back ") +
				format_money(save.merchants[size_t(save.player)].money() - money) + tr(" monedas.", " coins.")
			: tr("No se ha podido cancelar.", "It couldn't be cancelled.");
		ui.row = 0;
		break;
	}
	case 3: {
		const int level = save.ships[size_t(code)].memory.u8(0xF) + 1 + std::max(0, ui.amount);
		run_operations(save, sailing, {upgrade_operation(uint32_t(code), uint32_t(std::min(level, 2)))});
		ui.message = save.ships[size_t(code)].status() == 7
			? (g_spanish ? "El " + save.ships[size_t(code)].name() + " entra en el astillero."
						 : "The " + save.ships[size_t(code)].name() + " goes into the shipyard.")
			: tr("No se ha podido mejorar.", "It couldn't be upgraded.");
		ui.amount = 0;
		ui.row = 0;
		break;
	}
	default: {
		std::string name = ui.name;
		if (name.empty() && !save.ship_names.empty()) {
			name = save.ship_names[size_t(std::clamp(ui.amount, 0, int(save.ship_names.size()) - 1))];
		}
		run_operations(save, sailing, rename_operations(uint32_t(code), name));
		ui.message = (g_spanish ? "Ahora se llama " : "Now it is called ") + save.ships[size_t(code)].name() + ".";
		ui.name.clear();
		break;
	}
	}
}

// X or the act button: what the selected row sends, as the game's window
// builds it, run at once (the queue, as the market does).
void place_act(SaveGame& save, Sailing* sailing, int town, PlaceUi& ui, const PlaceView& v) {
	ui.message_until = app_ticks() + kMessageTime;
	if (ui.row >= int(v.rows.size()) || !v.rows[size_t(ui.row)].second ||
		(v.max_amount >= 0 && !v.amount_choice && ui.amount <= 0)) {
		ui.message = tr("Ahora no se puede.", "It can't be done now.");
		return;
	}
	if (ui.open == Place::Ship && ui.row >= 2) {
		ship_convoy_act(save, sailing, ui, v);
		return;
	}
	if (ui.open == Place::Shipyard && ui.page != 0) {
		shipyard_act(save, sailing, town, ui, v);
		return;
	}
	if (ui.open == Place::Lender) {
		lender_act(save, sailing, town, ui, v);
		return;
	}
	if (ui.open == Place::Building) {
		// 0x5AFD80 after «Yes»: 0x26 or 0x28 with the building's cell; the window closes.
		const BuildingRecord& r = save.towns[size_t(town)].records[size_t(ui.record)];
		const uint32_t player = uint32_t(save.player);
		run_operations(save, sailing,
			{ui.row == 0 ? demolish_operation(uint32_t(town), player, r.x, r.y)
						 : auction_operation(uint32_t(town), player, r.x, r.y)});
		ui.open = Place::None;
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

// --- The side panel: the game's, on the right of the town (scripts/
// screenGame.ini, sidemenu.ini and textures.ini of the CPR). The user's
// decision (2026-11-05): fixed and open by default as in the PC game, where it
// takes about a third of the screen too, foldable with a tab on its border,
// with bigger buttons for the finger. From the top: the town's minimap, the
// game's row of icons and its four ship buttons on their plank, and the room
// of its menus (SIDEMENU_*). ---

constexpr int kPanelBar = 24;  // Its left border (HauptscreenC800.bmp, 24 · 558).
constexpr int kPanelInner = 260;  // Its inside, as wide as the game's.
constexpr int kPanelWidth = kPanelBar + kPanelInner;
constexpr int kPanelIconSize = 40;  // The game's icons are 32 · 32.
constexpr int kPanelMapHeight = 150;
constexpr int kPanelPlankHeight = 92;

// The game's row of icons in the town (as in its screenshots): the ship list,
// fast forward, the messages, building, leaving the town and the options;
// their column in menu01.tga (its rows: normal, pressed, lit, disabled).
enum class PanelIcon { ShipList, FastForward, Messages, Build, LeaveTown, Options, Count };
constexpr int kPanelIcons = int(PanelIcon::Count);
constexpr int kPanelIconColumn[kPanelIcons] = {7, 4, 2, 0, 6, 5};
// Its four ship buttons (b_schiffe_anker.tga, b_schiffe_see.tga,
// b_konvoi_anker.tga, b_konvoi_see.tga: normal, pressed, lit): the player's
// ships in port, at sea, his convoys in port and at sea.
constexpr int kPanelShipButtons = 4;
constexpr const char* kPanelShipFiles[kPanelShipButtons] = {"images/b_schiffe_anker.tga", "images/b_schiffe_see.tga",
	"images/b_konvoi_anker.tga", "images/b_konvoi_see.tga"};

// What the menus' room shows.
enum class PanelMenu { None, Build, ShipList, Ship, Messages, Statistics };

// The ship menu's pages (SIDEMENU_SHIP: its buttons in the game's order, the
// convoy's showing the convoy's ships when the ship is in one, as the game's
// «show convoy» takes the place of «form convoy»).
enum class ShipPage { Goods, Crew, Deck, Convoy, LeaveConvoy, TradeRoute, Count };
constexpr int kShipPages = int(ShipPage::Count);

struct SidePanel {
	bool folded = false;
	PanelMenu menu = PanelMenu::None;
	// With the buttons (Select): the icon or ship button they point at
	// (0..5 the icons, 6..9 the ship buttons), -1 when the panel isn't theirs.
	int focus = -1;
	// The ship buttons go through their lists: the last one chosen of each.
	int ship_pick[kPanelShipButtons] = {-1, -1, -1, -1};
	// The build menu (SIDEMENU_BUILD): its category ([+0x104C]), the row the
	// buttons point at, the building chosen (its page, [+0x1070]; -1: the
	// menu), the picture the window asked the town view for, and the cell
	// where it goes (-1: none chosen yet).
	int build_category = 0;
	int build_row = 0;
	int build_type = -1;
	BuildGhost build_ghost{};
	int cell_x = -1;
	int cell_y = -1;
	// The marks of where it fits (build_marks), for that type, town and
	// buildings.
	std::vector<uint8_t> marks;
	int marks_type = -1;
	int marks_town = -1;
	uint64_t marks_key = 0;
	// The ship list: the row chosen.
	int list_row = 0;
	// The ship menu (SIDEMENU_SHIP): the ship it shows, its page, the first
	// cargo row shown, and its crew and convoy pages' state (as the flat
	// windows': open = Place::Ship, ship = the ship, the row, the amount, the
	// message).
	int ship = -1;
	ShipPage ship_page = ShipPage::Goods;
	int cargo_first = 0;
	PlaceUi ship_ui;
	// The statistics (SIDEMENU_STATISTIK and its parchment, statistics_window.hpp), opened by the top bar's money:
	// the page ([+0xB0], 2 at first), the entry the parchment shows ([+0xBE]: the balance when it opens), the
	// balance's period ([+0xA4]), the graphs' ([+0xC0]) and Suministro's town ([+0x2440]; -1: his home town).
	int stats_page = 2;
	int stats_entry = kStatisticsBalance;
	int stats_balance_period = 0;
	int stats_graph_period = 0;
	int stats_town = -1;
};

constexpr int kShipListRows = 6;  // Two lines each in the room.

// The panel's pictures, read once from the CPR (the same in both languages).
struct PanelArt {
	SDL_Texture* icons = nullptr;
	SDL_Texture* bar = nullptr;
	SDL_Texture* plank = nullptr;
	SDL_Texture* wood = nullptr;
	SDL_Texture* parchment = nullptr;
	SDL_Texture* build_tabs = nullptr;  // page_baumenue.tga: the build menu's five categories.
	SDL_Texture* stats_tabs = nullptr;  // page_statistiken.tga: the statistics' four pages (TEX20034).
	SDL_Texture* ships[kPanelShipButtons] = {};
	int ship_width[kPanelShipButtons] = {};  // Of each frame (three side by side).
	int ship_height[kPanelShipButtons] = {};
	// The ship menu's ([SIDEMENUSHIP0] of sidemenu.ini, textures.ini): its
	// page buttons (schiffinfo0001.tga: 7 columns, 4 rows), the morale's faces
	// and the wage's coins (moral_heuer.tga), the barrel of its hold and its
	// mask (fass0002/0003), the captain's skills (bonus.tga), the deck
	// (schiffsdeck.tga), each type's faded ship (backpic_*.bmp) and the list
	// icons of frames_listen/.
	SDL_Texture* ship_buttons = nullptr;
	SDL_Texture* morale = nullptr;
	SDL_Texture* barrel = nullptr;
	SDL_Texture* barrel_mask = nullptr;
	SDL_Texture* bonus = nullptr;
	SDL_Texture* deck = nullptr;
	SDL_Texture* backpics[4] = {};
	enum Icon { Anchor, Sailing, Sunk, Repair, Knots, Hull, Crew, Captain, Weapons, BigBall, SmallBall, Barrel, Bale,
		HandWeapons, Offered, Auction, Upgrade, Fleet, IconCount };
	SDL_Texture* list_icons[IconCount] = {};
	int icon_width[IconCount] = {};
	int icon_height[IconCount] = {};
	// The messages' (SIDEMENU_INFOS): their buttons (briefe0001.tga, TEX20018: 32 · 28, three a row) and the
	// letter's parchment (dialogparchment/Papier.tga, TEX16001: its rolls, edges and middle) with its seal.
	SDL_Texture* letters = nullptr;
	SDL_Texture* paper = nullptr;
	SDL_Texture* seal = nullptr;
	// The game's top bar (screenGame.ini ObenLinks / ObenRechts: HauptscreenA1280.bmp and HauptscreenB800.bmp),
	// its inhabitants' and buildings' icons (einwohner_haus_TP.tga, TEX16033: 36 · 28, the man and the house),
	// the money's coins (frames_konto/konto0001.tga, the first frame of its button) and the hourglass, the
	// chronicle's button, with its 32 frames (frames_sanduhr/sanduhr00*.tga).
	SDL_Texture* bar_left = nullptr;
	SDL_Texture* bar_right = nullptr;
	SDL_Texture* people = nullptr;
	SDL_Texture* coins = nullptr;
	static constexpr int kHourglassFrames = 32;
	SDL_Texture* hourglass[kHourglassFrames] = {};

	PanelArt(SDL_Renderer* renderer, const CprArchive& archive) {
		icons = load_texture(renderer, archive, "images/menu01.tga");
		bar = load_texture(renderer, archive, "images/HauptscreenC800.bmp");
		plank = load_texture(renderer, archive, "images/HauptscreenD800.bmp");
		wood = load_texture(renderer, archive, "images/sidemenu/HauptscreenE.bmp");
		parchment = load_texture(renderer, archive, "images/sidemenu/Pergamentrolle.bmp");
		build_tabs = load_texture(renderer, archive, "images/page_baumenue.tga");
		stats_tabs = load_texture(renderer, archive, "images/page_statistiken.tga");
		for (int k = 0; k < kPanelShipButtons; ++k) {
			ships[k] = load_texture(renderer, archive, kPanelShipFiles[k], &ship_width[k], &ship_height[k]);
			ship_width[k] /= 3;
		}
		ship_buttons = load_texture(renderer, archive, "images/sidemenu/schiffinfo0001.tga");
		morale = load_texture(renderer, archive, "images/sidemenu/moral_heuer.tga");
		barrel = load_texture(renderer, archive, "images/sidemenu/fass0002.tga");
		barrel_mask = load_texture(renderer, archive, "images/sidemenu/fass0003.tga");
		bonus = load_texture(renderer, archive, "images/sidemenu/bonus.tga");
		deck = load_texture(renderer, archive, "images/sidemenu/schiffsdeck.tga");
		// By type (snaikka, craier, cog, hulk: schnigge, kraier, kogge, holk).
		static constexpr const char* kBackpics[4] = {"images/sidemenu/backpic_schnigge01.bmp",
			"images/sidemenu/backpic_kraier01.bmp", "images/sidemenu/backpic_kogge01.bmp",
			"images/sidemenu/backpic_holk01.bmp"};
		for (int k = 0; k < 4; ++k) {
			backpics[k] = load_texture(renderer, archive, kBackpics[k]);
			if (backpics[k]) {
				SDL_SetTextureBlendMode(backpics[k], SDL_BLENDMODE_MOD);  // White leaves the parchment as it is.
			}
		}
		static constexpr const char* kIcons[IconCount] = {"anker0001.tga", "aufsee0001.tga", "schiff_weg0001.tga",
			"reparatur.tga", "knoten0001.tga", "schiffszustand0001.tga", "crew0002.tga", "kaptain0001.tga",
			"bewaffnung0002.tga", "kanonenkugel0001.tga", "kanonenkugel0002.tga", "fass0001.tga", "ballen0001.tga",
			"waffen.tga", "angeboten.tga", "gilde.tga", "ausbau.tga", "orlogg0001.tga"};
		for (int k = 0; k < IconCount; ++k) {
			list_icons[k] = load_texture(renderer, archive, (std::string("images/frames_listen/") + kIcons[k]).c_str(),
				&icon_width[k], &icon_height[k]);
		}
		letters = load_texture(renderer, archive, "images/briefe0001.tga");
		paper = load_texture(renderer, archive, "images/dialogparchment/Papier.tga");
		seal = load_texture(renderer, archive, "images/dialogparchment/Siegel_mit_Kogge.tga");
		bar_left = load_texture(renderer, archive, "images/HauptscreenA1280.bmp");
		bar_right = load_texture(renderer, archive, "images/HauptscreenB800.bmp");
		people = load_texture(renderer, archive, "images/einwohner_haus_TP.tga");
		coins = load_texture(renderer, archive, "images/frames_konto/konto0001.tga");
		for (int k = 0; k < kHourglassFrames; ++k) {
			char path[64];
			std::snprintf(path, sizeof(path), "images/frames_sanduhr/sanduhr00%02d.tga", k + 1);
			hourglass[k] = load_texture(renderer, archive, path);
		}
	}
	~PanelArt() {
		for (SDL_Texture* t : {icons, bar, plank, wood, parchment, build_tabs, ships[0], ships[1], ships[2], ships[3],
				 ship_buttons, morale, barrel, barrel_mask, bonus, deck, backpics[0], backpics[1], backpics[2], backpics[3],
				 letters, paper, seal, bar_left, bar_right, people, coins}) {
			if (t) {
				SDL_DestroyTexture(t);
			}
		}
		for (SDL_Texture* t : hourglass) {
			if (t) {
				SDL_DestroyTexture(t);
			}
		}
		for (SDL_Texture* t : list_icons) {
			if (t) {
				SDL_DestroyTexture(t);
			}
		}
	}
	// A list icon at (x, y) with its own size; its width (0 without it).
	int icon(SDL_Renderer* renderer, Icon k, int x, int y) const {
		if (!list_icons[k]) {
			return 0;
		}
		const SDL_Rect dst = {x, y, icon_width[k], icon_height[k]};
		SDL_RenderCopy(renderer, list_icons[k], nullptr, &dst);
		return icon_width[k];
	}
	PanelArt(const PanelArt&) = delete;
	PanelArt& operator=(const PanelArt&) = delete;
};

// Where everything of the panel is on the screen, below the top bar.
struct PanelLayout {
	SDL_Rect panel{};  // Its border and inside.
	SDL_Rect bar{};  // The border.
	SDL_Rect tab{};  // Folding and unfolding it.
	SDL_Rect map{};  // The minimap.
	SDL_Rect plank{};
	SDL_Rect icons[kPanelIcons] = {};
	SDL_Rect ships[kPanelShipButtons] = {};
	SDL_Rect room{};  // The menus'.
	int town_right = kScreenWidth;  // The town's part of the screen ends here.

	explicit PanelLayout(bool folded) {
		const int top = kBarHeight;
		const int x = kScreenWidth - (folded ? kPanelBar : kPanelWidth);
		panel = {x, top, kScreenWidth - x, kScreenHeight - top};
		bar = {x, top, kPanelBar, kScreenHeight - top};
		tab = {x + 1, top + 56, kPanelBar - 2, 72};
		town_right = x;
		const int inside = x + kPanelBar;
		map = {inside, top, kPanelInner, kPanelMapHeight};
		plank = {inside, top + kPanelMapHeight, kPanelInner, kPanelPlankHeight};
		for (int k = 0; k < kPanelIcons; ++k) {
			icons[k] = {inside + 3 + k * (kPanelIconSize + 3), plank.y + 5, kPanelIconSize, kPanelIconSize};
		}
		// The ship buttons a third bigger than the game's (40 · 27 and 50 · 27).
		const int widths[kPanelShipButtons] = {53, 53, 67, 67};
		int at = inside + 4;
		for (int k = 0; k < kPanelShipButtons; ++k) {
			ships[k] = {at, plank.y + 51, widths[k], 36};
			at += widths[k] + 4;
		}
		room = {inside, plank.y + plank.h, kPanelInner, kScreenHeight - (plank.y + plank.h)};
	}

	int icon_at(int x, int y) const {
		for (int k = 0; k < kPanelIcons; ++k) {
			if (inside(icons[k], x, y)) {
				return k;
			}
		}
		return -1;
	}
	int ship_at(int x, int y) const {
		for (int k = 0; k < kPanelShipButtons; ++k) {
			if (inside(ships[k], x, y)) {
				return k;
			}
		}
		return -1;
	}
};

// The ships each ship button goes through: the player's ships out of a
// convoy in port (docked, waiting, at the shipyard) or at sea (leaving,
// arriving, sailing), and the guides of his convoys in port or at sea.
std::vector<int> panel_ships(const SaveGame& save, int button) {
	std::vector<int> out;
	const uint16_t convoys = save.globals.u16(global::kShips + 0xF6);
	for (size_t i = 0; i < save.ships.size(); ++i) {
		const SavedShip& ship = save.ships[i];
		if (!ship.in_use() || save.player < 0 || ship.owner() != save.player || ship.status() == kShipSunk ||
			ship.status() == kShipPirate) {
			continue;
		}
		const uint16_t convoy = ship.convoy();
		const bool in_convoy = convoy < convoys && convoy < save.convoys.size();
		if (in_convoy && save.convoys[convoy].u16(0x10) != i) {
			continue;  // A convoy goes by its guide.
		}
		const uint16_t s = ship.status();
		const bool at_sea = s == kShipLeaving || s == kShipArriving || s == kShipSailing || s == 0x10;
		if (button == (in_convoy ? 2 : 0) + (at_sea ? 1 : 0)) {
			out.push_back(int(i));
		}
	}
	return out;
}

// A texture drawn stretched to `dst` (all of it, or `src`).
void draw_picture(SDL_Renderer* renderer, SDL_Texture* texture, const SDL_Rect& dst, const SDL_Rect* src = nullptr,
	Uint8 alpha = 255) {
	if (texture) {
		SDL_SetTextureAlphaMod(texture, alpha);
		SDL_RenderCopy(renderer, texture, src, &dst);
		SDL_SetTextureAlphaMod(texture, 255);
	} else {
		SDL_SetRenderDrawColor(renderer, 120, 85, 45, alpha);
		SDL_RenderFillRect(renderer, &dst);
	}
}

// What the icons can do now (the disabled ones are drawn with the game's
// grey row and don't react).
struct PanelState {
	bool icon_enabled[kPanelIcons] = {true, true, true, true, true, true};
	int ship_counts[kPanelShipButtons] = {};
};

PanelState panel_state(const SaveGame& save, bool at_sea = false) {
	PanelState s;
	for (int k = 0; k < kPanelShipButtons; ++k) {
		s.ship_counts[k] = int(panel_ships(save, k).size());
	}
	s.icon_enabled[int(PanelIcon::ShipList)] = !player_fleet(save).empty();
	s.icon_enabled[int(PanelIcon::Build)] = !at_sea;  // Building is in the towns.
	// «Leave town» (screenGame.ini's BUTTON11): disabled on the sea map, as the game's (seen in the PC game).
	s.icon_enabled[int(PanelIcon::LeaveTown)] = !at_sea;
	return s;
}

// Where the minimap shows the town, in the screen (as draw_side_panel draws
// it), and the screen's pixels per town pixel.
bool minimap_place(const PanelLayout& layout, const TownView& view, SDL_Rect& dst, double& scale) {
	if (!view.minimap() || view.map_columns() <= 0) {
		return false;
	}
	const double sx = double(layout.map.w) / (2 * view.map_columns() + 1);
	const double sy = sx * double(kRowStep) / double(kTileWidth / 2);
	const int h = std::min(layout.map.h, int(view.map_rows() * sy + 0.5));
	dst = {layout.map.x, layout.map.y + (layout.map.h - h) / 2, layout.map.w, h};
	scale = sx / double(kTileWidth / 2);
	return true;
}

// On the sea map the minimap is the sea's (the game's minimap of its navigation chart): the whole map fitted
// to the minimap's height, in its middle, and the screen's pixels per map pixel.
bool sea_minimap_place(const PanelLayout& layout, const SeaMapView& sea, SDL_Rect& dst, double& scale) {
	if (!sea.texture() || sea.width() <= 0 || sea.height() <= 0) {
		return false;
	}
	scale = std::min(double(layout.map.w) / sea.width(), double(layout.map.h) / sea.height());
	const int w = int(sea.width() * scale);
	const int h = int(sea.height() * scale);
	dst = {layout.map.x + (layout.map.w - w) / 2, layout.map.y + (layout.map.h - h) / 2, w, h};
	return true;
}

// The sea map's pixel under a point of its minimap.
bool minimap_sea_point(const PanelLayout& layout, const SeaMapView& sea, int x, int y, SDL_Point& out) {
	SDL_Rect dst;
	double scale = 0;
	if (!sea_minimap_place(layout, sea, dst, scale) || !inside(layout.map, x, y)) {
		return false;
	}
	out = {int((x - dst.x) / scale), int((y - dst.y) / scale)};
	return true;
}

// The minimap of the sea (as the manual's legend: the player's ships and convoys white, the others' grey,
// the pirates black; the towns blue with an office of his, red without), with the part on screen framed.
void draw_sea_minimap(SDL_Renderer* renderer, const PanelLayout& layout, const SeaMapView& sea, const SaveGame& save,
	const SDL_Rect& camera) {
	SDL_Rect dst;
	double px = 0;
	if (!sea_minimap_place(layout, sea, dst, px)) {
		return;
	}
	SDL_RenderCopy(renderer, sea.texture(), nullptr, &dst);
	auto at = [&](int x, int y) { return SDL_Point{dst.x + int(x * px), dst.y + int(y * px)}; };
	for (size_t i = 0; i < save.towns.size(); ++i) {
		bool office = false;
		for (const SavedOffice& o : save.offices) {
			office |= o.used() && o.owner() == save.player && o.town() == i;
		}
		const SDL_Point c = sea.town_center(save, i);
		const SDL_Point p = at(c.x, c.y);
		SDL_SetRenderDrawColor(renderer, office ? 40 : 200, office ? 90 : 30, office ? 230 : 20, 255);
		for (int k = 0; k <= 3; ++k) {
			SDL_RenderDrawLine(renderer, p.x - 3 + k, p.y - k, p.x + 3 - k, p.y - k);
			SDL_RenderDrawLine(renderer, p.x - 3 + k, p.y + k, p.x + 3 - k, p.y + k);
		}
	}
	for (const SavedShip& s : save.ships) {
		if (!s.in_use() || (s.status() != kShipSailing && s.status() != kShipPirate)) {
			continue;
		}
		const SDL_Point p = at(s.x() >> 16, s.y() >> 16);
		const bool mine = s.owner() == save.player;
		const bool pirate = s.status() == kShipPirate;
		SDL_SetRenderDrawColor(renderer, mine ? 255 : pirate ? 0 : 150, mine ? 255 : pirate ? 0 : 150,
			mine ? 255 : pirate ? 0 : 150, 255);
		const SDL_Rect r = mine || pirate ? SDL_Rect{p.x - 1, p.y - 1, 3, 3} : SDL_Rect{p.x, p.y, 2, 2};
		SDL_RenderFillRect(renderer, &r);
	}
	const SDL_Rect frame = {dst.x + int(camera.x * px), dst.y + int(camera.y * px), std::max(2, int(camera.w * px)),
		std::max(2, int(camera.h * px))};
	SDL_SetRenderDrawColor(renderer, 255, 255, 255, 230);
	SDL_RenderDrawRect(renderer, &frame);
}

// The town's pixel under a point of the minimap.
bool minimap_town_point(const PanelLayout& layout, const TownView& view, int x, int y, SDL_Point& out) {
	SDL_Rect dst;
	double scale = 0;
	if (!minimap_place(layout, view, dst, scale) || !inside(layout.map, x, y)) {
		return false;
	}
	out = {int((x - dst.x) / scale), int((y - dst.y) / scale)};
	return true;
}

// The panel. `camera` is the town's part of the screen in the town's pixels
// (its frame on the minimap).
void draw_side_panel(SDL_Renderer* renderer, TextRenderer& text, const PanelArt& art, const SidePanel& ui,
	const PanelState& state, const TownView* view, const SDL_Rect& camera, const SeaMapView* sea = nullptr,
	const SaveGame* save = nullptr) {
	const PanelLayout layout(ui.folded);
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	// The border, cut to the screen's height, and its tab: an arrow out
	// (fold) or in (unfold).
	SDL_Rect bar_src = {0, 0, kPanelBar, layout.bar.h};
	draw_picture(renderer, art.bar, layout.bar, &bar_src);
	SDL_SetRenderDrawColor(renderer, 60, 38, 18, 230);
	SDL_RenderFillRect(renderer, &layout.tab);
	SDL_SetRenderDrawColor(renderer, 190, 150, 90, 255);
	SDL_RenderDrawRect(renderer, &layout.tab);
	const int cx = layout.tab.x + layout.tab.w / 2;
	const int cy = layout.tab.y + layout.tab.h / 2;
	for (int k = 0; k < 7; ++k) {
		const int dx = ui.folded ? 3 - k / 2 : -3 + k / 2;  // ‹ to unfold, › to fold.
		SDL_RenderDrawLine(renderer, cx + dx, cy - 6 + k, cx + dx, cy + 6 - k);
	}
	if (ui.folded) {
		return;
	}
	// The minimap: the town's cells, fitted to the panel's width, with the part
	// on the screen framed.
	SDL_SetRenderDrawColor(renderer, 20, 14, 8, 255);
	SDL_RenderFillRect(renderer, &layout.map);
	SDL_Rect dst;
	double px = 0;  // Minimap pixels per town pixel.
	if (sea && save) {
		draw_sea_minimap(renderer, layout, *sea, *save, camera);
	} else if (view && minimap_place(layout, *view, dst, px)) {
		SDL_RenderCopy(renderer, view->minimap(), nullptr, &dst);
		if (ui.menu == PanelMenu::Build && ui.build_type >= 0 && !ui.marks.empty()) {
			// Where the chosen building fits, also on the minimap (to find it).
			const double sx = double(dst.w) / (2 * view->map_columns() + 1);
			const double sy = double(dst.h) / view->map_rows();
			std::vector<SDL_Rect> dots;
			for (size_t i = 0; i < ui.marks.size(); ++i) {
				if (ui.marks[i]) {
					const int x = int(i % size_t(view->map_columns()));
					const int y = int(i / size_t(view->map_columns()));
					dots.push_back({dst.x + int((2 * x + (y & 1)) * sx), dst.y + int(y * sy), std::max(1, int(2 * sx)),
						std::max(1, int(sy + 0.5))});
				}
			}
			SDL_SetRenderDrawColor(renderer, 80, 255, 80, 255);
			SDL_RenderFillRects(renderer, dots.data(), int(dots.size()));
		}
		const SDL_Rect frame = {dst.x + int(camera.x * px), dst.y + int(camera.y * px), std::max(2, int(camera.w * px)),
			std::max(2, int(camera.h * px))};
		SDL_SetRenderDrawColor(renderer, 255, 255, 255, 230);
		SDL_RenderDrawRect(renderer, &frame);
	}
	// The plank, its icons and its ship buttons.
	draw_picture(renderer, art.plank, layout.plank);
	for (int k = 0; k < kPanelIcons; ++k) {
		const bool lit = ui.focus == k || (k == int(PanelIcon::Build) && ui.menu == PanelMenu::Build) ||
			(k == int(PanelIcon::ShipList) && ui.menu == PanelMenu::ShipList) ||
			(k == int(PanelIcon::Messages) && ui.menu == PanelMenu::Messages);
		const int row = !state.icon_enabled[k] ? 3 : lit ? 2 : 0;
		const SDL_Rect src = {kPanelIconColumn[k] * 32, row * 32, 32, 32};
		draw_picture(renderer, art.icons, layout.icons[k], &src);
	}
	for (int k = 0; k < kPanelShipButtons; ++k) {
		const bool any = state.ship_counts[k] > 0;
		const int frame = ui.focus == kPanelIcons + k ? 2 : 0;
		const SDL_Rect src = {frame * art.ship_width[k], 0, art.ship_width[k], art.ship_height[k]};
		draw_picture(renderer, art.ships[k], layout.ships[k], &src, any ? 255 : 80);
		if (any) {
			const std::string n = std::to_string(state.ship_counts[k]);
			text.draw(n, layout.ships[k].x + layout.ships[k].w - text.measure(n) - 1,
				layout.ships[k].y + layout.ships[k].h - text.line_height() + 2, kTextColor);
		}
	}
	// With the buttons on the panel, a frame round the item they point at
	// (the game's lit row is faint on the Vita's screen).
	if (ui.focus >= 0) {
		const SDL_Rect r = ui.focus < kPanelIcons ? layout.icons[ui.focus] : layout.ships[ui.focus - kPanelIcons];
		const SDL_Rect frame = {r.x - 2, r.y - 2, r.w + 4, r.h + 4};
		SDL_SetRenderDrawColor(renderer, 255, 236, 140, 255);
		SDL_RenderDrawRect(renderer, &frame);
	}
	// The menus' room: the game's wood when no menu is open, its parchment
	// under a menu.
	draw_picture(renderer, ui.menu == PanelMenu::None ? art.wood : art.parchment, layout.room);
}

// The top bar's hourglass, the chronicle's button (as the game's, at the bar's right end).
constexpr SDL_Rect kHourglassButton = {kScreenWidth - 38, 1, 34, 28};
// The top bar's money, the statistics' button (screenGame.ini's, ToolTip 20045): where the bar drew it last.
SDL_Rect g_money_button{};

// The game's top bar (screenGame.ini; the manual's «Barra de información»): its wood (HauptscreenA1280 and
// B800, squeezed to the viewer's bar), from the left the inhabitants (the man) with their number, the town
// (on the sea map, the one chosen), the money (the coins) and, at the right end, the date and the hourglass
// that opens the chronicle, turning while the time runs. The viewer's button hints go in what is left.
void draw_top_bar(SDL_Renderer* renderer, TextRenderer& text, const PanelArt& art, const std::string& citizens,
	const std::string& place, const std::string& money, const std::string& date, int hourglass_frame,
	const std::vector<std::string>& hints) {
	// The right piece (its carved post on its left) as wide as the date and the hourglass need.
	const int right_w = std::max(kScreenWidth * 284 / 1280, kScreenWidth - (kHourglassButton.x - 4 - text.measure(date)) + 26);
	const SDL_Rect a = {0, 0, kScreenWidth - right_w, kBarHeight};
	const SDL_Rect b = {a.w, 0, right_w, kBarHeight};
	draw_picture(renderer, art.bar_left, a);
	draw_picture(renderer, art.bar_right, b);
	const SDL_Color ink = {35, 20, 5, 255};
	const int y = (kBarHeight * 30 / 42 - text.line_height()) / 2 + 1;  // On the wood, above its carved edge.
	int x = 6;
	if (!citizens.empty()) {
		const SDL_Rect src = {0, 0, 36, 28};
		draw_picture(renderer, art.people, {x, 1, 30, 23}, &src);
		x += 32;
		text.draw(citizens, x, y, ink);
		x += text.measure(citizens) + 14;
	}
	text.draw(place, x, y, ink);
	x += text.measure(place) + 14;
	g_money_button = {};
	if (!money.empty()) {
		g_money_button = {x - 4, 0, 20 + text.measure(money) + 8, kBarHeight};
		draw_picture(renderer, art.coins, {x, 0, 18, 28});
		x += 20;
		text.draw(money, x, y, ink);
		x += text.measure(money) + 14;
	}
	SDL_Texture* glass = art.hourglass[((hourglass_frame % PanelArt::kHourglassFrames) + PanelArt::kHourglassFrames) %
		PanelArt::kHourglassFrames];
	draw_picture(renderer, glass, kHourglassButton);
	const int date_x = kHourglassButton.x - 4 - text.measure(date);
	text.draw(date, date_x, y, ink);
	for (const std::string& hint : hints) {
		if (x + text.measure(hint) <= date_x - 14) {
			text.draw(hint, date_x - 14 - text.measure(hint), y, SDL_Color{90, 60, 30, 255});
			break;
		}
	}
}

// The ship list's rows in the room (two lines each), from the first shown.
SDL_Rect ship_list_row(const SDL_Rect& room, int line_height, int k) {
	const int h = 2 * line_height + 4;
	return {room.x + 8, room.y + 8 + k * h, room.w - 16, h};
}

// The ship list (the game's «Schiffsliste» icon): the player's ships, with
// their type and what they are doing; the chosen row is lit.
void draw_ship_list(SDL_Renderer* renderer, TextRenderer& text, const SaveGame& save,
	const std::vector<std::string>& names, const SidePanel& ui) {
	const PanelLayout panel(ui.folded);
	const std::vector<int> fleet = player_fleet(save);
	const int first = std::max(0, ui.list_row - kShipListRows + 1);
	const SDL_Color ink = {40, 25, 10, 255};
	const SDL_Color dim = {110, 85, 60, 255};
	for (int k = 0; k < kShipListRows && first + k < int(fleet.size()); ++k) {
		const SDL_Rect r = ship_list_row(panel.room, text.line_height(), k);
		if (first + k == ui.list_row) {
			SDL_SetRenderDrawColor(renderer, 120, 80, 30, 70);
			SDL_RenderFillRect(renderer, &r);
		}
		const SavedShip& ship = save.ships[size_t(fleet[size_t(first + k)])];
		text.draw(ship_label(ship), r.x + 4, r.y + 2, ink);
		std::string activity = ship_activity(save, names, ship);
		while (!activity.empty() && text.measure(activity) > r.w - 8) {
			// Cut to the room, a whole UTF-8 character at a time.
			while (!activity.empty() && (uint8_t(activity.back()) & 0xC0) == 0x80) {
				activity.pop_back();
			}
			if (!activity.empty()) {
				activity.pop_back();
			}
		}
		text.draw(activity, r.x + 4, r.y + 2 + text.line_height(), dim);
	}
}

// --- The build menu in the panel's room (the game's SIDEMENU_BUILD, read in
// player_actions.hpp): its five categories, the buildings of one, and the
// page of the building chosen with its costs; a building placed in the town
// is drawn where it goes, green or red as it fits, over green marks where it
// fits. ---

// A text of the game by its address in the English exe, cleaned (the
// moneylender's helper), or the viewer's own words without the exe.
std::string exe_text(uint32_t english, const char* spanish, const char* fallback) {
	return lender_text(english, spanish, fallback);
}

std::string building_name(int type) {
	if (type == 0) {
		return exe_text(0x6C23CC, "Oficina comercial", "trading office");
	}
	const std::string name = exe_text(0x6A57C8 + 4 * uint32_t(type), "", "");
	return name.empty() ? tr("Edificio ", "Building ") + std::to_string(type) : name;
}

constexpr int kBuildRows = 8;

struct BuildRoomLayout {
	SDL_Rect tabs[5] = {};
	int title_y = 0;
	SDL_Rect rows[kBuildRows] = {};
	SDL_Rect back_button{};
	SDL_Rect act_button{};

	BuildRoomLayout(const SDL_Rect& room, int line_height) {
		for (int k = 0; k < 5; ++k) {
			tabs[k] = {room.x + 7 + k * 50, room.y + 6, 44, 40};
		}
		title_y = room.y + 50;
		const int row_height = line_height + 3;
		for (int k = 0; k < kBuildRows; ++k) {
			rows[k] = {room.x + 8, title_y + line_height + 4 + k * row_height, room.w - 16, row_height};
		}
		const int button_height = line_height + 8;
		back_button = {room.x + 8, room.y + room.h - button_height - 6, 84, button_height};
		act_button = {room.x + 98, room.y + room.h - button_height - 6, room.w - 106, button_height};
	}

	int tab_at(int x, int y) const {
		for (int k = 0; k < 5; ++k) {
			if (inside(tabs[k], x, y)) {
				return k;
			}
		}
		return -1;
	}
	int row_at(int x, int y) const {
		for (int k = 0; k < kBuildRows; ++k) {
			if (inside(rows[k], x, y)) {
				return k;
			}
		}
		return -1;
	}
};

// Why the chosen building can't be built, as the window says it (0x479C94..
// 0x479D84, from 0x51F3A0's [+0x40..+0x43]); empty if it can.
std::string build_reason(const BuildChoice& c) {
	switch (c.cost.u8(0x43)) {
	case 0:
		break;
	case 1: return exe_text(0x6B1C60, "Este edificio ya ha sido construido", "This building already exists");
	case 2:
		return exe_text(0x6B1C64, "No se puede construir este edificio hasta que",
				   "This building can not be built inside of") +
			" " + exe_text(0x6B1C68, "se hayan ampliado las murallas de la ciudad", "the first town walls");
	case 3: return exe_text(0x6B1C6C, "Ya hay suficientes edificios de este tipo", "There are enough buildings of this sort");
	case 4: return exe_text(0x6B0578, "La ciudad no dispone de suficiente oro", "The town doesn't have enough money");
	case 5: return exe_text(0x6C256C, "Todas las calles están pavimentadas.", "All roads are developed.");
	case 6: return exe_text(0x6C2570, "La dotación de pozos ya es óptima.", "Well covering already perfect.");
	default: return "";
	}
	if (!c.cost.u8(0x42)) {
		return exe_text(0x6C255C, "Tienes que abrir primero una oficina comercial.", "You have to open a trading office first!");
	}
	if (!c.cost.u8(0x41)) {
		return exe_text(0x6C2564, "¡No tienes materiales suficientes!", "Not enough materials!");
	}
	if (!c.cost.u8(0x40)) {
		return exe_text(0x6C2560, "¡No tienes dinero suficiente!", "Not enough money!");
	}
	return "";
}

// The page of the chosen building's title: «build %s», the office's and the pitch shoot's own.
std::string build_title(int type) {
	if (type == 0) {
		return exe_text(0x6C22E8, "Construir oficina comercial", "build trading office");
	}
	if (type == 0x34 || type == 0x35) {
		return exe_text(0x6B23BC, "Construir Tronera de brea", "Arm town gate");
	}
	const std::string format = exe_text(0x6C22CC, "Construir %s", "build %s");
	return GameTexts::format(format, {building_name(type)});
}

// The days the works take as the window writes them (0x47B33B): its
// [+0x38], in days up to 10, otherwise in weeks.
std::string build_days_text(int32_t days) {
	if (days <= 10) {
		return GameTexts::format(exe_text(0x6C2318, "%i días", "%i days"), {std::to_string(days)});
	}
	return GameTexts::format(exe_text(0x6C2328, "%i semanas", "%i weeks"), {std::to_string((days + 6) / 7)});
}

void draw_build_room(SDL_Renderer* renderer, TextRenderer& text, const PanelArt& art, const SidePanel& ui,
	const BuildMenu& menu, const std::array<bool, 5>& tabs, const BuildChoice* choice, bool placed, bool fits) {
	const PanelLayout panel(ui.folded);
	const SDL_Rect& room = panel.room;
	const BuildRoomLayout layout(room, text.line_height());
	const SDL_Color ink = {40, 25, 10, 255};
	const SDL_Color dim = {110, 85, 60, 255};
	const SDL_Color red = {150, 20, 10, 255};
	const int left = room.x + 10;
	if (!choice) {
		// The menu: the categories (normal, pressed, lit, disabled: the game's
		// rows), the category's name and its buildings.
		for (int k = 0; k < 5; ++k) {
			const int row = !tabs[size_t(k)] ? 3 : k == menu.category ? 2 : 0;
			const SDL_Rect src = {k * 32, row * 32, 32, 32};
			draw_picture(renderer, art.build_tabs, layout.tabs[k], &src);
		}
		text.draw(exe_text(0x6C2380 + 4 * uint32_t(menu.category), "", ""), left, layout.title_y, ink);
		int row = 0;
		for (const int32_t type : menu.types) {
			if (type < 0 || row >= kBuildRows) {
				continue;
			}
			const SDL_Rect& r = layout.rows[row];
			if (row == ui.build_row) {
				SDL_SetRenderDrawColor(renderer, 120, 80, 30, 70);
				SDL_RenderFillRect(renderer, &r);
			}
			text.draw(building_name(type), r.x + 4, r.y + 1, ink);
			++row;
		}
		if (menu.reputation_text) {
			int y = layout.rows[0].y;
			for (const std::string& line : wrap_text(text,
					 exe_text(0x6B20A4, "Todavía no puedes construir una oficina comercial...",
						 "You are not yet able to build a trading office..."),
					 room.w - 20)) {
				text.draw(line, left, y, dim);
				y += text.line_height();
			}
		}
		return;
	}
	// The building's page: its title, the costs, the days, the materials (what
	// it needs, what he has there, what is bought), why not, and its buttons.
	const GameMemory<0x44>& s = choice->cost;
	int y = room.y + 8;
	text.draw(build_title(choice->type), left, y, ink);
	y += text.line_height() + 4;
	const bool short_money = s.u8(0x43) == 4 || !s.u8(0x40);
	text.draw(exe_text(0x6C253C, "Coste total del edificio:", "total building costs:") + " " + format_money(s.s32(0x34)),
		left, y, short_money ? red : ink);
	y += text.line_height();
	text.draw(exe_text(0x6C2540, "Duración de la obra:", "construction time:") + " " + build_days_text(s.s32(0x38)), left,
		y, ink);
	y += text.line_height() + 3;
	// The materials' table: the game's four column titles don't fit the
	// panel's width side by side, so they go as one line over the numbers.
	static constexpr int kWares[4] = {0x13, 0xB, 0xC, 0x11};  // 0x673720: bricks, timber, tools, hemp.
	const int right_edges[3] = {room.x + 168, room.x + 208, room.x + 248};
	for (const std::string& line : wrap_text(text,
			 exe_text(0x6C2548, "Material", "materials:") + " " + exe_text(0x6C254C, "Requerido", "need:") + " / " +
				 exe_text(0x6C2550, "Almacén", "in stock:") + " / " + exe_text(0x6C2554, "Comprar", "buy:"),
			 room.w - 20)) {
		text.draw(line, left, y, dim);
		y += text.line_height();
	}
	for (int k = 0; k < 4; ++k) {
		if (s.s32(4 * k) <= 0) {
			continue;
		}
		const bool short_of = s.s32(4 * k) > s.s32(0x10 + 4 * k) + s.s32(0x20 + 4 * k);
		text.draw(ware_name(kWares[k]), left, y, ink);
		const int32_t values[3] = {s.s32(4 * k), s.s32(0x10 + 4 * k), s.s32(0x20 + 4 * k)};
		for (int c = 0; c < 3; ++c) {
			const std::string n = std::to_string(values[c]);
			text.draw(n, right_edges[c] - text.measure(n), y, c == 2 && short_of ? red : ink);
		}
		y += text.line_height();
	}
	y += 4;
	std::string note = build_reason(*choice);
	if (note.empty() && choice->action == BuildAction::Place) {
		note = !placed ? tr("Toca en la ciudad dónde va (las zonas en verde).", "Touch the town where it goes (the green areas).")
			: fits     ? tr("Cabe aquí.", "It fits here.")
					   : tr("Aquí no cabe.", "It doesn't fit here.");
	}
	for (const std::string& line : wrap_text(text, note, room.w - 20)) {
		if (y + text.line_height() > layout.back_button.y) {
			break;
		}
		text.draw(line, left, y, build_reason(*choice).empty() ? ink : red);
		y += text.line_height();
	}
	// The buttons: back, and the window's own (the streets, the well, the wall)
	// or building it where it is placed.
	draw_button(renderer, text, layout.back_button, exe_text(0x6C2280, "Atrás", "back"));
	std::string act;
	bool can = false;
	switch (choice->action) {
	case BuildAction::Street: act = exe_text(0x6C22AC, "Construir calle", "build street"); can = choice->button; break;
	case BuildAction::Well: act = exe_text(0x6C22BC, "Construir pozo", "build well"); can = choice->button; break;
	case BuildAction::Wall: act = exe_text(0x6C2298, "Construir muralla", "build piece of wall"); can = choice->button; break;
	case BuildAction::Place: act = tr("Construir aquí", "Build here"); can = placed && fits; break;
	default: break;
	}
	if (!act.empty()) {
		if (can) {
			draw_button(renderer, text, layout.act_button, BTN_CROSS " " + act);
		} else {
			SDL_SetRenderDrawColor(renderer, 120, 100, 80, 160);
			SDL_RenderFillRect(renderer, &layout.act_button);
			text.draw(act, layout.act_button.x + (layout.act_button.w - text.measure(act)) / 2,
				layout.act_button.y + (layout.act_button.h - text.line_height()) / 2, dim);
		}
	}
}

// --- The ship menu in the panel's room (the game's SIDEMENU_SHIP, CSideMenu_Ship; what it shows is read
// in re/emu/sesion14/ship_menu_notes.md): its page buttons, the header with its hold's barrel, and the
// pages: the goods, the crew, the deck, the convoys and the trade route. What the crew and convoy pages
// offer and send are the ship's window's and the convoy window's (place_view's Place::Ship and
// place_act, checked with the game). ---

constexpr SDL_Color kParchmentInk = {40, 25, 10, 255};
constexpr SDL_Color kParchmentDim = {110, 85, 60, 255};
constexpr SDL_Color kParchmentRed = {150, 20, 10, 255};

// The column of each page's button in schiffinfo0001.tga (show convoy, form convoy, leave convoy, trade
// route, goods, crew, deck; the deck's is 40 wide).
int ship_page_column(ShipPage page, bool in_convoy) {
	switch (page) {
	case ShipPage::Goods: return 4;
	case ShipPage::Crew: return 5;
	case ShipPage::Deck: return 6;
	case ShipPage::Convoy: return in_convoy ? 0 : 1;
	case ShipPage::LeaveConvoy: return 2;
	default: return 3;
	}
}

bool ship_in_convoy(const SaveGame& save, int ship) {
	const uint16_t convoy = save.ships[size_t(ship)].convoy();
	return convoy < save.globals.u16(global::kShips + 0xF6) && convoy < save.convoys.size();
}

// What can be chosen (0x48A9A2..0x48AA9A): in a convoy (without its flag 0x10) leaving or disbanding it,
// unless the state has the bits 0x14, and showing it; otherwise forming one only in port (the states 0
// and 1). The trade routes (the game's button, with a captain and out of a convoy) aren't in p3vita yet
// (phase 6).
bool ship_page_enabled(const SaveGame& save, int ship, ShipPage page) {
	const SavedShip& s = save.ships[size_t(ship)];
	const bool in_convoy = ship_in_convoy(save, ship) && !(save.convoys[s.convoy()].u8(0x14) & 0x10);
	switch (page) {
	case ShipPage::LeaveConvoy: return in_convoy && (s.status() & 0x14) != 0x14;
	case ShipPage::Convoy: return ship_in_convoy(save, ship) || s.status() == 0 || s.status() == 1;
	case ShipPage::TradeRoute: return false;
	default: return true;
	}
}

// The ships of a convoy, from its first ([+0xA]) along [+0x6] (as 0x5386C0 walks them).
std::vector<int> convoy_ships(const SaveGame& save, int convoy) {
	std::vector<int> out;
	uint16_t s = save.convoys[size_t(convoy)].u16(0xA);
	while (s < save.ships.size() && out.size() < 64) {
		out.push_back(int(s));
		s = save.ships[s].next_in_list();
	}
	return out;
}

struct ShipMenuLayout {
	SDL_Rect pages[kShipPages] = {};
	int top = 0;  // Where the pages' content starts.
	int line = 0;
	SDL_Rect barrel{};
	int cargo_y = 0;  // The goods page's rows.
	int cargo_row = 0;
	int cargo_rows = 0;
	int deck_y = 0;
	// The crew page: the sailors to dismiss (minus, the amount, plus, the button) and the captain's button.
	SDL_Rect minus_button{};
	SDL_Rect amount_box{};
	SDL_Rect plus_button{};
	SDL_Rect sailors_button{};
	SDL_Rect captain_button{};
	SDL_Rect pirate_button{};  // «Pirate» / «Civil», beside it (0x492A70).
	// The convoy pages: their rows, the destination's minus and plus, and the act button.
	static constexpr int kConvoyRows = 4;
	SDL_Rect convoy_rows[kConvoyRows] = {};
	SDL_Rect dest_minus{};
	SDL_Rect dest_plus{};
	SDL_Rect act_button{};

	ShipMenuLayout(const SDL_Rect& room, int line_height) {
		int x = room.x + 6;
		for (int k = 0; k < kShipPages; ++k) {
			const int w = k == int(ShipPage::Deck) ? 45 : 36;
			pages[k] = {x, room.y + 4, w, 36};
			x += w + 5;
		}
		top = room.y + 46;
		line = line_height;
		barrel = {room.x + room.w - 74 - 34, top, 74, 65};
		cargo_row = line + 2;
		cargo_y = top + 4 * line + 4;
		cargo_rows = std::max(1, (room.y + room.h - 4 - cargo_y) / cargo_row);
		deck_y = top + 4 * line + 18;
		const int button_h = line + 10;
		const int sy = top + 2 * line + 14;
		minus_button = {room.x + 8, sy, 34, button_h};
		amount_box = {minus_button.x + minus_button.w + 2, sy, 44, button_h};
		plus_button = {amount_box.x + amount_box.w + 2, sy, 34, button_h};
		sailors_button = {room.x + 8, sy + button_h + 4, room.w - 16, button_h};
		captain_button = {room.x + 8, room.y + room.h - button_h - 6, (room.w - 22) * 3 / 5, button_h};
		pirate_button = {captain_button.x + captain_button.w + 6, captain_button.y,
			room.x + room.w - 8 - (captain_button.x + captain_button.w + 6), button_h};
		for (int k = 0; k < kConvoyRows; ++k) {
			convoy_rows[k] = {room.x + 8, top + line + 6 + k * (line + 10), room.w - 16, line + 8};
		}
		act_button = {room.x + 8, room.y + room.h - button_h - 6, room.w - 16, button_h};
		dest_minus = {room.x + 8, act_button.y - button_h - 6, 34, button_h};
		dest_plus = {room.x + room.w - 8 - 34, dest_minus.y, 34, button_h};
	}

	int page_at(int x, int y) const {
		for (int k = 0; k < kShipPages; ++k) {
			if (inside(pages[k], x, y)) {
				return k;
			}
		}
		return -1;
	}
	int convoy_row_at(int x, int y) const {
		for (int k = 0; k < kConvoyRows; ++k) {
			if (inside(convoy_rows[k], x, y)) {
				return k;
			}
		}
		return -1;
	}
};

// «%u day(s)» of the status line (0x490CA5, 0x490DCC).
std::string days_text(int32_t days) {
	return std::to_string(days) + " " +
		(days == 1 ? exe_text(0x699928, "día", "day") : exe_text(0x699920, "días", "days"));
}

// The status line (0x490C70, by the ship's state; the table 0x490F60): its icon and text.
std::pair<PanelArt::Icon, std::string> ship_status_line(const SaveGame& save, int ship_index) {
	const SavedShip& ship = save.ships[size_t(ship_index)];
	const size_t towns = save.towns.size();
	auto town_of = [&](uint8_t t) { return t < towns ? window_town_name(save, t) : std::string(); };
	const uint16_t state = ship.status();
	auto at_sea = [&](int s) {
		// 0x490DC5: the days (with a speed) and the destination, or «at sea».
		const SavedShip& d = save.ships[size_t(s)];
		std::string text = d.speed() != 0 ? days_text(ship_days_to_go(save, s)) + " " : "";
		text += d.destination() < towns ? town_of(d.destination()) : exe_text(0x699914, "En el mar", "at sea");
		return std::make_pair(PanelArt::Sailing, text);
	};
	switch (state) {
	case 0: case 1: case 4: case 5: case 0xE:
		return {PanelArt::Anchor, town_of(ship.town())};
	case 2: case 3: case 0x12: case 0x15:
		return at_sea(ship_index);
	case 6: case 7:
		return {state == 6 ? PanelArt::Repair : PanelArt::Upgrade,
			days_text(ship_shipyard_days(save, ship_index)) + " " + town_of(ship.town())};
	case 8: case 9:
		return {PanelArt::Fleet, town_of(ship.town())};
	case 0xA: case 0xB: case 0x16:
		return {PanelArt::Fleet, exe_text(0x6B0D88, "Flota de la Liga Hanseática", "Hanseatic League Fleet")};
	case 0xC:
		return {PanelArt::Auction, exe_text(0x699A14, "Subasta", "Auction")};
	case 0xD:
		return {PanelArt::Offered, exe_text(0x699A08, "Ofertas", "Offered")};
	case 0xF: case 0x10: {
		// In a convoy at sea: its guide's (0x490D5B), or «at sea» without a route.
		int s = ship_index;
		if (ship_in_convoy(save, ship_index) && save.convoys[ship.convoy()].u16(0x10) < save.ships.size()) {
			s = save.convoys[ship.convoy()].u16(0x10);
		}
		if (!save.ships[size_t(s)].route) {
			return {PanelArt::Sailing, exe_text(0x699914, "En el mar", "at sea")};
		}
		return at_sea(s);
	}
	case 0x11:
		return {PanelArt::Sunk, exe_text(0x699940, "hundido", "Sunk")};
	case 0x13:
		return {PanelArt::Sailing, exe_text(0x6999A0, "Expedición", "Expedition")};
	case 0x14:
		return {PanelArt::Sailing, exe_text(0x6999FC, "Batalla naval", "Sea battle")};
	default:
		return {PanelArt::Anchor, ""};
	}
}

// A text cut to `width`, a whole UTF-8 character at a time.
std::string cut_text(const TextRenderer& text, std::string s, int width) {
	while (!s.empty() && text.measure(s) > width) {
		while (!s.empty() && (uint8_t(s.back()) & 0xC0) == 0x80) {
			s.pop_back();
		}
		if (!s.empty()) {
			s.pop_back();
		}
	}
	return s;
}

// The crew page's «Pirate» or «Civil» (0x492A5C..0x492A88).
std::string ship_pirate_label(const SavedShip& ship) {
	return ship.raid() == 0 && ship.status() != kShipPirate ? exe_text(0x69C98C, "Pirata", "Pirate")
															  : exe_text(0x6999EC, "Civil", "Civil");
}

// The header (0x4916E0): its name and type, the status line, the knots and the hull, the crew (with the
// captain's star) and the firepower's cannonballs, and the barrel of the hold with its capacity, the hold
// used (red) and the equipment (yellow), in loads.
void draw_ship_header(SDL_Renderer* renderer, TextRenderer& text, const PanelArt& art, const SaveGame& save,
	int ship_index, const ShipMenuLayout& layout, const SDL_Rect& room, bool barrel) {
	const SavedShip& ship = save.ships[size_t(ship_index)];
	const int left = room.x + 8;
	const int width = (barrel ? layout.barrel.x : room.x + room.w) - left - 4;
	const int line = layout.line;
	int y = layout.top;
	text.draw(cut_text(text, barrel ? ship.name() : ship_label(ship), width), left, y, kParchmentInk);
	y += line;
	const auto [status_icon, status] = ship_status_line(save, ship_index);
	int x = left + art.icon(renderer, status_icon, left, y) + 3;
	text.draw(cut_text(text, status, left + width - x), x, y, kParchmentInk);
	y += line;
	// The knots ([+0x30] >> 4) and the hull (red under 40 %, 0x491E80).
	const int hull = ship.max_health() > 0 ? std::max(0, int(int64_t(ship.health()) * 100 / ship.max_health())) : 0;
	x = left + art.icon(renderer, PanelArt::Knots, left, y) + 2;
	const std::string knots = std::to_string(ship.speed() >> 4);
	text.draw(knots, x, y, kParchmentInk);
	x += text.measure(knots) + 10;
	x += art.icon(renderer, PanelArt::Hull, x, y) + 2;
	text.draw(std::to_string(hull), x, y, hull < 40 ? kParchmentRed : kParchmentInk);
	y += line;
	// The crew, the captain if it has one, the sailors; the firepower ([+0x120]) as cannonballs (0x4901C0).
	x = left + art.icon(renderer, PanelArt::Crew, left, y);
	if (ship.captain() < save.globals.u16(global::kCaptainCount)) {
		x += art.icon(renderer, PanelArt::Captain, x, y);
	}
	const std::string sailors = std::to_string(ship.sailors());
	text.draw(sailors, x + 2, y, kParchmentInk);
	x += text.measure(sailors) + 10;
	const int32_t firepower = ship.memory.s32(0x120);
	if (firepower > 0) {
		x += art.icon(renderer, PanelArt::Weapons, x, y) + 2;
		const FirepowerBalls balls = firepower_balls(firepower);
		for (int k = 0; k < balls.big; ++k) {
			x += art.icon(renderer, PanelArt::BigBall, x, y + 3);
		}
		for (int k = 0; k < balls.small; ++k) {
			x += art.icon(renderer, PanelArt::SmallBall, x, y + 3);
		}
		if (balls.half && art.list_icons[PanelArt::SmallBall]) {
			const int w = art.icon_width[PanelArt::SmallBall];
			const int h = art.icon_height[PanelArt::SmallBall];
			const SDL_Rect src = {0, 0, w / 2, h};
			const SDL_Rect dst = {x, y + 3, w / 2, h};
			SDL_RenderCopy(renderer, art.list_icons[PanelArt::SmallBall], &src, &dst);
		}
	}
	if (!barrel) {
		return;
	}
	// The barrel: from its top, what is free, the equipment and the hold used (0x491A3D..0x491B36), each
	// a part of the mask's height by the capacity.
	const SDL_Rect& b = layout.barrel;
	draw_picture(renderer, art.barrel, b);
	const int32_t capacity = ship.capacity();
	const int32_t used = ship.used();  // A ship of the player's as it is (0x48F0E0; another's would be blurred and cut).
	int32_t equipment = ship.equipment();
	if (equipment % 200) {
		equipment += 200 - equipment % 200;  // A whole load (0x48F07E).
	}
	if (capacity > 0 && art.barrel_mask) {
		int mask_w = 0;
		int mask_h = 0;
		SDL_QueryTexture(art.barrel_mask, nullptr, nullptr, &mask_w, &mask_h);
		const SDL_Rect mask = {b.x + (b.w - mask_w) / 2 - 8, b.y, mask_w, mask_h};
		int cargo_px = int(double(used) / double(capacity) * mask.h);
		int equip_px = int(double(equipment) / double(capacity) * mask.h);
		cargo_px = std::min(cargo_px, mask.h);
		equip_px = std::min(equip_px, mask.h - cargo_px);
		auto band = [&](int from, int h, SDL_Color c) {
			if (h <= 0) {
				return;
			}
			const SDL_Rect src = {0, from, mask_w, h};
			const SDL_Rect dst = {mask.x, mask.y + from, mask_w, h};
			SDL_SetTextureColorMod(art.barrel_mask, c.r, c.g, c.b);
			SDL_SetTextureAlphaMod(art.barrel_mask, 150);
			SDL_RenderCopy(renderer, art.barrel_mask, &src, &dst);
			SDL_SetTextureColorMod(art.barrel_mask, 255, 255, 255);
			SDL_SetTextureAlphaMod(art.barrel_mask, 255);
		};
		band(mask.h - cargo_px - equip_px, equip_px, {0xFF, 0xD4, 0x25, 255});  // WeaponsCrewColor
		band(mask.h - cargo_px, cargo_px, {0xD1, 0x00, 0x00, 255});  // GoodsColor
	}
	// Its numbers in barrels (/ 200, 0x491BA6..0x491C48): the capacity (in red if the hold used and the
	// equipment pass it), the hold used and the equipment.
	const int nx = room.x + room.w - 6;
	const bool over = ship.used() + equipment > capacity;
	const std::string numbers[3] = {std::to_string(capacity / 200), std::to_string(used / 200),
		std::to_string(uint32_t(equipment) / 200)};
	const SDL_Color colors[3] = {over ? kParchmentRed : kParchmentInk, kParchmentRed, {150, 110, 0, 255}};
	for (int k = 0; k < 3; ++k) {
		text.draw(numbers[k], nx - text.measure(numbers[k]), b.y + 2 + k * (line + 2), colors[k]);
	}
}

// The goods page (0x48F510): each cargo slot not empty, in barrels, loads or pieces, its ware and the price
// it shows (0x5187F0), from the first shown; then the packets and the travellers.
void draw_ship_goods(SDL_Renderer* renderer, TextRenderer& text, const PanelArt& art, const SaveGame& save,
	int ship_index, const ShipMenuLayout& layout, const SDL_Rect& room, int first) {
	const SavedShip& ship = save.ships[size_t(ship_index)];
	const std::array<int32_t, 24> prices = ship_cargo_prices(save, ship_index);
	struct Row {
		PanelArt::Icon icon;
		std::string amount;
		std::string name;
		std::string price;
	};
	std::vector<Row> rows;
	for (int w = 0; w < 24; ++w) {
		const int32_t units = ship.cargo(w);
		if (units == 0) {
			continue;
		}
		const bool barrels = w < 20 && kWares[size_t(w)].unit == WareUnit::Barrel;
		const int32_t shown = w >= 20 ? units / 10 : barrels ? units / 200 : units / 2000;
		const std::string name = w < 20 ? ware_label(w)
			: (g_texts ? g_texts->get(GameText(int(GameText::WareSword) + (w - 20))) : std::string());
		rows.push_back({barrels || w >= 20 ? PanelArt::Barrel : PanelArt::Bale, std::to_string(shown), name,
			format_money(prices[size_t(w)])});
	}
	if (ship.extra_loads()) {
		const int n = ship.extra_loads();
		rows.push_back({PanelArt::Bale, std::to_string(n),
			n == 1 ? exe_text(0x699C8C, "fardo", "Packet") : exe_text(0x699C90, "fardos", "Packets"), ""});
	}
	if (ship.memory.u8(0x157)) {
		const int n = ship.memory.u8(0x157);
		rows.push_back({PanelArt::Crew, std::to_string(n),
			n == 1 ? exe_text(0x69C9E8, "Viajero", "Traveller") : exe_text(0x6B2C90, "Viajero", "Traveller"), ""});
	}
	const int left = room.x + 8;
	if (rows.empty()) {
		text.draw(tr("La bodega está vacía.", "The hold is empty."), left, layout.cargo_y, kParchmentDim);
		return;
	}
	for (int k = 0; k < layout.cargo_rows && first + k < int(rows.size()); ++k) {
		const Row& r = rows[size_t(first + k)];
		const int y = layout.cargo_y + k * layout.cargo_row;
		art.icon(renderer, r.icon, left, y);
		text.draw(r.amount, left + 62 - text.measure(r.amount), y, kParchmentInk);
		text.draw(cut_text(text, r.name, 120), left + 68, y, kParchmentInk);
		text.draw(r.price, room.x + room.w - 8 - text.measure(r.price), y, kParchmentDim);
	}
	if (first > 0 || first + layout.cargo_rows < int(rows.size())) {
		// More above or below: the pad or a drag moves the list.
		const std::string more = std::to_string(first + 1) + "-" +
			std::to_string(std::min(int(rows.size()), first + layout.cargo_rows)) + "/" + std::to_string(rows.size());
		text.draw(more, room.x + room.w - 8 - text.measure(more), layout.cargo_y - layout.line - 2, kParchmentDim);
	}
}

// How many rows the goods page has (the cargo slots not empty, the packets, the travellers).
int ship_goods_rows(const SaveGame& save, int ship_index) {
	const SavedShip& ship = save.ships[size_t(ship_index)];
	int n = 0;
	for (int w = 0; w < 24; ++w) {
		n += ship.cargo(w) != 0 ? 1 : 0;
	}
	return n + (ship.extra_loads() ? 1 : 0) + (ship.memory.u8(0x157) ? 1 : 0);
}

// A greyed button that can't be pressed (the game's disabled row).
void draw_disabled_button(SDL_Renderer* renderer, TextRenderer& text, const SDL_Rect& r, const std::string& label) {
	SDL_SetRenderDrawColor(renderer, 120, 100, 80, 160);
	SDL_RenderFillRect(renderer, &r);
	text.draw(label, r.x + (r.w - text.measure(label)) / 2, r.y + (r.h - text.line_height()) / 2, kParchmentDim);
}

void draw_ship_menu(SDL_Renderer* renderer, TextRenderer& text, const PanelArt& art, const SaveGame& save,
	const SidePanel& ui, const PlaceView& actions, bool can_pirate) {
	const PanelLayout panel(ui.folded);
	const SDL_Rect& room = panel.room;
	const ShipMenuLayout layout(room, text.line_height());
	const int ship_index = ui.ship;
	const SavedShip& ship = save.ships[size_t(ship_index)];
	const bool in_convoy = ship_in_convoy(save, ship_index);
	// The ship type's faded picture at the room's bottom (0x48D820).
	if (art.backpics[ship.type() & 3]) {
		const SDL_Rect dst = {room.x, room.y + room.h - 160, 260, 160};
		SDL_RenderCopy(renderer, art.backpics[ship.type() & 3], nullptr, &dst);
	}
	// The page buttons (normal, pressed, lit, disabled).
	for (int k = 0; k < kShipPages; ++k) {
		const ShipPage page = ShipPage(k);
		const bool enabled = ship_page_enabled(save, ship_index, page);
		const int row = !enabled ? 3 : page == ui.ship_page ? 2 : 0;
		const int column = ship_page_column(page, in_convoy);
		const SDL_Rect src = {column * 32, row * 32, column == 6 ? 40 : 32, 32};
		draw_picture(renderer, art.ship_buttons, layout.pages[k], &src);
	}
	const int left = room.x + 8;
	const int line = layout.line;
	const PlaceUi& act = ui.ship_ui;  // Its results go to the viewer's notices (ship_act).
	switch (ui.ship_page) {
	case ShipPage::Goods:
		draw_ship_header(renderer, text, art, save, ship_index, layout, room, true);
		draw_ship_goods(renderer, text, art, save, ship_index, layout, room, ui.cargo_first);
		break;
	case ShipPage::Crew: {
		// 0x48DF90: the morale (the high byte of [+0x3E], 0..4) with the sailors, the least and the most;
		// the wage of a week; the name; the hand weapons; the sailors to dismiss; the captain.
		int y = layout.top;
		const int morale = std::clamp(int(int8_t(ship.memory.u8(0x3F))), 0, 4);
		static constexpr SDL_Point kFaces[6] = {{0, 0}, {0, 32}, {32, 0}, {32, 32}, {64, 0}, {64, 32}};
		const SDL_Rect face = {kFaces[morale].x, kFaces[morale].y, 32, 32};
		draw_picture(renderer, art.morale, {left - 2, y - 4, 30, 30}, &face);
		static constexpr uint8_t kLeast[4] = {5, 8, 10, 12};  // 0x673660
		const std::string crew = std::to_string(ship.sailors()) + " (" + std::to_string(kLeast[ship.type() & 3]) + " - " +
			std::to_string(ship_most_sailors(save, ship_index)) + ")";
		text.draw(crew, left + 30, y + 2, kParchmentInk);
		const SDL_Rect coins = {kFaces[5].x, kFaces[5].y, 32, 32};
		const std::string wage =
			std::to_string(7 * ship_daily_wage(save, ship_index)) + " (" + exe_text(0x699C78, "Semana", "Week") + ")";
		const int wx = room.x + room.w - 8 - text.measure(wage);
		draw_picture(renderer, art.morale, {wx - 31, y - 4, 30, 30}, &coins);
		text.draw(wage, wx, y + 2, kParchmentInk);
		y += line + 10;
		text.draw(cut_text(text, ship.name(), room.w - 80), left, y, kParchmentInk);
		const std::string hand = std::to_string(ship.hand_weapons());
		const int hx = room.x + room.w - 8 - text.measure(hand);
		text.draw(hand, hx, y, kParchmentInk);
		art.icon(renderer, PanelArt::HandWeapons, hx - 28, y);
		// The sailors to dismiss.
		const bool rows = actions.rows.size() >= 2;
		const bool can_dismiss = rows && actions.rows[0].second;
		const std::string amount = std::to_string(act.row == 0 ? act.amount : 0);
		if (can_dismiss) {
			draw_button(renderer, text, layout.minus_button, "-");
			draw_button(renderer, text, layout.plus_button, "+");
		} else {
			draw_disabled_button(renderer, text, layout.minus_button, "-");
			draw_disabled_button(renderer, text, layout.plus_button, "+");
		}
		SDL_SetRenderDrawColor(renderer, 245, 235, 205, 255);
		SDL_RenderFillRect(renderer, &layout.amount_box);
		text.draw(amount, layout.amount_box.x + (layout.amount_box.w - text.measure(amount)) / 2,
			layout.amount_box.y + (layout.amount_box.h - line) / 2, kParchmentInk);
		const std::string dismiss = rows ? actions.rows[0].first : tr("Despedir marineros", "Dismiss sailors");
		if (can_dismiss && act.row == 0 && act.amount > 0) {
			draw_button(renderer, text, layout.sailors_button, BTN_CROSS + std::string(" ") + dismiss);
		} else {
			draw_disabled_button(renderer, text, layout.sailors_button, dismiss);
		}
		if (act.row == 0 && can_dismiss) {
			const SDL_Rect frame = {layout.minus_button.x - 2, layout.minus_button.y - 2, layout.sailors_button.w + 4,
				layout.sailors_button.y + layout.sailors_button.h - layout.minus_button.y + 4};
			SDL_SetRenderDrawColor(renderer, 120, 80, 30, 255);
			SDL_RenderDrawRect(renderer, &frame);
		}
		// The captain: his three skills (bonus.tga: trading, sailing, fighting; / 50, as the tavern's),
		// «Captain» with his names and «(age Years)», and dismissing him.
		y = layout.sailors_button.y + layout.sailors_button.h + 6;
		if (ship.captain() < save.globals.u16(global::kCaptainCount)) {
			const CaptainInfo c = captain_info(save, ship.captain());
			static constexpr SDL_Point kSkills[3] = {{0, 0}, {16, 0}, {0, 16}};
			const int values[3] = {c.trading, c.sailing, c.fighting};
			int x = left;
			for (int k = 0; k < 3; ++k) {
				const SDL_Rect src = {kSkills[k].x, kSkills[k].y, 16, 16};
				draw_picture(renderer, art.bonus, {x, y + 1, 16, 16}, &src);
				text.draw(std::to_string(values[k]), x + 18, y, kParchmentInk);
				x += 46;
			}
			y += line + 2;
			const std::string who = exe_text(0x6999D4, "Capitán", "Captain") + " " + captain_name(save, ship.captain()) +
				" (" + std::to_string(c.age) + " " + exe_text(0x6999E0, "Años", "Years") + ")";
			for (const std::string& l : wrap_text(text, who, room.w - 16)) {
				if (y + line > layout.captain_button.y) {
					break;
				}
				text.draw(l, left, y, kParchmentInk);
				y += line;
			}
			const std::string label = rows ? actions.rows[1].first : "";
			if (rows && actions.rows[1].second) {
				draw_button(renderer, text, layout.captain_button, (act.row == 1 ? BTN_CROSS " " : "") + label);
			} else {
				draw_disabled_button(renderer, text, layout.captain_button, label);
			}
			if (act.row == 1) {
				const SDL_Rect frame = {layout.captain_button.x - 2, layout.captain_button.y - 2, layout.captain_button.w + 4,
					layout.captain_button.h + 4};
				SDL_SetRenderDrawColor(renderer, 120, 80, 30, 255);
				SDL_RenderDrawRect(renderer, &frame);
			}
			// «Pirate» while [+0x15C] is 0 and the state isn't 0x12, «Civil» otherwise (0x492A5C); only at sea (0x518750).
			const std::string pirate = ship_pirate_label(ship);
			if (can_pirate) {
				draw_button(renderer, text, layout.pirate_button, (act.row == 2 ? BTN_CROSS " " : "") + pirate);
			} else {
				draw_disabled_button(renderer, text, layout.pirate_button, pirate);
			}
			if (act.row == 2) {
				const SDL_Rect frame = {layout.pirate_button.x - 2, layout.pirate_button.y - 2, layout.pirate_button.w + 4,
					layout.pirate_button.h + 4};
				SDL_SetRenderDrawColor(renderer, 120, 80, 30, 255);
				SDL_RenderDrawRect(renderer, &frame);
			}
		} else {
			text.draw(exe_text(0x6999D4, "Capitán", "Captain") + ": " + tr("ninguno", "none"), left, y, kParchmentDim);
			y += line;
		}
		if (!ship_window_open(save, ship_index)) {
			text.draw(tr("Solo con el barco atracado.", "Only with the ship docked."), left, y, kParchmentDim);
		}
		break;
	}
	case ShipPage::Deck: {
		// 0x48E700 → 0x495210: the deck (frame 5 of schiffsdeck.tga) and its 24 slots in two rows of 12, 17
		// pixels apart from its 13th pixel: the slots k with k % 4 < 2 on the upper line (DeckTopLine 36),
		// standing up from it, the others on the lower one (DeckBottomLine 60), hanging down; a piece of
		// type t is the frame 6 + t above and 12 + t below, an empty slot (0xFF) the placeholder (frame 18).
		draw_ship_header(renderer, text, art, save, ship_index, layout, room, false);
		static constexpr SDL_Rect kFrames[19] = {{0, 0, 32, 12}, {32, 0, 192, 12}, {224, 0, 32, 12}, {45, 124, 26, 22},
			{0, 0, 0, 0}, {0, 26, 248, 96}, {130, 128, 20, 52}, {109, 128, 20, 52}, {217, 128, 32, 52}, {184, 128, 32, 52},
			{151, 128, 32, 52}, {88, 128, 20, 52}, {130, 181, 20, 52}, {109, 181, 20, 52}, {217, 181, 32, 52},
			{184, 181, 32, 52}, {151, 181, 32, 52}, {88, 181, 20, 52}, {7, 123, 17, 34}};
		const SDL_Rect deck = {room.x + 6, layout.deck_y, 248, 96};
		draw_picture(renderer, art.deck, deck, &kFrames[5]);
		const int step = (deck.w - 0x42) / 12 + 2;
		for (int upper = 1; upper >= 0; --upper) {
			int x = deck.x + 13;
			const int anchor = deck.y + (upper ? 36 : 60);
			for (int k = upper ? 0 : 2; k < 24; k += (k & 1) ? 3 : 1) {
				const uint8_t type = ship.artillery(k);
				const SDL_Rect* f = type < 6 ? &kFrames[(upper ? 6 : 12) + type] : type == 0xFF ? &kFrames[18] : nullptr;
				if (f) {
					const SDL_Rect dst = {x, upper ? anchor - f->h : anchor, f->w, f->h};
					draw_picture(renderer, art.deck, dst, f);
				}
				x += step;
			}
		}
		break;
	}
	case ShipPage::Convoy:
		if (in_convoy) {
			// «Show convoy»: its ships, the guide first with the captain's star (0x48CF50's list).
			const int convoy = ship.convoy();
			text.draw(convoy_label(save, convoy), left, layout.top, kParchmentInk);
			int y = layout.top + line + 4;
			const uint16_t guide = save.convoys[size_t(convoy)].u16(0x10);
			for (const int s : convoy_ships(save, convoy)) {
				if (y + line > room.y + room.h - 4) {
					break;
				}
				const SavedShip& m = save.ships[size_t(s)];
				int x = left;
				if (s == guide) {
					x += art.icon(renderer, PanelArt::Captain, x, y) + 2;
				}
				std::string label = ship_label(m);
				if (m.owner() != save.player && m.owner() < save.merchants.size()) {
					label += " - " + tr("otro comerciante", "another merchant");
				}
				text.draw(cut_text(text, label, room.x + room.w - 8 - x), x, y, m.owner() == save.player ? kParchmentInk : kParchmentDim);
				y += line + 2;
			}
			break;
		}
		[[fallthrough]];
	case ShipPage::LeaveConvoy: {
		// The convoy window's choices (0x40C170) or its question (0x494DC0): the rows from 2 of the ship's
		// actions, the selected one lit, the public's destination with minus and plus, and its button.
		text.draw(ui.ship_page == ShipPage::LeaveConvoy ? tr("Convoy", "Convoy") : tr("Formar convoy", "Form convoy"), left,
			layout.top, kParchmentInk);
		int k = 0;
		for (size_t r = 2; r < actions.rows.size() && k < ShipMenuLayout::kConvoyRows; ++r, ++k) {
			const SDL_Rect& rr = layout.convoy_rows[k];
			if (int(r) == act.row) {
				SDL_SetRenderDrawColor(renderer, 120, 80, 30, 70);
				SDL_RenderFillRect(renderer, &rr);
			}
			text.draw(cut_text(text, actions.rows[r].first, rr.w - 8), rr.x + 4, rr.y + 4,
				actions.rows[r].second ? kParchmentInk : kParchmentDim);
		}
		int y = layout.convoy_rows[0].y + k * (line + 10) + 4;
		for (const auto& [l, color] : actions.lines) {
			for (const std::string& w : wrap_text(text, l, room.w - 16)) {
				if (y + line > layout.act_button.y - 2) {
					break;
				}
				text.draw(w, left, y, color.r > 200 ? kParchmentInk : kParchmentDim);
				y += line;
			}
		}
		const bool selected = act.row >= 2 && act.row < int(actions.rows.size());
		if (selected && actions.amount_choice && actions.max_amount >= 0) {
			// The public convoy's destination: minus and plus round it.
			const SDL_Rect& minus = layout.dest_minus;
			const SDL_Rect& plus = layout.dest_plus;
			draw_button(renderer, text, minus, "-");
			draw_button(renderer, text, plus, "+");
			const std::string town = cut_text(text, actions.amount_label, plus.x - minus.x - minus.w - 8);
			text.draw(town, (minus.x + minus.w + plus.x - text.measure(town)) / 2, minus.y + (minus.h - line) / 2,
				kParchmentInk);
		}
		if (selected) {
			const std::string label = actions.rows[size_t(act.row)].first;
			if (actions.rows[size_t(act.row)].second) {
				draw_button(renderer, text, layout.act_button, BTN_CROSS + std::string(" ") + cut_text(text, label, room.w - 50));
			} else {
				draw_disabled_button(renderer, text, layout.act_button, cut_text(text, label, room.w - 30));
			}
		}
		break;
	}
	default:
		draw_ship_header(renderer, text, art, save, ship_index, layout, room, true);
		text.draw(tr("Las rutas comerciales aún no están.", "The trade routes aren't there yet."), left, layout.cargo_y,
			kParchmentDim);
		break;
	}
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
		panel = {g_town_right - kWidth - 8, kBarHeight + 4, kWidth, kScreenHeight - kBarHeight - 12};
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
	// In the side panel: whether the letter's parchment is open over the town
	// (the chronicle's always is), and the letters' settings window
	// (0x408F50): open, its row, and what it shows (the player's [+0x38..+0x3A],
	// [+0x26] and [+0x3B] when it opened).
	bool letter_open = false;
	bool options = false;
	int options_row = 0;
	std::array<uint8_t, 3> option_days{};
	uint8_t option_notices = 0;
	bool option_event_sound = false;
	int last_check = -1;  // The tick's low byte of the last 0x71 queued while it is open (0x47CD83).
};

// The letters' settings window's rows (0x408F50): for each class its days, its
// notice and its sound, then the event sound.
constexpr int kMailOptionRows = 10;

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

// Where everything goes, shared by the drawing and the touch screen. On the
// whole screen (the sea map), or in the side panel (the game's SIDEMENU_INFOS):
// its six buttons (the four tabs, the letters' settings and delete) and the
// list in the panel's room, the letter on a parchment over the town.
struct MailLayout {
	static constexpr int kPadding = 8;
	static constexpr int kListWidth = 360;
	bool in_panel = false;
	SDL_Rect panel{};  // On the whole screen, the window; in the panel, the parchment.
	std::array<SDL_Rect, 4> tabs{};
	SDL_Rect list{};
	int row_height = 0;
	int rows = 0;  // Rows in a page.
	SDL_Rect text{};  // The letter (the whole width in the chronicle).
	SDL_Rect delete_button{};
	SDL_Rect close_button{};
	SDL_Rect options_button{};  // In the panel only.
	SDL_Rect title{};  // In the panel: the tab's name over the list.

	// In the side panel's room (`room`), with the parchment over the town on its left.
	MailLayout(int line_height, const SDL_Rect& room) : in_panel(true) {
		for (int k = 0; k < 6; ++k) {
			const SDL_Rect r = {room.x + 4 + 42 * k, room.y + 4, 40, 35};
			if (k < 4) {
				tabs[size_t(k)] = r;
			} else if (k == 4) {
				options_button = r;
			} else {
				delete_button = r;
			}
		}
		title = {room.x + 8, room.y + 44, room.w - 16, line_height};
		row_height = line_height + 4;
		list = {room.x + 6, title.y + line_height + 4, room.w - 12, 0};
		list.h = room.y + room.h - 4 - list.y;
		rows = std::max(1, list.h / row_height);
		panel = {12, kBarHeight + 10, g_town_right - 24, kScreenHeight - kBarHeight - 20};
		text = {panel.x + 40, panel.y + 62, panel.w - 80, panel.h - 62 - 58};
		close_button = {panel.x + panel.w / 2 - 70, panel.y + panel.h - 52, 140, line_height + 10};
	}

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

// The game's parchment (dialogparchment/Papier.tga, TEX16001) stretched over `r`: its rolls with their
// ends at the top and the bottom, its edges and its middle (a nine-piece frame of its 32-pixel tiles).
void draw_scroll(SDL_Renderer* renderer, SDL_Texture* paper, const SDL_Rect& r) {
	if (!paper) {
		draw_panel_box(renderer, r);
		return;
	}
	const int x0 = r.x, x1 = r.x + 38, x2 = r.x + r.w - 38;
	const int y0 = r.y, y1 = r.y + 64, y2 = r.y + r.h - 64;
	auto piece = [&](int sx, int sy, int sw, int sh, int dx, int dy, int dw, int dh) {
		if (dw <= 0 || dh <= 0) {
			return;
		}
		const SDL_Rect src = {sx, sy, sw, sh};
		const SDL_Rect dst = {dx, dy, dw, dh};
		SDL_RenderCopy(renderer, paper, &src, &dst);
	};
	piece(0, 0, 38, 64, x0, y0, 38, 64);  // The top roll's end and the paper's corner.
	piece(38, 0, 96, 64, x1, y0, x2 - x1, 64);
	piece(134, 0, 38, 64, x2, y0, 38, 64);
	piece(6, 64, 32, 96, x0 + 6, y1, 32, y2 - y1);  // The edges and the middle.
	piece(38, 64, 96, 96, x1, y1, x2 - x1, y2 - y1);
	piece(134, 64, 32, 96, x2, y1, 32, y2 - y1);
	piece(0, 160, 38, 64, x0, y2, 38, 64);  // The bottom roll.
	piece(38, 160, 96, 64, x1, y2, x2 - x1, 64);
	piece(134, 160, 38, 64, x2, y2, 38, 64);
}

// The letter's or the chronicle's lines (MailUi::lines) in `box`, scrolled, in `color`.
void draw_letter_lines(SDL_Renderer* renderer, TextRenderer& text, const MailUi& ui, const SDL_Rect& box, SDL_Color color,
	SDL_Color rule) {
	SDL_Rect clip = box;
	SDL_RenderSetClipRect(renderer, &clip);
	int y = box.y + 2 - ui.scroll;
	for (const auto& [align, line] : ui.lines) {
		if (line.size() == 1 && line[0] == kPictureLine) {
			if (ui.picture) {
				const int x = align == LetterParagraph::Right ? box.x + box.w - 4 - ui.picture_width
					: align == LetterParagraph::Center ? box.x + (box.w - ui.picture_width) / 2
					: box.x + 4;
				const SDL_Rect where = {x, y, ui.picture_width, ui.picture_height};
				SDL_RenderCopy(renderer, ui.picture.get(), nullptr, &where);
			}
		} else if (line.size() == 1 && line[0] == kOrnament) {
			const int cx = box.x + box.w / 2;
			const int cy = y + text.line_height() / 2;
			SDL_SetRenderDrawColor(renderer, rule.r, rule.g, rule.b, 255);
			SDL_RenderDrawLine(renderer, cx - 90, cy, cx - 8, cy);
			SDL_RenderDrawLine(renderer, cx + 8, cy, cx + 90, cy);
			for (int k = 0; k <= 4; ++k) {
				SDL_RenderDrawLine(renderer, cx - 4 + k, cy - k, cx + 4 - k, cy - k);
				SDL_RenderDrawLine(renderer, cx - 4 + k, cy + k, cx + 4 - k, cy + k);
			}
		} else if (y + text.line_height() >= box.y && y < box.y + box.h) {
			const int w = text.measure(line);
			const int x = align == LetterParagraph::Right ? box.x + box.w - 4 - w
				: align == LetterParagraph::Center ? box.x + (box.w - w) / 2
				: box.x + 4;
			text.draw(line, x, y, color);
		}
		y += text.line_height();
	}
	SDL_RenderSetClipRect(renderer, nullptr);
	const int height = int(ui.lines.size()) * text.line_height() + 4;
	if (height > box.h) {
		const int bar = std::max(20, box.h * box.h / height);
		const int at = box.y + (box.h - bar) * ui.scroll / std::max(1, height - box.h);
		const SDL_Rect thumb = {box.x + box.w - 3, at, 3, bar};
		SDL_SetRenderDrawColor(renderer, rule.r, rule.g, rule.b, 255);
		SDL_RenderFillRect(renderer, &thumb);
	}
}

// A frame of briefe0001.tga (TEX20018, 32 · 28 each): 0..11 three a row, 12..15 the delete button's four
// (its disabled one the fourth), 16..18 the settings'.
void draw_letter_button(SDL_Renderer* renderer, const PanelArt& art, const SDL_Rect& dst, int frame) {
	const SDL_Rect src = frame < 12 ? SDL_Rect{(frame % 3) * 32, (frame / 3) * 28, 32, 28}
		: frame < 16              ? SDL_Rect{(frame - 12) * 32, 112, 32, 28}
								  : SDL_Rect{(frame - 16) * 32, 140, 32, 28};
	draw_picture(renderer, art.letters, dst, &src);
}

// The letters' settings window's text by its address (0x6C0F34...), or the viewer's.
std::string letter_option_text(uint32_t english, const char* spanish, const char* fallback) {
	return exe_text(english, spanish, fallback);
}

// The messages in the side panel (the game's SIDEMENU_INFOS, *0x6CC960): its six buttons (the public news,
// the official letters, the personal letters, the trade information, the letters' settings and delete:
// briefe0001.tga's frames 0, 3, 6, 9, 16, 12, lit + 2), the tab's name and its letters, one a row (the
// unread ones darker, as the game's black and brown, 0x47D8E0). The letter or the chronicle goes on a
// parchment over the town (draw_mail_parchment).
void draw_mail_panel(SDL_Renderer* renderer, TextRenderer& text, const PanelArt& art, const SaveGame& save,
	const MailUi& ui, const std::vector<uint16_t>& rows, const MailLayout& layout) {
	static constexpr int kFrames[4] = {0, 3, 6, 9};
	for (int k = 0; k < 4; ++k) {
		draw_letter_button(renderer, art, layout.tabs[size_t(k)], kFrames[k] + (k == ui.tab ? 2 : 0));
	}
	draw_letter_button(renderer, art, layout.options_button, ui.options ? 18 : 16);
	const bool can_delete = ui.tab != 0 && ui.row < int(rows.size());
	draw_letter_button(renderer, art, layout.delete_button, can_delete ? 12 : 15);
	text.draw(cut_text(text, mail_tab_name(ui.tab), layout.title.w), layout.title.x, layout.title.y, kParchmentInk);
	if (ui.tab == 0) {
		text.draw(tr("(sobre la ciudad)", "(over the town)"), layout.list.x + 2, layout.list.y, kParchmentDim);
		return;
	}
	if (rows.empty()) {
		text.draw(tr("No hay cartas.", "No letters."), layout.list.x + 2, layout.list.y, kParchmentDim);
	}
	for (int i = 0; i < layout.rows && ui.first + i < int(rows.size()); ++i) {
		const int index = ui.first + i;
		const uint16_t entry = rows[size_t(index)];
		const SDL_Rect r = {layout.list.x, layout.list.y + i * layout.row_height, layout.list.w, layout.row_height};
		if (index == ui.row) {
			SDL_SetRenderDrawColor(renderer, 120, 80, 30, ui.letter_open ? 90 : 50);
			SDL_RenderFillRect(renderer, &r);
		}
		const LetterRow row = g_texts ? letter_row(save, *g_texts, entry) : LetterRow();
		const bool unread = (save.mail[entry].u8(1) & 0x80) && !queued_letter_operation(save, 0x6E, entry);
		const std::string date = row.date.size() >= 5 ? row.date.substr(0, 5) : row.date;  // «DD.MM».
		const int dw = text.measure(date);
		text.draw(cut_text(text, clean_game_text(row.title), r.w - dw - 12), r.x + 2, r.y + 2,
			unread ? SDL_Color{10, 5, 0, 255} : SDL_Color{130, 95, 60, 255});
		text.draw(date, r.x + r.w - 2 - dw, r.y + 2, kParchmentDim);
	}
	if (int(rows.size()) > layout.rows) {
		const std::string more = std::to_string(ui.first + 1) + "-" +
			std::to_string(std::min(int(rows.size()), ui.first + layout.rows)) + "/" + std::to_string(rows.size());
		text.draw(more, layout.title.x + layout.title.w - text.measure(more), layout.title.y, kParchmentDim);
	}
}

// What the letters' settings window shows of one row (0x409840..0x4099E0): the class's name with its days
// (0x6C1058: never deleted .. deleted at once), its notice and its sound, the event sound.
std::string mail_option_label(const MailUi& ui, int row) {
	if (row == kMailOptionRows - 1) {
		return letter_option_text(0x6C0FD0, "Notificación de sucesos", "event sound") + ": " +
			(ui.option_event_sound ? letter_option_text(0x6C0FD4, "Activo", "play") : "-");
	}
	const int c = row / 3;
	const std::string name = letter_option_text(0x6C0FB8 + 4 * uint32_t(c), "", "");
	switch (row % 3) {
	case 0: return name + ": " + letter_option_text(0x6C1058 + 4 * uint32_t(ui.option_days[size_t(c)]), "", "");
	case 1: return letter_option_text(0x6C0FC8, "Mostrar aviso", "show notice");
	default: return letter_option_text(0x6C0FCC, "Reproducir sonido", "play sound");
	}
}

// Where a row of the letters' settings goes on the parchment.
SDL_Rect mail_option_row(const MailLayout& layout, int line, int row) {
	return {layout.text.x, layout.text.y + 2 * line + 6 + row * (line + 6), layout.text.w, line + 4};
}

// The parchment over the town: the letter (or the chronicle), or the letters' settings window.
void draw_mail_parchment(SDL_Renderer* renderer, TextRenderer& text, const PanelArt& art, const MailUi& ui,
	const MailLayout& layout) {
	draw_scroll(renderer, art.paper, layout.panel);
	const SDL_Color rule = {120, 90, 50, 255};
	if (ui.options) {
		// The settings (0x409840): its title, then each class, its days with minus and plus and its two
		// boxes; «When news arrive...»; the event sound. The selected row is lit.
		const std::string title = letter_option_text(0x6C0F34, "Configuración de las notificaciones", "Configuration of letters");
		text.draw(title, layout.text.x + (layout.text.w - text.measure(title)) / 2, layout.text.y - 4, kParchmentInk);
		const int line = text.line_height();
		text.draw(letter_option_text(0x6C0FC4, "Cuando lleguen...", "When news arrive..."), layout.text.x + 4,
			layout.text.y + line, kParchmentDim);
		for (int row = 0; row < kMailOptionRows; ++row) {
			const SDL_Rect r = mail_option_row(layout, line, row);
			const int y = r.y + 2;
			if (row == ui.options_row) {
				SDL_SetRenderDrawColor(renderer, 120, 80, 30, 70);
				SDL_RenderFillRect(renderer, &r);
			}
			const bool box = row == kMailOptionRows - 1 || row % 3 != 0;
			int x = layout.text.x + (box && row != kMailOptionRows - 1 ? 28 : 4);
			if (box) {
				const int c = row / 3;
				const bool on = row == kMailOptionRows - 1 ? ui.option_event_sound
					: (ui.option_notices >> (row % 3 == 1 ? 3 + c : c)) & 1;
				const SDL_Rect check = {x, y + 2, line - 4, line - 4};
				SDL_SetRenderDrawColor(renderer, 245, 235, 205, 255);
				SDL_RenderFillRect(renderer, &check);
				SDL_SetRenderDrawColor(renderer, 90, 60, 30, 255);
				SDL_RenderDrawRect(renderer, &check);
				if (on) {
					SDL_RenderDrawLine(renderer, check.x + 3, check.y + check.h / 2, check.x + check.w / 2, check.y + check.h - 3);
					SDL_RenderDrawLine(renderer, check.x + check.w / 2, check.y + check.h - 3, check.x + check.w - 3, check.y + 3);
				}
				x += check.w + 8;
			}
			text.draw(mail_option_label(ui, row), x, y, kParchmentInk);
			if (row % 3 == 0 && row != kMailOptionRows - 1) {
				// Its arrows (the game's buttons 0x728 and 0x9E0, off at the ends).
				const int d = ui.option_days[size_t(row / 3)];
				text.draw("<", r.x + r.w - 46, y, d > 0 ? kParchmentInk : kParchmentDim);
				text.draw(">", r.x + r.w - 18, y, d < 5 ? kParchmentInk : kParchmentDim);
			}
		}
		draw_button(renderer, text, layout.close_button, BTN_CIRCLE + std::string(" ") + tr("Aceptar", "Accept"));
		return;
	}
	draw_letter_lines(renderer, text, ui, layout.text, kParchmentInk, rule);
	draw_button(renderer, text, layout.close_button, BTN_CIRCLE + std::string(" ") + tr("Cerrar", "Close"));
}

// --- The statistics (the game's SIDEMENU_STATISTIK, statistics_window.hpp): the menu in the panel's room (its four
// pages' tabs, the page's title, its entries) and the window on a parchment over the town (the entry's title, the
// period's buttons, the balance's table, a graph or Suministro's table). Opened by the top bar's money. Only the
// company overview (page 2) is done: the other pages' tabs are off. ---

// The tabs' frames in page_statistiken.tga (sidemenu.ini Button14..17 → textures.ini TEX20034's frames 8, 4, 12,
// 0): their column and width (rows: normal, pressed, lit, off).
constexpr int kStatsTabX[kStatisticsPages] = {0, 100, 36, 68};
constexpr int kStatsTabW[kStatisticsPages] = {36, 36, 32, 32};
constexpr bool kStatsPageDone[kStatisticsPages] = {false, false, true, false};

struct StatsRoomLayout {
	SDL_Rect tabs[kStatisticsPages] = {};
	int title_y = 0;
	SDL_Rect rows[7] = {};

	StatsRoomLayout(const SDL_Rect& room, int line_height) {
		int x = room.x + 10;
		for (int k = 0; k < kStatisticsPages; ++k) {
			const int w = kStatsTabW[k] * 5 / 4;
			tabs[k] = {x, room.y + 6, w, 40};
			x += w + 14;
		}
		title_y = room.y + 52;
		for (int k = 0; k < 7; ++k) {  // Under two lines of the page's title.
			rows[k] = {room.x + 8, title_y + 2 * line_height + 8 + k * (line_height + 10), room.w - 16, line_height + 6};
		}
	}
	int tab_at(int x, int y) const {
		for (int k = 0; k < kStatisticsPages; ++k) {
			if (inside(tabs[k], x, y)) {
				return k;
			}
		}
		return -1;
	}
	int row_at(int x, int y) const {
		for (int k = 0; k < 7; ++k) {
			if (inside(rows[k], x, y)) {
				return k;
			}
		}
		return -1;
	}
};

std::string stats_text(const std::string& raw) {
	return clean_game_text(raw);
}

void draw_stats_room(SDL_Renderer* renderer, TextRenderer& text, const PanelArt& art, const SidePanel& ui) {
	if (!g_texts) {
		return;
	}
	const PanelLayout panel(ui.folded);
	const StatsRoomLayout layout(panel.room, text.line_height());
	for (int k = 0; k < kStatisticsPages; ++k) {
		const int row = !kStatsPageDone[k] ? 3 : k == ui.stats_page ? 2 : 0;
		const SDL_Rect src = {kStatsTabX[k], row * 32, kStatsTabW[k], 32};
		draw_picture(renderer, art.stats_tabs, layout.tabs[k], &src);
	}
	int y = layout.title_y;
	for (const std::string& line : wrap_text(text, stats_text(statistics_page_title(*g_texts, ui.stats_page)), panel.room.w - 20)) {
		text.draw(line, panel.room.x + (panel.room.w - text.measure(line)) / 2, y, kParchmentInk);
		y += text.line_height();
	}
	const int first = kStatisticsPageFirst[size_t(ui.stats_page)];
	const int count = kStatisticsPageFirst[size_t(ui.stats_page) + 1] - first;
	for (int k = 0; k < count && k < 7; ++k) {
		const SDL_Rect& r = layout.rows[k];
		const bool lit = first + k == ui.stats_entry;
		SDL_SetRenderDrawColor(renderer, lit ? 120 : 150, lit ? 80 : 115, lit ? 30 : 70, lit ? 110 : 45);
		SDL_RenderFillRect(renderer, &r);
		const std::string name = stats_text(statistics_button(*g_texts, first + k));
		text.draw(name, r.x + (r.w - text.measure(name)) / 2, r.y + 3, kStatsPageDone[ui.stats_page] ? kParchmentInk : kParchmentDim);
	}
}

// The parchment over the town and its parts.
struct StatsLayout {
	SDL_Rect panel{};
	SDL_Rect inner{};  // Inside the rolls.
	int title_y = 0;
	SDL_Rect periods[4] = {};  // The balance's or the graph's period; Suministro: previous, the town's list, next.
	SDL_Rect body{};
	SDL_Rect close_button{};

	explicit StatsLayout(int line_height) {
		panel = {12, kBarHeight + 10, g_town_right - 24, kScreenHeight - kBarHeight - 20};
		inner = {panel.x + 40, panel.y + 52, panel.w - 80, panel.h - 52 - 50};
		title_y = inner.y;
		const int bh = line_height + 8;
		const int bw = (inner.w - 3 * 8) / 4;
		for (int k = 0; k < 4; ++k) {
			periods[k] = {inner.x + k * (bw + 8), title_y + line_height + 8, bw, bh};
		}
		body = {inner.x, periods[0].y + bh + 10, inner.w, 0};
		close_button = {panel.x + panel.w / 2 - 70, panel.y + panel.h - 48, 140, line_height + 10};
		body.h = close_button.y - 8 - body.y;
	}
	int period_at(int x, int y) const {
		for (int k = 0; k < 4; ++k) {
			if (inside(periods[k], x, y)) {
				return k;
			}
		}
		return -1;
	}
};

void draw_stats_toggle(SDL_Renderer* renderer, TextRenderer& text, const SDL_Rect& r, const std::string& label, bool lit,
	bool enabled) {
	SDL_SetRenderDrawColor(renderer, lit ? 120 : 150, lit ? 80 : 115, lit ? 30 : 70, enabled ? (lit ? 140 : 60) : 25);
	SDL_RenderFillRect(renderer, &r);
	SDL_SetRenderDrawColor(renderer, 110, 80, 40, enabled ? 255 : 90);
	SDL_RenderDrawRect(renderer, &r);
	text.draw(label, r.x + (r.w - text.measure(label)) / 2, r.y + (r.h - text.line_height()) / 2,
		enabled ? kParchmentInk : kParchmentDim);
}

// Suministro's town: the one chosen, or his home town, or the first of his offices' towns; -1 without any.
int stats_supply_town(const SaveGame& save, const SidePanel& ui) {
	const std::vector<int> towns = statistics_supply_towns(save, uint32_t(save.player));
	if (towns.empty()) {
		return -1;
	}
	const int home = save.merchants[size_t(save.player)].memory.u8(0x19);
	for (int want : {ui.stats_town, home}) {
		if (std::find(towns.begin(), towns.end(), want) != towns.end()) {
			return want;
		}
	}
	return towns.front();
}

// The graphs' «playing time»: only with more than a year recorded (0x4A5DD0: the years' list and [0x6DFC98] > 1).
bool stats_years_shown(const SaveGame& save) {
	return !save.statistics.towns[3].empty() && save.globals.u16(kStatistics + 0x8) > 1;
}

void draw_stats_balance(SDL_Renderer* renderer, TextRenderer& text, const StatisticsBalance& b, const SDL_Rect& body) {
	if (!b.found) {
		return;
	}
	// The game's rows (0x4A0C30), in two columns: on the left the company's value (now and at the start, each with
	// its date), the building and the income; on the right the expenditure and the total. The header «balance»
	// gives its period's line over both.
	const int line = text.line_height() + 2;
	const int column_w = (body.w - 20) / 2;
	auto value_text = [&](int k) { return k >= 0 && k < 20 ? stats_text(b.texts[size_t(k)]) : std::string(); };
	text.draw(stats_text(b.period), body.x, body.y, kParchmentInk);
	const int top = body.y + line + 4;
	auto header = [&](int column, int at, const std::string& label, const std::string& value) {
		const int x = body.x + column * (column_w + 20);
		const int y = top + at * line;
		text.draw(label, x, y, kParchmentInk);
		SDL_SetRenderDrawColor(renderer, 110, 80, 40, 255);
		SDL_RenderDrawLine(renderer, x, y + line - 2, x + column_w, y + line - 2);
		text.draw(value, x + column_w - text.measure(value), y, kParchmentInk);
	};
	auto row = [&](int column, int at, const std::string& label, const std::string& value) {
		const int x = body.x + column * (column_w + 20);
		const int y = top + at * line;
		const SDL_Rect band = {x - 2, y, column_w + 4, line - 1};
		SDL_SetRenderDrawColor(renderer, 230, 255, 255, 40);  // The game's 0x64FFE6CA.
		SDL_RenderFillRect(renderer, &band);
		text.draw(label, x, y, kParchmentDim);
		text.draw(value, x + column_w - text.measure(value), y, kParchmentInk);
	};
	const std::string value_label = stats_text(balance_label(*g_texts, 1));
	header(0, 0, value_label, std::string());
	row(0, 1, "  " + value_text(19), value_text(0));
	row(0, 2, "  " + value_text(18), value_text(1));
	row(0, 3, stats_text(balance_label(*g_texts, 3)), value_text(2));
	row(0, 4, stats_text(balance_label(*g_texts, 4)), value_text(3));
	header(0, 5, stats_text(balance_label(*g_texts, 5)), std::string());
	for (int k = 6; k <= 11; ++k) {
		row(0, k, stats_text(balance_label(*g_texts, k)), value_text(k - 2));
	}
	header(1, 0, stats_text(balance_label(*g_texts, 12)), std::string());
	for (int k = 13; k <= 19; ++k) {
		row(1, k - 12, stats_text(balance_label(*g_texts, k)), value_text(k - 3));
	}
	header(1, 8, stats_text(balance_label(*g_texts, 20)), value_text(17));
}

void draw_stats_graph(SDL_Renderer* renderer, TextRenderer& text, const PanelArt& art, const StatisticsGraph& g,
	int entry, const SDL_Rect& body) {
	const int line = text.line_height();
	const int side_w = 230;
	const SDL_Rect plot = {body.x + 4, body.y + 6, body.w - side_w - 18, body.h - 2 * line - 14};
	SDL_SetRenderDrawColor(renderer, 245, 232, 200, 120);
	SDL_RenderFillRect(renderer, &plot);
	SDL_SetRenderDrawColor(renderer, 110, 80, 40, 255);
	SDL_RenderDrawRect(renderer, &plot);
	// 0x4A5DD0's scale: from 2000 (1000 for Riqueza, kind 2 of 0x66FC0C) up to the largest point, at 0.95 of the height.
	float top = entry == kStatisticsWealth ? 1000.0f : 2000.0f;
	for (uint32_t s = 0; s < g.series && s < 4; ++s) {
		for (float v : g.values[s]) {
			top = std::max(top, v);
		}
	}
	for (int k = 1; k <= 4; ++k) {
		const int y = plot.y + plot.h - int(plot.h * 0.95f * float(k) / 4.0f);
		SDL_SetRenderDrawColor(renderer, 160, 130, 90, 120);
		SDL_RenderDrawLine(renderer, plot.x + 1, y, plot.x + plot.w - 2, y);
		const std::string label = format_money(int64_t(top * float(k) / 4.0f));
		text.draw(label, plot.x + 4, y - line + 2, kParchmentDim);
	}
	if (g.points >= 1) {
		// The recorded points (the oldest `missing` aren't), joined, each with a dot.
		const float step = g.points > 1 ? float(plot.w - 6) / float(g.points - 1) : 0.0f;
		for (uint32_t s = 0; s < g.series && s < 4; ++s) {
			const uint32_t colour = kStatisticsColours[s];
			SDL_SetRenderDrawColor(renderer, Uint8(colour >> 16), Uint8(colour >> 8), Uint8(colour), 255);
			auto at = [&](uint32_t k) {
				return SDL_Point{plot.x + 3 + int(step * float(k)),
					plot.y + plot.h - 1 - int(float(plot.h) * 0.95f * std::max(0.0f, g.values[s][k]) / top)};
			};
			for (uint32_t p = g.missing; p < g.points && p < g.values[s].size(); ++p) {
				const SDL_Point c = at(p);
				const SDL_Rect dot = {c.x - 2, c.y - 2, 5, 5};
				SDL_RenderFillRect(renderer, &dot);
				if (p > g.missing) {
					const SDL_Point a = at(p - 1);
					SDL_RenderDrawLine(renderer, a.x, a.y, c.x, c.y);
					SDL_RenderDrawLine(renderer, a.x, a.y - 1, c.x, c.y - 1);
				}
			}
		}
	}
	// The series' names under the graph, each with its colour.
	int x = plot.x;
	for (uint32_t s = 0; s < 4; ++s) {
		if (!g.names[s]) {
			continue;
		}
		const uint32_t colour = kStatisticsColours[s];
		const SDL_Rect swatch = {x, plot.y + plot.h + 8 + line / 4, line / 2 + 4, line / 2};
		SDL_SetRenderDrawColor(renderer, Uint8(colour >> 16), Uint8(colour >> 8), Uint8(colour), 255);
		SDL_RenderFillRect(renderer, &swatch);
		const std::string name = stats_text(g_texts->raw_at(g.names[s]));
		text.draw(name, swatch.x + swatch.w + 6, plot.y + plot.h + 6, kParchmentInk);
		x += swatch.w + 6 + text.measure(name) + 18;
	}
	// The texts beside it: each label, then its value (with the coins for money, «%» for the averages).
	int y = body.y + 4;
	const int sx = body.x + body.w - side_w;
	for (size_t k = 0; k < g.texts.size(); ++k) {
		const std::string label = k < g.labels.size() ? stats_text(g_texts->raw_at(g.labels[k])) : std::string();
		for (const std::string& part : wrap_text(text, label, side_w)) {
			text.draw(part, sx, y, kParchmentDim);
			y += line;
		}
		std::string value = stats_text(g.texts[k]);
		const uint8_t kind = k < g.kinds.size() ? g.kinds[k] : 0;
		if (kind == 4) {
			value += " %";
		}
		const int vw = text.measure(value);
		int vx = sx + side_w - vw;
		if (kind == 1) {
			draw_picture(renderer, art.coins, {vx - 16, y - 2, 13, 20});
		} else if (kind == 2 || kind == 3) {
			SDL_Texture* icon = art.list_icons[PanelArt::Bale];
			draw_picture(renderer, icon, {vx - 20, y, 16, 16});
		}
		text.draw(value, vx, y, kParchmentInk);
		y += line + 6;
	}
}

void draw_stats_supply(SDL_Renderer* renderer, TextRenderer& text, const PanelArt& art,
	const std::vector<SupplyRow>& rows, const SDL_Rect& body) {
	const int line = text.line_height() + 3;
	const int column_w = (body.w - 20) / 2;
	const SDL_Color red = {170, 20, 10, 255};
	static constexpr bool kInBarrels[20] = {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0};
	const std::string office = stats_text(g_texts->raw_at(0x6A0468));
	const std::string hall = stats_text(g_texts->raw_at(0x6A0814));
	const std::string buy = stats_text(g_texts->raw_at(0x6A0810));
	for (int column = 0; column < 2; ++column) {
		const int x = body.x + column * (column_w + 20);
		const int c1 = x + column_w * 58 / 100;  // The office's, the hall's and the price's right edges.
		const int c2 = x + column_w * 77 / 100;
		const int c3 = x + column_w;
		text.draw(stats_text(g_texts->raw_at(0x6A0464)), x, body.y, kParchmentInk);
		text.draw(office, c1 - text.measure(office), body.y, kParchmentInk);
		text.draw(hall, c2 - text.measure(hall), body.y, kParchmentInk);
		text.draw(buy, c3 - text.measure(buy), body.y, kParchmentInk);
		SDL_SetRenderDrawColor(renderer, 110, 80, 40, 255);
		SDL_RenderDrawLine(renderer, x, body.y + line - 1, c3, body.y + line - 1);
		for (int k = 0; k < 10; ++k) {
			const size_t i = size_t(column * 10 + k);
			if (i >= rows.size()) {
				break;
			}
			const SupplyRow& r = rows[i];
			const int y = body.y + (k + 1) * line + 2;
			const int unit = kInBarrels[r.ware] ? 200 : 2000;
			SDL_Texture* icon = art.list_icons[kInBarrels[r.ware] ? PanelArt::Barrel : PanelArt::Bale];
			draw_picture(renderer, icon, {x, y, 16, 16});
			text.draw(ware_name(r.ware), x + 20, y, kParchmentDim);
			if (r.office / unit > 0 || r.needed > 0) {
				const std::string v = (r.needed > 0 ? "*" : "") + std::to_string(r.office / unit);
				text.draw(v, c1 - text.measure(v), y, r.needed > r.office ? red : kParchmentInk);
			}
			if (r.town / unit > 0) {
				const std::string v = std::to_string(r.town / unit);
				text.draw(v, c2 - text.measure(v), y, kParchmentInk);
			}
			if (r.price > 0) {
				const std::string v = format_money(r.price);
				text.draw(v, c3 - text.measure(v), y, kParchmentInk);
			}
		}
	}
}

void draw_stats_window(SDL_Renderer* renderer, TextRenderer& text, const PanelArt& art, const SaveGame& save,
	const std::vector<std::string>& town_names, const SidePanel& ui) {
	if (!g_texts || save.player < 0) {
		return;
	}
	const StatsLayout layout(text.line_height());
	draw_scroll(renderer, art.paper, layout.panel);
	const int entry = ui.stats_entry;
	const uint32_t player = uint32_t(save.player);
	std::string title = stats_text(statistics_title(*g_texts, entry));
	if (entry == kStatisticsSupply) {
		const int town = stats_supply_town(save, ui);
		title = GameTexts::format(title, {town >= 0 ? town_name(save, town_names, town) : std::string()});
		const std::string choose = stats_text(g_texts->raw_at(0x6B0D68));
		draw_stats_toggle(renderer, text, layout.periods[0], "<", false, town >= 0);
		draw_stats_toggle(renderer, text, {layout.periods[1].x, layout.periods[1].y,
			layout.periods[2].x + layout.periods[2].w - layout.periods[1].x, layout.periods[1].h},
			town >= 0 ? town_name(save, town_names, town) : choose, true, town >= 0);
		draw_stats_toggle(renderer, text, layout.periods[3], ">", false, town >= 0);
		if (town >= 0) {
			draw_stats_supply(renderer, text, art, statistics_supply(save, player, uint32_t(town)), layout.body);
		}
	} else if (entry == kStatisticsBalance) {
		for (int k = 0; k < 4; ++k) {
			draw_stats_toggle(renderer, text, layout.periods[k], stats_text(g_texts->raw_at(0x6C1A74 + 4 * uint32_t(k))),
				k == ui.stats_balance_period, true);
		}
		draw_stats_balance(renderer, text, statistics_balance(save, *g_texts, player, ui.stats_balance_period), layout.body);
	} else {
		static constexpr uint32_t kPeriodText[4] = {0x6C1A38, 0x6C1A3C, 0x6C1A40, 0x6C1A44};
		for (int k = 0; k < 4; ++k) {
			draw_stats_toggle(renderer, text, layout.periods[k], stats_text(g_texts->raw_at(kPeriodText[k])),
				k == ui.stats_graph_period, k < 3 || stats_years_shown(save));
		}
		draw_stats_graph(renderer, text, art, statistics_graph(save, player, entry, ui.stats_graph_period), entry,
			layout.body);
	}
	text.draw(title, layout.inner.x + (layout.inner.w - text.measure(title)) / 2, layout.title_y, kParchmentInk);
	draw_button(renderer, text, layout.close_button, BTN_CIRCLE + std::string(" ") + tr("Cerrar", "Close"));
}

// --- The tooltips of the side panel's and the top bar's buttons (the scripts' «ToolTip=», the strings
// 20000.. of the exe's string table, GameTexts::resource): on the Vita, while a finger is on the button
// (before it is lifted) or the pad points at it (Select). ---

std::string tooltip_text(uint32_t id) {
	return g_texts ? clean_game_text(g_texts->resource(id)) : std::string();
}

// The tooltip of what is at (x, y), and where it is; 0 if nothing there has one.
uint32_t panel_tooltip(const SidePanel& side, const SaveGame& save, int line, bool messages, bool build_menu, int x,
	int y, SDL_Rect& where) {
	auto hit = [&](const SDL_Rect& r) {
		if (inside(r, x, y)) {
			where = r;
			return true;
		}
		return false;
	};
	if (hit(kHourglassButton)) {
		return 20046;  // Chronicle
	}
	if (hit(g_money_button)) {
		return 20045;  // Statistics
	}
	const PanelLayout panel(side.folded);
	if (side.folded) {
		return 0;
	}
	static constexpr uint32_t kIcons[kPanelIcons] = {20027, 20001, 20008, 20002, 20026, 20000};
	for (int k = 0; k < kPanelIcons; ++k) {
		if (hit(panel.icons[k])) {
			return kIcons[k];
		}
	}
	for (int k = 0; k < kPanelShipButtons; ++k) {
		if (hit(panel.ships[k])) {
			return 20004 + uint32_t(k);  // Ships in port, at sea, convoys in port, at sea.
		}
	}
	if (side.menu == PanelMenu::Ship && side.ship >= 0 && size_t(side.ship) < save.ships.size()) {
		const ShipMenuLayout layout(panel.room, line);
		static constexpr uint32_t kPages[kShipPages] = {20028, 20029, 20030, 20031, 20032, 20034};
		for (int k = 0; k < kShipPages; ++k) {
			if (hit(layout.pages[k])) {
				return k == int(ShipPage::Convoy) && ship_in_convoy(save, side.ship) ? 20033 : kPages[k];
			}
		}
	}
	if (messages) {
		const MailLayout layout(line, panel.room);
		for (int k = 0; k < 4; ++k) {
			if (hit(layout.tabs[size_t(k)])) {
				return 20009 + uint32_t(k);
			}
		}
		if (hit(layout.options_button)) {
			return 20013;
		}
		if (hit(layout.delete_button)) {
			return 20014;
		}
	}
	if (side.menu == PanelMenu::Statistics) {
		const StatsRoomLayout layout(panel.room, line);
		for (int k = 0; k < kStatisticsPages; ++k) {
			if (hit(layout.tabs[k])) {
				return 20015 + uint32_t(k);  // City data, Municipal comparison, Own company, Competition
			}
		}
	}
	if (build_menu) {
		const BuildRoomLayout layout(panel.room, line);
		static constexpr uint32_t kTabs[5] = {20019, 20020, 20041, 20021, 20022};
		for (int k = 0; k < 5; ++k) {
			if (hit(layout.tabs[k])) {
				return kTabs[k];
			}
		}
	}
	return 0;
}

// The tooltip's box: to the left of a button of the panel, under one of the top bar.
void draw_tooltip(SDL_Renderer* renderer, TextRenderer& text, const std::string& tip, const SDL_Rect& where) {
	if (tip.empty()) {
		return;
	}
	const int w = text.measure(tip) + 12;
	const int h = text.line_height() + 6;
	const bool bar = where.y < kBarHeight;
	SDL_Rect box = bar ? SDL_Rect{where.x + where.w - w, where.y + where.h + 4, w, h}
		: SDL_Rect{where.x - w - 6, where.y + (where.h - h) / 2, w, h};
	box.x = std::clamp(box.x, 4, kScreenWidth - w - 4);
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(renderer, 250, 240, 200, 245);  // The game's yellowish tooltip.
	SDL_RenderFillRect(renderer, &box);
	SDL_SetRenderDrawColor(renderer, 60, 40, 20, 255);
	SDL_RenderDrawRect(renderer, &box);
	text.draw(tip, box.x + 6, box.y + 3, SDL_Color{20, 12, 4, 255});
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
	auto panel_art = std::make_unique<PanelArt>(renderer, *archive);
	SidePanel side;  // The side panel.
	std::unique_ptr<SeaMapView> sea;  // Se carga la primera vez que se abre.
	std::vector<std::string> town_names = load_town_names(*archive);
	g_town_names = &town_names;
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
		mail.letter_open = mail.tab == 0;  // In the side panel: the list first, the chronicle on its parchment.
		mail.options = false;
		mail.last_check = -1;
		market.open = false;
		hanse.open = false;
		place.open = Place::None;
		if (selection.mode == Mode::Saved || (selection.mode == Mode::Sea && sea && panel_art)) {
			side.menu = PanelMenu::Messages;  // The side panel's messages (SIDEMENU_INFOS).
			side.focus = -1;
		}
	};
	auto mail_tab = [&](int tab) {
		expire_letters(save, save.player);
		mail.tab = (tab + 4) % 4;
		mail.row = 0;
		mail.selected = 0xFFFF;
		mail.first = 0;
		mail.stale = true;
		mail.letter_open = mail.tab == 0;
		mail.options = false;
	};
	// The letters' settings window (0x408F50): it opens with the player's settings, and on closing queues
	// them (0x70) and the check of his letters (0x71 with 1), as the game.
	auto mail_options = [&](bool open) {
		if (save.player < 0 || size_t(save.player) >= save.merchants.size()) {
			return;
		}
		const MerchantMemory& m = save.merchants[size_t(save.player)].memory;
		if (open) {
			mail.options = true;
			mail.options_row = 0;
			mail.option_days = {m.u8(0x38), m.u8(0x39), m.u8(0x3A)};
			mail.option_notices = m.u8(0x26);
			mail.option_event_sound = m.u8(0x3B) != 0;
			return;
		}
		if (mail.options) {
			mail.options = false;
			queue_operation(save, letter_settings_operation(uint32_t(save.player), mail.option_days, mail.option_event_sound,
				mail.option_notices));
			queue_operation(save, letters_check_operation(uint32_t(save.player), true));
		}
	};
	// A row of the settings changed: the days of its class one more or less (0..5), or its box.
	auto mail_option_change = [&](int row, int step) {
		if (row == kMailOptionRows - 1) {
			mail.option_event_sound = !mail.option_event_sound;
		} else if (row % 3 == 0) {
			uint8_t& d = mail.option_days[size_t(row / 3)];
			d = uint8_t(std::clamp(int(d) + step, 0, 5));
		} else {
			mail.option_notices ^= uint8_t(1u << (row % 3 == 1 ? 3 + row / 3 : row / 3));
		}
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
	float touch_scroll = 0;  // A drag on a list of the side panel, not yet a whole row.
	bool finger_down = false;  // A finger on the screen now (the tooltip of the button under it).
	bool touch_on_panel = false;
	bool touch_on_side = false;  // It started on the side panel.
	bool touch_building = false;  // It places the building chosen in the build menu.
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
		if (trading) {
			fit_market_mode(save, town_index, market);  // A convoy chosen or no longer there (Select, the operations).
		}
		// The Hanse panel, in the save's towns like the market (never both open).
		const bool in_hanse = hanse.open && !trading && selection.mode == Mode::Saved && save.player >= 0 && small_text;
		const HanseView hanse_now = in_hanse ? hanse_view(save, town_index, hanse.page) : HanseView();
		// The window of a place or of the ship, in the save's towns (never with
		// the market or the Hanse panel).
		const bool in_place = place.open != Place::None && !trading && !in_hanse && selection.mode == Mode::Saved &&
			save.player >= 0 && small_text;
		const PlaceView place_now = in_place ? place_view(save, town_index, place) : PlaceView();
		if (!in_place && place.typing) {
			place.typing = false;
			SDL_StopTextInput();
		}
		const bool in_menu = menu.open() && small_text;
		// In a town the messages are the side panel's: another of its menus closes them.
		if (mail.open && (selection.mode == Mode::Saved || (selection.mode == Mode::Sea && sea && panel_art)) &&
			side.menu != PanelMenu::Messages) {
			mail_options(false);
			mail.open = false;
		}
		// The mailbox, over the town or the sea map.
		const bool in_mail = mail.open && has_save && save.player >= 0 && small_text &&
			(selection.mode == Mode::Saved || selection.mode == Mode::Sea);
		// The side panel (the game's, on the right) in the save's towns; the
		// town takes the screen on its left.
		const bool panel_shown = (selection.mode == Mode::Saved || (selection.mode == Mode::Sea && sea)) && has_save &&
			save.player >= 0 && small_text && panel_art;
		const bool sea_panel = panel_shown && selection.mode == Mode::Sea;  // On the sea map: its minimap and the ships.
		if (sea_panel && side.menu == PanelMenu::Build) {
			side.menu = PanelMenu::None;
		}
		const PanelLayout panel_layout(side.folded);
		const int town_width = panel_shown ? panel_layout.town_right : kScreenWidth;
		g_town_right = town_width;
		if (!panel_shown) {
			side.focus = -1;
		}
		// The building in the middle of the town's part of the screen, the
		// buttons' cursor (as the game's mouse pointer): X goes into it.
		const bool town_free = selection.mode == Mode::Saved && has_save && save.player >= 0 && !trading && !in_hanse &&
			!in_place && !in_mail && !in_menu;
		const int hover = town_free ? view->place_at(camera_x + town_width / 2, camera_y + kScreenHeight / 2) : -1;
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
			if (p == Place::Lender) {
				// When it opens (0x5B2B00): his debts, and the shares on sale
				// (0x4D9320, which may drop a second pool of his company).
				place.lender_debts = int(loans_owed(save, save.player).size());
				shares_on_sale(save);
			}
			market.open = false;
			hanse.open = false;
		};
		// With a window open the pad, L, R, X, Circle and Select are its own.
		auto place_button = [&](Uint8 button) {
			const int rows = int(place_now.rows.size());
			auto add_amount = [&](int step) {
				place.amount = std::clamp(place.amount + step, 0, std::max(0, place_now.max_amount));
				place.name.clear();  // The rename page: back to the game's names.
			};
			const bool pages = place_pages(place) > 0;
			const bool renaming = place.open == Place::Shipyard && pages && place.page == 4;
			if (place.typing) {
				// Writing a ship's name: X or Circle end it.
				if (button == SDL_CONTROLLER_BUTTON_A || button == SDL_CONTROLLER_BUTTON_B) {
					place.typing = false;
					SDL_StopTextInput();
				}
				return true;
			}
			if (pages && (button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER || button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) {
				turn_page(place, button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER ? -1 : 1);
				return true;
			}
			if (place.open == Place::Lender && place.page == 2 &&
				(button == SDL_CONTROLLER_BUTTON_DPAD_LEFT || button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) {
				// The interest's kind to grant a loan.
				place.lender_kind = std::clamp(place.lender_kind + (button == SDL_CONTROLLER_BUTTON_DPAD_LEFT ? -1 : 1), 0, 4);
				return true;
			}
			if (renaming && button == SDL_CONTROLLER_BUTTON_X) {
				place.typing = true;
				SDL_StartTextInput();
				return true;
			}
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
		// In a town the messages are the side panel's (its room and a parchment over the town).
		const bool mail_in_panel = in_mail && panel_shown && side.menu == PanelMenu::Messages;
		const bool mail_parchment = mail_in_panel && (mail.letter_open || mail.options || mail.tab == 0);
		const MailLayout mail_layout = mail_in_panel ? MailLayout(small_text->line_height(), panel_layout.room)
													 : MailLayout(small_text ? small_text->line_height() : 20, mail.tab == 0);
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
		// The messages closed in the side panel (the settings sent, if they were open).
		auto close_mail = [&]() {
			mail_options(false);
			mail.open = false;
			if (side.menu == PanelMenu::Messages) {
				side.menu = PanelMenu::None;
			}
		};
		// In the side panel: the settings window's buttons; the list's (Up and Down a letter, which opens,
		// Left and Right the tabs, L and R the letter's pages, X opens it, Square deletes it, Select the
		// settings, Circle closes the letter and then the messages).
		auto mail_panel_button = [&](Uint8 button) {
			const int line = small_text->line_height();
			if (mail.options) {
				switch (button) {
				case SDL_CONTROLLER_BUTTON_DPAD_UP:
				case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
					mail.options_row = (mail.options_row + kMailOptionRows + (button == SDL_CONTROLLER_BUTTON_DPAD_UP ? -1 : 1)) %
						kMailOptionRows;
					return true;
				case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
				case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
					mail_option_change(mail.options_row, button == SDL_CONTROLLER_BUTTON_DPAD_LEFT ? -1 : 1);
					return true;
				case SDL_CONTROLLER_BUTTON_A:
					mail_option_change(mail.options_row, 1);
					return true;
				case SDL_CONTROLLER_BUTTON_B:
				case SDL_CONTROLLER_BUTTON_BACK:
					mail_options(false);
					return true;
				case SDL_CONTROLLER_BUTTON_START:
					return false;
				default:
					return true;
				}
			}
			switch (button) {
			case SDL_CONTROLLER_BUTTON_DPAD_UP:
			case SDL_CONTROLLER_BUTTON_DPAD_DOWN: {
				const int step = button == SDL_CONTROLLER_BUTTON_DPAD_UP ? -1 : 1;
				if (mail.tab == 0) {
					mail_scroll(step * line);
				} else {
					mail_select(mail.row + step);
					mail.letter_open = !mail_list.empty();
				}
				return true;
			}
			case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
			case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: {
				const int step = button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER ? -1 : 1;
				if (mail_parchment) {
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
			case SDL_CONTROLLER_BUTTON_A:
				mail_select(mail.row);
				mail.letter_open = mail.tab == 0 || !mail_list.empty();
				return true;
			case SDL_CONTROLLER_BUTTON_BACK:  // Select: the letters' settings.
				mail_options(true);
				return true;
			case SDL_CONTROLLER_BUTTON_X:  // Square.
				mail_delete();
				return true;
			case SDL_CONTROLLER_BUTTON_B:  // Circle.
				if (mail.letter_open && mail.tab != 0) {
					mail.letter_open = false;
				} else {
					close_mail();
				}
				return true;
			case SDL_CONTROLLER_BUTTON_START:
				return false;
			default:
				return true;
			}
		};
		// With the mailbox open every button but Start is its own.
		auto mail_button = [&](Uint8 button) {
			if (mail_in_panel) {
				return mail_panel_button(button);
			}
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
			const std::vector<int> modes = market_modes(save, town_index, market);
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
			const int sea_width = has_save && panel_art && save.player >= 0 ? PanelLayout(side.folded).town_right : kScreenWidth;
			camera_x = focus.x - sea_width / 2;
			camera_y = focus.y - kScreenHeight / 2;
			// The panel's room: the chosen ship's menu, as the game shows it for the ship selected.
			if (has_save && save.player >= 0 && (side.menu == PanelMenu::None || side.menu == PanelMenu::Build) &&
				fleet_ship() >= 0) {
				side.menu = PanelMenu::Ship;
				side.ship = fleet_ship();
			}
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
				// In the middle of the town's part of the screen (left of the side panel).
				const int width = selection.mode == Mode::Saved && has_save && save.player >= 0
					? PanelLayout(side.folded).town_right
					: kScreenWidth;
				const SDL_Point focus = focus_point(*view, save, selection);
				camera_x = focus.x - width / 2;
				camera_y = focus.y - kScreenHeight / 2;
			}
			reload = false;
			keep_camera = false;
		}
		// The build menu: what it offers now, the chosen building's page, the
		// marks of where it fits (made again when the town's buildings change)
		// and whether it fits the cell it is placed on, with the picture the
		// game's window would ask for there (0x47B8C0).
		BuildMenu build_now;
		std::array<bool, 5> build_tabs_now{};
		std::optional<BuildChoice> build_choice_now;
		bool build_fits_now = false;
		BuildGhost build_ghost_now = side.build_ghost;
		if (panel_shown && side.menu == PanelMenu::Build) {
			const uint32_t player = uint32_t(save.player);
			build_now = build_menu(save, town_index, player, side.build_category);
			side.build_category = build_now.category;
			build_tabs_now = build_tabs(save, town_index, player);
			if (side.build_type >= 0) {
				build_choice_now = build_choice(save, town_index, player, side.build_type);
				if (build_choice_now->action == BuildAction::Place) {
					const uint64_t key = drawn_buildings_key(save, size_t(town_index));
					if (side.marks_type != side.build_type || side.marks_town != town_index || side.marks_key != key) {
						side.marks = build_marks(save, uint32_t(town_index), side.build_type);
						side.marks_type = side.build_type;
						side.marks_town = town_index;
						side.marks_key = key;
					}
					if (side.cell_x >= 0) {
						int type = side.build_type;
						build_fits_now = fn_47b8c0(save, uint32_t(town_index), uint8_t(side.cell_x), uint8_t(side.cell_y),
							type, build_ghost_now);
					}
				}
			}
		}
		const bool placing = build_choice_now && build_choice_now->action == BuildAction::Place && town_free;
		// The ship menu (SIDEMENU_SHIP): its ship, while it is still the player's; what its crew and convoy
		// pages offer (the ship's window's rows: 0 and 1 the crew's, from 2 the convoy's).
		PlaceView ship_now;
		bool ship_pirate_ok = false;  // Its crew page's «Pirate» / «Civil» can be pressed (0x518750).
		if (sea_panel && side.menu == PanelMenu::Ship && fleet_ship() >= 0) {
			side.ship = fleet_ship();  // On the sea map the menu is the chosen ship's (Square, the ship buttons).
		}
		if (side.menu == PanelMenu::Ship) {
			const bool valid = side.ship >= 0 && size_t(side.ship) < save.ships.size() && save.player >= 0 &&
				save.ships[size_t(side.ship)].in_use() && save.ships[size_t(side.ship)].owner() == save.player;
			if (!valid || !panel_shown) {
				side.menu = PanelMenu::None;
				side.ship = -1;
			} else {
				if (!ship_page_enabled(save, side.ship, side.ship_page)) {
					side.ship_page = ShipPage::Goods;
				}
				side.ship_ui.open = Place::Ship;
				side.ship_ui.ship = size_t(side.ship);
				ship_now = place_view(save, town_index, side.ship_ui);
				// «Pirate» makes the ship raid: a town it reaches is attacked (the battle in a town, the battles'
				// piece 6, which p3vita hasn't yet: seen with the scenario pirate_sailing in save 5), and it may attack
				// ships (piece 7). Until then the viewer shows it off; 0x0D and 0x518750 are rewritten and checked.
				constexpr bool kPirateModeReady = false;
				ship_pirate_ok = kPirateModeReady && sailing && can_turn_pirate(save, *sailing, side.ship);
				const int rows = ship_goods_rows(save, side.ship);
				const ShipMenuLayout ship_layout(panel_layout.room, small_text->line_height());
				side.cargo_first = std::clamp(side.cargo_first, 0, std::max(0, rows - ship_layout.cargo_rows));
			}
		}
		// The finger at the town's pixel (x, y) puts the building there: on the
		// cell under it if it fits there, otherwise on the nearest cell around
		// where it fits (the game's cell is the building's front one, so a touch
		// inside a green area finds it), otherwise under the finger (red).
		auto place_at = [&](int x, int y) {
			const SDL_Point under = TownView::cell_at(x, y);
			SDL_Point best = under;
			double nearest = 1e9;
			for (int cy = under.y - 4; cy <= under.y + 4; ++cy) {
				for (int cx = under.x - 2; cx <= under.x + 2; ++cx) {
					if (cx < 0 || cy < 0 || cx > 0xFF || cy > 0xFF) {
						continue;
					}
					int type = side.build_type;
					BuildGhost ghost = side.build_ghost;
					if (!fn_47b8c0(save, uint32_t(town_index), uint8_t(cx), uint8_t(cy), type, ghost)) {
						continue;
					}
					// The building stands up from its front cell: its middle a row above.
					const double dx = x - (cell_screen_x(cx, cy) + kTileWidth / 2.0);
					const double dy = y - (cell_screen_y(cy) + kTileHeight / 2.0 - kRowStep);
					const double d = dx * dx + dy * dy + (cx == under.x && cy == under.y ? -1e6 : 0);
					if (d < nearest) {
						nearest = d;
						best = {cx, cy};
					}
				}
			}
			side.cell_x = best.x;
			side.cell_y = best.y;
		};
		// Building it where it is placed, or the window's own buttons (the
		// streets, the well, the wall): the game's operations, run at once.
		auto build_act = [&]() {
			if (!build_choice_now) {
				return;
			}
			const uint32_t player = uint32_t(save.player);
			const uint32_t town = uint32_t(town_index);
			std::vector<OperationBytes> ops;
			switch (build_choice_now->action) {
			case BuildAction::Street:
				if (build_choice_now->button) {
					ops.push_back(street_operation(town, player));
				}
				break;
			case BuildAction::Well:
				if (build_choice_now->button) {
					ops.push_back(well_operation(save, town, player));
				}
				break;
			case BuildAction::Wall:
				if (build_choice_now->button) {
					ops.push_back(wall_operation(player, town, build_choice_now->type));
				}
				break;
			case BuildAction::Place:
				if (side.cell_x >= 0) {
					int type = side.build_type;
					if (const std::optional<OperationBytes> op = place_operation(save, player, town, uint8_t(side.cell_x),
							uint8_t(side.cell_y), type, side.build_ghost)) {
						ops.push_back(*op);
					}
					side.build_type = type;
				}
				break;
			default:
				break;
			}
			if (!ops.empty()) {
				run_operations(save, sailing.get(), ops);
				side.cell_x = -1;  // The new building shows: no picture over it.
				side.cell_y = -1;
			}
		};
		// Choosing a building of the menu (the window asks for its picture).
		auto build_choose = [&](int row) {
			int n = 0;
			for (const int32_t type : build_now.types) {
				if (type >= 0 && n++ == row) {
					side.build_type = type;
					side.build_row = row;
					side.build_ghost = build_ghost(save, uint32_t(save.player), type);
					side.cell_x = -1;
					side.cell_y = -1;
					side.marks_type = -1;
					return;
				}
			}
		};
		// Its buttons while it is open: the menu (Up and Down, L and R the
		// categories, X chooses, Circle closes it) or the building's page (the
		// pad moves the building in the town, X builds, Circle goes back).
		auto build_button = [&](Uint8 button) {
			if (!build_choice_now) {
				const int rows = int(std::count_if(build_now.types.begin(), build_now.types.end(),
					[](int32_t t) { return t >= 0; }));
				switch (button) {
				case SDL_CONTROLLER_BUTTON_DPAD_UP:
				case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
					if (rows > 0) {
						side.build_row = (side.build_row + rows + (button == SDL_CONTROLLER_BUTTON_DPAD_UP ? -1 : 1)) % rows;
					}
					return true;
				case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
				case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
					for (int k = 1; k <= 5; ++k) {
						const int c = (side.build_category + 5 + (button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER ? -k : k)) % 5;
						if (build_tabs_now[size_t(c)]) {
							side.build_category = c;
							side.build_row = 0;
							break;
						}
					}
					return true;
				case SDL_CONTROLLER_BUTTON_A:
					build_choose(side.build_row);
					return true;
				case SDL_CONTROLLER_BUTTON_B:
					side.menu = PanelMenu::None;
					return true;
				default:
					return false;
				}
			}
			switch (button) {
			case SDL_CONTROLLER_BUTTON_DPAD_UP:
			case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
			case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
			case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
				if (placing) {
					if (side.cell_x < 0) {
						const SDL_Point c = TownView::cell_at(camera_x + town_width / 2, camera_y + kScreenHeight / 2);
						side.cell_x = c.x;
						side.cell_y = c.y;
					} else if (button == SDL_CONTROLLER_BUTTON_DPAD_LEFT || button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) {
						side.cell_x = std::max(0, side.cell_x + (button == SDL_CONTROLLER_BUTTON_DPAD_LEFT ? -1 : 1));
					} else {
						side.cell_y = std::max(0, side.cell_y + (button == SDL_CONTROLLER_BUTTON_DPAD_UP ? -2 : 2));
					}
				}
				return true;
			case SDL_CONTROLLER_BUTTON_A:
				build_act();
				return true;
			case SDL_CONTROLLER_BUTTON_B:
				side.build_type = -1;
				side.cell_x = -1;
				side.cell_y = -1;
				return true;
			default:
				return false;
			}
		};
		// The ship menu's page: its first row (the crew's or the convoy's) and amount.
		auto ship_page = [&](ShipPage page) {
			if (side.ship < 0 || !ship_page_enabled(save, side.ship, page)) {
				return;
			}
			side.ship_page = page;
			side.ship_ui.row = page == ShipPage::Crew ? 0 : page == ShipPage::Convoy || page == ShipPage::LeaveConvoy ? 2 : 0;
			side.ship_ui.amount = 0;
		};
		// Its action: the selected row of the ship's window (place_act, as the game's window sends it).
		auto ship_act = [&]() {
			if (side.menu != PanelMenu::Ship || side.ship < 0) {
				return;
			}
			if (side.ship_page == ShipPage::Crew && side.ship_ui.row == 2) {
				// «Pirate» / «Civil»: 0x0D {ship, [+0x15C] == 0} (0x494244), run at once.
				if (ship_pirate_ok) {
					run_operations(save, sailing.get(), {pirate_operation(uint32_t(side.ship), save.ships[size_t(side.ship)].raid() == 0)});
				}
				return;
			}
			const bool crew = side.ship_page == ShipPage::Crew && side.ship_ui.row < 2;
			const bool convoy = (side.ship_page == ShipPage::Convoy || side.ship_page == ShipPage::LeaveConvoy) &&
				side.ship_ui.row >= 2 && !(side.ship_page == ShipPage::Convoy && ship_in_convoy(save, side.ship));
			if (crew || convoy) {
				place_act(save, sailing.get(), town_index, side.ship_ui, ship_now);
				if (!side.ship_ui.message.empty()) {
					notify(side.ship_ui.message);  // As the game's lines, over the town.
					side.ship_ui.message.clear();
				}
			}
		};
		// Its buttons while it is open: L and R the pages, Up and Down the rows (the goods page's list),
		// Left and Right the amount (the sailors to dismiss, the public convoy's destination), X does it,
		// Circle closes it.
		auto ship_button = [&](Uint8 button) {
			const int rows = int(ship_now.rows.size());
			switch (button) {
			case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
			case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
				for (int k = 1; k < kShipPages; ++k) {
					const int p = (int(side.ship_page) + kShipPages + (button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER ? -k : k)) % kShipPages;
					if (ship_page_enabled(save, side.ship, ShipPage(p))) {
						ship_page(ShipPage(p));
						break;
					}
				}
				return true;
			case SDL_CONTROLLER_BUTTON_DPAD_UP:
			case SDL_CONTROLLER_BUTTON_DPAD_DOWN: {
				const int step = button == SDL_CONTROLLER_BUTTON_DPAD_UP ? -1 : 1;
				if (side.ship_page == ShipPage::Goods) {
					side.cargo_first += step;  // Clamped next frame.
				} else if (side.ship_page == ShipPage::Crew) {
					const bool captain = save.ships[size_t(side.ship)].captain() < save.globals.u16(global::kCaptainCount);
					side.ship_ui.row = std::clamp(side.ship_ui.row + step, 0, captain ? 2 : 1);
					side.ship_ui.amount = 0;
				} else if (side.ship_page == ShipPage::Convoy && !ship_in_convoy(save, side.ship) && rows > 2) {
					side.ship_ui.row = 2 + (side.ship_ui.row - 2 + step + (rows - 2)) % (rows - 2);
					side.ship_ui.amount = 0;
				}
				return true;
			}
			case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
			case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
				if (ship_now.max_amount >= 0) {
					side.ship_ui.amount = std::clamp(side.ship_ui.amount + (button == SDL_CONTROLLER_BUTTON_DPAD_LEFT ? -1 : 1), 0,
						ship_now.max_amount);
				}
				return true;
			case SDL_CONTROLLER_BUTTON_A:
				ship_act();
				return true;
			case SDL_CONTROLLER_BUTTON_B:
				side.menu = PanelMenu::None;
				return true;
			default:
				return false;
			}
		};

		// Pasa el tiempo: los ticks que caben en el tiempo real transcurrido.
		if (sailing && in_menu) {
			clock = app_ticks();  // The game stops while the menu is open.
		} else if (sailing) {
			const GameSpeed& speed = kGameSpeeds[game_speed];
			const Uint32 now = app_ticks();
			const Uint32 elapsed = now - clock;
			clock = now - elapsed % speed.ms_per_tick;
			const int ticks =
				std::min(int(elapsed / speed.ms_per_tick), speed.max_ticks) + (script ? script->extra_ticks() : 0);
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
		// While the letter window is open the game queues 0x71 {player, 0} once a tick (0x47CD83: when the
		// tick's low byte isn't the one it last saw): the player's old letters removed (0x4D6780).
		if (in_mail && mail.open && sailing && save.player >= 0) {
			const int t = int(save.tick() & 0xFF);
			if (t != mail.last_check) {
				queue_operation(save, letters_check_operation(uint32_t(save.player), false));
				mail.last_check = t;
			}
		}

		// The living town: when the simulation changes the buildings of the town
		// on screen, it is drawn again, with the camera where it was.
		if (selection.mode == Mode::Saved && live.changed(save)) {
			TownMap town;
			live.draw(save, view->tiles(), town);
			view->show(town);
		}

		int town_step = 0;
		bool next_mode = false;  // Triángulo: entre la ciudad y el mapa del mar.
		bool next_debug_view = false;  // Select: vistas de las plantillas de ciudad.
		// A ship of the player chosen in the panel: one in port, its window (in
		// its town); one at sea, the sea map with it chosen.
		auto go_to_ship = [&](int chosen) {
			const SavedShip& ship = save.ships[size_t(chosen)];
			const uint16_t status = ship.status();
			const bool at_sea = status == kShipLeaving || status == kShipArriving || status == kShipSailing || status == 0x10;
			// Its menu in the panel (the game's ship menu); the sea map's chosen ship too.
			if (side.ship != chosen) {
				side.ship_ui = PlaceUi();
				side.cargo_first = 0;
			}
			side.menu = PanelMenu::Ship;
			side.ship = chosen;
			const std::vector<int> fleet = player_fleet(save);
			const auto at = std::find(fleet.begin(), fleet.end(), chosen);
			if (at != fleet.end()) {
				fleet_index = size_t(at - fleet.begin());
			}
			if (!at_sea) {
				// In its town (from the sea map, into it).
				if (ship.town() < save.towns.size() && (ship.town() != town_index || selection.mode == Mode::Sea)) {
					selection.saved_index = ship.town();
					market.open = false;
					place.open = Place::None;
					if (selection.mode == Mode::Sea) {
						next_mode = true;
					} else {
						reload = true;
					}
				}
			} else if (selection.mode == Mode::Sea) {
				// On the sea map: it is chosen there, in the middle of the map's part.
				camera_x = (ship.x() >> 16) - town_width / 2;
				camera_y = (ship.y() >> 16) - kScreenHeight / 2;
			} else {
				next_mode = true;
			}
		};
		// The side panel's icons (0..5) and ship buttons (6..9), tapped or with X.
		auto side_act = [&](int item) {
			if (item < kPanelIcons) {
				if (!panel_state(save, sea_panel).icon_enabled[item]) {
					return;  // A disabled icon doesn't react, with the pad as with a touch.
				}
				switch (PanelIcon(item)) {
				case PanelIcon::ShipList:
					side.menu = side.menu == PanelMenu::ShipList ? PanelMenu::None : PanelMenu::ShipList;
					break;
				case PanelIcon::FastForward:
					game_speed = (game_speed + 1) % kGameSpeedCount;
					notify((g_texts ? g_texts->get(GameText::GameSpeed) : tr("Velocidad", "Speed")) + ": " +
						speed_name(game_speed));
					break;
				case PanelIcon::Messages:
					if (side.menu == PanelMenu::Messages && mail.open) {
						close_mail();
					} else {
						open_mailbox();
					}
					break;
				case PanelIcon::Build:
					side.menu = side.menu == PanelMenu::Build ? PanelMenu::None : PanelMenu::Build;
					break;
				case PanelIcon::LeaveTown:
					next_mode = true;
					break;
				case PanelIcon::Options:
					open_menu(MenuScreen::Main, kRowContinue);
					break;
				default:
					break;
				}
				return;
			}
			// A ship button: the next ship of its list.
			const int button = item - kPanelIcons;
			const std::vector<int> ships = panel_ships(save, button);
			if (ships.empty()) {
				return;
			}
			int& pick = side.ship_pick[button];
			pick = (pick + 1) % int(ships.size());
			go_to_ship(ships[size_t(pick)]);
		};
		// The buttons on the side panel (Select): left and right go along its
		// icons and ship buttons, X does it, Circle and Select leave it.
		auto side_button = [&](Uint8 button) {
			const int items = kPanelIcons + kPanelShipButtons;
			switch (button) {
			case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
			case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
				side.focus = (side.focus + items + (button == SDL_CONTROLLER_BUTTON_DPAD_LEFT ? -1 : 1)) % items;
				return true;
			case SDL_CONTROLLER_BUTTON_DPAD_UP:
			case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
				// Between the icons and the ship buttons below them.
				side.focus = side.focus < kPanelIcons ? kPanelIcons + std::min(side.focus, kPanelShipButtons - 1)
													  : side.focus - kPanelIcons;
				return true;
			case SDL_CONTROLLER_BUTTON_A:
				side_act(side.focus);
				return true;
			case SDL_CONTROLLER_BUTTON_B:
			case SDL_CONTROLLER_BUTTON_BACK:
				side.focus = -1;
				return true;
			default:
				return false;
			}
		};
		// The statistics (the top bar's money): Left and Right their pages (the done ones), Up and Down the page's
		// entries (the parchment shows each), L and R the period (Suministro: the town), Circle closes them.
		const bool stats_open = side.menu == PanelMenu::Statistics && panel_shown && !side.folded && has_save &&
			save.player >= 0 && small_text && !trading && !in_hanse && !in_place && !in_mail && !in_menu;
		auto stats_pick_town = [&](int step) {
			const std::vector<int> towns = statistics_supply_towns(save, uint32_t(save.player));
			if (towns.empty()) {
				return;
			}
			const auto it = std::find(towns.begin(), towns.end(), stats_supply_town(save, side));
			const int i = it == towns.end() ? 0 : int(it - towns.begin());
			side.stats_town = towns[size_t((i + int(towns.size()) + step) % int(towns.size()))];
		};
		auto stats_step_period = [&](int step) {
			if (side.stats_entry == kStatisticsSupply) {
				stats_pick_town(step);
			} else if (side.stats_entry == kStatisticsBalance) {
				side.stats_balance_period = (side.stats_balance_period + 4 + step) % 4;
			} else {
				const int periods = stats_years_shown(save) ? 4 : 3;
				side.stats_graph_period = (std::min(side.stats_graph_period, periods - 1) + periods + step) % periods;
			}
		};
		auto stats_button = [&](Uint8 button) {
			switch (button) {
			case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
			case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
				for (int k = 1; k < kStatisticsPages; ++k) {
					const int page = (side.stats_page + kStatisticsPages + (button == SDL_CONTROLLER_BUTTON_DPAD_LEFT ? -k : k)) %
						kStatisticsPages;
					if (kStatsPageDone[page]) {
						side.stats_page = page;
						side.stats_entry = kStatisticsPageFirst[size_t(page)];
						break;
					}
				}
				return true;
			case SDL_CONTROLLER_BUTTON_DPAD_UP:
			case SDL_CONTROLLER_BUTTON_DPAD_DOWN: {
				const int first = kStatisticsPageFirst[size_t(side.stats_page)];
				const int count = kStatisticsPageFirst[size_t(side.stats_page) + 1] - first;
				const int step = button == SDL_CONTROLLER_BUTTON_DPAD_UP ? -1 : 1;
				side.stats_entry = first + (side.stats_entry - first + count + step) % count;
				return true;
			}
			case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
			case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
				stats_step_period(button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER ? -1 : 1);
				return true;
			case SDL_CONTROLLER_BUTTON_B:
				side.menu = PanelMenu::None;
				return true;
			default:
				return false;
			}
		};
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
				if (side.focus >= 0 && panel_shown && town_free && side_button(event.cbutton.button)) {
					break;
				}
				if (side.menu == PanelMenu::Build && panel_shown && town_free && build_button(event.cbutton.button)) {
					break;
				}
				if (stats_open && side.focus < 0 && stats_button(event.cbutton.button)) {
					break;
				}
				if (side.menu == PanelMenu::Ship && panel_shown && town_free && side.focus < 0 &&
					ship_button(event.cbutton.button)) {
					break;
				}
				if (side.menu == PanelMenu::ShipList && panel_shown && town_free && side.focus < 0) {
					// The ship list: Up and Down choose, X goes to the ship, Circle closes it.
					const int ships = int(player_fleet(save).size());
					const Uint8 b = event.cbutton.button;
					if ((b == SDL_CONTROLLER_BUTTON_DPAD_UP || b == SDL_CONTROLLER_BUTTON_DPAD_DOWN) && ships > 0) {
						side.list_row = (side.list_row + ships + (b == SDL_CONTROLLER_BUTTON_DPAD_UP ? -1 : 1)) % ships;
						break;
					}
					if (b == SDL_CONTROLLER_BUTTON_A && ships > 0) {
						go_to_ship(player_fleet(save)[size_t(std::min(side.list_row, ships - 1))]);
						break;
					}
					if (b == SDL_CONTROLLER_BUTTON_B) {
						side.menu = PanelMenu::None;
						break;
					}
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
					// en la ciudad el panel lateral (sus iconos y botones de barcos
					// con la cruceta y X) o vista de plantilla.
					if (trading) {
						market.ship++;
					} else if (selection.mode == Mode::Sea) {
						game_speed = (game_speed + 1) % kGameSpeedCount;
					} else if (panel_shown && town_free) {
						side.folded = false;
						side.focus = 0;
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
			case SDL_TEXTINPUT:
				// A ship's name being written (the shipyard's rename page), in Latin-1
				// as the game's names; 20 characters at most.
				if (place.typing) {
					place.name += utf8_to_latin1(event.text.text);
					if (place.name.size() > 20) {
						place.name.resize(20);
					}
				}
				break;
			case SDL_KEYDOWN:
				if (place.typing) {
					const SDL_Keycode key = event.key.keysym.sym;
					if (key == SDLK_BACKSPACE && !place.name.empty()) {
						place.name.pop_back();
					} else if (key == SDLK_RETURN || key == SDLK_ESCAPE) {
						place.typing = false;
						SDL_StopTextInput();
					}
					break;
				}
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
						: key == SDLK_v || key == SDLK_n ? SDL_CONTROLLER_BUTTON_X
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
				touch_scroll = 0;
				finger_down = true;
				touch_on_side = panel_shown && !in_menu && (!in_mail || mail_in_panel) && !messages.open && !end_window.open &&
					inside(panel_layout.panel, touch_start.x, touch_start.y);
				touch_on_panel = touch_on_side ||
					(trading && inside(MarketLayout(small_text->line_height()).panel, touch_start.x, touch_start.y)) ||
					(in_hanse && inside(HanseLayout(small_text->line_height()).panel, touch_start.x, touch_start.y)) ||
					(in_place && inside(PlaceLayout(small_text->line_height()).panel, touch_start.x, touch_start.y)) ||
					(in_menu && (inside(MenuLayout(small_text->line_height(), menu.screen).panel, touch_start.x, touch_start.y) ||
						menu.ask != MenuAsk::None)) ||
					(in_mail && (!mail_in_panel || mail_parchment) && inside(mail_layout.panel, touch_start.x, touch_start.y)) ||
					(stats_open && inside(StatsLayout(small_text->line_height()).panel, touch_start.x, touch_start.y));
				// Placing a building: the finger puts it on the cell it touches.
				touch_building = placing && !touch_on_side && !in_menu && !in_mail && touch_start.x < town_width &&
					touch_start.y >= kBarHeight;
				if (touch_building) {
					place_at(camera_x + touch_start.x, camera_y + touch_start.y);
					touch_on_panel = true;
				}
				break;
			case SDL_FINGERMOTION:
				touch_moved += std::abs(int(event.tfinger.dx * kScreenWidth)) +
					std::abs(int(event.tfinger.dy * kScreenHeight));
				// Arrastrar desplaza la ciudad con el dedo, salvo sobre el mercado;
				// sobre el minimapa del panel, la vista va donde está el dedo.
				if (touch_building) {
					// Dragging the building: it follows the finger, cell by cell.
					place_at(camera_x + int(event.tfinger.x * kScreenWidth), camera_y + int(event.tfinger.y * kScreenHeight));
				} else if (touch_on_side) {
					SDL_Point at;
					if (!side.folded && side.menu == PanelMenu::Ship && side.ship_page == ShipPage::Goods &&
						inside(panel_layout.room, touch_start.x, touch_start.y)) {
						// Dragging the goods page's list moves it a row at a time.
						touch_scroll -= event.tfinger.dy * kScreenHeight;
						const float row = float(small_text->line_height() + 2);
						for (; touch_scroll >= row; touch_scroll -= row) {
							++side.cargo_first;
						}
						for (; touch_scroll <= -row; touch_scroll += row) {
							--side.cargo_first;
						}
					} else if (!side.folded && inside(panel_layout.map, touch_start.x, touch_start.y) &&
						(sea_panel ? minimap_sea_point(panel_layout, *sea, int(event.tfinger.x * kScreenWidth),
										 int(event.tfinger.y * kScreenHeight), at)
								   : minimap_town_point(panel_layout, *view, int(event.tfinger.x * kScreenWidth),
										 int(event.tfinger.y * kScreenHeight), at))) {
						camera_x = at.x - town_width / 2;
						camera_y = at.y - kScreenHeight / 2;
					}
				} else if (!touch_on_panel) {
					camera_x -= int(event.tfinger.dx * kScreenWidth);
					camera_y -= int(event.tfinger.dy * kScreenHeight);
				} else if (in_mail && !in_menu && inside(mail_layout.text, touch_start.x, touch_start.y)) {
					mail_scroll(-int(event.tfinger.dy * kScreenHeight));  // Dragging the letter moves it.
				}
				break;
			case SDL_FINGERUP:
				finger_down = false;
				if (touch_moved < kTapDistance && !in_menu && !messages.open && !end_window.open && has_save &&
					save.player >= 0 && (selection.mode == Mode::Saved || selection.mode == Mode::Sea) &&
					inside(kHourglassButton, touch_start.x, touch_start.y)) {
					// The top bar's hourglass: the chronicle (the messages' public news).
					touch_building = false;
					touch_on_side = false;
					touch_on_panel = false;
					if (!mail.open) {
						open_mailbox();
					}
					mail_tab(0);
					break;
				}
				if (touch_moved < kTapDistance && !in_menu && !messages.open && !end_window.open && has_save &&
					save.player >= 0 && (selection.mode == Mode::Saved || selection.mode == Mode::Sea) && panel_shown &&
					!trading && !in_hanse && !in_place && inside(g_money_button, touch_start.x, touch_start.y)) {
					// The top bar's money: the statistics, as the game's on the company overview with its balance.
					touch_building = false;
					touch_on_side = false;
					touch_on_panel = false;
					if (side.menu == PanelMenu::Statistics) {
						side.menu = PanelMenu::None;
					} else {
						if (mail.open) {
							close_mail();
						}
						side.menu = PanelMenu::Statistics;
						side.folded = false;
						side.focus = -1;
						side.stats_page = 2;
						side.stats_entry = kStatisticsBalance;
					}
					break;
				}
				if (stats_open && touch_moved < kTapDistance &&
					inside(StatsLayout(small_text->line_height()).panel, touch_start.x, touch_start.y)) {
					// The statistics' parchment: its period's buttons (Suministro: the towns), or closed.
					const StatsLayout sl(small_text->line_height());
					const int x = touch_start.x;
					const int y = touch_start.y;
					const int k = sl.period_at(x, y);
					if (inside(sl.close_button, x, y)) {
						side.menu = PanelMenu::None;
					} else if (k >= 0 && side.stats_entry == kStatisticsSupply) {
						if (k == 0 || k == 3) {
							stats_pick_town(k == 0 ? -1 : 1);
						}
					} else if (k >= 0 && side.stats_entry == kStatisticsBalance) {
						side.stats_balance_period = k;
					} else if (k >= 0 && (k < 3 || stats_years_shown(save))) {
						side.stats_graph_period = k;
					}
					touch_building = false;
					touch_on_side = false;
					touch_on_panel = false;
					break;
				}
				if (touch_building) {
					touch_building = false;  // It stays where the finger left it.
					touch_on_panel = false;
					break;
				}
				if (touch_on_side) {
					// The side panel: its tab, icons, ship buttons and minimap.
					touch_on_side = false;
					touch_on_panel = false;
					if (touch_moved >= kTapDistance) {
						break;
					}
					const int x = touch_start.x;
					const int y = touch_start.y;
					SDL_Point at;
					if (inside(panel_layout.tab, x, y)) {
						side.folded = !side.folded;
						side.focus = -1;
					} else if (side.folded) {
						// Folded, only its tab.
					} else if (const int icon = panel_layout.icon_at(x, y); icon >= 0) {
						side_act(icon);  // Nothing if it is disabled.
					} else if (const int ship = panel_layout.ship_at(x, y); ship >= 0) {
						side_act(kPanelIcons + ship);
					} else if (side.menu == PanelMenu::ShipList && inside(panel_layout.room, x, y)) {
						const std::vector<int> fleet = player_fleet(save);
						const int first = std::max(0, side.list_row - kShipListRows + 1);
						for (int k = 0; k < kShipListRows && first + k < int(fleet.size()); ++k) {
							if (inside(ship_list_row(panel_layout.room, small_text->line_height(), k), x, y)) {
								side.list_row = first + k;
								go_to_ship(fleet[size_t(first + k)]);
							}
						}
					} else if (side.menu == PanelMenu::Statistics && inside(panel_layout.room, x, y)) {
						// The statistics' menu: a page's tab (the done ones), or an entry of the page.
						const StatsRoomLayout room(panel_layout.room, small_text->line_height());
						const int tab = room.tab_at(x, y);
						const int row = room.row_at(x, y);
						const int first = kStatisticsPageFirst[size_t(side.stats_page)];
						const int count = kStatisticsPageFirst[size_t(side.stats_page) + 1] - first;
						if (tab >= 0 && kStatsPageDone[tab]) {
							side.stats_page = tab;
							side.stats_entry = kStatisticsPageFirst[size_t(tab)];
						} else if (row >= 0 && row < count && kStatsPageDone[side.stats_page]) {
							side.stats_entry = first + row;
						}
					} else if (side.menu == PanelMenu::Build && inside(panel_layout.room, x, y)) {
						const BuildRoomLayout room(panel_layout.room, small_text->line_height());
						if (!build_choice_now) {
							const int tab = room.tab_at(x, y);
							const int row = room.row_at(x, y);
							if (tab >= 0 && build_tabs_now[size_t(tab)]) {
								side.build_category = tab;
								side.build_row = 0;
							} else if (row >= 0) {
								build_choose(row);
							}
						} else if (inside(room.back_button, x, y)) {
							side.build_type = -1;
							side.cell_x = -1;
							side.cell_y = -1;
						} else if (inside(room.act_button, x, y)) {
							build_act();
						}
					} else if (mail_in_panel && inside(panel_layout.room, x, y)) {
						// The messages: a tab, the settings, delete, or a letter (it opens).
						const int tab = mail_layout.tab_at(x, y);
						const int row = mail.tab != 0 ? mail_layout.row_at(x, y) : -1;
						if (tab >= 0) {
							mail_options(false);
							mail_tab(tab);
						} else if (inside(mail_layout.options_button, x, y)) {
							mail_options(!mail.options);
						} else if (inside(mail_layout.delete_button, x, y)) {
							mail_delete();
						} else if (row >= 0 && mail.first + row < int(mail_list.size())) {
							mail_options(false);
							mail_select(mail.first + row);
							mail.letter_open = true;
						}
					} else if (side.menu == PanelMenu::Ship && side.ship >= 0 && inside(panel_layout.room, x, y)) {
						// The ship menu: its page buttons, the crew page's and the convoy pages' buttons and rows.
						const ShipMenuLayout room(panel_layout.room, small_text->line_height());
						const int page = room.page_at(x, y);
						auto again = [&]() { ship_now = place_view(save, town_index, side.ship_ui); };
						if (page >= 0) {
							ship_page(ShipPage(page));
						} else if (side.ship_page == ShipPage::Crew) {
							if (inside(room.minus_button, x, y) || inside(room.plus_button, x, y)) {
								if (side.ship_ui.row != 0) {
									side.ship_ui.row = 0;
									side.ship_ui.amount = 0;
								}
								const int most = ship_window_open(save, side.ship) ? save.ships[size_t(side.ship)].sailors() : 0;
								side.ship_ui.amount =
									std::clamp(side.ship_ui.amount + (inside(room.minus_button, x, y) ? -1 : 1), 0, most);
							} else if (inside(room.sailors_button, x, y) && side.ship_ui.row == 0) {
								ship_act();
							} else if (inside(room.captain_button, x, y)) {
								side.ship_ui.row = 1;
								side.ship_ui.amount = 0;
								again();
								ship_act();
							} else if (inside(room.pirate_button, x, y) &&
								save.ships[size_t(side.ship)].captain() < save.globals.u16(global::kCaptainCount)) {
								side.ship_ui.row = 2;
								ship_act();
							}
						} else if (side.ship_page == ShipPage::Convoy || side.ship_page == ShipPage::LeaveConvoy) {
							const int row = room.convoy_row_at(x, y);
							const bool destination = ship_now.amount_choice && ship_now.max_amount >= 0;
							if (row >= 0 && 2 + row < int(ship_now.rows.size()) && 2 + row != side.ship_ui.row) {
								side.ship_ui.row = 2 + row;
								side.ship_ui.amount = 0;
							} else if (destination && (inside(room.dest_minus, x, y) || inside(room.dest_plus, x, y))) {
								side.ship_ui.amount = std::clamp(side.ship_ui.amount + (inside(room.dest_minus, x, y) ? -1 : 1), 0,
									ship_now.max_amount);
							} else if (inside(room.act_button, x, y)) {
								ship_act();
							}
						}
					} else if (sea_panel ? minimap_sea_point(panel_layout, *sea, x, y, at)
										 : minimap_town_point(panel_layout, *view, x, y, at)) {
						camera_x = at.x - town_width / 2;
						camera_y = at.y - kScreenHeight / 2;
					}
					break;
				}
				if (mail_in_panel && !in_menu && touch_moved < kTapDistance) {
					// The parchment over the town: its button, the settings' rows; a tap on the town outside it
					// closes it.
					const int x = touch_start.x;
					const int y = touch_start.y;
					touch_on_panel = false;
					if (!mail_parchment || !inside(mail_layout.panel, x, y)) {
						if (mail_parchment && x < town_width && y >= kBarHeight) {
							mail_options(false);
							if (mail.tab != 0) {
								mail.letter_open = false;
							} else {
								close_mail();
							}
						}
						break;
					}
					if (inside(mail_layout.close_button, x, y)) {
						if (mail.options) {
							mail_options(false);
						} else if (mail.tab != 0) {
							mail.letter_open = false;
						} else {
							close_mail();
						}
					} else if (mail.options) {
						for (int row = 0; row < kMailOptionRows; ++row) {
							const SDL_Rect r = mail_option_row(mail_layout, small_text->line_height(), row);
							if (inside(r, x, y)) {
								mail.options_row = row;
								// The days: their arrows on the right (the left one less, the right one more).
								const bool days = row % 3 == 0 && row != kMailOptionRows - 1;
								if (days && x >= r.x + r.w - 56) {
									mail_option_change(row, x >= r.x + r.w - 28 ? 1 : -1);  // Its arrows.
								} else if (days) {
									const uint8_t d = mail.option_days[size_t(row / 3)];
									mail.option_days[size_t(row / 3)] = uint8_t((d + 1) % 6);  // Its name: the next one.
								} else {
									mail_option_change(row, 1);
								}
							}
						}
					}
					break;
				}
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
					const bool pages = place_pages(place) > 0;
					const bool kinds = place.open == Place::Lender && place.page == 2;  // The interest's kind.
					if (place.typing) {
						place.typing = false;  // A tap ends the writing.
						SDL_StopTextInput();
					} else if (pages && y >= layout.panel.y && y < layout.rows_y && x >= layout.panel.x &&
						x < layout.panel.x + layout.panel.w) {
						// The title: the page before (its left half) or after.
						turn_page(place, x < layout.panel.x + layout.panel.w / 2 ? -1 : 1);
					} else if (kinds && place_now.max_amount >= 0 &&
						(inside(layout.minus_button, x, y) || inside(layout.plus_button, x, y))) {
						place.lender_kind = std::clamp(place.lender_kind + (inside(layout.minus_button, x, y) ? -1 : 1), 0, 4);
					} else if (place_now.max_amount >= 0 && inside(layout.minus_button, x, y)) {
						place.amount = std::max(0, place.amount - 1);
						place.name.clear();
					} else if (place_now.max_amount >= 0 && inside(layout.plus_button, x, y)) {
						place.amount = std::min(place_now.max_amount, place.amount + 1);
						place.name.clear();
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
					// A tap in the town: a building to go into, as in the game, or one
					// of the player's own (demolish it or auction it).
					const int building = view->place_at(camera_x + touch_start.x, camera_y + touch_start.y);
					const SDL_Point cell = TownView::cell_at(camera_x + touch_start.x, camera_y + touch_start.y);
					const int own = own_building_at(save, uint32_t(town_index), cell.x, cell.y, uint32_t(save.player));
					if (const PlaceInfo* p = building >= 0 ? place_of(uint32_t(building)) : nullptr) {
						open_place(p->place, building);
					} else if (own >= 0) {
						open_place(Place::Building, -1);
						place.record = own;
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
		if (!trading && !in_hanse && !in_mail && !in_place && side.focus < 0 && side.menu == PanelMenu::None) {
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
			camera_x = cell_screen_x(look_x, look_y) - town_width / 2;
			camera_y = cell_screen_y(look_y) - kScreenHeight / 2;
		}
		const int view_width = at_sea ? sea->width() : view->width();
		const int view_height = at_sea ? sea->height() : view->height();
		camera_x = std::clamp(camera_x, 0, std::max(0, view_width - town_width));  // The panel's part isn't the view's.
		camera_y = std::clamp(camera_y, 0, std::max(0, view_height - kScreenHeight));

		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
		SDL_RenderClear(renderer);
		const int course_ship = fleet_ship();
		if (at_sea) {
			sea->draw(save, small_text.get(), town_names, camera_x, camera_y, selection.saved_index,
				course_ship >= 0 ? &save.ships[size_t(course_ship)] : nullptr);
			if (panel_shown) {
				// The side panel on the sea map: the sea's minimap, the icons, the ship buttons and the
				// chosen ship's menu.
				draw_side_panel(renderer, *small_text, *panel_art, side, panel_state(save, true), nullptr,
					{camera_x, camera_y, town_width, kScreenHeight}, sea.get(), &save);
				if (side.menu == PanelMenu::ShipList && !side.folded) {
					draw_ship_list(renderer, *small_text, save, town_names, side);
				}
				if (side.menu == PanelMenu::Ship && side.ship >= 0 && !side.folded) {
					draw_ship_menu(renderer, *small_text, *panel_art, save, side, ship_now, ship_pirate_ok);
				}
				if (side.menu == PanelMenu::Statistics && !side.folded) {
					draw_stats_room(renderer, *small_text, *panel_art, side);
					draw_stats_window(renderer, *small_text, *panel_art, save, town_names, side);
				}
			}
		} else {
			// The building under the cursor now (after this frame's camera): glowing, with its name.
			const bool free_now = selection.mode == Mode::Saved && has_save && save.player >= 0 && !market.open &&
				!hanse.open && place.open == Place::None && !mail.open && !menu.open();
			const int pointed = free_now && side.menu != PanelMenu::Build
				? view->place_at(camera_x + town_width / 2, camera_y + kScreenHeight / 2)
				: -1;
			view->draw(camera_x, camera_y, pointed);
			if (placing && side.cell_x >= 0) {
				// After this frame's touches: whether it fits where it is now.
				int type = side.build_type;
				build_ghost_now = side.build_ghost;
				build_fits_now = fn_47b8c0(save, uint32_t(town_index), uint8_t(side.cell_x), uint8_t(side.cell_y), type,
					build_ghost_now);
			}
			if (placing) {
				// Where the chosen building fits, marked green as the game's town view
				// marks it, and the building where it is placed, green or red.
				view->draw_marks(side.marks, save.towns[size_t(town_index)].memory.s32(0x7A8), camera_x, camera_y,
					town_width);
				if (side.cell_x >= 0) {
					const TileDef* tile =
						view->building_tile(build_ghost_now.catalog, build_param(build_ghost_now), live.region());
					if (build_fits_now) {
						view->draw_tile(tile, side.cell_x, side.cell_y, camera_x, camera_y, 120, 255, 120);
					} else {
						view->draw_tile(tile, side.cell_x, side.cell_y, camera_x, camera_y, 255, 90, 90);
					}
				}
			}
			if (free_now && small_text && side.menu != PanelMenu::Build) {
				// The cursor: a small cross in the middle of the screen, light on dark.
				const int cx = town_width / 2;
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
				const SDL_Rect box = {std::clamp(b.x + b.w / 2 - camera_x - w / 2, 4, town_width - w - 4),
					std::clamp(b.y - camera_y - h - 2, kBarHeight + 2, kScreenHeight - h - 4), w, h};
				draw_panel_box(renderer, box);
				small_text->draw(name, box.x + padding, box.y + padding / 2, kTextColor);
			}
			if (panel_shown) {
				draw_side_panel(renderer, *small_text, *panel_art, side, panel_state(save), view.get(),
					{camera_x, camera_y, town_width, kScreenHeight});
				if (side.menu == PanelMenu::ShipList && !side.folded) {
					draw_ship_list(renderer, *small_text, save, town_names, side);
				}
				if (side.menu == PanelMenu::Build && !side.folded) {
					draw_build_room(renderer, *small_text, *panel_art, side, build_now, build_tabs_now,
						build_choice_now ? &*build_choice_now : nullptr, side.cell_x >= 0, build_fits_now);
				}
				if (side.menu == PanelMenu::Ship && side.ship >= 0 && !side.folded) {
					draw_ship_menu(renderer, *small_text, *panel_art, save, side, ship_now, ship_pirate_ok);
				}
				if (side.menu == PanelMenu::Statistics && !side.folded) {
					draw_stats_room(renderer, *small_text, *panel_art, side);
					draw_stats_window(renderer, *small_text, *panel_art, save, town_names, side);
				}
			}
		}
		if (text) {
			std::vector<std::string> right_texts;
			if (side.menu == PanelMenu::Statistics && panel_shown && !side.folded && !in_mail && !in_menu) {
				right_texts = {tr("L/R: periodo   " BTN_CIRCLE " cerrar", "L/R: period   " BTN_CIRCLE " close"),
					tr(BTN_CIRCLE " cerrar", BTN_CIRCLE " close")};
			} else if (in_mail && mail_in_panel) {
				// The side panel's messages: the tabs with Left and Right, the settings with Select.
				right_texts = mail.options
					? std::vector<std::string>{tr(BTN_CROSS " cambiar   " BTN_CIRCLE " aceptar", BTN_CROSS " change   " BTN_CIRCLE " accept")}
					: std::vector<std::string>{tr("Select: ajustes   " BTN_SQUARE " borrar   " BTN_CIRCLE " cerrar",
												   "Select: settings   " BTN_SQUARE " delete   " BTN_CIRCLE " close"),
						  tr(BTN_SQUARE " borrar   " BTN_CIRCLE " cerrar", BTN_SQUARE " delete   " BTN_CIRCLE " close")};
			} else if (in_mail) {
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
				const int pointed = view->place_at(camera_x + town_width / 2, camera_y + kScreenHeight / 2);
				const std::string x = BTN_CROSS " " + (pointed >= 0 ? place_name(pointed) : tr("mercado", "market"));
				const std::string ship = tr("   Select: panel", "   Select: panel");
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
			if (has_save && save.player >= 0 && panel_art && small_text &&
				(selection.mode == Mode::Saved || selection.mode == Mode::Sea)) {
				// The game's top bar; its hourglass turns while the time runs (the menu stops it).
				const SavedTown& here = save.towns[selection.saved_index];
				const std::string place = selection.mode == Mode::Sea
					? tr("Mapa del mar - ", "Sea map - ") + town_name(save, town_names, int(selection.saved_index))
					: town_name(save, town_names, int(selection.saved_index));
				const int frame = in_menu ? 0 : int(app_ticks() / 90);
				draw_top_bar(renderer, *small_text, *panel_art,
					selection.mode == Mode::Saved ? format_money(here.citizens()) : std::string(), place,
					format_money(save.merchants[size_t(save.player)].money()), game_date(save.day(), save.month(), save.year()),
					frame, right_texts);
			} else {
				draw_bar(renderer, *text, 0, bar_title(save, town_names, template_ids, selection), right_texts);
			}
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
				{tr(BTN_SQUARE " barco   Select: ", BTN_SQUARE " ship   Select: ") + speed, "Select: " + speed, speed},
				town_width);
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
			const bool in_panel = mail_in_panel && side.menu == PanelMenu::Messages;
			const MailLayout layout = in_panel ? MailLayout(small_text->line_height(), PanelLayout(side.folded).room)
											   : MailLayout(small_text->line_height(), mail.tab == 0);
			const auto at = std::find(rows.begin(), rows.end(), mail.selected);
			mail.row = at != rows.end() ? int(at - rows.begin()) : std::clamp(mail.row, 0, std::max(0, int(rows.size()) - 1));
			mail.selected = rows.empty() ? 0xFFFF : rows[size_t(mail.row)];
			mail.first = std::clamp(mail.first, std::max(0, mail.row - layout.rows + 1), mail.row);
			if (in_panel && rows.empty() && mail.tab != 0) {
				mail.letter_open = false;
			}
			// In the side panel a letter is made (and marked read) only once its parchment is open.
			const uint16_t entry = mail.tab == 0 ? 0xFFFE : in_panel && !mail.letter_open ? 0xFFFF : mail.selected;
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
			if (in_panel) {
				if (!side.folded) {
					draw_mail_panel(renderer, *small_text, *panel_art, save, mail, rows, layout);
				}
				if (mail.letter_open || mail.options || mail.tab == 0) {
					draw_mail_parchment(renderer, *small_text, *panel_art, mail, layout);
				}
			} else {
				draw_mailbox(renderer, *small_text, save, mail, rows);
			}
		}
		if (small_text && has_save && save.player >= 0 && !in_menu && panel_shown) {
			// A button's tooltip: under the finger before it is lifted, or the one the pad points at.
			SDL_Rect where{};
			uint32_t tip = 0;
			if (finger_down && touch_moved < kTapDistance) {
				tip = panel_tooltip(side, save, small_text->line_height(), mail_in_panel, side.menu == PanelMenu::Build &&
					side.build_type < 0, touch_start.x, touch_start.y, where);
			} else if (side.focus >= 0 && !side.folded) {
				const PanelLayout focus_panel(side.folded);
				where = side.focus < kPanelIcons ? focus_panel.icons[side.focus] : focus_panel.ships[side.focus - kPanelIcons];
				tip = panel_tooltip(side, save, small_text->line_height(), false, false, where.x + 1, where.y + 1, where);
			}
			if (tip) {
				draw_tooltip(renderer, *small_text, tooltip_text(tip), where);
			}
		}
		if (small_text) {
			// The notices still in time, one under another (with the mailbox
			// open, at the bottom on the right, under its letter).
			const int padding = 8;
			const int box_height = small_text->line_height() + padding;
			while (!notices.empty() && SDL_TICKS_PASSED(app_ticks(), notices.front().second)) {
				notices.pop_front();
			}
			const bool mail_full = in_mail && mail.open && !mail_in_panel;  // The whole-screen mailbox (the sea map).
			int y = mail_full ? kScreenHeight - 18 - int(notices.size()) * (box_height + 2) : kBarHeight + 6;
			for (const auto& [line, until] : notices) {
				const int width = small_text->measure(line) + 2 * padding;
				const SDL_Rect box = {mail_full ? kScreenWidth - 18 - width : 8, y, width, box_height};
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
	panel_art.reset();
	if (controller) {
		SDL_GameControllerClose(controller);
	}
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_FreeSurface(screen);
	SDL_Quit();
	return screenshot_failed ? 1 : 0;
}
