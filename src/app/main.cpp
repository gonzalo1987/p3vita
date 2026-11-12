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
#include <ctime>
#include <deque>
#include <dirent.h>
#include <map>
#include <memory>
#include <optional>
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
#include "p3data/building_windows.hpp"
#include "p3data/letter_window.hpp"
#include "p3data/mail.hpp"
#include "p3data/navigation.hpp"
#include "p3data/new_game.hpp"
#include "p3data/player_actions.hpp"
#include "p3data/prices.hpp"
#include "p3data/sailing.hpp"
#include "p3data/simulation.hpp"
#include "p3data/statistics.hpp"
#include "p3data/statistics_window.hpp"
#include "p3data/savegame.hpp"
#include "p3data/savegame_write.hpp"
#include "p3data/seamap.hpp"
#include "p3data/town.hpp"
#include "p3data/town_buildings.hpp"
#include "p3data/trade.hpp"
#include "p3data/trade_route_window.hpp"
#include "p3data/trade_routes.hpp"
#include "p3data/wares.hpp"
#include "battle_view.hpp"
#include "debug_states.hpp"
#include "script.hpp"
#include "savegames.hpp"
#include "settings.hpp"
#include "new_game_window.hpp"
#include "start_screen.hpp"
#include "text_renderer.hpp"

using namespace p3::data;
using p3::app::InputScript;
using p3::app::symbol_byte;
using p3::app::TextRenderer;

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
constexpr uint32_t kWaterTileType = 0xE;  // The water's .let tiles (g_k: frame g, tile k), as the game's 0x60329B.
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

// The Vita's rear touchpad (an indirect touch device; the screen is direct) is
// of no use in this game: its touches are ignored. On the PC, the mouse's and
// the scripts' touches have no device and pass.
bool rear_touch(SDL_TouchID id) {
	const SDL_TouchDeviceType type = SDL_GetTouchDeviceType(id);
	return type == SDL_TOUCH_DEVICE_INDIRECT_ABSOLUTE || type == SDL_TOUCH_DEVICE_INDIRECT_RELATIVE;
}

// Mapa de una ciudad listo para dibujar: los trozos de sprite con su textura.
// The town's buildings the player goes into, as in the game by clicking on
// them: the Market Hall (building type 0x23, «stadtkontor», .let type 0x00),
// the tavern (0x24, «kneipe», 0x02), the shipyard (0x2E, «werft», 0x01) and
// the repair dock (0x01, «repwerft», 0x1B) (savegame.md §8.1); their names,
// the game's (0x6A57C8 + 4 · the building's type).
enum class Place { None, MarketHall, Tavern, Shipyard, Ship, Office, Lender, Building, Church, BathHouse };

struct PlaceInfo {
	uint32_t let_type;
	Place place;
	GameText name;
	const char* spanish;
	const char* english;
	int building = -1;  // Its name the game's of this building type (0x6A57C8 + 4 · type), without a GameText.
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
	// The church (building type 0x1F, «kircheR*») and the bath house (0x26, «badhaus»): building_windows.hpp.
	{0x04, Place::Church, GameText::Count, "Iglesia", "Church", 0x1F},
	{0x09, Place::BathHouse, GameText::Count, "Baño", "Bath House", 0x26},
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

	// The town's drawing; `owners`, each cell's building owner (place_saved_town; empty for a template map).
	void show(const TownMap& town, std::vector<uint8_t> owners = {}) {
		items_.clear();
		width_ = 0;
		height_ = 0;
		first_object_ = 0;
		owners_ = std::move(owners);
		const std::vector<TownSprite> sprites = build_town_sprites(town, tiles_);
		items_.reserve(sprites.size());
		for (const TownSprite& sprite : sprites) {
			SDL_Texture* texture = tile_texture(sprite.tile);
			if (!texture) {
				continue;
			}
			const size_t cell = size_t(sprite.cell_y) * size_t(town.width) + size_t(sprite.cell_x);
			const uint8_t owner = sprite.object && cell < owners_.size() ? owners_[cell] : kNoBuildingOwner;
			items_.push_back({texture, {sprite.src_x, sprite.src_y, sprite.width, sprite.height},
				{sprite.dst_x, sprite.dst_y, sprite.width, sprite.height}, sprite.tile, owner, sprite.cell_y,
				!sprite.object && sprite.tile->type == kWaterTileType});
			if (!sprite.object) {
				first_object_ = items_.size();
			}
			width_ = std::max(width_, sprite.dst_x + sprite.width);
			height_ = std::max(height_, sprite.dst_y + sprite.height);
		}
		// Each cell's kind as the game's town view keeps it (its map [+0xC308]: the building or object on it, else
		// its ground): the .let type of what covers it.
		cell_types_.assign(size_t(town.width) * size_t(town.height), 0);
		for (size_t i = 0; i < cell_types_.size(); ++i) {
			for (const std::vector<TownCell>* layer : {&town.objects, &town.ground}) {
				if (i >= layer->size() || (*layer)[i].tile == 0) {
					continue;
				}
				const auto it = tiles_.find((*layer)[i].tile);
				if (it != tiles_.end() && !it->second.is_editor_marker()) {
					cell_types_[i] = it->second.type;
					break;
				}
			}
		}
		make_minimap(town);
	}

	// The town under a battle at it (BattleTown, the battles' piece 7, part 3), in two parts so that the battle's
	// ships stand among its buildings: the ground (without the water, which the battle draws animated under it),
	// then the objects up to a row (the next call goes on from there: `cursor`, from first_object()).
	void draw_ground(int camera_x, int camera_y, bool skip_water) const {
		const SDL_Rect screen = {camera_x, camera_y, kScreenWidth, kScreenHeight};
		for (size_t i = 0; i < first_object_ && i < items_.size(); ++i) {
			const Item& item = items_[i];
			if ((skip_water && item.water) || !SDL_HasIntersection(&item.dst, &screen)) {
				continue;
			}
			const SDL_Rect dst = {item.dst.x - camera_x, item.dst.y - camera_y, item.dst.w, item.dst.h};
			SDL_RenderCopy(renderer_, item.texture, &item.src, &dst);
		}
	}
	size_t first_object() const { return first_object_; }
	void draw_objects(size_t& cursor, int last_row, int camera_x, int camera_y) const {
		const SDL_Rect screen = {camera_x, camera_y, kScreenWidth, kScreenHeight};
		for (; cursor < items_.size() && items_[cursor].row <= last_row; ++cursor) {
			const Item& item = items_[cursor];
			if (!SDL_HasIntersection(&item.dst, &screen)) {
				continue;
			}
			const SDL_Rect dst = {item.dst.x - camera_x, item.dst.y - camera_y, item.dst.w, item.dst.h};
			SDL_RenderCopy(renderer_, item.texture, &item.src, &dst);
		}
	}
	// Whether the cell under the town's pixel (x, y) is a building's (of the town's records: owners_).
	bool building_at(int x, int y) const {
		const SDL_Point c = cell_at(x, y);
		const size_t i = size_t(c.y) * size_t(map_columns_) + size_t(c.x);
		return c.x >= 0 && c.y >= 0 && c.x < map_columns_ && c.y < map_rows_ && i < owners_.size() &&
			owners_[i] != kNoBuildingOwner;
	}
	// Whether the town's pixel (x, y) is on the water (.let type 0xE, a sand bank 0x13, bare or off the map), by
	// the cell of its row: rough but quick, for the battle's ripples.
	bool water_at(int x, int y) const {
		const int row = y / kRowStep;
		const int col = (x - (row & 1) * (kTileWidth / 2)) / kTileWidth;
		if (row < 0 || col < 0 || col >= map_columns_ || row >= map_rows_) {
			return true;
		}
		const size_t i = size_t(row) * size_t(map_columns_) + size_t(col);
		const uint32_t type = i < cell_types_.size() ? cell_types_[i] : 0;
		return type == 0 || type == kWaterTileType || type == 0x13;
	}
	// The .let type of what covers the cell under the town's pixel (x, y) (0 off the map or bare).
	uint32_t cell_type_at(int x, int y) const {
		const SDL_Point c = cell_at(x, y);
		if (c.x < 0 || c.y < 0 || c.x >= map_columns_ || c.y >= map_rows_) {
			return 0;
		}
		const size_t i = size_t(c.y) * size_t(map_columns_) + size_t(c.x);
		return i < cell_types_.size() ? cell_types_[i] : 0;
	}

	// Dibuja lo que cae dentro de la pantalla con la cámara en (camera_x, camera_y).
	// The building of the .let type `highlight` (one of kPlaces) is drawn once
	// more on top, added, so that it glows (the one the cursor points at). With
	// `player` >= 0, the competition's buildings shaded (the top bar's house, the
	// manual's figure 4-4): another merchant's (below `merchants`) and the rival kontor.
	void draw(int camera_x, int camera_y, int highlight = -1, int player = -1, int merchants = 0) {
		const SDL_Rect screen = {camera_x, camera_y, kScreenWidth, kScreenHeight};
		for (const Item& item : items_) {
			if (!SDL_HasIntersection(&item.dst, &screen)) {
				continue;
			}
			SDL_Rect dst = item.dst;
			dst.x -= camera_x;
			dst.y -= camera_y;
			const bool rival = player >= 0 && (item.owner == kRivalOwner ||
				(item.owner != kNoBuildingOwner && int(item.owner) < merchants && int(item.owner) != player));
			if (rival) {
				SDL_SetTextureColorMod(item.texture, 105, 105, 115);
			}
			SDL_RenderCopy(renderer_, item.texture, &item.src, &dst);
			if (rival) {
				SDL_SetTextureColorMod(item.texture, 255, 255, 255);
			}
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
		uint8_t owner;  // Its cell's building owner (kNoBuildingOwner: the ground, nature, the town's fixed objects).
		int row = 0;  // The row of the cell it stands on.
		bool water = false;  // A ground tile of the water (.let type 0xE).
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
	std::vector<uint8_t> owners_;  // Each cell's building owner (show()).
	int map_columns_ = 0;
	int map_rows_ = 0;
	std::map<std::string, SDL_Texture*> atlases_;
	std::unordered_map<const TileDef*, SDL_Texture*> tile_textures_;
	std::map<std::string, std::vector<const TileDef*>> places_by_atlas_;  // The tiles of kPlaces, by atlas.
	std::unordered_map<const TileDef*, Mask> masks_;
	std::vector<Item> items_;
	size_t first_object_ = 0;  // items_: the ground's, then the objects' row by row (build_town_sprites).
	std::vector<uint32_t> cell_types_;  // cell_type_at.
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

// An AIM of the archive as a texture (its own alpha); nullptr if it fails.
SDL_Texture* load_aim_texture(SDL_Renderer* renderer, const CprArchive& archive, const char* path) {
	const CprEntry* entry = archive.find(path);
	std::vector<uint8_t> data;
	Image image;
	if (!entry || !archive.read(*entry, data) || !decode_aim(data.data(), data.size(), image)) {
		return nullptr;
	}
	SDL_Texture* texture =
		SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, image.width, image.height);
	if (texture) {
		SDL_UpdateTexture(texture, nullptr, image.rgba.data(), image.width * 4);
		SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
	}
	return texture;
}

// How many of the sea map's rows load a frame (SeaMapView::step) while the
// town waits for it with its notice.
constexpr int kSeaMapRowsPerFrame = 240;

// A town's event symbols (TEX30009 of textures.ini: 15 frames of 20 · 20, six a row, the last three in the third
// row from its second column; player_actions.hpp kTownViewEvents, kSeaMapEvents) and the wares' (TEX30040: 20
// frames, five a row), the sea map's symbol of a ware short.
constexpr char kEventSymbols[] = "images/Ereignisse/page_glkarte01.bmp";
constexpr char kWareSymbols[] = "images/Ereignisse/Ware_fehlt.bmp";
constexpr int kEventSymbolSize = 20;

SDL_Rect event_symbol_frame(int frame) {
	if (frame >= 12) {
		return {(frame - 11) * kEventSymbolSize, 2 * kEventSymbolSize, kEventSymbolSize, kEventSymbolSize};
	}
	return {(frame % 6) * kEventSymbolSize, (frame / 6) * kEventSymbolSize, kEventSymbolSize, kEventSymbolSize};
}

SDL_Rect ware_symbol_frame(int ware) {
	return {(ware % 5) * kEventSymbolSize, (ware / 5) * kEventSymbolSize, kEventSymbolSize, kEventSymbolSize};
}

// An event symbol's tooltip (0x4A6290, 0x452973), the game's text (the viewer's in its language without the exe).
std::string town_event_tooltip(uint32_t bit) {
	switch (bit) {
	case 0x800:
		return game_text(GameText::EventPirates, tr("La ciudad está siendo atacada por piratas.", "The town is being attacked by pirates"));
	case 0x10:
		return game_text(GameText::EventSiege, tr("La ciudad está siendo asediada.", "The town is under siege"));
	case 0x8:
		return game_text(GameText::EventPlague, tr("Hay una epidemia de peste en la ciudad.", "There is an outbreak of the plague in the city"));
	case 0x8000:
		return game_text(GameText::EventFire, tr("Hay un incendio en la ciudad.", "Fire in the town"));
	case 0x1000:
		return game_text(GameText::EventFamine, tr("Hay hambruna en la ciudad", "There is famine in the town"));
	case 0x10000:
		return game_text(GameText::EventWeapons, tr("Hacen falta más armas en la ciudad.", "Weapons are needed in the city"));
	case 0x4000000:
		return game_text(GameText::EventFrozen, tr("Las aguas del puerto están completamente heladas", "The harbour is frozen solid"));
	case 0x200:
		return game_text(GameText::EventBlockade, tr("El puerto está bloqueado", "The harbour is blockaded"));
	case 0x400:
		return game_text(GameText::EventBoycott, tr("La ciudad está sufriendo un boicot", "The town is being boycotted"));
	}
	return std::string();
}

// The town view's event symbols (0x589360, 0x5892D0): fixed places in a row at the top right of the town's part,
// from the right one at its edge, then 32 and 39 pixels apart (the last two in one place); twice the game's size
// for the finger, under the top bar.
constexpr int kTownEventScale = 2;

SDL_Rect town_event_rect(int slot, int town_right) {
	const int from_right = slot == 0 ? kEventSymbolSize : 52 + 39 * (std::min(slot, 6) - 1);
	const int size = kEventSymbolSize * kTownEventScale;
	return {town_right - 6 - from_right * kTownEventScale, kBarHeight + 8, size, size};
}

// The town's symbol under (x, y): its bit, or 0.
uint32_t town_event_at(uint32_t flags, int town_right, int x, int y, SDL_Rect& where) {
	for (int slot = 7; slot >= 0; --slot) {
		const SDL_Rect r = town_event_rect(slot, town_right);
		if ((flags & kTownViewEvents[slot].bit) && x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) {
			where = r;
			return kTownViewEvents[slot].bit;
		}
	}
	return 0;
}

// A symbol for each of the town's bits that is set (0x58BF3A: the town's [+0x2C8] as it is).
void draw_town_events(SDL_Renderer* renderer, SDL_Texture* symbols, uint32_t flags, int town_right) {
	if (!symbols) {
		return;
	}
	for (int slot = 0; slot < 8; ++slot) {
		if (flags & kTownViewEvents[slot].bit) {
			const SDL_Rect src = event_symbol_frame(kTownViewEvents[slot].frame);
			const SDL_Rect dst = town_event_rect(slot, town_right);
			SDL_RenderCopy(renderer, symbols, &src, &dst);
		}
	}
}

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
		events_ = load_texture(renderer, archive, kEventSymbols);
		wares_ = load_texture(renderer, archive, kWareSymbols);
		marker_ = load_texture(renderer, archive, kShipMarker, &marker_width_, &marker_height_);
		battle_ = load_texture(renderer, archive, kSeaBattleIcon, &battle_width_, &battle_height_);
		for (size_t i = 0; i < ships_.size(); ++i) {
			ships_[i] = load_texture(renderer, archive, kShipSprites[i]);
		}
	}

	~SeaMapView() {
		for (SDL_Texture* texture : {map_, symbols_, events_, wares_, marker_, battle_}) {
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

		// The towns' symbols (0x44BFD0), from what the player knows of each town (town_situation_known): its shortage
		// and, 20 pixels to its right, its event.
		for (size_t i = 0; i < save.towns.size(); ++i) {
			const SeaMapTown& t = towns_[size_t(save.towns[i].town_id())];
			const TownSymbols s = symbols_of(save, i);
			SDL_Rect dst = {t.event_x - camera_x, t.event_y - camera_y, kEventSymbolSize, kEventSymbolSize};
			if (s.shortage_texture) {
				SDL_RenderCopy(renderer_, s.shortage_texture, &s.shortage, &dst);
			}
			dst.x += kEventSymbolSize;
			if (s.event_bit && events_) {
				SDL_RenderCopy(renderer_, events_, &s.event, &dst);
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

	// A battle's icon at the map's place (x, y) (0x44AEAE: centred there).
	void draw_battle(int x, int y, int camera_x, int camera_y) const {
		if (!battle_) {
			return;
		}
		const SDL_Rect dst = {x - battle_width_ / 2 - camera_x, y - battle_height_ / 2 - camera_y, battle_width_,
			battle_height_};
		SDL_RenderCopy(renderer_, battle_, nullptr, &dst);
	}

	// Whether the map's point (px, py) is on the icon of a battle at (x, y).
	bool battle_at(int x, int y, int px, int py) const {
		return std::abs(px - x) <= battle_width_ / 2 && std::abs(py - y) <= battle_height_ / 2;
	}

	// The tooltip of a town's symbol at the map's point (x, y), with a margin for the finger (0x452973: the game's
	// box is the symbol's), and where the symbol is on the screen; empty if none there.
	std::string symbol_tooltip_at(const SaveGame& save, int x, int y, int camera_x, int camera_y, SDL_Rect& where) const {
		constexpr int kMargin = 6;
		for (size_t i = 0; i < save.towns.size(); ++i) {
			const SeaMapTown& t = towns_[size_t(save.towns[i].town_id())];
			const TownSymbols s = symbols_of(save, i);
			for (int k = 0; k < 2; ++k) {
				const int sx = t.event_x + k * kEventSymbolSize;
				const bool there = k == 0 ? s.shortage_texture != nullptr : s.event_bit != 0;
				if (!there || x < sx - kMargin || x >= sx + kEventSymbolSize + kMargin || y < t.event_y - kMargin ||
					y >= t.event_y + kEventSymbolSize + kMargin) {
					continue;
				}
				where = {sx - camera_x, t.event_y - camera_y, kEventSymbolSize, kEventSymbolSize};
				if (k == 1) {
					return town_event_tooltip(s.event_bit);
				}
				if (s.ware < 0) {
					return town_event_tooltip(s.shortage_bit);
				}
				// The ware's name, a space and «needed» (0x452C87; two texts by 0x66E0D4, the same words).
				static constexpr uint8_t kFirst[20] = {1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 0};
				return game_text(GameText(int(GameText::WareGrain) + s.ware), std::string()) + " " +
					game_text(kFirst[s.ware] ? GameText::EventWareNeeded : GameText::EventWareNeededOther,
						tr(": reservas agotadas", "needed"));
			}
		}
		return std::string();
	}

private:
	// A town's two symbols on the sea map (0x44BFD0): the shortage, the famine 0x1000 (frame 6), else a ware short
	// in bits 18..22 (0x1F: none; the wares' symbols), else the weapons 0x10000 (frame 5); and the first event of
	// kSeaMapEvents that is set.
	struct TownSymbols {
		SDL_Texture* shortage_texture = nullptr;
		SDL_Rect shortage{};
		uint32_t shortage_bit = 0;
		int ware = -1;
		uint32_t event_bit = 0;
		SDL_Rect event{};
	};
	TownSymbols symbols_of(const SaveGame& save, size_t town) const {
		TownSymbols s;
		const uint32_t v = town_situation_known(save, uint32_t(town));
		if (v & 0x1000) {
			s.shortage_texture = events_;
			s.shortage = event_symbol_frame(6);
			s.shortage_bit = 0x1000;
		} else if ((v & 0x7C0000) != 0x7C0000) {
			const int ware = int(v >> 18) & 0x1F;
			if (ware < 20) {
				s.shortage_texture = wares_;
				s.shortage = ware_symbol_frame(ware);
				s.ware = ware;
			}
		} else if (v & 0x10000) {
			s.shortage_texture = events_;
			s.shortage = event_symbol_frame(5);
			s.shortage_bit = 0x10000;
		}
		for (const TownEventSymbol& e : kSeaMapEvents) {
			if (v & e.bit) {
				s.event_bit = e.bit;
				s.event = event_symbol_frame(e.frame);
				break;
			}
		}
		return s;
	}

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
	SDL_Texture* events_ = nullptr;  // The towns' event symbols (TEX30009).
	SDL_Texture* wares_ = nullptr;  // The wares' symbols (TEX30040).
	SDL_Texture* marker_ = nullptr;
	SDL_Texture* battle_ = nullptr;  // A battle's icon (TEX30034).
	std::array<SDL_Texture*, 4> ships_{};
	int width_ = 0;
	int height_ = 0;
	int marker_width_ = 0;
	int marker_height_ = 0;
	int battle_width_ = 45;
	int battle_height_ = 45;
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

	// The town as it is now (and each cell's building owner).
	void draw(const SaveGame& save, const TileSet& tiles, TownMap& out, std::vector<uint8_t>* owners = nullptr) {
		place_saved_town(base_, save, index_, tiles, out, owners);
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

// The town under a battle at it (the battles' piece 7, part 3): the game's town view shows the attacked town's
// battle map (0x58A310 → 0x58EB30, load_battle_town_template: its harbour with the open sea before it) with the
// town's buildings as they are now, moved by the map's offset (the battle's [+0x1ED8], [+0x1EDC]; the view's
// [+0x528], [+0x52C]); at a pirate nest (0xFE − k), the nest's map (0x515D20: 251..254) with its pool's buildings
// (0x515B80), moved by that file's last two numbers (not the battle's, which 0x5FFC10 works out otherwise for a
// nest). Its template is loaded once; its buildings are placed again when they change (a tower falls and burns).
class BattleTown {
public:
	bool load(const CprArchive& archive, const SaveGame& save, size_t battle, const std::vector<int>& regions,
		std::string& error) {
		const uint8_t place = save.battles.get<uint8_t>(battle, 0x1E98);
		const bool nest = place >= save.towns.size();
		const size_t k = size_t(0xFE - place);
		if (nest && (place < 0xFA || !save.pirates[k] || k >= save.pirate_nests.size())) {
			loaded_ = false;
			error = "no pirate nest " + std::to_string(place);
			return false;
		}
		const int map = nest ? 0xFE - (save.pirates[k]->u8(0xF) & 3) : save.towns[place].template_map() | 0x80;
		if (loaded_ && place == place_ && map == map_) {
			return true;
		}
		place_ = place;
		map_ = map;
		nest_ = nest;
		key_ = 0;
		if (nest) {
			const uint8_t id = save.pirates[k]->u8(0xE);
			const int region = id < regions.size() ? regions[id] : regions.empty() ? 0 : regions[0];
			int32_t x = 0, y = 0;
			loaded_ = load_nest_town_template(archive, map, region, base_, &error);
			load_town_ends(archive, map, x, y);
			origin_ = {x, y};
		} else {
			loaded_ = load_battle_town_template(archive, save.towns[place], regions, base_, &error);
			origin_ = {save.battles.get<int32_t>(battle, 0x1ED8), save.battles.get<int32_t>(battle, 0x1EDC)};
		}
		return loaded_;
	}

	void draw(const SaveGame& save, const TileSet& tiles, TownMap& out, std::vector<uint8_t>* owners) {
		if (nest_) {
			const size_t k = size_t(0xFE - place_);
			out = base_.town;
			place_nest_buildings(save.pirate_nests[k], save.pirates[k]->u16(0x8), base_.region, tiles, out, owners,
				origin_.x, origin_.y);
		} else {
			place_saved_town(base_, save, place_, tiles, out, owners, origin_.x, origin_.y);
		}
		key_ = key(save);
	}

	bool changed(const SaveGame& save) const { return loaded_ && key(save) != key_; }
	// The view's origin [+0x528], [+0x52C]: what its buildings' cells are moved by.
	SDL_Point origin() const { return origin_; }
	bool nest() const { return nest_; }

private:
	uint64_t key(const SaveGame& save) const {
		if (!nest_) {
			return drawn_buildings_key(save, place_);
		}
		const size_t k = size_t(0xFE - place_);
		return save.pirates[k] ? nest_buildings_key(save.pirate_nests[k], save.pirates[k]->u16(0x8)) : 0;
	}

	SavedTownTemplate base_;
	size_t place_ = 0;  // The town, or the nest's 0xFE − k.
	int map_ = -1;
	bool nest_ = false;
	SDL_Point origin_ = {0, 0};
	bool loaded_ = false;
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
// The world's tick while a battle has a human side (time mode 2: [0x673CF8], 0x546729).
constexpr Uint32 kBattleTickMs = 3375;
// A finger held still this long on the battle's screen is a long press (a ship added or taken out; the held order).
constexpr Uint32 kBattleLongPressMs = 450;
constexpr int kBattleStill = 20;  // Pixels a finger held still may wander (each way) for a long press.

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
	// The church's feeding page: each row's amount (the row being changed is `amount`).
	std::array<int, 5> feed{};
	int feed_row = -1;
	// The bath house: a bath taken (its 0x39 goes once the player goes on) and the game's reaction to it.
	bool bathed = false;
	int bath_reaction = 0;
};

constexpr int kShipyardPages = 5;
constexpr int kLenderPages = 6;

// How many pages the window has (L/R, or touching its title): the shipyard's
// and the moneylender's; 0 for the others (the repair dock only repairs).
int place_pages(const PlaceUi& ui) {
	if (ui.open == Place::Shipyard && ui.let_type == 0x01) {
		return kShipyardPages;
	}
	if (ui.open == Place::Church) {
		return 3;
	}
	if (ui.open == Place::BathHouse) {
		return 2;
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

std::string building_name(int type);

std::string place_name(int let_type) {
	const PlaceInfo* p = place_of(uint32_t(let_type));
	if (p && p->building >= 0) {
		const std::string name = building_name(p->building);
		return name.empty() ? tr(p->spanish, p->english) : name;
	}
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

std::string building_name(int type);

// The church (.let 0x04; building_windows.hpp): its pages (L/R) 0 a donation (0x32), 1 for its extension (0x31), 2
// feeding the poor (0x30, a row per ware with its own amount). The donations go by 500 (the game's step, 0x5CB441).
constexpr int kChurchPages = 3;
constexpr int kDonationStep = 500;

void church_view(SaveGame& save, int town, PlaceUi& ui, PlaceView& v) {
	static const char* const kPages[kChurchPages][2] = {{"Donativo", "Donation"}, {"Ampliación", "Extension"},
		{"Dar de comer a los pobres", "Feed the poor"}};
	ui.page = ((ui.page % kChurchPages) + kChurchPages) % kChurchPages;
	v.title = place_name(ui.let_type) + " - " + tr(kPages[ui.page][0], kPages[ui.page][1]) + "   (L/R)";
	const int32_t money = save.merchants[size_t(save.player)].money();
	const int fed = church_feeding_level(save, uint32_t(town));
	static const char* const kFed[6][2] = {{"Los pobres pasan hambre.", "The poor are starving."},
		{"Los pobres apenas comen.", "The poor hardly eat."}, {"Los pobres comen poco.", "The poor eat little."},
		{"Los pobres comen lo justo.", "The poor eat just enough."}, {"Los pobres comen bien.", "The poor eat well."},
		{"Los pobres comen de sobra.", "The poor eat plenty."}};
	switch (ui.page) {
	case 0:
	case 1: {
		const bool extension = ui.page == 1;
		v.max_amount = std::max(0, std::min(money, 999999) / kDonationStep);
		v.amount_label = format_money(int64_t(ui.amount) * kDonationStep);
		bool can = true;
		if (extension) {
			const int state = church_extension_state(save, uint32_t(town));
			const int32_t cost = church_extension_cost(save, uint32_t(town));
			const int32_t gathered = save.towns[size_t(town)].memory.s32(0x794 + 4);
			v.lines.push_back({tr("Nivel de la iglesia: ", "The church's level: ") +
					std::to_string(save.towns[size_t(town)].memory.u8(0x794 + 0xF)), kTextColor});
			if (state == 2) {
				v.lines.push_back({tr("La iglesia no se puede ampliar más.", "The church can't be extended further."), kDimColor});
			} else {
				v.lines.push_back({tr("Reunido para la ampliación: ", "Gathered for the extension: ") +
						format_money(gathered) + " / " + format_money(cost), kTextColor});
				if (state == 0) {
					v.lines.push_back({tr("Ya está todo el dinero: falta el material.", "The money is there: the materials are missing."),
						kDimColor});
				} else if (state == 1) {
					v.lines.push_back({tr("La última ampliación es muy reciente.", "The last extension is too recent."), kDimColor});
				}
			}
			can = state == -1;
		} else {
			v.lines.push_back({tr(kFed[fed][0], kFed[fed][1]), kTextColor});
		}
		v.rows.push_back({tr("Donar", "Donate"), can});
		v.act = tr("Donar", "Donate");
		break;
	}
	default: {
		// The rows: the five wares, each with its amount (Left and Right on the selected row).
		ui.row = std::clamp(ui.row, 0, 4);
		if (ui.row != ui.feed_row) {
			ui.amount = ui.feed[size_t(ui.row)];
			ui.feed_row = ui.row;
		} else {
			ui.feed[size_t(ui.row)] = ui.amount;
		}
		v.max_amount = 999;
		v.lines.push_back({tr(kFed[fed][0], kFed[fed][1]), kTextColor});
		std::array<uint16_t, 5> amounts{};
		for (size_t k = 0; k < 5; ++k) {
			amounts[k] = uint16_t(ui.feed[k]);
			const uint8_t w = kFeedingWares[k];
			v.rows.push_back({capitalized(ware_name(w)) + ": " + amount_label(ui.feed[k], kWares[w].unit), true});
		}
		v.lines.push_back({tr("Lo toman de tu oficina en la ciudad y después de tus barcos en ella.",
								"Taken from your office in the town, then from your ships there."), kDimColor});
		v.act = tr("Repartir", "Give");
		v.amount_choice = true;  // Each row has its own amount; 0 too.
		v.amount_label = std::to_string(ui.amount);
		(void)amounts;
		break;
	}
	}
}

void church_act(SaveGame& save, Sailing* sailing, int town, PlaceUi& ui) {
	const uint32_t player = uint32_t(save.player);
	if (ui.page <= 1) {
		run_operations(save, sailing,
			{church_donation_operation(ui.page == 0 ? 0x32 : 0x31, player, uint32_t(town), ui.amount * kDonationStep)});
		ui.amount = 0;
		ui.message = tr("Donado.", "Donated.");
		return;
	}
	std::array<uint16_t, 5> amounts{};
	bool any = false;
	for (size_t k = 0; k < 5; ++k) {
		amounts[k] = uint16_t(ui.feed[k]);
		any = any || amounts[k] != 0;
	}
	if (!any) {
		ui.message = tr("Ahora no se puede.", "It can't be done now.");
		return;
	}
	run_operations(save, sailing, {feeding_operation(save, player, uint32_t(town), amounts)});
	ui.feed = {};
	ui.amount = 0;
	ui.message = tr("Repartido.", "Given.");
}

// The bath house (.let 0x09; building_windows.hpp): its pages 0 the bath (X: take one; the game's reaction by the days
// since the last, and the operation 0x39 with the tick once the player goes on: the next page or leaving), 1 the
// councillors' bribes (a row per councillor, the offer by 500; 0x42 or 0x43 by the councillor's price, 0x529E20).
constexpr int kBathPages = 2;

void bath_view(SaveGame& save, int town, PlaceUi& ui, PlaceView& v) {
	static const uint32_t kReactions[3] = {0x6A2A48, 0x6A2A44, 0x6A2A40};
	static const char* const kFallback[3][2] = {
		{"Ni siquiera te has secado desde tu último baño.", "You are not really dry from your last bath."},
		{"Un baño así le hace sentirse bien a uno.", "Such a bath always makes one feel good."},
		{"¡Por fin un baño! Han pasado más de tres semanas.", "A bath at last! After more than 3 weeks."}};
	ui.page = ((ui.page % kBathPages) + kBathPages) % kBathPages;
	static const char* const kPages[kBathPages][2] = {{"Baño", "Bath"}, {"Concejales", "Councillors"}};
	v.title = place_name(ui.let_type) + " - " + tr(kPages[ui.page][0], kPages[ui.page][1]) + "   (L/R)";
	const uint32_t player = uint32_t(save.player);
	if (ui.page == 0) {
		if (ui.bathed) {
			const int k = ui.bath_reaction;
			v.lines.push_back({exe_text(kReactions[k], kFallback[k][0], kFallback[k][1]), kTextColor});
		}
		v.rows.push_back({tr("Bañarse", "Take a bath"), !ui.bathed});
		v.act = tr("Bañarse", "Take a bath");
		return;
	}
	const TownMemory& t = save.towns[size_t(town)].memory;
	const uint16_t merchants = save.globals.u16(global::kWorldMerchantCount);
	for (int j = 0; j < 4; ++j) {
		std::string name = clean_game_text(game_first_name(save, t.u8(0x6D4 + j))) + " " +
			clean_game_text(game_family_name(save, t.u8(0x6D8 + j)));
		const uint8_t holder = t.u8(0x6DC + j);
		if (holder < merchants) {
			const MerchantMemory& m = save.merchants[holder].memory;
			name += tr(" (con ", " (with ") + clean_game_text(game_family_name(save, m.u8(0x1D))) + ")";
		}
		v.rows.push_back({name, true});
	}
	const int32_t money = save.merchants[size_t(player)].money();
	v.max_amount = std::max(0, std::min(money, 999999) / kDonationStep);
	v.amount_label = format_money(int64_t(ui.amount) * kDonationStep);
	v.lines.push_back({tr("Elige el concejal y lo que le ofreces.", "Choose the councillor and your offer."), kDimColor});
	v.act = tr("Sobornar", "Bribe");
}

void bath_act(SaveGame& save, Sailing* sailing, int town, PlaceUi& ui) {
	if (ui.page == 0) {
		// The reaction is told by the days since the last bath, before this one counts (0x5B1FC5).
		ui.bath_reaction = bath_state(save, uint32_t(save.player));
		ui.bathed = true;
		return;
	}
	const OperationBytes op = bribe_operation(save, uint32_t(town), uint32_t(save.player), uint32_t(ui.row),
		uint32_t(ui.amount * kDonationStep));
	run_operations(save, sailing, {op});
	ui.message = op[0] == 0x42 ? tr("El concejal acepta.", "The councillor accepts.")
							   : tr("El concejal rechaza tu oferta.", "The councillor turns your offer down.");
	ui.amount = 0;
}

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
	case Place::Church:
		church_view(save, town, ui, v);
		break;
	case Place::BathHouse:
		bath_view(save, town, ui, v);
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
		draw_button(renderer, text, box,
			v.amount_choice || !v.amount_label.empty() ? v.amount_label : std::to_string(ui.amount));
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
	if (ui.open == Place::Church) {
		church_act(save, sailing, town, ui);
		return;
	}
	if (ui.open == Place::BathHouse) {
		bath_act(save, sailing, town, ui);
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
	// Its trade route page ([+0xE4], trade_route_window.hpp), for the ship `route_for`: the first row shown
	// ([+0x57D8]), the pad's row (-1: the title's «Archivo» and «Activa») and column (the town, «Mercancías», the
	// mode); a row's town button dragged (-1 none; where its top is now); the towns' list a town button opened (its
	// row, -1 closed; the pad's entry); and the wares window of a stop (*0x6CBA74), with its first row shown, the
	// pad's row (kStopRows: the bottom buttons) and column, and a ware's row dragged.
	int route_for = -1;
	int route_first = 0;
	int route_row = 0;
	int route_column = 0;
	int route_drag = -1;
	int route_drag_top = 0;
	int route_towns_row = -1;
	int route_towns_pick = 0;
	std::optional<StopWindow> stop_window;
	int stop_first = 0;
	int stop_row = 0;
	int stop_column = 0;
	int stop_drag = -1;
	int stop_drag_top = 0;
	// A box of the wares window being written (its edit boxes, 0x407235): the ware (-1 none), the price's or the
	// amount's, the digits so far.
	int stop_typing = -1;
	bool stop_typing_price = false;
	std::string stop_typed;
	// The route files' list («Archivo»: the towns' list class in its mode 4): open; its files (save\AutoRoute's
	// .rou, without the extension); the name in its box ([+0x938]) and whether it's being written; the pad's row
	// (a file; past them the bottom row: the box, «Cargar», «Guardar», «Borrar», close) and column; the first file
	// shown; the question open (1 overwrite, 2 delete; 0 none: X answers yes, Circle no).
	bool route_files = false;
	std::vector<std::string> route_file_list;
	std::string route_file_name;
	bool route_file_typing = false;
	std::string route_file_before;  // The name before it is written (the keyboard starts empty; left empty, it comes back).
	int route_file_row = 0;
	int route_file_column = 0;
	int route_file_first = 0;
	int route_file_question = 0;
	// The statistics (SIDEMENU_STATISTIK and its parchment, statistics_window.hpp), opened by the top bar's money:
	// the page ([+0xB0], 2 at first), the entry the parchment shows ([+0xBE]: the balance when it opens), the
	// balance's period ([+0xA4]), the graphs' ([+0xC0]) and Suministro's town ([+0x2440]; -1: his home town).
	int stats_page = 2;
	int stats_entry = kStatisticsBalance;
	int stats_balance_period = 0;
	int stats_graph_period = 0;
	int stats_town = -1;
	// The competition's four merchants ([+0xB4..+0xB7]; the empty ones filled when the statistics open, 0x49D380),
	// the merchant button the pad points at, and the list one opens (its slot, -1 closed; sorted by name or by the
	// entry's key; the pad's row).
	StatisticsMerchants stats_merchants = kNoStatisticsMerchants;
	int stats_slot = 0;
	int stats_choice_slot = -1;
	bool stats_choice_by_key = false;
	int stats_choice_row = 0;
	// The towns' pages' choices ([+0xAC..+0xBD]): the comparison's four towns and its ware (the slot 4 of the
	// buttons: the ware button [+0x214]).
	StatisticsTownChoices stats_towns;
	// The minimap's arrow (screenGame.ini BUTTON10, «Change map»; off on the sea map, 0x42A709): in a town, the
	// sea's minimap instead of the town's (only shown, as the game's: 0x66EAB0 handles no touch).
	bool sea_minimap = false;
	// The top bar's house (BUTTON14, «Own buildings»; 0x59A1E0 toggles the town view's [+0x564]): the competition's
	// buildings shaded in the town (the manual's figure 4-4).
	bool shade_rivals = false;
	// The top bar's man (BUTTON13, «Population»): the citizens' window (*0x6CBA90) open.
	bool citizens = false;
};

bool stats_competition(int entry) {
	return entry >= kStatisticsCompanies && entry <= kStatisticsPopularity;
}

// The town comparison's graphs (entries 7..10); 9 and 10 have the ware button.
bool stats_town_graph(int entry) {
	return entry >= kStatisticsTownResidents && entry <= kStatisticsTownStock;
}

// The town data's graphs (entries 0..5); 3..5 have the four ware buttons.
bool stats_town_data(int entry) {
	return entry >= kStatisticsPopulation && entry <= kStatisticsTownDataStock;
}

bool stats_town_data_wares(int entry) {
	return entry >= kStatisticsExportBalance && entry <= kStatisticsTownDataStock;
}

// The button [+0x214] in the place of «4 weeks» (the slot 4): the comparison's ware (9, 10) and the town data's town.
bool stats_choice_button(int entry) {
	return entry == kStatisticsTownProduction || entry == kStatisticsTownStock || stats_town_data(entry);
}

// The pages whose graphs have the four buttons (merchants, towns or wares) under them.
bool stats_slot_buttons(int entry) {
	return stats_competition(entry) || stats_town_graph(entry) || stats_town_data_wares(entry);
}

// The graphs without «4 weeks» (the game hides its button and turns the period 0 into 1): the competition's and the
// towns' pages'.
bool stats_no_days(int entry) {
	return stats_competition(entry) || stats_town_graph(entry) || stats_town_data(entry);
}

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
	SDL_Texture* battle_buttons = nullptr;  // menu_seeschlacht.tga (TEX16045): the battle's six, 32 · 32 in 4 rows.
	SDL_Texture* fire = nullptr;  // sidemenu/fire.aim (TEX20037): the deck's fire button, 64 · 64 in 4 frames.
	SDL_Texture* backpics[4] = {};
	// The wares window's direction buttons (bttn_transfer.tga, TEX16035: 48 · 18, three frames each (neutral, pressed,
	// hover) for to the town, to the office, from the town, from the office).
	SDL_Texture* transfer = nullptr;
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
	// The minimap's arrow (bttn_schiffskarte.bmp, TEX13: 19 · 70, frames neutral, pressed, off, hover).
	SDL_Texture* map_switch = nullptr;
	// The town view's event symbols (images/Ereignisse/page_glkarte01.bmp, TEX30009), at its top right.
	SDL_Texture* events = nullptr;
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
		battle_buttons = load_texture(renderer, archive, "images/menu_seeschlacht.tga");
		fire = load_aim_texture(renderer, archive, "images/sidemenu/fire.aim");
		transfer = load_texture(renderer, archive, "images/bttn_transfer.tga");
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
		map_switch = load_texture(renderer, archive, "images/bttn_schiffskarte.bmp");
		events = load_texture(renderer, archive, kEventSymbols);
		for (int k = 0; k < kHourglassFrames; ++k) {
			char path[64];
			std::snprintf(path, sizeof(path), "images/frames_sanduhr/sanduhr00%02d.tga", k + 1);
			hourglass[k] = load_texture(renderer, archive, path);
		}
	}
	~PanelArt() {
		for (SDL_Texture* t : {icons, bar, plank, wood, parchment, build_tabs, ships[0], ships[1], ships[2], ships[3],
				 ship_buttons, morale, barrel, barrel_mask, bonus, deck, battle_buttons, fire, backpics[0], backpics[1], backpics[2], backpics[3],
				 transfer, letters, paper, seal, bar_left, bar_right, people, coins, map_switch, events}) {
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
	SDL_Rect map_switch{};  // The minimap's arrow (the game's 19 · 70, on the border under the tab).
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
		map_switch = {x + 2, top + 134, kPanelBar - 4, 70};
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
	if (camera.w <= 0) {
		return;  // In a town (the minimap's arrow): no part of the sea map on the screen.
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
	// The minimap's arrow: off on the sea map (0x42A709), lit while the town shows the sea's minimap.
	if (art.map_switch) {
		const int frame = !view ? 2 : ui.sea_minimap ? 1 : 0;
		const SDL_Rect src = {0, frame * 70, 19, 70};
		draw_picture(renderer, art.map_switch, layout.map_switch, &src);
	}
	// The minimap: the town's cells, fitted to the panel's width, with the part
	// on the screen framed; on the sea map, or in a town with the arrow, the sea's.
	SDL_SetRenderDrawColor(renderer, 20, 14, 8, 255);
	SDL_RenderFillRect(renderer, &layout.map);
	SDL_Rect dst;
	double px = 0;  // Minimap pixels per town pixel.
	if (sea && save && (!view || ui.sea_minimap)) {
		draw_sea_minimap(renderer, layout, *sea, *save, view ? SDL_Rect{0, 0, 0, 0} : camera);
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
// The top bar's money, the statistics' button (screenGame.ini's, ToolTip 20045): where the bar drew it last. And
// its man (BUTTON13, «Population», 20024: the citizens' window) and house (BUTTON14, «Own buildings», 20025: the
// competition's buildings shaded).
SDL_Rect g_money_button{};
SDL_Rect g_people_button{};
SDL_Rect g_house_button{};

// The game's top bar (screenGame.ini; the manual's «Barra de información»): its wood (HauptscreenA1280 and
// B800, squeezed to the viewer's bar), from the left the inhabitants (the man) with their number, the town
// (on the sea map, the one chosen), the player's buildings there (the house), the money (the coins) and, at the
// right end, the date and the hourglass that opens the chronicle, turning while the time runs. The viewer's
// button hints go in what is left.
void draw_top_bar(SDL_Renderer* renderer, TextRenderer& text, const PanelArt& art, const std::string& citizens,
	const std::string& place, const std::string& houses, const std::string& money, const std::string& date,
	int hourglass_frame, const std::vector<std::string>& hints) {
	// The right piece (its carved post on its left) as wide as the date and the hourglass need.
	const int right_w = std::max(kScreenWidth * 284 / 1280, kScreenWidth - (kHourglassButton.x - 4 - text.measure(date)) + 26);
	const SDL_Rect a = {0, 0, kScreenWidth - right_w, kBarHeight};
	const SDL_Rect b = {a.w, 0, right_w, kBarHeight};
	draw_picture(renderer, art.bar_left, a);
	draw_picture(renderer, art.bar_right, b);
	const SDL_Color ink = {35, 20, 5, 255};
	const int y = (kBarHeight * 30 / 42 - text.line_height()) / 2 + 1;  // On the wood, above its carved edge.
	int x = 6;
	g_people_button = {};
	g_house_button = {};
	if (!citizens.empty()) {
		g_people_button = {x - 4, 0, 32 + text.measure(citizens) + 8, kBarHeight};
		const SDL_Rect src = {0, 0, 36, 28};
		draw_picture(renderer, art.people, {x, 1, 30, 23}, &src);
		x += 32;
		text.draw(citizens, x, y, ink);
		x += text.measure(citizens) + 14;
	}
	text.draw(place, x, y, ink);
	x += text.measure(place) + 14;
	if (!houses.empty()) {
		g_house_button = {x - 4, 0, 32 + text.measure(houses) + 8, kBarHeight};
		const SDL_Rect src = {36, 0, 36, 28};  // TEX16033's frame 3, the house.
		draw_picture(renderer, art.people, {x, 1, 30, 23}, &src);
		x += 32;
		text.draw(houses, x, y, ink);
		x += text.measure(houses) + 14;
	}
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

// The menu shows a convoy: the ship is in one without its flag 0x10 (0x48A983..0x48A9B2).
bool ship_menu_convoy(const SaveGame& save, int ship) {
	const SavedShip& s = save.ships[size_t(ship)];
	return ship_in_convoy(save, ship) && !(save.convoys[s.convoy()].u8(0x14) & 0x10);
}

// What can be chosen (0x48A9A2..0x48AA9A): in a convoy (without its flag 0x10) leaving or disbanding it,
// unless the state has the bits 0x14, and showing it; otherwise forming one only in port (the states 0
// and 1). The trade route: a ship alone with a captain (0x48AA79); the convoy's menu always (0x48AAB2):
// p3vita has no menu of its own for a convoy, and a ship's in a convoy stands for it (the guide's route).
bool ship_page_enabled(const SaveGame& save, int ship, ShipPage page) {
	const SavedShip& s = save.ships[size_t(ship)];
	const bool in_convoy = ship_menu_convoy(save, ship);
	switch (page) {
	case ShipPage::LeaveConvoy: return in_convoy && (s.status() & 0x14) != 0x14;
	case ShipPage::Convoy: return ship_in_convoy(save, ship) || s.status() == 0 || s.status() == 1;
	case ShipPage::TradeRoute: return in_convoy || s.captain() < save.globals.u16(global::kCaptainCount);
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

// schiffsdeck.tga's frames (TEX20010): the rope's left end, middle and right end, the ship marking the sails on
// it, unused, the deck, the guns standing (6..11) and hanging (12..17), and an empty slot's placeholder.
constexpr SDL_Rect kDeckFrames[19] = {{0, 0, 32, 12}, {32, 0, 192, 12}, {224, 0, 32, 12}, {45, 124, 26, 22},
	{0, 0, 0, 0}, {0, 26, 248, 96}, {130, 128, 20, 52}, {109, 128, 20, 52}, {217, 128, 32, 52}, {184, 128, 32, 52},
	{151, 128, 32, 52}, {88, 128, 20, 52}, {130, 181, 20, 52}, {109, 181, 20, 52}, {217, 181, 32, 52},
	{184, 181, 32, 52}, {151, 181, 32, 52}, {88, 181, 20, 52}, {7, 123, 17, 34}};

// 0x48E700 → 0x495210: the deck (frame 5 of schiffsdeck.tga) and its 24 slots in two rows of 12, 17 pixels apart
// from its 13th pixel: the slots k with k % 4 < 2 on the upper line (DeckTopLine 36), standing up from it, the
// others on the lower one (DeckBottomLine 60), hanging down; a piece of type t is the frame 6 + t above and
// 12 + t below, an empty slot (0xFF) the placeholder (frame 18).
void draw_deck(SDL_Renderer* renderer, const PanelArt& art, const SavedShip& ship, const SDL_Rect& deck) {
	draw_picture(renderer, art.deck, deck, &kDeckFrames[5]);
	const int step = (deck.w - 0x42) / 12 + 2;
	for (int upper = 1; upper >= 0; --upper) {
		int x = deck.x + 13;
		const int anchor = deck.y + (upper ? 36 : 60);
		for (int k = upper ? 0 : 2; k < 24; k += (k & 1) ? 3 : 1) {
			const uint8_t type = ship.artillery(k);
			const SDL_Rect* f = type < 6 ? &kDeckFrames[(upper ? 6 : 12) + type] : type == 0xFF ? &kDeckFrames[18] : nullptr;
			if (f) {
				const SDL_Rect dst = {x, upper ? anchor - f->h : anchor, f->w, f->h};
				draw_picture(renderer, art.deck, dst, f);
			}
			x += step;
		}
	}
}

// --- The battle's panel (the battles' piece 7, part 3; the manual's figure 2-3): the side panel's frame with the
// battle's minimap, the action row on the plank (screenGame.ini Button26..31, menu_seeschlacht.tga; their state
// 0x610D69, BattleView::panel_buttons; touch only, as the user chose) and, in the menus' room, the deck of the
// chosen ship (the ship window's in a battle: its header, the fire button, the deck with its guns and the
// sails' rope).
enum class BattleButton { SelectAll, Capture, Plunder, Flee, WhiteFlag, Count };
constexpr int kBattleButtons = int(BattleButton::Count);
// After them, in the row's sixth place, the town panel's icon to the sea map (menu01.tga; a tooltip of the viewer):
// leaving the battle's screen gives the battle to the computer, as the PC's view does when another town or the
// sea map is shown (0x58919F → 0x611FA0 → 0x6100D9). The user's choice; it asks first.
constexpr int kBattleLeaveIcon = int(PanelIcon::Options);
constexpr int kBattleLeavePressed = kBattleButtons + 1;  // battle_button_down (kBattleButtons: the fire button).

// The action row in the panel's icon places: its picture's column in menu_seeschlacht.tga (TEX16045: «Alle
// selektieren» 0, «Seeschlacht beenden» 32 in Flee's place when the enemy is all gone, «Kapern» 64, «Fliehen» 96,
// «Plündern» 128, «Aufgeben» 160; rows neutral, pressed, hover, disabled), its tooltip, whether it is on and
// whether it shows pressed (a check button: all the chosen ships have it).
struct BattleButtonState {
	int column = 0;
	uint32_t tooltip = 0;
	bool enabled = false;
	bool checked = false;
};
BattleButtonState battle_button_state(BattleButton k, uint16_t bits, bool any_ship) {
	const bool leave = bits & 0x8000;
	switch (k) {
	case BattleButton::SelectAll:
		return {0, 20048, any_ship, false};
	case BattleButton::Capture:
		return {64, 20049, (bits & 0x10) != 0, (bits & 0x20) != 0};
	case BattleButton::Plunder:
		return {128, 20050, (bits & 0x100) != 0, (bits & 0x200) != 0};
	case BattleButton::Flee:
		return leave ? BattleButtonState{32, 20054, true, false}
					 : BattleButtonState{96, 20051, (bits & 0x1) != 0, (bits & 0x2) != 0};
	case BattleButton::WhiteFlag:
		return {160, 20052, (bits & 0x1000) != 0, (bits & 0x2000) != 0};
	default:
		return {};
	}
}

// The deck page of the battle's panel in the menus' room: the header (left of the fire button, 64 · 64 at the
// top right), the deck and, under it, the sails' rope (256 · 12, its marker ship at five places, furled on the
// left: a ship's sails 0..4).
struct BattleDeckLayout {
	ShipMenuLayout ship;
	SDL_Rect header_room{};
	SDL_Rect fire{};
	SDL_Rect deck{};
	SDL_Rect rope{};

	BattleDeckLayout(const SDL_Rect& room, int line) : ship(room, line) {
		ship.top = room.y + 8;
		header_room = {room.x, room.y, room.w - 74, room.h};
		fire = {room.x + room.w - 72, room.y + 6, 64, 64};
		deck = {room.x + 6, room.y + 8 + 4 * line + 8, 248, 96};
		rope = {room.x + 2, deck.y + deck.h + 14, 256, 12};
	}
	int sails_x(int sails) const { return rope.x + 32 + sails * 192 / 4; }
	// The sails asked by a touch on the rope (the nearest of its five places), or -1.
	int sails_at(int x, int y) const {
		if (y < rope.y - 14 || y > rope.y + rope.h + 14 || x < rope.x || x > rope.x + rope.w) {
			return -1;
		}
		return std::clamp((x - rope.x - 32 + 24) * 4 / 192, 0, 4);
	}
};

void draw_battle_panel(SDL_Renderer* renderer, TextRenderer& text, const PanelArt& art, const SidePanel& ui,
	const SaveGame& save, size_t battle, const p3::app::BattleView& view, SDL_Texture* town_minimap,
	const SDL_Rect& camera, int deck_place,
	uint16_t bits, bool any_ship, int pressed_button, bool fire_pressed, bool leave_pressed) {
	const PanelLayout layout(ui.folded);
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	const SDL_Rect bar_src = {0, 0, kPanelBar, layout.bar.h};
	draw_picture(renderer, art.bar, layout.bar, &bar_src);
	SDL_SetRenderDrawColor(renderer, 60, 38, 18, 230);
	SDL_RenderFillRect(renderer, &layout.tab);
	SDL_SetRenderDrawColor(renderer, 190, 150, 90, 255);
	SDL_RenderDrawRect(renderer, &layout.tab);
	const int cx = layout.tab.x + layout.tab.w / 2;
	const int cy = layout.tab.y + layout.tab.h / 2;
	for (int k = 0; k < 7; ++k) {
		const int dx = ui.folded ? 3 - k / 2 : -3 + k / 2;
		SDL_RenderDrawLine(renderer, cx + dx, cy - 6 + k, cx + dx, cy + 6 - k);
	}
	if (ui.folded) {
		return;
	}
	view.draw_minimap(save, battle, layout.map, camera, town_minimap);
	draw_picture(renderer, art.plank, layout.plank);
	for (int k = 0; k < kBattleButtons; ++k) {
		const BattleButtonState s = battle_button_state(BattleButton(k), bits, any_ship);
		const int row = !s.enabled ? 3 : pressed_button == k ? 2 : s.checked ? 1 : 0;
		const SDL_Rect src = {s.column, row * 32, 32, 32};
		draw_picture(renderer, art.battle_buttons, layout.icons[k], &src);
	}
	const SDL_Rect leave_src = {kPanelIconColumn[int(PanelIcon::LeaveTown)] * 32, leave_pressed ? 32 : 0, 32, 32};
	draw_picture(renderer, art.icons, layout.icons[kBattleLeaveIcon], &leave_src);
	draw_picture(renderer, art.wood, layout.room);
	const int e = deck_place >= 0 ? p3::app::BattleView::scene_entry(save, battle, uint16_t(deck_place)) : -1;
	if (e < 0) {
		return;
	}
	const BattleGraph& g = save.battles;
	const uint32_t index = g.get<uint32_t>(size_t(e), 0x4);
	if (index >= save.ships.size()) {
		return;
	}
	draw_picture(renderer, art.parchment, layout.room);
	const BattleDeckLayout deck(layout.room, text.line_height());
	draw_ship_header(renderer, text, art, save, int(index), deck.ship, deck.header_room, false);
	// The fire button (0x4942C0): on while a side's guns are loaded ([+0x130] or [+0x131] 0) and the ship has
	// firepower; fire.aim's frames: hover 0, pressed 1, neutral 2, disabled 3.
	const bool loaded = g.get<uint8_t>(size_t(e), 0x130) == 0 || g.get<uint8_t>(size_t(e), 0x131) == 0;
	const bool can_fire = loaded && save.ships[index].memory.s32(0x120) != 0;
	const SDL_Rect fire_src = {64 * (!can_fire ? 3 : fire_pressed ? 1 : 2), 0, 64, 64};
	draw_picture(renderer, art.fire, deck.fire, &fire_src);
	draw_deck(renderer, art, save.ships[index], deck.deck);
	// The rope (frames 0, 1, 2) and its marker (frame 3) at the sails asked ([+0x148]).
	const SDL_Rect left = {deck.rope.x, deck.rope.y, 32, 12};
	const SDL_Rect middle = {deck.rope.x + 32, deck.rope.y, 192, 12};
	const SDL_Rect right = {deck.rope.x + 224, deck.rope.y, 32, 12};
	draw_picture(renderer, art.deck, left, &kDeckFrames[0]);
	draw_picture(renderer, art.deck, middle, &kDeckFrames[1]);
	draw_picture(renderer, art.deck, right, &kDeckFrames[2]);
	const int sails = std::min<int>(g.get<uint8_t>(size_t(e), 0x148), 4);
	const SDL_Rect marker = {deck.sails_x(sails) - 13, deck.rope.y - 5, 26, 22};
	draw_picture(renderer, art.deck, marker, &kDeckFrames[3]);
}

// A greyed button that can't be pressed (the game's disabled row).
void draw_disabled_button(SDL_Renderer* renderer, TextRenderer& text, const SDL_Rect& r, const std::string& label) {
	SDL_SetRenderDrawColor(renderer, 120, 100, 80, 160);
	SDL_RenderFillRect(renderer, &r);
	text.draw(label, r.x + (r.w - text.measure(label)) / 2, r.y + (r.h - text.line_height()) / 2, kParchmentDim);
}

// --- The trade route page of the ship menu (CSideMenu_Ship's page [+0xE4], drawn by 0x48F2C0), its towns' list
// (0x46F9B0, mode 1) and the wares window of a stop (*0x6CBA74): their rules and operations in
// trade_route_window.hpp; the layout adapted to the Vita. ---

std::vector<std::string> stats_town_names();
void draw_scroll(SDL_Renderer* renderer, SDL_Texture* paper, const SDL_Rect& r);
void draw_panel_box(SDL_Renderer* renderer, const SDL_Rect& box, Uint8 alpha);

// A button of the route's parchments: lit where the pad points, greyed when off.
void draw_route_button(SDL_Renderer* renderer, TextRenderer& text, const SDL_Rect& r, const std::string& label, bool lit,
	bool enabled) {
	SDL_SetRenderDrawColor(renderer, lit ? 120 : 150, lit ? 80 : 115, lit ? 30 : 70, enabled ? (lit ? 140 : 60) : 25);
	SDL_RenderFillRect(renderer, &r);
	SDL_SetRenderDrawColor(renderer, 110, 80, 40, enabled ? 255 : 90);
	SDL_RenderDrawRect(renderer, &r);
	const std::string shown = cut_text(text, label, r.w - 6);
	text.draw(shown, r.x + (r.w - text.measure(shown)) / 2, r.y + (r.h - text.line_height()) / 2,
		enabled ? kParchmentInk : kParchmentDim);
}

// A scroll's arrow: a triangle up or down in `r`, dark when there is more that way.
void draw_route_arrow(SDL_Renderer* renderer, const SDL_Rect& r, bool up, bool enabled) {
	SDL_SetRenderDrawColor(renderer, 150, 115, 70, enabled ? 60 : 25);
	SDL_RenderFillRect(renderer, &r);
	SDL_SetRenderDrawColor(renderer, 110, 80, 40, enabled ? 255 : 90);
	SDL_RenderDrawRect(renderer, &r);
	SDL_SetRenderDrawColor(renderer, 70, 45, 20, enabled ? 255 : 90);
	const int cx = r.x + r.w / 2;
	const int cy = r.y + r.h / 2;
	for (int i = 0; i < 6; ++i) {
		const int y = up ? cy - 3 + i : cy + 3 - i;
		SDL_RenderDrawLine(renderer, cx - i, y, cx + i, y);
	}
}

// The page under the ship menu's buttons: one line of the header (the convoy's or the ship's name with its state's
// icon), «File» and «active», then the rows: the stops from the first and, while there
// are fewer than 20, one more with «Select town» (0x493A59) that appends a stop. A row: the number (red for the
// ship's stop now), the town button (its towns' list; dragged, the stop moves), «Goods» (the wares window) and the
// mode button («X», «R», «-»); the scroll's arrows on the right.
struct RoutePageLayout {
	SDL_Rect room{};
	int name_y = 0;
	int title_y = 0;
	SDL_Rect files{};
	SDL_Rect active{};
	int rows_y = 0;
	int row_h = 0;
	int rows = 0;  // Shown.
	SDL_Rect up{};
	SDL_Rect down{};

	RoutePageLayout(const SDL_Rect& menu_room, int line) : room(menu_room) {
		name_y = room.y + 46;
		const int button_h = line + 6;
		title_y = name_y + line + 4;
		files = {room.x + 8, title_y, 96, button_h};
		active = {room.x + room.w - 6 - 30 - 96, title_y, 96, button_h};
		row_h = line + 6;
		rows_y = title_y + button_h + 4;
		rows = std::max(1, (room.y + room.h - 4 - rows_y) / row_h);
		up = {room.x + room.w - 6 - 18, rows_y, 18, row_h - 2};
		down = {up.x, rows_y + (rows - 1) * row_h, 18, row_h - 2};
	}
	int top(int k) const { return rows_y + k * row_h; }
	SDL_Rect number(int k) const { return {room.x + 2, top(k), 18, row_h - 2}; }
	SDL_Rect town(int k) const { return {room.x + 23, top(k), 85, row_h - 2}; }
	SDL_Rect wares(int k) const { return {room.x + 111, top(k), 95, row_h - 2}; }
	SDL_Rect mode(int k) const { return {room.x + 209, top(k), 22, row_h - 2}; }
	SDL_Rect append(int k) const { return {room.x + 23, top(k), 183, row_h - 2}; }  // «Select town», alone in its row.
	// The shown row at y, -1 off the rows.
	int row_at(int y) const {
		if (y < rows_y || y >= rows_y + rows * row_h) {
			return -1;
		}
		return (y - rows_y) / row_h;
	}
};

// How many rows the page has: the stops and the row that appends one (none at 20).
int route_page_rows(const SaveGame& save, int ship) {
	const size_t stops = route_rows(save, ship).size();
	return int(stops) + (stops < 20 ? 1 : 0);
}

std::string route_town_label(const SaveGame& save, const std::vector<std::string>& names, uint8_t town) {
	// The town button's text: 0x6DDA00 by the town's index (the letters' bug, statistics_town_name).
	return clean_game_text(statistics_town_name(save, names, town));
}

void draw_route_page(SDL_Renderer* renderer, TextRenderer& text, const PanelArt& art, const SaveGame& save,
	const SidePanel& ui, const SDL_Rect& room) {
	const RoutePageLayout layout(room, text.line_height());
	const int line = text.line_height();
	const int ship_index = ui.ship;
	const SavedShip& ship = save.ships[size_t(ship_index)];
	const bool convoy = ship_menu_convoy(save, ship_index);
	int x = room.x + 8;
	x += art.icon(renderer, ship_status_line(save, ship_index).first, x, layout.name_y) + 3;
	text.draw(cut_text(text, convoy ? convoy_label(save, ship.convoy()) : ship.name(), room.x + room.w - 8 - x), x,
		layout.name_y, kParchmentInk);
	// (The game's title «Trade route» doesn't fit beside its two buttons: the page's button says it.)
	const int title_text_y = layout.title_y + (layout.files.h - line) / 2;
	const bool title_pad = ui.route_row < 0;
	draw_route_button(renderer, text, layout.files, exe_text(0x6B0394, "Archivo", "File"), title_pad && ui.route_column == 0,
		true);
	// «Activa»: a check box with its word.
	const bool can_activate = route_active_operation(save, ship_index).has_value();
	draw_route_button(renderer, text, layout.active, std::string(), title_pad && ui.route_column == 1, can_activate);
	const SDL_Rect box = {layout.active.x + 6, layout.active.y + (layout.active.h - 12) / 2, 12, 12};
	SDL_SetRenderDrawColor(renderer, 60, 40, 20, can_activate ? 255 : 110);
	SDL_RenderDrawRect(renderer, &box);
	if (route_active(save, ship_index)) {
		for (int d = 0; d < 2; ++d) {
			SDL_RenderDrawLine(renderer, box.x + 2, box.y + 5 + d, box.x + 5, box.y + 8 + d);
			SDL_RenderDrawLine(renderer, box.x + 5, box.y + 8 + d, box.x + 10, box.y + 2 + d);
		}
	}
	text.draw(cut_text(text, exe_text(0x6A43B8, "Activa", "active"), layout.active.w - 26), box.x + 17, title_text_y,
		can_activate ? kParchmentInk : kParchmentDim);
	const std::vector<RouteRow> rows = route_rows(save, ship_index);
	const int count = route_page_rows(save, ship_index);
	const std::vector<std::string> names = stats_town_names();
	static constexpr const char* kModes[3] = {"X", "R", "-"};  // 0x6BA478, 0x6BA476, 0x6BA474
	for (int k = 0; k < layout.rows && ui.route_first + k < count; ++k) {
		const int r = ui.route_first + k;
		const bool pad_row = ui.route_row == r;
		if (r == int(rows.size())) {
			draw_route_button(renderer, text, layout.append(k), exe_text(0x6A43AC, "Ciudad", "Select town"), pad_row, true);
			continue;
		}
		const RouteRow& row = rows[size_t(r)];
		const SDL_Rect n = layout.number(k);
		const std::string number = std::to_string(r + 1);
		text.draw(number, n.x + n.w - text.measure(number), n.y + (n.h - line) / 2, row.current ? kParchmentRed : kParchmentInk);
		if (r != ui.route_drag) {
			draw_route_button(renderer, text, layout.town(k), route_town_label(save, names, row.town),
				pad_row && ui.route_column == 0, true);
		}
		draw_route_button(renderer, text, layout.wares(k), exe_text(0x69B310, "Mercancías", "Goods"),
			pad_row && ui.route_column == 1, true);
		draw_route_button(renderer, text, layout.mode(k), kModes[row.mode], pad_row && ui.route_column == 2, true);
	}
	draw_route_arrow(renderer, layout.up, true, ui.route_first > 0);
	draw_route_arrow(renderer, layout.down, false, ui.route_first + layout.rows < count);
	if (ui.route_drag >= 0 && ui.route_drag < int(rows.size())) {
		// The town button being dragged, where the finger has it.
		SDL_Rect t = layout.town(0);
		t.y = ui.route_drag_top;
		draw_route_button(renderer, text, t, route_town_label(save, names, rows[size_t(ui.route_drag)].town), true, true);
	}
}

// The towns' list a town button opens (0x46F9B0 in its mode 1; sorted by name when it opens, 0x471FF0): the towns the
// stop can have and, after them, «none» (0x4713DE: 0xFF, which removes the stop); a choice for the page's row
// (0x492CA0). Over the town, in columns.
std::vector<int> route_town_entries(const SaveGame& save, int ship) {
	StatisticsChoice choice;
	choice.mode = 1;
	choice.merchants = route_town_choices(save, ship);
	if (g_texts) {
		sort_choice_by_name(save, *g_texts, stats_town_names(), choice);
	}
	std::vector<int> out(choice.merchants.begin(), choice.merchants.end());
	out.push_back(-1);
	return out;
}

struct RouteTownsLayout {
	SDL_Rect panel{};
	int title_y = 0;
	int entries_y = 0;
	int row_h = 0;
	int columns = 1;
	int rows_per_column = 1;
	int column_w = 0;
	SDL_Rect close_button{};

	RouteTownsLayout(int line, int entries) {
		const int w = std::min(560, g_town_right - 24);
		row_h = line + 6;
		const int button_h = line + 10;
		const int top = kBarHeight + 12;
		title_y = top + 10;
		entries_y = title_y + line + 10;
		const int most = std::max(1, (kScreenHeight - 12 - 12 - button_h - 8 - entries_y) / row_h);
		columns = std::max(1, (entries + most - 1) / most);
		rows_per_column = std::max(1, (entries + columns - 1) / columns);
		const int h = entries_y - top + rows_per_column * row_h + 8 + button_h + 12;
		panel = {(g_town_right - w) / 2, top, w, h};
		column_w = (panel.w - 24) / columns;
		close_button = {panel.x + panel.w / 2 - 60, panel.y + panel.h - button_h - 10, 120, button_h};
	}
	SDL_Rect entry(int k) const {
		return {panel.x + 12 + (k / rows_per_column) * column_w + 2, entries_y + (k % rows_per_column) * row_h,
			column_w - 4, row_h - 2};
	}
	int entry_at(int x, int y, int entries) const {
		for (int k = 0; k < entries; ++k) {
			if (inside(entry(k), x, y)) {
				return k;
			}
		}
		return -1;
	}
};

void draw_route_towns(SDL_Renderer* renderer, TextRenderer& text, const SaveGame& save, const SidePanel& ui) {
	if (ui.route_towns_row < 0) {
		return;
	}
	const std::vector<int> entries = route_town_entries(save, ui.ship);
	const RouteTownsLayout layout(text.line_height(), int(entries.size()));
	SDL_SetRenderDrawColor(renderer, 236, 220, 180, 250);
	SDL_RenderFillRect(renderer, &layout.panel);
	SDL_SetRenderDrawColor(renderer, 110, 80, 40, 255);
	SDL_RenderDrawRect(renderer, &layout.panel);
	text.draw(exe_text(0x6C10CC, "Elegir ciudad", "choose town"), layout.panel.x + 12, layout.title_y, kParchmentInk);
	// The row's town now, lit.
	const std::vector<RouteRow> rows = route_rows(save, ui.ship);
	const int current = ui.route_towns_row < int(rows.size()) ? int(rows[size_t(ui.route_towns_row)].town) : -2;
	const std::vector<std::string> names = stats_town_names();
	for (size_t k = 0; k < entries.size(); ++k) {
		const SDL_Rect r = layout.entry(int(k));
		const int t = entries[k];
		const bool lit = int(k) == ui.route_towns_pick || t == current;
		SDL_SetRenderDrawColor(renderer, lit ? 120 : 150, lit ? 80 : 115, lit ? 30 : 70, lit ? 110 : 40);
		SDL_RenderFillRect(renderer, &r);
		const std::string name = t < 0 ? exe_text(0x6C1078, "ninguna", "none") : route_town_label(save, names, uint8_t(t));
		text.draw(cut_text(text, name, r.w - 8), r.x + 4, r.y + (r.h - text.line_height()) / 2, kParchmentInk);
	}
	draw_button(renderer, text, layout.close_button, BTN_CIRCLE + std::string(" ") + tr("Cerrar", "Close"));
}

// The route files' list («File»: the towns' list class in its mode 4, 0x470B4B..0x470F4F), over the town: the title
// «Trade route», the files (save\AutoRoute\*.rou, 0x4721F0; a file puts its name in the box), the box with the name
// (written with the Vita's keyboard), «Load» (the name is a file's: 0x63AC5B, without case), «Save» (a name), «Delete»
// (a file's) and the close button; «Save» on a file's name and «Delete» ask first (Yes / No).
constexpr int kRouteFileColumns = 5;  // The bottom row's for the pad: the box, «Load», «Save», «Delete», close.

struct RouteFilesLayout {
	SDL_Rect panel{};
	SDL_Rect list{};
	SDL_Rect track{};
	SDL_Rect box{};
	SDL_Rect buttons[4] = {};  // «Load», «Save», «Delete», close
	SDL_Rect question{};
	SDL_Rect yes{};
	SDL_Rect no{};
	int title_y = 0;
	int row_h = 0;
	int rows = 0;  // Shown.

	explicit RouteFilesLayout(int line) {
		const int w = std::min(420, g_town_right - 24);
		panel = {(g_town_right - w) / 2, kBarHeight + 8, w, kScreenHeight - kBarHeight - 16};
		title_y = panel.y + 10;
		row_h = line + 6;
		const int button_h = line + 10;
		const int buttons_y = panel.y + panel.h - 10 - button_h;
		box = {panel.x + 12, buttons_y - 8 - (line + 8), panel.w - 24, line + 8};
		const int list_y = title_y + line + 10;
		rows = std::max(1, (box.y - 8 - list_y) / row_h);
		list = {panel.x + 12, list_y, panel.w - 24 - 10, rows * row_h};
		track = {list.x + list.w + 4, list.y, 5, list.h - 2};
		const int close_w = 48;
		const int bw = (panel.w - 24 - close_w - 3 * 6) / 3;
		for (int k = 0; k < 3; ++k) {
			buttons[k] = {panel.x + 12 + k * (bw + 6), buttons_y, bw, button_h};
		}
		buttons[3] = {panel.x + panel.w - 12 - close_w, buttons_y, close_w, button_h};
		question = {panel.x + 24, panel.y + panel.h / 2 - 80, panel.w - 48, 160};
		yes = {question.x + question.w / 2 - 6 - 100, question.y + question.h - button_h - 14, 100, button_h};
		no = {question.x + question.w / 2 + 6, yes.y, 100, button_h};
	}
	SDL_Rect row(int k) const { return {list.x, list.y + k * row_h, list.w, row_h - 2}; }
	int row_at(int x, int y) const { return inside(list, x, y) ? (y - list.y) / row_h : -1; }
	// The bottom row's cell for the pad (the box, then the buttons).
	SDL_Rect cell(int column) const { return column == 0 ? box : buttons[column - 1]; }
};

// The file whose name is `name`, without case (0x63AC5B), or -1.
int route_file_match(const std::vector<std::string>& files, const std::string& name) {
	for (size_t i = 0; i < files.size(); ++i) {
		if (files[i].size() == name.size() &&
			std::equal(files[i].begin(), files[i].end(), name.begin(), [](char a, char b) {
				return std::tolower(uint8_t(a)) == std::tolower(uint8_t(b));
			})) {
			return int(i);
		}
	}
	return -1;
}

// A question's text without the line breaks it starts with.
std::string route_question_text(uint32_t english, const char* spanish, const char* fallback) {
	std::string s = exe_text(english, spanish, fallback);
	while (!s.empty() && (s.front() == '\n' || s.front() == ' ')) {
		s.erase(s.begin());
	}
	return s;
}

void draw_route_files(SDL_Renderer* renderer, TextRenderer& text, const SidePanel& ui) {
	if (!ui.route_files) {
		return;
	}
	const RouteFilesLayout layout(text.line_height());
	const int line = text.line_height();
	SDL_SetRenderDrawColor(renderer, 236, 220, 180, 250);
	SDL_RenderFillRect(renderer, &layout.panel);
	SDL_SetRenderDrawColor(renderer, 110, 80, 40, 255);
	SDL_RenderDrawRect(renderer, &layout.panel);
	const std::string title = exe_text(0x6B0388, "Ruta comercial", "Trade route");
	text.draw(title, layout.panel.x + (layout.panel.w - text.measure(title)) / 2, layout.title_y, kParchmentInk);
	const int n = int(ui.route_file_list.size());
	const int match = route_file_match(ui.route_file_list, ui.route_file_name);
	for (int k = 0; k < layout.rows && ui.route_file_first + k < n; ++k) {
		const int i = ui.route_file_first + k;
		const SDL_Rect r = layout.row(k);
		const bool lit = i == ui.route_file_row || i == match;
		SDL_SetRenderDrawColor(renderer, lit ? 120 : 150, lit ? 80 : 115, lit ? 30 : 70, lit ? 110 : 30);
		SDL_RenderFillRect(renderer, &r);
		text.draw(cut_text(text, ui.route_file_list[size_t(i)], r.w - 8), r.x + 4, r.y + (r.h - line) / 2, kParchmentInk);
	}
	if (n > layout.rows) {
		SDL_SetRenderDrawColor(renderer, 150, 115, 70, 60);
		SDL_RenderFillRect(renderer, &layout.track);
		const int bar = std::max(8, layout.track.h * layout.rows / n);
		const SDL_Rect thumb = {layout.track.x,
			layout.track.y + (layout.track.h - bar) * ui.route_file_first / std::max(1, n - layout.rows), layout.track.w, bar};
		SDL_SetRenderDrawColor(renderer, 110, 80, 40, 255);
		SDL_RenderFillRect(renderer, &thumb);
	}
	const bool bottom = ui.route_file_row >= n;
	const bool asking = ui.route_file_question != 0;
	SDL_SetRenderDrawColor(renderer, 150, 115, 70, ui.route_file_typing ? 110 : bottom && ui.route_file_column == 0 ? 90 : 45);
	SDL_RenderFillRect(renderer, &layout.box);
	SDL_SetRenderDrawColor(renderer, 110, 80, 40, 255);
	SDL_RenderDrawRect(renderer, &layout.box);
	const std::string name = ui.route_file_name + (ui.route_file_typing ? "_" : "");
	text.draw(cut_text(text, name, layout.box.w - 10), layout.box.x + 5, layout.box.y + (layout.box.h - line) / 2,
		kParchmentInk);
	const std::string labels[3] = {exe_text(0x6B039C, "Cargar", "Load"), exe_text(0x6B03A4, "Guardar", "Save"),
		exe_text(0x69AF30, "Borrar", "Delete")};
	const bool enabled[3] = {match >= 0 && !asking, !ui.route_file_name.empty() && !asking, match >= 0 && !asking};
	for (int k = 0; k < 3; ++k) {
		draw_route_button(renderer, text, layout.buttons[k], labels[k], bottom && ui.route_file_column == k + 1, enabled[k]);
	}
	draw_route_button(renderer, text, layout.buttons[3], BTN_CIRCLE, bottom && ui.route_file_column == 4, true);
	if (!asking) {
		return;
	}
	// The question: «This file already exists, overwrite?» or «Do you really want to delete this trade route?».
	draw_panel_box(renderer, layout.question, 235);
	const std::string ask = ui.route_file_question == 1
		? route_question_text(0x6B03AC, "Este archivo ya existe. ¿Sobrescribir?", "This file already exists, overwrite?")
		: route_question_text(0x6B0508, "¿Realmente quieres borrar esta ruta comercial?",
			  "Do you really want to delete this trade route?");
	int y = layout.question.y + 14;
	for (const std::string& l : wrap_text(text, ask, layout.question.w - 24)) {
		text.draw(l, layout.question.x + (layout.question.w - text.measure(l)) / 2, y, kTextColor);
		y += line;
	}
	draw_button(renderer, text, layout.yes, BTN_CROSS + std::string(" ") + exe_text(0x699948, "Sí", "Yes"));
	draw_button(renderer, text, layout.no, BTN_CIRCLE + std::string(" ") + exe_text(0x699950, "No", "No"));
}

// The wares window of a stop (0x405A20; parchment.ini [Autotradeparchment0]) on the game's parchment over the town:
// the title «Automatic maritime trading in %s», the columns' names, the 20 rows in the stop's order (the ware's
// name, dragged to move its row; its direction button; the amount's −, its units or «Max» with the barrel or the
// bale, +; «min» or «max» with the price's −, the price, +), the legend and the bottom buttons: «<», «>», the
// direction of them all, «Max», «0», «Undo» and the close button. The rows that don't fit scroll.
constexpr int kStopRows = 20;
constexpr int kStopBottomButtons = 7;  // «<», «>», all, «Máx.», «0», «Deshaz», close
// A row's cells for the pad: the direction, the amount's −, box and +, the price's −, box and +.
constexpr int kStopColumns = 7;

struct StopWindowLayout {
	SDL_Rect panel{};
	SDL_Rect inner{};
	int title_y = 0;
	int head_y = 0;
	int rows_y = 0;
	int row_h = 0;
	int rows = 0;  // Shown.
	int legend_y = 0;
	// The columns: x and width.
	int name_x = 0, name_w = 0, dir_x = 0, dir_w = 0, aminus_x = 0, amount_x = 0, amount_w = 0, unit_x = 0, aplus_x = 0;
	int label_x = 0, label_w = 0, pminus_x = 0, price_x = 0, price_w = 0, pplus_x = 0, step_w = 0;
	SDL_Rect track{};  // The scroll's.
	SDL_Rect bottom[kStopBottomButtons] = {};

	explicit StopWindowLayout(int line) {
		const int w = std::min(620, g_town_right - 12);
		panel = {(g_town_right - w) / 2, kBarHeight + 2, w, kScreenHeight - kBarHeight - 4};
		inner = {panel.x + 36, panel.y + 38, panel.w - 72, panel.h - 38 - 38};
		title_y = inner.y;
		head_y = title_y + line + 6;
		rows_y = head_y + line + 2;
		row_h = line + 3;
		const int button_h = line + 6;
		const int bottom_y = inner.y + inner.h - button_h;
		legend_y = bottom_y - line - 6;
		rows = std::max(1, std::min(kStopRows, (legend_y - 4 - rows_y) / row_h));
		step_w = 24;
		name_w = 136;
		dir_w = 52;
		amount_w = 52;
		label_w = 44;
		price_w = 56;
		const int used = name_w + 6 + dir_w + 12 + step_w + amount_w + 26 + step_w + 12 + label_w + step_w + price_w + step_w;
		int x = inner.x + std::max(0, (inner.w - 10 - used) / 2);
		name_x = x;
		x += name_w + 6;
		dir_x = x;
		x += dir_w + 12;
		aminus_x = x;
		x += step_w;
		amount_x = x;
		x += amount_w;
		unit_x = x;
		x += 26;
		aplus_x = x;
		x += step_w + 12;
		label_x = x;
		x += label_w;
		pminus_x = x;
		x += step_w;
		price_x = x;
		x += price_w;
		pplus_x = x;
		track = {inner.x + inner.w - 6, rows_y, 5, rows * row_h - 2};
		const int widths[kStopBottomButtons] = {36, 36, 58, 58, 36, 84, 48};
		const int xs[kStopBottomButtons] = {inner.x, inner.x + 42, inner.x + 90, inner.x + inner.w / 2 - 40,
			inner.x + inner.w / 2 + 24, inner.x + inner.w - 48 - 8 - 84, inner.x + inner.w - 48};
		for (int k = 0; k < kStopBottomButtons; ++k) {
			bottom[k] = {xs[k], bottom_y, widths[k], button_h};
		}
	}
	int top(int k) const { return rows_y + k * row_h; }
	SDL_Rect name(int k) const { return {name_x, top(k), name_w, row_h - 2}; }
	SDL_Rect dir(int k) const { return {dir_x, top(k), dir_w, row_h - 2}; }
	// The pad's columns: 0 the direction, 1..3 the amount's −, box and +, 4..6 the price's.
	SDL_Rect cell(int k, int column) const {
		const int xs[kStopColumns] = {dir_x, aminus_x, amount_x + 2, aplus_x, pminus_x, price_x + 2, pplus_x};
		const int ws[kStopColumns] = {dir_w, step_w, amount_w - 4, step_w, step_w, price_w - 4, step_w};
		return {xs[column], top(k), ws[column], row_h - 2};
	}
	int row_at(int y) const {
		if (y < rows_y || y >= rows_y + rows * row_h) {
			return -1;
		}
		return (y - rows_y) / row_h;
	}
};

// A direction's picture in bttn_transfer.tga (its neutral frame; +2 the lit one), -1 for none («---»).
int stop_direction_frame(StopDirection d) {
	switch (d) {
	case StopDirection::Sell: return 0;
	case StopDirection::ToOffice: return 3;
	case StopDirection::Buy: return 6;
	case StopDirection::FromOffice: return 9;
	default: return -1;
	}
}

void draw_stop_direction(SDL_Renderer* renderer, TextRenderer& text, const PanelArt& art, const SDL_Rect& r,
	StopDirection d, bool lit) {
	const int frame = stop_direction_frame(d);
	if (frame < 0 || !art.transfer) {
		// «---» (Button11, a large button that always looks off).
		draw_route_button(renderer, text, r, "---", lit, true);
		return;
	}
	const SDL_Rect src = {0, 18 * (frame + (lit ? 2 : 0)), 48, 18};
	const SDL_Rect dst = {r.x + (r.w - 48) / 2, r.y + (r.h - 18) / 2, 48, 18};
	SDL_RenderCopy(renderer, art.transfer, &src, &dst);
}

void draw_stop_window(SDL_Renderer* renderer, TextRenderer& text, const PanelArt& art, const SaveGame& save,
	const SidePanel& ui) {
	if (!ui.stop_window) {
		return;
	}
	const StopWindow& w = *ui.stop_window;
	const StopWindowLayout layout(text.line_height());
	const int line = text.line_height();
	draw_scroll(renderer, art.paper, layout.panel);
	// «Automatic maritime trading in %s», the town by its index as the page's.
	const uint8_t town = w.stop < save.trade_routes.size() ? save.trade_routes[w.stop].u8(0x2) : 0;
	const std::string title = exe_text(0x69B2B0, "Ruta comercial automática", "Automatic maritime trading") + " " +
		exe_text(0x69B2FC, "en", "in") + " " + route_town_label(save, stats_town_names(), town);
	text.draw(cut_text(text, title, layout.inner.w), layout.inner.x + (layout.inner.w - text.measure(title)) / 2,
		layout.title_y, kParchmentInk);
	text.draw(exe_text(0x69B310, "Mercancías", "Goods"), layout.name_x + 4, layout.head_y, kParchmentInk);
	const std::string amount = exe_text(0x69B308, "Cantidad", "Ammount");
	text.draw(amount, (layout.aminus_x + layout.aplus_x + layout.step_w - text.measure(amount)) / 2, layout.head_y,
		kParchmentInk);
	const std::string price = exe_text(0x69B300, "Precio", "Price");
	text.draw(price, (layout.pminus_x + layout.pplus_x + layout.step_w - text.measure(price)) / 2, layout.head_y,
		kParchmentInk);
	const std::string most = exe_text(0x69A358, "Máx.", "Max");
	for (int k = 0; k < layout.rows && ui.stop_first + k < kStopRows; ++k) {
		const int r = ui.stop_first + k;
		const int ware = w.order[size_t(r)];
		if (ware >= kStopRows) {
			continue;
		}
		const bool pad_row = ui.stop_row == r;
		auto lit = [&](int column) { return pad_row && ui.stop_column == column; };
		const int y = layout.top(k);
		const int h = layout.row_h - 2;
		if (r != ui.stop_drag) {
			draw_route_button(renderer, text, layout.name(k), ware_label(ware), pad_row, true);
		}
		const StopDirection d = w.state[size_t(ware)];
		draw_stop_direction(renderer, text, art, layout.dir(k), d, lit(0));
		if (d == StopDirection::None) {
			continue;
		}
		// An edit box: its number, or the digits being written with a cursor.
		auto draw_box = [&](int column, const std::string& value, bool price_box) {
			const SDL_Rect box = layout.cell(k, column);
			const bool typing = ui.stop_typing == ware && ui.stop_typing_price == price_box;
			SDL_SetRenderDrawColor(renderer, 150, 115, 70, typing ? 110 : lit(column) ? 90 : 35);
			SDL_RenderFillRect(renderer, &box);
			const std::string shown = typing ? ui.stop_typed + "_" : value;
			text.draw(shown, box.x + (box.w - text.measure(shown)) / 2, y + (h - line) / 2, kParchmentInk);
		};
		draw_route_button(renderer, text, layout.cell(k, 1), "-", lit(1), stop_amount_step_enabled(w, ware, -1));
		const int32_t units = w.units[size_t(ware)];
		draw_box(2, units < 0 ? most : std::to_string(units), false);
		const bool barrels = kWares[size_t(ware)].unit == WareUnit::Barrel;
		const PanelArt::Icon unit = barrels ? PanelArt::Barrel : PanelArt::Bale;
		art.icon(renderer, unit, layout.unit_x + (26 - art.icon_width[unit]) / 2, y + (h - art.icon_height[unit]) / 2);
		draw_route_button(renderer, text, layout.cell(k, 3), "+", lit(3), stop_amount_step_enabled(w, ware, 1));
		if (d != StopDirection::Sell && d != StopDirection::Buy) {
			continue;
		}
		// Selling: the least price («min»); buying: the most («max»).
		const std::string label = d == StopDirection::Sell ? exe_text(0x69B2D0, "mín.", "min") : exe_text(0x69B2CC, "máx.", "max");
		text.draw(label, layout.label_x + (layout.label_w - text.measure(label)) / 2, y + (h - line) / 2, kParchmentInk);
		draw_route_button(renderer, text, layout.cell(k, 4), "-", lit(4), stop_price_step_enabled(w, ware, -1));
		draw_box(5, std::to_string(w.price[size_t(ware)]), true);
		draw_route_button(renderer, text, layout.cell(k, 6), "+", lit(6), stop_price_step_enabled(w, ware, 1));
	}
	if (layout.rows < kStopRows) {
		// The scroll: its track and the part shown.
		SDL_SetRenderDrawColor(renderer, 150, 115, 70, 60);
		SDL_RenderFillRect(renderer, &layout.track);
		const int bar = layout.track.h * layout.rows / kStopRows;
		const SDL_Rect thumb = {layout.track.x, layout.track.y + (layout.track.h - bar) * ui.stop_first / (kStopRows - layout.rows),
			layout.track.w, bar};
		SDL_SetRenderDrawColor(renderer, 110, 80, 40, 255);
		SDL_RenderFillRect(renderer, &thumb);
	}
	if (ui.stop_drag >= 0 && ui.stop_drag < kStopRows && w.order[size_t(ui.stop_drag)] < kStopRows) {
		// The ware's name being dragged, where the finger has it.
		SDL_Rect n = layout.name(0);
		n.y = ui.stop_drag_top;
		draw_route_button(renderer, text, n, ware_label(w.order[size_t(ui.stop_drag)]), true, true);
	}
	// The legend: the town's and the office's pictures (the buttons' right halves) with their words.
	int x = layout.inner.x + 8;
	const std::string legend[2] = {exe_text(0x69B2D4, "Comerciar con ciudad", "...Town trade"),
		exe_text(0x69B2E4, "Comerciar con oficina", "...Trading office trade")};
	for (int k = 0; k < 2; ++k) {
		if (art.transfer) {
			const SDL_Rect src = {22, 18 * (k == 0 ? 0 : 3), 26, 18};
			const SDL_Rect dst = {x, layout.legend_y + (line - 18) / 2, 26, 18};
			SDL_RenderCopy(renderer, art.transfer, &src, &dst);
		}
		text.draw(legend[k], x + 30, layout.legend_y, kParchmentInk);
		x = layout.inner.x + layout.inner.w / 2 + 8;
	}
	const bool bottom_pad = ui.stop_row == kStopRows;
	auto bottom_lit = [&](int k) { return bottom_pad && ui.stop_column == k; };
	draw_route_button(renderer, text, layout.bottom[0], "<", bottom_lit(0), true);
	draw_route_button(renderer, text, layout.bottom[1], ">", bottom_lit(1), true);
	draw_stop_direction(renderer, text, art, layout.bottom[2], w.all, bottom_lit(2));
	draw_route_button(renderer, text, layout.bottom[3], most, bottom_lit(3), true);
	draw_route_button(renderer, text, layout.bottom[4], "0", bottom_lit(4), true);
	draw_route_button(renderer, text, layout.bottom[5], exe_text(0x692C48, "Deshaz", "Undo"), bottom_lit(5), true);
	draw_route_button(renderer, text, layout.bottom[6], BTN_CIRCLE, bottom_lit(6), true);
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
	case ShipPage::Deck:
		draw_ship_header(renderer, text, art, save, ship_index, layout, room, false);
		draw_deck(renderer, art, ship, {room.x + 6, layout.deck_y, 248, 96});
		break;
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
		draw_route_page(renderer, text, art, save, ui, room);
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

// The battles' window (SaveGame::battle_window, the game's dialog *0x6CBB10 opened by 0x41D9B0): a battle's
// question when one against the player starts (the user's choice: two buttons, «Dirigir manualmente» and «Que
// luche el ordenador», and a bar of the time left: the player's side's countdown [+0x15C] of 250, which the
// engine counts down while it is open and at whose end it closes the window), the end's or a boarding's message
// (no button: «Continuar»), and the window of the sea map's battle icon (0x611FC0). Each button has its own
// key, as the viewer's other windows: X the first (closing the question with it answers «steer manually»:
// result 10, the game's button), Circle the last (the computer fights it; or closes). Steering needs the
// battle's screen (piece 7, part 3: battle_view.cpp, a sea battle for now); without it that button is off.
constexpr bool kBattleScreen = true;

struct ChoiceBox {
	std::string title;
	std::string body;
	std::vector<std::pair<std::string, bool>> buttons;  // Each label (with its key) and whether it can be pressed.
	int bar = -1;  // The time bar's part left, in thousandths; -1 without one.
	bool low = false;  // At the screen's bottom (over the battle's screen, so that the battle shows above it).
};

// A game's text as a box's title (its first line) and body (the rest), its empty lines at the ends dropped.
void split_title(const std::string& raw, ChoiceBox& box) {
	std::string clean = clean_game_text(raw);
	const size_t start = clean.find_first_not_of('\n');
	clean = start == std::string::npos ? std::string() : clean.substr(start);
	const size_t line = clean.find('\n');
	box.title = clean.substr(0, line);
	std::string body = line == std::string::npos ? std::string() : clean.substr(line + 1);
	const size_t first = body.find_first_not_of('\n');
	const size_t last = body.find_last_not_of('\n');
	box.body = first == std::string::npos ? std::string() : body.substr(first, last - first + 1);
}

struct ChoiceBoxLayout {
	SDL_Rect box{};
	SDL_Rect bar{};
	std::vector<SDL_Rect> buttons;
	std::vector<std::string> lines;
	int body_y = 0;

	ChoiceBoxLayout(const TextRenderer& title, const TextRenderer& text, const ChoiceBox& c) {
		const int width = 640;
		const std::string& body = c.body;
		size_t from = 0;
		while (from <= body.size()) {
			const size_t end = body.find('\n', from);
			const std::string paragraph = body.substr(from, end == std::string::npos ? std::string::npos : end - from);
			const std::vector<std::string> wrapped = wrap_text(text, paragraph, width - 40);
			if (wrapped.empty()) {
				lines.emplace_back();
			}
			lines.insert(lines.end(), wrapped.begin(), wrapped.end());
			if (end == std::string::npos) {
				break;
			}
			from = end + 1;
		}
		const int button_height = 48;
		int height = 20 + title.line_height() + 14 + int(lines.size()) * text.line_height() + 18;
		if (c.bar >= 0) {
			height += 14 + 14;
		}
		height += button_height + 16;
		box = {(kScreenWidth - width) / 2, c.low ? kScreenHeight - height - 12 : std::max(8, (kScreenHeight - height) / 2),
			width, height};
		body_y = box.y + 20 + title.line_height() + 14;
		int y = body_y + int(lines.size()) * text.line_height() + 18;
		if (c.bar >= 0) {
			bar = {box.x + 20, y, width - 40, 14};
			y += 14 + 14;
		}
		const int n = std::max(1, int(c.buttons.size()));
		const int gap = 20;
		const int button_width = std::min(280, (width - 40 - gap * (n - 1)) / n);
		const int total = n * button_width + (n - 1) * gap;
		for (int k = 0; k < int(c.buttons.size()); ++k) {
			buttons.push_back({box.x + (width - total) / 2 + k * (button_width + gap), y, button_width, button_height});
		}
	}

	int button_at(int x, int y) const {
		for (int k = 0; k < int(buttons.size()); ++k) {
			if (inside(buttons[size_t(k)], x, y)) {
				return k;
			}
		}
		return -1;
	}
};

void draw_choice_box(SDL_Renderer* renderer, TextRenderer& title, TextRenderer& text, const ChoiceBox& c) {
	const ChoiceBoxLayout l(title, text, c);
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(renderer, 30, 22, 14, 240);
	SDL_RenderFillRect(renderer, &l.box);
	SDL_SetRenderDrawColor(renderer, 190, 150, 90, 255);
	SDL_RenderDrawRect(renderer, &l.box);
	title.draw(c.title, l.box.x + (l.box.w - title.measure(c.title)) / 2, l.box.y + 20, kTextColor);
	int y = l.body_y;
	for (const std::string& line : l.lines) {
		text.draw(line, l.box.x + 20, y, kTextColor);
		y += text.line_height();
	}
	if (c.bar >= 0) {
		SDL_SetRenderDrawColor(renderer, 60, 45, 28, 255);
		SDL_RenderFillRect(renderer, &l.bar);
		const SDL_Rect left = {l.bar.x, l.bar.y, l.bar.w * std::clamp(c.bar, 0, 1000) / 1000, l.bar.h};
		SDL_SetRenderDrawColor(renderer, 200, 160, 70, 255);
		SDL_RenderFillRect(renderer, &left);
		SDL_SetRenderDrawColor(renderer, 150, 120, 70, 255);
		SDL_RenderDrawRect(renderer, &l.bar);
	}
	for (size_t k = 0; k < c.buttons.size(); ++k) {
		const SDL_Rect& r = l.buttons[k];
		const bool enabled = c.buttons[k].second;
		SDL_SetRenderDrawColor(renderer, enabled ? 90 : 55, enabled ? 65 : 45, enabled ? 35 : 35, 255);
		SDL_RenderFillRect(renderer, &r);
		SDL_SetRenderDrawColor(renderer, 190, 150, 90, 255);
		SDL_RenderDrawRect(renderer, &r);
		const std::string& label = c.buttons[k].first;
		text.draw(label, r.x + (r.w - text.measure(label)) / 2, r.y + (r.h - text.line_height()) / 2,
			enabled ? kTextColor : SDL_Color{200, 185, 160, 110});
	}
}

void draw_panel_box(SDL_Renderer* renderer, const SDL_Rect& box, Uint8 alpha = 235) {
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(renderer, 30, 22, 14, alpha);
	SDL_RenderFillRect(renderer, &box);
	SDL_SetRenderDrawColor(renderer, 150, 120, 70, 255);
	SDL_RenderDrawRect(renderer, &box);
}

// A ship's label on the battle's screen (the manual's figure 2-3: its name above; below, its sailors, the
// hull's heart and percent, and the firepower's cannonballs as the ship's panel shows them, 0x4901C0), its
// bottom at the label's point; the player's in a blue box, the others' in a brown one. Over a chosen ship of
// his, the manual's blue arrow (markerpfeil01.aim) above its label, pointing at it.
void draw_battle_label(SDL_Renderer* renderer, TextRenderer& text, const PanelArt& art, const p3::app::BattleLabel& label,
	SDL_Texture* arrow) {
	const std::string sailors = std::to_string(label.sailors);
	const std::string hull = std::to_string(std::max(label.hull, 0));
	const FirepowerBalls balls = firepower_balls(label.firepower);
	auto icon_w = [&](PanelArt::Icon k) { return art.list_icons[k] ? art.icon_width[k] : 0; };
	const int balls_w = balls.big * icon_w(PanelArt::BigBall) + balls.small * icon_w(PanelArt::SmallBall) +
		(balls.half ? icon_w(PanelArt::SmallBall) / 2 : 0);
	const int stats_w = text.measure(sailors) + 8 + icon_w(PanelArt::Hull) + 2 + text.measure(hull) +
		(balls_w > 0 ? 8 + balls_w : 0);
	const int w = std::max(text.measure(label.name), stats_w) + 10;
	const int line = text.line_height();
	const SDL_Rect box = {label.x - w / 2, label.y - 2 * line - 4, w, 2 * line + 4};
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(renderer, label.own ? 40 : 60, label.own ? 50 : 26, label.own ? 80 : 20, 170);
	SDL_RenderFillRect(renderer, &box);
	if (label.selected && arrow) {
		int aw = 0;
		int ah = 0;
		SDL_QueryTexture(arrow, nullptr, nullptr, &aw, &ah);
		const SDL_Rect dst = {label.x - aw / 2, box.y - ah - 2, aw, ah};
		SDL_RenderCopy(renderer, arrow, nullptr, &dst);
	}
	text.draw(label.name, label.x - text.measure(label.name) / 2, box.y + 1, kTextColor);
	const int y = box.y + line + 2;
	int x = label.x - stats_w / 2;
	text.draw(sailors, x, y, kDimColor);
	x += text.measure(sailors) + 8;
	x += art.icon(renderer, PanelArt::Hull, x, y) + 2;
	text.draw(hull, x, y, label.hull < 40 ? SDL_Color{255, 120, 100, 255} : kDimColor);
	x += text.measure(hull) + 8;
	for (int k = 0; k < balls.big; ++k) {
		x += art.icon(renderer, PanelArt::BigBall, x, y + 3);
	}
	for (int k = 0; k < balls.small; ++k) {
		x += art.icon(renderer, PanelArt::SmallBall, x, y + 3);
	}
	if (balls.half && art.list_icons[PanelArt::SmallBall]) {
		const int bw = art.icon_width[PanelArt::SmallBall];
		const SDL_Rect src = {0, 0, bw / 2, art.icon_height[PanelArt::SmallBall]};
		const SDL_Rect dst = {x, y + 3, bw / 2, art.icon_height[PanelArt::SmallBall]};
		SDL_RenderCopy(renderer, art.list_icons[PanelArt::SmallBall], &src, &dst);
	}
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
	// Wrapped, UTF-8 (the game's pictures by number in them, as the chronicle's ornament: TextRenderer draws them).
	std::vector<std::pair<LetterParagraph::Align, std::string>> lines;
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
	// The buttons of the letter shown (letter_window.hpp: their labels in UTF-8; X the first, Triangle the second),
	// made each frame. Once one is pressed, the letter shows none until another letter is shown (as the game's,
	// whose buttons come back when the letter is opened again; some, as the proposal's, stay valid till the day's
	// end).
	std::vector<std::string> answers;
	uint16_t answered = 0xFFFF;
};

// The letters' settings window's rows (0x408F50): for each class its days, its
// notice and its sound, then the event sound.
constexpr int kMailOptionRows = 10;

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
	std::array<SDL_Rect, 3> answers{};  // The letter's buttons (with_answers).
	int answer_count = 0;

	// Room for the letter's `count` buttons: in the panel, in a row with the close button at the parchment's foot;
	// on the whole screen, under the letter.
	void with_answers(int count) {
		answer_count = std::clamp(count, 0, int(answers.size()));
		if (answer_count == 0) {
			return;
		}
		constexpr int kGap = 10;
		if (in_panel) {
			const int w = 200;
			const int total = answer_count * (w + kGap) + close_button.w;
			const int x0 = panel.x + (panel.w - total) / 2;
			for (int k = 0; k < answer_count; ++k) {
				answers[size_t(k)] = {x0 + k * (w + kGap), close_button.y, w, close_button.h};
			}
			close_button.x = x0 + answer_count * (w + kGap);
			return;
		}
		const int h = close_button.h;
		text.h -= h + 6;
		const int w = std::min(240, (text.w - (answer_count - 1) * kGap) / answer_count);
		for (int k = 0; k < answer_count; ++k) {
			answers[size_t(k)] = {text.x + k * (w + kGap), text.y + text.h + 6, w, h};
		}
	}
	int answer_at(int x, int y) const {
		for (int k = 0; k < answer_count; ++k) {
			if (inside(answers[size_t(k)], x, y)) {
				return k;
			}
		}
		return -1;
	}

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

// Whether an operation with these very bytes is still in the queue.
bool operation_queued(const SaveGame& save, const OperationBytes& op) {
	for (const std::vector<uint8_t>& q : save.queued_operations) {
		if (q.size() >= op.size() && std::equal(op.begin(), op.end(), q.begin())) {
			return true;
		}
	}
	return false;
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

// The letter's buttons (letter_window.hpp), each with its key: X the first, Triangle the second; a label too long
// for its button is cut.
void draw_letter_answers(SDL_Renderer* renderer, TextRenderer& text, const MailUi& ui, const MailLayout& layout) {
	static const char* const kKeys[3] = {BTN_CROSS " ", BTN_TRIANGLE " ", ""};
	for (int k = 0; k < layout.answer_count && k < int(ui.answers.size()); ++k) {
		const SDL_Rect& r = layout.answers[size_t(k)];
		std::string label = ui.answers[size_t(k)];
		while (!label.empty() && text.measure(kKeys[k] + label) > r.w - 8) {
			label.pop_back();
			while (!label.empty() && (uint8_t(label.back()) & 0xC0) == 0x80) {
				label.pop_back();  // Never half a UTF-8 character.
			}
		}
		draw_button(renderer, text, r, kKeys[k] + label);
	}
}

void draw_mailbox(SDL_Renderer* renderer, TextRenderer& text, const SaveGame& save, const MailUi& ui,
	const std::vector<uint16_t>& rows) {
	const bool chronicle = ui.tab == 0;
	MailLayout layout(text.line_height(), chronicle);
	layout.with_answers(int(ui.answers.size()));
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
	draw_letter_answers(renderer, text, ui, layout);
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
	draw_letter_answers(renderer, text, ui, layout);
	draw_button(renderer, text, layout.close_button, BTN_CIRCLE + std::string(" ") + tr("Cerrar", "Close"));
}

// --- The statistics (the game's SIDEMENU_STATISTIK, statistics_window.hpp): the menu in the panel's room (its four
// pages' tabs, the page's title, its entries) and the window on a parchment over the town (the entry's title, the
// period's buttons, the balance's table, a graph or Suministro's table; the competition's merchant buttons and their
// list; the town comparison's residents' table, its graphs with their town and ware buttons). Opened by the top
// bar's money. Done: the town comparison (page 1), the company overview (2) and the competition (3); the town data's
// tab is off. ---

// The tabs' frames in page_statistiken.tga (sidemenu.ini Button14..17 → textures.ini TEX20034's frames 8, 4, 12,
// 0): their column and width (rows: normal, pressed, lit, off).
constexpr int kStatsTabX[kStatisticsPages] = {0, 100, 36, 68};
constexpr int kStatsTabW[kStatisticsPages] = {36, 36, 32, 32};
constexpr bool kStatsPageDone[kStatisticsPages] = {true, true, true, true};

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
		// The competition's merchant buttons, under its graph and the texts beside it.
		const int plot_w = body.w - kStatsSideW - 18;
		const int mw = (body.w - 8 - 3 * 6) / 4;
		for (int k = 0; k < 4; ++k) {
			merchants[k] = {body.x + 4 + k * (mw + 6), body.y + body.h - line_height - 8, mw, line_height + 6};
		}
		// A merchant button's list, over the graph and its texts: its title and sort button, then the rows in two
		// columns.
		choice = {body.x, body.y, body.w, merchants[0].y - 6 - body.y};
		(void)plot_w;
		sort_button = {choice.x + choice.w - 230, choice.y + 4, 226, line_height + 6};
		row_h = line_height + 6;
		rows_top = choice.y + line_height + 18;
		rows_per_column = std::max(1, (choice.y + choice.h - rows_top) / row_h);
	}
	static constexpr int kStatsSideW = 230;  // The texts beside a graph.
	SDL_Rect merchants[4] = {};
	SDL_Rect choice{};
	SDL_Rect sort_button{};
	int row_h = 0;
	int rows_top = 0;
	int rows_per_column = 1;
	// The list's columns for n rows (2 to 4) and a row's place.
	int choice_columns(int n) const {
		return std::clamp((n + rows_per_column - 1) / rows_per_column, 2, 4);
	}
	SDL_Rect choice_row(int k, int columns) const {
		const int w = (choice.w - 4 - 4 * columns) / columns;
		return {choice.x + 4 + (k / rows_per_column) * (w + 4), rows_top + (k % rows_per_column) * row_h, w, row_h - 2};
	}
	int period_at(int x, int y) const {
		for (int k = 0; k < 4; ++k) {
			if (inside(periods[k], x, y)) {
				return k;
			}
		}
		return -1;
	}
	int merchant_at(int x, int y) const {
		for (int k = 0; k < 4; ++k) {
			if (inside(merchants[k], x, y)) {
				return k;
			}
		}
		return -1;
	}
};

// The town names by number as the game holds them (Latin-1, like its other texts), for the statistics' engine.
std::vector<std::string> stats_town_names() {
	std::vector<std::string> out;
	if (g_town_names) {
		for (const std::string& name : *g_town_names) {
			out.push_back(utf8_to_latin1(name));
		}
	}
	return out;
}

// The list a button of the statistics opens, as the game's (0x49E850, 0x49FC1C, 0x49FB41): a merchant's (the
// competition), a town's (the comparison's slots), a ware's (the town data's slots) or the button [+0x214]'s (slot 4:
// the comparison's ware, the town data's town), sorted by name (or by the entry's key, with its button), and the
// extra row (-1: «no trader», «none») when it may empty the slot.
StatisticsChoice stats_choice(const SaveGame& save, const SidePanel& ui) {
	StatisticsChoice choice;
	if (ui.stats_choice_slot < 0 || !g_texts) {
		return choice;
	}
	if (stats_competition(ui.stats_entry)) {
		choice = statistics_competition_choice(save, ui.stats_merchants, ui.stats_choice_slot, ui.stats_entry);
	} else if (ui.stats_choice_slot == 4) {
		choice = stats_town_data(ui.stats_entry) ? statistics_town_list(save) : statistics_ware_choice();
	} else {
		choice = statistics_town_choice(save, ui.stats_towns, ui.stats_choice_slot, ui.stats_entry);
	}
	sort_choice_by_name(save, *g_texts, stats_town_names(), choice);
	if (ui.stats_choice_by_key) {
		sort_choice_by_key(choice);
	}
	return choice;
}

std::vector<int> stats_choice_rows(const SaveGame& save, const SidePanel& ui) {
	const StatisticsChoice choice = stats_choice(save, ui);
	std::vector<int> rows(choice.merchants.begin(), choice.merchants.end());
	if (choice.none) {
		rows.push_back(-1);
	}
	return rows;
}

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
	// 0x4A5DD0's scale: from the entry's floor up to the largest point, at 0.95 of the height.
	float top = statistics_scale_floor(entry);
	for (uint32_t s = 0; s < g.series && s < 5; ++s) {
		for (float v : g.values[s]) {
			top = std::max(top, v);
		}
	}
	auto height_of = [&](float v) { return plot.y + plot.h - 1 - int(float(plot.h) * 0.95f * std::max(0.0f, v) / top); };
	// The needs, the quality of life and the export balance (the scale's kind 5) have no numbers on the axis: «max.»
	// and «min.», or «exp.», «0» and «imp.», as the game's axes (0x4A0190), at the zero line (their last series,
	// colour 0xFFFFFFFF; the needs without one, at the bottom).
	const bool marked = entry >= kStatisticsRequirement && entry <= kStatisticsExportBalance;
	for (int k = 1; k <= 4; ++k) {
		const int y = plot.y + plot.h - int(plot.h * 0.95f * float(k) / 4.0f);
		SDL_SetRenderDrawColor(renderer, 160, 130, 90, 120);
		SDL_RenderDrawLine(renderer, plot.x + 1, y, plot.x + plot.w - 2, y);
		if (!marked) {
			const std::string label = format_money(int64_t(top * float(k) / 4.0f));
			text.draw(label, plot.x + 4, y - line + 2, kParchmentDim);
		}
	}
	if (marked) {
		const bool zero = g.series >= 1 && g.colours[g.series - 1] == 0xFFFFFFFFu && !g.values[g.series - 1].empty();
		const int zero_y = zero ? height_of(g.values[g.series - 1][0]) : plot.y + plot.h - 1;
		const bool exports = entry == kStatisticsExportBalance;
		text.draw(stats_text(g_texts->raw_at(exports ? 0x6C225C : 0x6C2268)), plot.x + 4, plot.y + 2, kParchmentDim);
		if (exports) {
			text.draw("0", plot.x + plot.w - 4 - text.measure("0"), zero_y - line, kParchmentDim);
			text.draw(stats_text(g_texts->raw_at(0x6C2250)), plot.x + 4, plot.y + plot.h - line - 2, kParchmentDim);
		} else {
			text.draw(stats_text(g_texts->raw_at(0x6C2274)), plot.x + 4, std::min(zero_y, plot.y + plot.h - 2) - line,
				kParchmentDim);
		}
	}
	if (g.points >= 1) {
		// The recorded points (the oldest `missing` aren't), joined, each with a dot; the zero line, plain ink.
		const float step = g.points > 1 ? float(plot.w - 6) / float(g.points - 1) : 0.0f;
		for (uint32_t s = 0; s < g.series && s < 5; ++s) {
			const uint32_t colour = g.colours[s];
			if (colour == 0xFFFFFFFFu) {
				if (!g.values[s].empty()) {
					const int y = height_of(g.values[s][0]);
					SDL_SetRenderDrawColor(renderer, 70, 45, 20, 255);
					SDL_RenderDrawLine(renderer, plot.x + 1, y, plot.x + plot.w - 2, y);
				}
				continue;
			}
			SDL_SetRenderDrawColor(renderer, Uint8(colour >> 16), Uint8(colour >> 8), Uint8(colour), 255);
			auto at = [&](uint32_t k) { return SDL_Point{plot.x + 3 + int(step * float(k)), height_of(g.values[s][k])}; };
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
	// The series' names under the graph, each with its colour (the town data's classes: its hidden buttons' texts).
	int x = plot.x;
	const bool classes = entry >= kStatisticsPopulation && entry <= kStatisticsQualityOfLife;
	for (uint32_t s = 0; s < 4; ++s) {
		const std::string raw = classes ? g.buttons[s] : g.names[s] ? g_texts->raw_at(g.names[s]) : std::string();
		if (raw.empty() || raw == " ") {
			continue;
		}
		const uint32_t colour = kStatisticsColours[s];
		const SDL_Rect swatch = {x, plot.y + plot.h + 8 + line / 4, line / 2 + 4, line / 2};
		SDL_SetRenderDrawColor(renderer, Uint8(colour >> 16), Uint8(colour >> 8), Uint8(colour), 255);
		SDL_RenderFillRect(renderer, &swatch);
		const std::string name = stats_text(raw);
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

// A text cut (by whole UTF-8 letters) to fit `width`.
std::string stats_fit(TextRenderer& text, std::string s, int width) {
	while (!s.empty() && text.measure(s) > width) {
		size_t last = s.size() - 1;  // The last letter's first byte.
		while (last > 0 && (uint8_t(s[last]) & 0xC0) == 0x80) {
			--last;
		}
		s.resize(last);
	}
	return s;
}

// The list of a competition's merchant button, over the graph (the game's list 0x46F760 in its mode 2): «choose
// trader», its sort button (what touching it does: by the entry's key, or back to by name), the merchants with
// their home towns, and «no trader» when it may empty the slot.
void draw_stats_choice(SDL_Renderer* renderer, TextRenderer& text, const SaveGame& save,
	const std::vector<std::string>& town_names, const SidePanel& ui, const StatsLayout& layout) {
	SDL_SetRenderDrawColor(renderer, 236, 220, 180, 250);
	SDL_RenderFillRect(renderer, &layout.choice);
	SDL_SetRenderDrawColor(renderer, 110, 80, 40, 255);
	SDL_RenderDrawRect(renderer, &layout.choice);
	const StatisticsChoice choice = stats_choice(save, ui);
	// «choose goods», «choose town», «choose trader» (0x6C10C8 + 4 · mode).
	text.draw(stats_text(g_texts->raw_at(0x6C10C8 + 4 * uint32_t(choice.mode))), layout.choice.x + 6,
		layout.choice.y + 6, kParchmentInk);
	if (!choice.keys.empty()) {
		// The sort button: what touching it does (by the key, or back to by name).
		const uint32_t sort_text = ui.stats_choice_by_key ? 0x6C10EC : 0x6C1190 + 4 * uint32_t(choice.kind);
		draw_stats_toggle(renderer, text, layout.sort_button,
			stats_fit(text, stats_text(g_texts->raw_at(sort_text)), layout.sort_button.w - 8), false, true);
	}
	const std::vector<std::string> names = stats_town_names();
	const std::vector<int> rows = stats_choice_rows(save, ui);
	const int slot = ui.stats_choice_slot;
	const int current = choice.mode == 2 ? ui.stats_merchants[size_t(slot)]
		: slot == 4						 ? (choice.mode == 1 ? ui.stats_towns.town : ui.stats_towns.ware)
		: choice.mode == 0				 ? ui.stats_towns.wares[size_t(slot)]
										 : ui.stats_towns.towns[size_t(slot)];
	const int columns = layout.choice_columns(int(rows.size()));
	for (size_t k = 0; k < rows.size() && int(k) < columns * layout.rows_per_column; ++k) {
		const SDL_Rect r = layout.choice_row(int(k), columns);
		const int m = rows[k];
		const bool lit = int(k) == ui.stats_choice_row || m == current;
		SDL_SetRenderDrawColor(renderer, lit ? 120 : 150, lit ? 80 : 115, lit ? 30 : 70, lit ? 110 : 40);
		SDL_RenderFillRect(renderer, &r);
		std::string line;
		if (m < 0) {
			line = stats_text(g_texts->raw_at(choice.mode == 2 ? 0x6C1088 : 0x6C1078));  // «no trader», «none»
		} else if (choice.mode == 0) {
			line = stats_text(g_texts->raw_at(0x6C0BB0 + 4 * uint32_t(m)));
		} else if (choice.mode == 1) {
			line = stats_text(statistics_town_name(save, names, uint32_t(m)));
		} else {
			line = stats_text(statistics_first_name(save, uint32_t(m)) + " " + statistics_family_name(save, uint32_t(m)));
			const int home = save.merchants[size_t(m)].memory.u8(0x19);
			if (home < int(save.towns.size())) {
				line += " (" + stats_text(statistics_town_name(save, names, uint32_t(home))) + ")";
			}
		}
		text.draw(stats_fit(text, line, r.w - 8), r.x + 4, r.y + 2, kParchmentInk);
	}
	(void)town_names;
}

// The towns' residents (entry 6, 0x4A3BB0 and its draw 0x4A0980): the towns by name in two columns, each with its
// residents, and the total line («Total population in the Hanseatic League:» and «%d %s» with the first entry's
// button).
void draw_stats_residents(SDL_Renderer* renderer, TextRenderer& text, const SaveGame& save, const SDL_Rect& body) {
	const StatisticsResidents r = statistics_residents(save, *g_texts, stats_town_names());
	const std::vector<std::string> names = stats_town_names();
	const int line = text.line_height() + 3;
	const int per_column = std::max(1, (body.h - 2 * line - 6) / line);
	const int column_w = (body.w - 20) / 2;
	for (size_t k = 0; k < r.towns.size() && int(k) < 2 * per_column; ++k) {
		const int x = body.x + int(k) / per_column * (column_w + 20);
		const int y = body.y + int(k) % per_column * line;
		const SDL_Rect band = {x - 2, y, column_w + 4, line - 1};
		SDL_SetRenderDrawColor(renderer, 230, 255, 255, 40);  // The game's 0x64FFE6CA.
		SDL_RenderFillRect(renderer, &band);
		text.draw(stats_text(statistics_town_name(save, names, r.towns[k])), x, y, kParchmentDim);
		const std::string v = stats_text(r.texts[k]);
		text.draw(v, x + column_w - text.measure(v), y, kParchmentInk);
	}
	const int y = body.y + body.h - line;
	SDL_SetRenderDrawColor(renderer, 110, 80, 40, 255);
	SDL_RenderDrawLine(renderer, body.x, y - 4, body.x + body.w, y - 4);
	text.draw(stats_text(g_texts->raw_at(0x6B0D6C)), body.x, y, kParchmentInk);
	const std::string total = stats_text(GameTexts::sprintf_raw(g_texts->raw_at(0x6BACA4),
		{std::to_string(r.total), statistics_button(*g_texts, 0)}));
	text.draw(total, body.x + body.w - text.measure(total), y, kParchmentInk);
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
		// The town's name as the window takes it (statistics_town_name: by the index, the game's bug, or fixed).
		const std::string name = town >= 0 ? stats_text(statistics_town_name(save, stats_town_names(), uint32_t(town)))
										   : std::string();
		title = GameTexts::format(title, {name});
		const std::string choose = stats_text(g_texts->raw_at(0x6B0D68));
		draw_stats_toggle(renderer, text, layout.periods[0], "<", false, town >= 0);
		draw_stats_toggle(renderer, text, {layout.periods[1].x, layout.periods[1].y,
			layout.periods[2].x + layout.periods[2].w - layout.periods[1].x, layout.periods[1].h},
			town >= 0 ? name : choose, true, town >= 0);
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
	} else if (entry == kStatisticsResidents) {
		draw_stats_residents(renderer, text, save, layout.body);
	} else if (stats_no_days(entry)) {
		// The competition's and the towns' graphs: no «4 weeks» (the game hides its button and turns that period into
		// 6 months); in its place, the button [+0x214]: the comparison's ware of the production and the stock, the
		// town data's town.
		static constexpr uint32_t kPeriodText[4] = {0x6C1A38, 0x6C1A3C, 0x6C1A40, 0x6C1A44};
		const int period = statistics_competition_period(ui.stats_graph_period);
		for (int k = 1; k < 4; ++k) {
			draw_stats_toggle(renderer, text, layout.periods[k], stats_text(g_texts->raw_at(kPeriodText[k])),
				k == period, k < 3 || stats_years_shown(save));
		}
		// (The population's change to the choices is kept when the entry is shown: stats_select.)
		StatisticsTownChoices choices = ui.stats_towns;
		const StatisticsGraph g = stats_competition(entry)
			? statistics_competition(save, *g_texts, ui.stats_merchants, entry, period)
			: statistics_towns(save, *g_texts, stats_town_names(), choices, entry, period);
		title = stats_text(statistics_title(*g_texts, entry) + g.title_end);  // (Cleaned whole: its space kept.)
		if (stats_choice_button(entry)) {
			// (The town data's town is named even when the graph is empty.)
			const std::string choice = stats_town_data(entry)
				? statistics_town_name(save, stats_town_names(), ui.stats_towns.town)
				: g.choice;
			draw_stats_toggle(renderer, text, layout.periods[0], stats_fit(text, stats_text(choice),
				layout.periods[0].w - 8), ui.stats_choice_slot == 4 || (ui.stats_choice_slot < 0 && ui.stats_slot == 4), true);
		}
		draw_stats_graph(renderer, text, art, g, entry, layout.body);
		if (ui.stats_choice_slot >= 0) {
			draw_stats_choice(renderer, text, save, town_names, ui, layout);
		}
		// The merchant, town or ware buttons: each slot's colour and its name (empty: none); touching one opens its
		// list. (The town data's first three entries have none: the legend names their classes.)
		for (int k = 0; k < 4 && stats_slot_buttons(entry); ++k) {
			const SDL_Rect& r = layout.merchants[k];
			const bool lit = k == ui.stats_choice_slot || (ui.stats_choice_slot < 0 && k == ui.stats_slot);
			draw_stats_toggle(renderer, text, r, std::string(), lit, true);
			const uint32_t colour = kStatisticsColours[size_t(k)];
			const SDL_Rect swatch = {r.x + 5, r.y + (r.h - 10) / 2, 14, 10};
			SDL_SetRenderDrawColor(renderer, Uint8(colour >> 16), Uint8(colour >> 8), Uint8(colour), 255);
			SDL_RenderFillRect(renderer, &swatch);
			const std::string name = stats_fit(text, stats_text(g.buttons[size_t(k)]), r.w - 28);
			text.draw(name, swatch.x + swatch.w + 5, r.y + (r.h - text.line_height()) / 2, kParchmentInk);
		}
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

// --- The citizens' window (the top bar's man, «Population»: the game's «CitizenStats», 0x40B570 and its draw
// 0x40B860; player_actions.hpp citizen_stats), on a parchment over the town. ---

struct CitizensLayout {
	SDL_Rect panel{};
	SDL_Rect inner{};
	SDL_Rect close_button{};
	explicit CitizensLayout(int line_height) {
		const int w = std::min(520, g_town_right - 40);
		const int h = 9 * (line_height + 6) + 120;
		panel = {(g_town_right - w) / 2, kBarHeight + 30, w, h};
		inner = {panel.x + 44, panel.y + 52, panel.w - 88, panel.h - 52 - 50};
		close_button = {panel.x + panel.w / 2 - 70, panel.y + panel.h - 48, 140, line_height + 10};
	}
};

void draw_citizens_window(SDL_Renderer* renderer, TextRenderer& text, const PanelArt& art, const SaveGame& save,
	uint32_t town) {
	if (!g_texts || town >= save.towns.size()) {
		return;
	}
	const CitizensLayout layout(text.line_height());
	draw_scroll(renderer, art.paper, layout.panel);
	const CitizenStats cs = citizen_stats(save, town);
	const int line = text.line_height() + 8;
	// «Citizens in %s» with the town's name as the window takes it (0x6DDA00 by the index: the letters' bug).
	const std::string title = stats_text(GameTexts::sprintf_raw(g_texts->raw_at(0x699BC0),
		{statistics_town_name(save, stats_town_names(), town)}));
	text.draw(title, layout.inner.x + (layout.inner.w - text.measure(title)) / 2, layout.inner.y, kParchmentInk);
	int y = layout.inner.y + 2 * line;
	const int value_x = layout.inner.x + layout.inner.w * 45 / 100;
	for (size_t k = 0; k < 3; ++k) {
		// «%s :» the class's name; «%u (%s) » its residents and mood; the arrow (up: above twice [+0x308 + 2k]).
		text.draw(stats_text(g_texts->raw_at(0x6C1AF8 + 4 * uint32_t(k))) + " :", layout.inner.x, y, kParchmentInk);
		const std::string value = std::to_string(cs.residents[k]) + " (" +
			stats_text(g_texts->raw_at(0x699C34 + 4 * uint32_t(cs.mood[k]))) + ")";
		text.draw(value, value_x, y, kParchmentInk);
		if (cs.arrow[k] != 2) {
			const bool up = cs.arrow[k] == 1;
			const int ax = value_x + text.measure(value) + 14;
			const int ay = y + text.line_height() / 2;
			SDL_SetRenderDrawColor(renderer, up ? 30 : 170, up ? 120 : 30, up ? 30 : 20, 255);
			for (int i = 0; i < 7; ++i) {
				const int yy = up ? ay - 3 + i : ay + 3 - i;
				SDL_RenderDrawLine(renderer, ax - i, yy, ax + i, yy);
			}
		}
		y += line;
	}
	y += line / 2;
	const std::string beggars = stats_text(GameTexts::sprintf_raw(g_texts->raw_at(0x699BE8),
		{std::to_string(cs.residents[3])}));
	text.draw(beggars, layout.inner.x, y, kParchmentInk);
	if (cs.shortage) {
		y += line;
		text.draw(stats_text(g_texts->raw_at(0x699CAC)), layout.inner.x, y, SDL_Color{170, 20, 10, 255});
	}
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
	if (hit(g_people_button)) {
		return 20024;  // Population
	}
	if (hit(g_house_button)) {
		return 20025;  // Own buildings
	}
	const PanelLayout panel(side.folded);
	if (side.folded) {
		return 0;
	}
	if (hit(panel.map_switch)) {
		return 20023;  // Change map
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
enum class MenuAsk { None, Overwrite, Delete, Quit, MainMenu };

struct MenuUi {
	MenuScreen screen = MenuScreen::Closed;
	int row = 0;
	MenuAsk ask = MenuAsk::None;
	std::array<p3::app::SlotInfo, p3::app::kSaveSlots> slots{};

	bool open() const { return screen != MenuScreen::Closed; }
	bool slot_screen() const { return screen == MenuScreen::Save || screen == MenuScreen::Load; }
};

// The rows of the main screen, in order.
enum MainRow { kRowContinue, kRowMailbox, kRowSave, kRowLoad, kRowOptions, kRowMainMenu, kRowQuit, kMainRows };
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
	case MenuAsk::MainMenu:  // Leaving the game for the main menu (the start screen): the same question.
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
				GameText::MenuLoadGame, GameText::MenuOptions, GameText::MainMenu, GameText::QuitGame};
			static const char* const kFallbacks[kMainRows] = {"Continuar", "Buzón", "Guardar partida", "Cargar partida",
				"Opciones", "Menú principal", "Salir del juego"};
			static const char* const kEnglishFallbacks[kMainRows] = {"Continue", "Mailbox", "Save game", "Load game", "Options",
				"Main menu", "Quit"};
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
	bool start_screen = false;  // The start screen also with --steps (it opens without --save otherwise).
};

void print_usage() {
	std::printf(
		"usage: p3vita [DATA_DIR] [--cpr FILE] [--steps STEPS] [--screenshot FILE.png]\n"
		"              [--debug-state STATES] [--exe Patrician3.exe] [--fix-game-bugs on|off]\n"
		"              [--save missav/N.pat] [--start-screen]\n"
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
		"Without --save (or --debug-state) it starts on the game's main menu: a new\n"
		"game or a saved one. --start-screen shows it also with --steps; otherwise a\n"
		"run with --steps starts on missav/1.pat as before.\n"
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
		if (arg == "--start-screen") {
			options.start_screen = true;
			continue;
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
	// The sea battle the player directs, on the screen (made the first time; it reads the archive and the town
	// view's tiles), the block of the battle it shows (-1: none), and the camera it took over.
	std::unique_ptr<p3::app::BattleView> battle_view;
	int battle_shown = -1;
	SDL_Point battle_saved_camera = {0, 0};
	bool battle_centre = false;  // The camera goes to the player's ships once they are placed.
	// The battle's orders by touch (the user's choices, roadmap phase 4 point 7): the ships chosen (their places;
	// the engine's [+0x66C] follows it), when the finger went down, whether it was held still (a long press: a
	// ship of his added or taken out, or on the water the held order) and whether the held order goes on.
	std::vector<uint16_t> battle_selection;
	Uint32 battle_touch_ms = 0;
	bool battle_long = false;
	bool battle_hold = false;
	std::vector<std::string> town_names = load_town_names(*archive);
	g_town_names = &town_names;
	std::unique_ptr<TextRenderer> text;
	std::unique_ptr<TextRenderer> small_text;
	if (auto font = FontAtlas::load(*archive, kUiFont, kUiFontHeight)) {
		text = std::make_unique<TextRenderer>(renderer, std::move(font));
		text->set_archive(archive.get());
	}
	if (auto font = FontAtlas::load(*archive, kUiFont, kSmallFontHeight)) {
		small_text = std::make_unique<TextRenderer>(renderer, std::move(font));
		small_text->set_archive(archive.get());
	}
	MarketUi market;
	HanseUi hanse;  // The Hanse panel (Square in a town).
	PlaceUi place;  // The town's places (the tavern, the shipyard) and the ship's window.
	SaveGame save;
	// The game's main menu first (the user's choice, 2026-11-11), unless a save
	// or debug states are given or a scripted run doesn't ask for it.
	bool start_open = !options.save && !options.debug_state && (!script || options.start_screen);
	std::unique_ptr<p3::app::StartScreen> start_screen;  // Made the first time it shows.
	int start_lit = int(p3::app::StartEntry::Single);  // The entry lit (the buttons').
	// The «Game settings» window, between «Single player» and the game
	// (new_game_window.hpp): open over the start screen; its choices (the last
	// ones, kept in the settings file as the game in P2.CFG), the map
	// (Standard24), the control the buttons point at, the one under the finger
	// and the name field being written with what it held before.
	std::unique_ptr<p3::app::NewGameWindow> new_game_window;  // Made the first time it opens.
	bool new_game_open = false;
	p3::data::NewGameWindowState new_game_state;
	new_game_state.choices = settings.new_game;
	GameMap new_game_map;
	bool new_game_map_read = false;
	int new_game_focus = int(p3::app::NewGameControl::Start);
	int new_game_pressed = -1;
	int new_game_typing = -1;
	std::string new_game_name_before;
	// Its home town's window (the map), opened by the home town's button: the
	// town the panel is for and the one under the finger; the 40 towns' places
	// on the sea map (StadtDaten.ini), read once.
	std::unique_ptr<p3::app::HomeTownWindow> home_town_window;
	bool home_town_open = false;
	int home_town_cursor = -1;
	int home_town_pressed = -1;
	std::vector<SDL_Point> home_town_places;
	// Its «Details» window: the details being changed (a copy, 0x49B180), the
	// buttons' cursor and the finger's control.
	bool details_open = false;
	std::array<uint8_t, p3::app::kDetailRows> details_copy{};
	int details_focus = p3::app::kDetailOk;
	int details_pressed = -1;
	bool has_save = !start_open &&
		load_savegame(*archive, options.save ? options.save : kStartSave, save) && !save.towns.empty();
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
	// The window of a sea map battle icon (ChoiceBox: its battle's slot, or -1); and the sea map's battle icons
	// (Sailing::take_sea_map_icons, each with its battle's block: it goes when its battle goes, as the game's
	// destructor drops it, 0x457DF0).
	int battle_info = -1;
	// The battle screen's leave icon tapped: its window asks whether the computer is to fight on; then the sea
	// map once the screen has closed (battle_to_sea).
	bool battle_leave = false;
	bool battle_to_sea = false;
	struct SeaBattleIcon {
		uint8_t slot;
		int16_t x, y;
		int block;
	};
	std::vector<SeaBattleIcon> battle_icons;
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
		sailing->set_battle_window(true);  // The viewer is the game's battle window (ChoiceBox).
		sailing->set_real_time_battles(true);  // In time mode 2 the viewer's clock drives the battles (0x612DB0).
		sailing->fn_611e50(uint8_t(save.player));  // After a load (0x43C5C7): the battle the player directs on the screen.
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
	bool sea_switch = true;  // Whether to go to the sea map once loaded (not for the minimap's arrow).
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

	// The battles' window: open in the engine (the question, an end, a boarding), its box, and its buttons.
	auto battle_window_up = [&]() { return sailing && save.battle_window.present && save.battle_window.open; };
	auto steer_label = [&]() { return lender_text(0x6B2BC0, "Dirigir manualmente", "Steer manually"); };
	// Whether the battle in that slot can be steered: the battle's screen draws the sea battles, those at a town
	// and those at a pirate nest.
	auto can_steer = [&](int slot) {
		const int b = slot >= 0 && slot < 0xFF ? save.battles.battle(size_t(slot)) : -1;
		if (!kBattleScreen || b < 0) {
			return false;
		}
		const uint32_t vtable = save.battles.get<uint32_t>(size_t(b), 0);
		return vtable == Sailing::kSeaBattleVtable || vtable == Sailing::kTownBattleVtable;
	};
	auto battle_window_box = [&]() {
		ChoiceBox box;
		const SaveGame::BattleWindow& w = save.battle_window;
		split_title(w.text, box);
		if (w.buttons > 0) {
			box.buttons = {{BTN_CROSS " " + steer_label(), can_steer(w.battle)},
				{BTN_CIRCLE " " + tr("Que luche el ordenador", "Let the computer fight"), true}};
			const uint16_t left = sailing->battle_question_count();
			box.bar = left <= 250 ? int(left) * 1000 / 250 : 0;
		} else {
			box.buttons = {{BTN_CROSS " " + tr("Continuar", "Continue"), true}};
		}
		box.low = battle_shown >= 0;  // An end or a boarding over the battle: below it, the ships still show.
		return box;
	};
	// The window of a sea map battle icon (0x611FC0): its button takes the battle (0x6102A9) once it can be steered.
	auto battle_info_box = [&]() {
		ChoiceBox box;
		split_title(sailing ? sailing->battle_info_text(uint8_t(battle_info)) : std::string(), box);
		if (box.title.empty()) {
			box.title = tr("Batalla naval", "Sea battle");
		}
		box.buttons = {{BTN_CROSS " " + steer_label(), can_steer(battle_info)},
			{BTN_CIRCLE " " + tr("Cerrar", "Close"), true}};
		return box;
	};
	// The leave icon's window: the title of the game's battle window (0x611FC0, «Sea battle:» without its colon),
	// and the question.
	auto battle_leave_box = [&]() {
		ChoiceBox box;
		if (battle_shown >= 0 && sailing) {
			split_title(sailing->battle_info_text(uint8_t(save.battles.get<uint16_t>(size_t(battle_shown), 0x662))), box);
		}
		while (!box.title.empty() && (box.title.back() == ':' || box.title.back() == ' ')) {
			box.title.pop_back();
		}
		if (box.title.empty()) {
			box.title = tr("Batalla naval", "Sea battle");
		}
		// A battle at a town has no sea map icon in the game (its setup doesn't tell the map, 0x605906): once
		// given back it can't be taken again (the user's choice: as the PC), and the window says so.
		if (battle_shown >= 0 && p3::app::BattleView::at_town(save, size_t(battle_shown))) {
			box.body = tr("¿Dejas que el ordenador siga con la batalla? Ya no podrás volver a dirigirla.",
				"Leave the battle to the computer? You won't be able to steer it again.");
		} else {
			box.body = tr("¿Dejas que el ordenador siga con la batalla?", "Leave the battle to the computer?");
		}
		box.buttons = {{BTN_CROSS " " + tr("Que luche el ordenador", "Let the computer fight"), true},
			{BTN_CIRCLE " " + tr("Seguir dirigiéndola", "Keep steering it"), true}};
		box.low = true;
		return box;
	};
	auto battle_popup = [&]() { return battle_window_up() || battle_info >= 0 || battle_leave; };
	auto popup_box = [&]() {
		return battle_window_up() ? battle_window_box() : battle_info >= 0 ? battle_info_box() : battle_leave_box();
	};
	auto battle_press = [&](int k) {
		if (battle_window_up()) {
			if (save.battle_window.buttons > 0 && k == 0 && !can_steer(save.battle_window.battle)) {
				return;
			}
			// The question closed with its button (10, [0x66BDC0]) or otherwise; an end or a boarding closed.
			sailing->close_battle_window(save.battle_window.buttons > 0 && k == 0 ? 10 : 0);
		} else if (battle_info >= 0) {
			if (k == 0) {
				const int b = save.battles.battle(uint8_t(battle_info));
				if (!can_steer(battle_info) || b < 0) {
					return;
				}
				sailing->fn_6102a9(size_t(b));
			}
			battle_info = -1;
		} else if (battle_leave) {
			if (k == 0 && battle_shown >= 0) {
				sailing->fn_6100d9(size_t(battle_shown));  // 0x611FA0: the battle on the screen given back.
				battle_to_sea = true;
			}
			battle_leave = false;
		}
	};
	// The pad on the window: X its first button (if it can be pressed), Circle its last (the computer,
	// «Cerrar»; a window of one button closes with either); the other buttons do nothing while it is open but
	// Start (the menu). True when it took the button.
	auto battle_button = [&](Uint8 button) {
		if (!battle_popup()) {
			return false;
		}
		const ChoiceBox box = popup_box();
		const int n = int(box.buttons.size());
		if (button == SDL_CONTROLLER_BUTTON_A) {
			if (box.buttons[0].second) {
				battle_press(0);
			}
		} else if (button == SDL_CONTROLLER_BUTTON_B) {
			battle_press(n - 1);
		}
		return button != SDL_CONTROLLER_BUTTON_START && button != SDL_CONTROLLER_BUTTON_INVALID;
	};

	SDL_GameController* controller = nullptr;
	bool reload = true;
	bool keep_camera = false;
	LiveTown live;  // The saved town on screen (Mode::Saved).
	BattleTown battle_town;  // The town's battle map under the battle on the screen at a town.
	const bool debug_weak_towers =
		options.debug_state && std::string(options.debug_state).find("weak-towers") != std::string::npos;
	bool debug_nest_attack =
		options.debug_state && std::string(options.debug_state).find("nest-attack") != std::string::npos;
	int battle_town_index = -1;  // That town, while its battle map is shown; −1.
	// The town under that battle: where a ball splashes (0x60329B: a cell of the water, .let type 0xE, or a sand
	// bank, 0x13; a puff on anything else); its drawing's parts are added where it is drawn.
	p3::app::BattleTownLayer battle_town_layer;
	battle_town_layer.splash = [&](int x, int y) {
		const uint32_t type = view->cell_type_at(x, y);
		return type == 0 || type == kWaterTileType || type == 0x13;
	};
	battle_town_layer.water = [&](int x, int y) { return view->water_at(x, y); };
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
	// What the viewer keeps of the game in play, forgotten for another one
	// (loaded or new); the town on screen is the player's home town.
	auto forget_game = [&]() {
		battle_icons.clear();
		battle_info = -1;
		if (battle_view) {
			battle_view->clear();  // Another game's battle; the next frame takes the new one's.
		}
		battle_shown = -1;
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
		sea_switch = true;
		sea_notice_shown = false;
		start_open = false;  // From the start screen, the game begins.
		new_game_open = false;
	};
	// Replaces the game in play with the slot's: a new Sailing for it, and
	// what the viewer keeps of the old one forgotten (forget_game). On a
	// failure the game in play stays.
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
			sailing->set_battle_window(true);
			sailing->set_real_time_battles(true);
			sailing->fn_611e50(uint8_t(save.player));
			use_sailing(*sailing);
		}
		forget_game();
		return true;
	};
	// A new game (the user's choice, 2026-11-11: from the game's main menu, the
	// start screen) as the game's window starts it (p3data/new_game.hpp): the
	// game's data in a new save, its settings, the window's map, the player's
	// names, and srand with the time. On a failure the game in play stays.
	auto new_game = [&](const SettingsMemory& game_settings, const NewGamePlayer& player, const GameMap& map) {
		show_loading_text(renderer, text.get(), game_text(GameText::LoadingGame, tr("Iniciando partida...", "Loading game...")));
		if (!nav) {
			nav = NavData::load(*archive);
		}
		if (!nav) {
			notify(tr("No se puede empezar la partida.", "The game can't be started."));
			return false;
		}
		sailing.reset();  // It refers to the game it was made for.
		save = SaveGame();
		save.settings = game_settings;
		// What the game's start leaves (the world's constructor 0x52F520: [+0x88]
		// 0xFF) and its data (0x5121C0...).
		save.globals.u8(p3::data::global::kWorld + 0x88) = 0xFF;
		load_game_scripts(*archive, save);
		load_statistics_file(nullptr, save);
		sailing = std::make_unique<Sailing>(save, *nav, load_sea_map_towns(*archive));
		sailing->set_texts(game_texts.get());
		sailing->set_battle_window(true);
		sailing->set_real_time_battles(true);
		start_new_game(save, *sailing, map, player, uint32_t(std::time(nullptr)));
		use_sailing(*sailing);
		has_save = true;
		forget_game();
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
		battle_view.reset();  // It reads from the old archive and the old view's tiles.
		battle_shown = -1;
		view.reset();  // It reads from the old archive.
		sea.reset();
		start_screen.reset();  // Its book is the language's.
		new_game_window.reset();  // It reads from the old archive.
		home_town_window.reset();
		new_game_map_read = false;
		spanish_mounted = mounted;
		archive = std::move(next);
		view = std::make_unique<TownView>(renderer, *archive);
		town_names = load_town_names(*archive);
		regions = load_town_regions(*archive);
		names = load_name_lists(*archive);
		if (auto font = FontAtlas::load(*archive, kUiFont, kUiFontHeight)) {
			text = std::make_unique<TextRenderer>(renderer, std::move(font));
			text->set_archive(archive.get());
		}
		if (auto font = FontAtlas::load(*archive, kUiFont, kSmallFontHeight)) {
			small_text = std::make_unique<TextRenderer>(renderer, std::move(font));
			small_text->set_archive(archive.get());
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
		if (ask == MenuAsk::MainMenu) {
			menu.screen = MenuScreen::Closed;
			start_open = true;  // The game stays behind until another one is loaded or started.
			start_lit = int(p3::app::StartEntry::Single);
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
			} else if (menu.row == kRowMainMenu) {
				menu.ask = MenuAsk::MainMenu;
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
	SDL_Point touch_now = {0, 0};  // Where the finger is now.
	int touch_moved = 0;
	float touch_scroll = 0;  // A drag on a list of the side panel, not yet a whole row.
	// A finger held on a − or + of the wares window repeats it (as the game's held buttons): when it went down and how
	// many times it has repeated.
	Uint32 touch_down_ms = 0;
	int stop_repeats = 0;
	bool finger_down = false;  // A finger on the screen now (the tooltip of the button under it).
	bool touch_on_panel = false;
	bool touch_on_side = false;  // It started on the side panel.
	bool touch_building = false;  // It places the building chosen in the build menu.
	// A tap on the open menu.
	auto menu_tap = [&](int x, int y) {
		const MenuLayout layout(small_text->line_height(), menu.screen);
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
	};
	// --- The «Game settings» window (new_game_window.hpp), over the start screen. ---
	using NewGameControl = p3::app::NewGameControl;
	using HomeTownWindow = p3::app::HomeTownWindow;
	p3::app::NewGameTexts new_game_texts;  // In the language, made when it opens.
	auto make_new_game_texts = [&]() {
		p3::app::NewGameTexts t;
		t.title = game_text(GameText::NewGameTitle, tr("Configuración del juego", "Game settings"));
		t.first_name = game_text(GameText::NewGameFirstName, tr("Nombre del mercader", "First name of trader"));
		t.surname = game_text(GameText::NewGameSurname, tr("Apellido", "Surname of trader"));
		t.gender = game_text(GameText::NewGameGender, tr("Género", "Gender"));
		t.world_map = game_text(GameText::NewGameWorldMap, tr("Mapa", "World map"));
		t.home_town = game_text(GameText::NewGameHomeTown, tr("Ciudad", "Home town"));
		t.year = game_text(GameText::NewGameStartingYear, tr("Año de inicio", "Starting year"));
		t.level = game_text(GameText::NewGameLevel, tr("Nivel de dificultad", "Level of difficulty"));
		t.objective = game_text(GameText::NewGameObjective, tr("Condiciones del objetivo", "Objective conditions"));
		t.speed = game_text(GameText::GameSpeed, tr("Velocidad del juego", "Game speed"));
		t.male = game_text(GameText::NewGameMale, tr("Masculino", "male"));
		t.female = game_text(GameText::NewGameFemale, tr("Femenino", "female"));
		t.standard_map = game_text(GameText::NewGameStandardMap, tr("Mapa original", "Standard Map"));
		t.details = game_text(GameText::NewGameDetails, tr("Detalles", "Details"));
		static constexpr const char* kLevels[5][2] = {{"Mercader", "Shopkeeper"}, {"Maestro mercader", "Trader"},
			{"Patrono", "Merchant"}, {"Consejero", "Councillor"}, {"Patricio", "Patrician"}};
		for (size_t k = 0; k < t.levels.size(); ++k) {
			t.levels[k] = game_text(GameText(int(GameText::LevelShopkeeper) + int(k)), tr(kLevels[k][0], kLevels[k][1]));
		}
		static constexpr const char* kGoals[8][2] = {{"Ninguno", "none"}, {"Consejero", "Councillor"},
			{"Alcalde", "Lord Mayor"}, {"Gobernador", "Alderman"}, {"Máxima puntuación", "Balance of points"},
			{"Riqueza", "Balance of wealth"}, {"Valor total", "Wealth"}, {"Capacidad", "Capacity"}};
		for (size_t k = 0; k < t.goals.size(); ++k) {
			t.goals[k] = game_text(GameText(int(GameText::GoalNone) + int(k)), tr(kGoals[k][0], kGoals[k][1]));
		}
		t.hanse_population = game_text(GameText::GoalHansePopulation, tr("Población total", "Hanse population"));
		for (size_t k = 0; k < t.speeds.size(); ++k) {
			// The window's speeds (0 very slow .. 4 very fast, 5 slowdown) in speed_name's order.
			t.speeds[k] = speed_name(k == 5 ? 0 : int(k) + 1);
		}
		t.back = game_text(GameText::ButtonBack, tr("Atrás", "Back"));
		t.start = game_text(GameText::NewGameStart, tr("Jugar", "Start"));
		static constexpr const char* kYears[15][2] = {{"1 año", "1 year"}, {"2 años", "2 years"}, {"3 años", "3 years"},
			{"4 años", "4 years"}, {"5 años", "5 years"}, {"6 años", "6 years"}, {"7 años", "7 years"}, {"8 años", "8 years"},
			{"9 años", "9 years"}, {"10 años", "10 years"}, {"15 años", "15 years"}, {"20 años", "20 years"},
			{"30 años", "30 years"}, {"50 años", "50 years"}, {"sin límite de años", "endless"}};
		for (size_t k = 0; k < t.years.size(); ++k) {
			const GameText id = k < 10 ? GameText(int(GameText::YearsOne) + int(k))
									   : GameText(int(GameText::YearsFifteen) + int(k) - 10);
			t.years[k] = game_text(id, tr(kYears[k][0], kYears[k][1]));
		}
		static constexpr const char* kPopulation[3][2] = {{"70.000 habitantes", "70,000 inhabitants"},
			{"100.000 habitantes", "100,000 inhabitants"}, {"200.000 habitantes", "200,000 inhabitants"}};
		for (size_t k = 0; k < t.population.size(); ++k) {
			t.population[k] =
				game_text(GameText(int(GameText::PopulationGoal) + int(k)), tr(kPopulation[k][0], kPopulation[k][1]));
		}
		static constexpr const char* kDetailLabels[8][2] = {{"Dificultad para comerciar", "Difficulty level of trade"},
			{"Dificultad para subir reputación", "Difficulty level of reputation"},
			{"Demanda de población", "Needs of the citizens"}, {"Dificultad en batallas navales", "Difficulty level of sea battle"},
			{"Actividad de los piratas", "Pirates activity"}, {"Actividad del príncipe", "Activity of the Prince"},
			{"Riqueza inicial", "Starting capital"}, {"Número de barcos", "Number of ships"}};
		for (size_t k = 0; k < t.detail_labels.size(); ++k) {
			t.detail_labels[k] =
				game_text(GameText(int(GameText::DetailTrade) + int(k)), tr(kDetailLabels[k][0], kDetailLabels[k][1]));
		}
		// The 7 tables of 3 (trade, reputation, needs, sea battle, pirates, prince, capital).
		static constexpr const char* kEasy[3][2] = {{"Fácil", "easy"}, {"Normal", "normal"}, {"Difícil", "hard"}};
		static constexpr const char* kLow[3][2] = {{"Baja", "low"}, {"Normal", "normal"}, {"Alta", "high"}};
		static constexpr const char* kAverage[3][2] = {{"Baja", "low"}, {"Normal", "average"}, {"Alta", "high"}};
		static constexpr const char* const (*kTables[7])[2] = {kEasy, kEasy, kLow, kEasy, kLow, kLow, kAverage};
		for (size_t k = 0; k < t.detail_values.size(); ++k) {
			const char* const* pair = kTables[k / 3][k % 3];
			t.detail_values[k] = game_text(GameText(int(GameText::DetailValueTrade) + int(k)), tr(pair[0], pair[1]));
		}
		t.cancel = game_text(GameText::ButtonCancel, tr("Cancelar", "Cancel"));
		t.ok = game_text(GameText::ButtonOk, tr("OK", "OK"));
		t.choose_map = game_text(GameText::MapChoose, tr("Elige un mapa", "Choose Map"));
		t.map_information = game_text(GameText::MapInformation, tr("Información del mapa", "Map Information"));
		t.standard_map_text = game_text(GameText::MapStandardText,
			tr("Este es el mapa original proporcionado con el juego.", "This is the standard map provided with the game."));
		t.file = game_text(GameText::MapFile, tr("Archivo", "File"));
		t.towns = game_text(GameText::MapTowns, tr("Número de ciudades", "Number of towns"));
		t.commentary = game_text(GameText::MapCommentary, tr("Comentario", "Commentary"));
		t.load = game_text(GameText::ButtonLoad, tr("Cargar", "Load"));
		return t;
	};
	// What the window shows: every control can be used («Start» only with both
	// names).
	auto new_game_view = [&]() {
		p3::app::NewGameWindow::View v;
		const NewGameChoices& c = new_game_state.choices;
		v.choices = &c;
		v.texts = &new_game_texts;
		v.home_town = latin1_to_utf8(game_map_town_name(new_game_map, c.home));
		for (int k = 0; k < p3::app::kNewGameControls; ++k) {
			const NewGameControl control = NewGameControl(k);
			const NewGameBall ball = p3::app::new_game_ball(control);
			v.shown[size_t(k)] = ball == NewGameBall::Count || new_game_ball_shown(c, ball);
			bool on = false;
			switch (control) {
			case NewGameControl::FirstName:
			case NewGameControl::FamilyName:
			case NewGameControl::Map:
			case NewGameControl::HomeTown:
			case NewGameControl::Details:
			case NewGameControl::Back:
				on = true;
				break;
			case NewGameControl::Start:
				on = !c.first_name.empty() && !c.family_name.empty();  // 0x49A906..
				break;
			default:
				on = ball != NewGameBall::Count && new_game_ball_enabled(c, ball);
				break;
			}
			v.enabled[size_t(k)] = on;
		}
		v.focus = new_game_focus;
		v.pressed = new_game_pressed;
		v.typing = new_game_typing;
		return v;
	};
	// The names, written with the keyboard (the Vita's, SDL_StartTextInput):
	// it starts empty; an empty name isn't taken (the one before stays).
	auto new_game_name = [&](int control) -> std::string& {
		return control == int(NewGameControl::FirstName) ? new_game_state.choices.first_name
														 : new_game_state.choices.family_name;
	};
	auto new_game_end_typing = [&]() {
		if (new_game_typing < 0) {
			return;
		}
		std::string& name = new_game_name(new_game_typing);
		if (name.empty()) {
			name = new_game_name_before;
		}
		new_game_typing = -1;
		SDL_StopTextInput();
	};
	auto new_game_start_typing = [&](int control) {
		new_game_end_typing();
		new_game_typing = control;
		new_game_name_before = new_game_name(control);
		new_game_name(control).clear();
		SDL_StartTextInput();
	};
	// The choices kept for the next time (as the game in P2.CFG, 0x499BE0, on
	// «Back» and «Start»).
	auto keep_new_game_choices = [&]() {
		settings.new_game = new_game_state.choices;
		if (!script && !p3::app::save_settings(settings_file, settings)) {
			std::fprintf(stderr, "can't save %s\n", settings_file.c_str());
		}
	};
	// The window's map (0x549940 with [+0x470]): the standard one is the
	// archive's maps/Standard24.gmf; another, its .gmf in the Maps folder of the
	// viewer's (as the game's in its own folder), or Standard24 if it can't be
	// read (0x499192). Then the home town a Hanse town of it (0x4991AC).
	auto read_new_game_map = [&]() {
		NewGameChoices& c = new_game_state.choices;
		std::string error;
		if (!c.map.empty() &&
			!load_game_map_file(data_dir + "Maps/" + latin1_to_utf8(c.map) + ".gmf", new_game_map, &error)) {
#ifndef __vita__
			std::fprintf(stderr, "%s\n", error.c_str());
#endif
			c.map.clear();
		}
		if (c.map.empty() && !load_game_map(*archive, "maps/Standard24.gmf", new_game_map, &error)) {
#ifndef __vita__
			std::fprintf(stderr, "%s\n", error.c_str());
#endif
			return false;
		}
		if (c.home >= new_game_map.count) {
			c.home = 0;
		}
		c.home = previous_hanse_town(new_game_map, next_hanse_town(new_game_map, c.home));
		return true;
	};
	auto open_new_game = [&]() {
		new_game_state.choices.map.clear();  // The standard map each time it opens (0x49802E).
		if (!read_new_game_map()) {
			notify(tr("No se puede empezar la partida.", "The game can't be started."));
			return;
		}
		new_game_map_read = true;
		if (!new_game_window) {
			new_game_window = std::make_unique<p3::app::NewGameWindow>(renderer, *archive, kScreenWidth, kScreenHeight);
		}
		if (!new_game_window->ok()) {
			notify(tr("No se puede empezar la partida.", "The game can't be started."));
			return;
		}
		new_game_texts = make_new_game_texts();
		new_game_open = true;
		new_game_focus = int(NewGameControl::Start);
		new_game_pressed = -1;
	};
	// The map choice's window: the standard map and the .gmf of the Maps folder
	// (0x435DA0: their titles, in the folder's order; here sorted), the one
	// chosen with its information (0x4368B0 reads its towns and commentary),
	// the first row shown, the finger's control and the buttons' cursor.
	bool map_choice_open = false;
	std::vector<std::string> map_names;  // UTF-8, the first the standard map's.
	int map_selected = -1;
	int map_first = 0;
	int map_pressed = -1;
	int map_focus = -1;
	GameMap map_info;
	auto open_map_choice = [&]() {
		map_names.assign(1, new_game_texts.standard_map);
		std::vector<std::string> titles;
		const std::string folder = data_dir + "Maps";
		if (DIR* dir = opendir(folder.c_str())) {
			while (const dirent* entry = readdir(dir)) {
				const std::string name = entry->d_name;
				if (name.size() > 4 && name[0] != '.') {
					std::string ext = name.substr(name.size() - 4);
					std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) { return char(std::tolower(ch)); });
					if (ext == ".gmf") {
						titles.push_back(name.substr(0, name.size() - 4));
					}
				}
			}
			closedir(dir);
		}
		std::sort(titles.begin(), titles.end());
		map_names.insert(map_names.end(), titles.begin(), titles.end());
		map_selected = -1;  // 0x435DA0
		map_first = 0;
		map_pressed = -1;
		map_focus = -1;
		map_choice_open = true;
	};
	auto map_view = [&]() {
		p3::app::NewGameWindow::MapView v;
		v.names = &map_names;
		v.texts = &new_game_texts;
		v.first = map_first;
		v.selected = map_selected;
		v.pressed = map_pressed;
		v.focus = map_focus;
		if (map_selected == 0) {
			v.file = new_game_texts.standard_map;
			v.towns = 24;  // 0x4365F6
			v.commentary = new_game_texts.standard_map_text;
		} else if (map_selected > 0) {
			v.file = map_names[size_t(map_selected)];
			v.towns = map_info.count;
			v.commentary = latin1_to_utf8(map_info.commentary);
		}
		return v;
	};
	auto map_select = [&](int row) {
		if (row < 0 || row >= int(map_names.size())) {
			return;
		}
		if (row != map_selected && row > 0) {
			map_info = GameMap();
			load_game_map_file(data_dir + "Maps/" + map_names[size_t(row)] + ".gmf", map_info);
		}
		map_selected = row;
		map_first = std::clamp(map_first, std::max(0, row - p3::app::NewGameWindow::kMapRows + 1), row);
	};
	// «Load» (0x435729): the map's name into the window (empty for the standard
	// one) and the map's productions (the window's production, 1: its buttons
	// are hidden); the window reads the map (0x499153).
	auto map_load = [&]() {
		if (map_selected < 0) {
			return;
		}
		NewGameChoices& c = new_game_state.choices;
		c.map = map_selected == 0 ? std::string() : utf8_to_latin1(map_names[size_t(map_selected)]);
		c.production = 1;
		map_choice_open = false;
		if (!read_new_game_map()) {
			c.map.clear();
			read_new_game_map();
		}
	};
	auto map_choice_frame = [&]() {
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			if ((event.type == SDL_FINGERDOWN || event.type == SDL_FINGERMOTION || event.type == SDL_FINGERUP) &&
				rear_touch(event.tfinger.touchId)) {
				continue;
			}
			const int x = int(event.tfinger.x * kScreenWidth);
			const int y = int(event.tfinger.y * kScreenHeight);
			switch (event.type) {
			case SDL_QUIT:
				running = false;
				break;
			case SDL_CONTROLLERBUTTONDOWN: {
				const Uint8 button = event.cbutton.button;
				if (button == SDL_CONTROLLER_BUTTON_DPAD_UP || button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
					const int step = button == SDL_CONTROLLER_BUTTON_DPAD_UP ? -1 : 1;
					map_select(std::clamp(map_selected < 0 ? 0 : map_selected + step, 0, int(map_names.size()) - 1));
					map_focus = -1;
				} else if (button == SDL_CONTROLLER_BUTTON_A) {
					map_load();
				} else if (button == SDL_CONTROLLER_BUTTON_B) {
					map_choice_open = false;
				}
				break;
			}
			case SDL_FINGERDOWN:
			case SDL_FINGERMOTION:
				map_pressed = new_game_window->map_control_at(x, y, map_view());
				break;
			case SDL_FINGERUP: {
				const int k = new_game_window->map_control_at(x, y, map_view());
				const int pressed = map_pressed;
				map_pressed = -1;
				if (k < 0 || k != pressed) {
					break;
				}
				if (k == p3::app::NewGameWindow::kMapBack) {
					map_choice_open = false;
				} else if (k == p3::app::NewGameWindow::kMapLoad) {
					map_load();
				} else if (k == map_selected) {
					map_load();  // A second tap on the chosen map loads it.
				} else {
					map_select(k);
				}
				break;
			}
			default:
				break;
			}
		}
		if (!map_choice_open || !running) {
			return;
		}
		new_game_window->draw_map(renderer, map_view());
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
	};
	// The home town's window opens on the home town.
	auto open_home_town = [&]() {
		if (!home_town_window) {
			home_town_window = std::make_unique<p3::app::HomeTownWindow>(renderer, *archive, kScreenWidth, kScreenHeight);
		}
		if (!home_town_window->ok()) {
			return;
		}
		if (home_town_places.empty()) {
			for (const SeaMapTown& town : load_sea_map_towns(*archive)) {
				home_town_places.push_back({town.symbol_x, town.symbol_y});
			}
		}
		home_town_cursor = int(new_game_state.choices.home);
		home_town_pressed = -1;
		home_town_open = true;
	};
	// A control used (tapped, or X on it): a name field opens the keyboard, a
	// ball does what the game's does, the home town's button opens its map, «Back» goes back to the book and «Start»
	// starts the game with the window's settings over what the game held (the
	// game in play's, or the start's).
	auto new_game_press = [&](int control) {
		const p3::app::NewGameWindow::View v = new_game_view();
		if (control < 0 || control >= p3::app::kNewGameControls || !v.shown[size_t(control)] ||
			!v.enabled[size_t(control)]) {
			return;
		}
		const NewGameControl k = NewGameControl(control);
		if (k == NewGameControl::FirstName || k == NewGameControl::FamilyName) {
			new_game_start_typing(control);
			return;
		}
		new_game_end_typing();
		if (const NewGameBall ball = p3::app::new_game_ball(k); ball != NewGameBall::Count) {
			press_new_game_ball(new_game_state, ball);
		} else if (k == NewGameControl::HomeTown) {
			open_home_town();
		} else if (k == NewGameControl::Map) {
			open_map_choice();
		} else if (k == NewGameControl::Details) {
			details_copy = new_game_state.choices.details;
			details_focus = p3::app::kDetailOk;
			details_pressed = -1;
			details_open = true;
		} else if (k == NewGameControl::Back) {
			keep_new_game_choices();
			new_game_open = false;
		} else if (k == NewGameControl::Start) {
			keep_new_game_choices();
			const NewGameChoices& c = new_game_state.choices;
			const SettingsMemory before = has_save ? save.settings : initial_game_settings();
			if (new_game(new_game_settings(c, new_game_map, before), new_game_player(c), new_game_map)) {
				new_game_open = false;
				start_open = false;
			}
		}
	};
	// The rows the buttons go through (Up and Down), by their first control;
	// Left and Right press a row's balls or go between «Back» and «Start».
	static constexpr NewGameControl kNewGameRows[] = {NewGameControl::FirstName, NewGameControl::FamilyName,
		NewGameControl::GenderLeft, NewGameControl::Map, NewGameControl::HomeTown, NewGameControl::YearMinus,
		NewGameControl::LevelMinus, NewGameControl::Details, NewGameControl::GoalLeft, NewGameControl::TimeMinus,
		NewGameControl::SpeedMinus, NewGameControl::Back};
	constexpr int kNewGameRowCount = int(sizeof(kNewGameRows) / sizeof(kNewGameRows[0]));
	auto new_game_row = [&](int control) {
		int row = 0;
		for (int r = 0; r < kNewGameRowCount; ++r) {
			if (control >= int(kNewGameRows[r])) {
				row = r;
			}
		}
		return row;
	};
	auto new_game_button = [&](Uint8 button) {
		if (new_game_typing >= 0) {
			if (button == SDL_CONTROLLER_BUTTON_A || button == SDL_CONTROLLER_BUTTON_B) {
				new_game_end_typing();
			}
			return;
		}
		const p3::app::NewGameWindow::View v = new_game_view();
		const int row = new_game_row(new_game_focus);
		const int first = int(kNewGameRows[row]);
		const bool balls = p3::app::new_game_ball(NewGameControl(first)) != NewGameBall::Count;
		switch (button) {
		case SDL_CONTROLLER_BUTTON_DPAD_UP:
		case SDL_CONTROLLER_BUTTON_DPAD_DOWN: {
			// The next row with something that can be used.
			const int step = button == SDL_CONTROLLER_BUTTON_DPAD_UP ? -1 : 1;
			for (int r = row + step; r >= 0 && r < kNewGameRowCount; r += step) {
				const int c = int(kNewGameRows[r]);
				const int last = r + 1 < kNewGameRowCount ? int(kNewGameRows[r + 1]) : p3::app::kNewGameControls;
				bool usable = false;
				for (int k = c; k < last; ++k) {
					usable = usable || (v.shown[size_t(k)] && v.enabled[size_t(k)]);
				}
				if (usable) {
					new_game_focus = c == int(NewGameControl::Back) ? int(NewGameControl::Start) : c;
					break;
				}
			}
			break;
		}
		case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
		case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: {
			const bool right = button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
			if (balls) {
				new_game_press(first + (right ? 1 : 0));
			} else if (first == int(NewGameControl::Back)) {
				new_game_focus = int(right ? NewGameControl::Start : NewGameControl::Back);
			}
			break;
		}
		case SDL_CONTROLLER_BUTTON_A:
			if (!balls) {
				new_game_press(new_game_focus);
			}
			break;
		case SDL_CONTROLLER_BUTTON_B:
			new_game_press(int(NewGameControl::Back));
			break;
		case SDL_CONTROLLER_BUTTON_START:
			new_game_press(int(NewGameControl::Start));
			break;
		default:
			break;
		}
	};
	auto new_game_frame = [&]() {
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			if ((event.type == SDL_FINGERDOWN || event.type == SDL_FINGERMOTION || event.type == SDL_FINGERUP) &&
				rear_touch(event.tfinger.touchId)) {
				continue;
			}
			const int x = int(event.tfinger.x * kScreenWidth);
			const int y = int(event.tfinger.y * kScreenHeight);
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
				new_game_button(event.cbutton.button);
				break;
			case SDL_FINGERDOWN:
			case SDL_FINGERMOTION:
				new_game_pressed = new_game_window->control_at(x, y, new_game_view());
				break;
			case SDL_FINGERUP: {
				const int k = new_game_window->control_at(x, y, new_game_view());
				const int pressed = new_game_pressed;
				new_game_pressed = -1;
				if (k >= 0 && k == pressed) {
					new_game_focus = p3::app::new_game_ball(NewGameControl(k)) != NewGameBall::Count
						? int(kNewGameRows[new_game_row(k)])
						: k;
					new_game_press(k);
				} else if (k < 0) {
					new_game_end_typing();
				}
				break;
			}
			case SDL_TEXTINPUT:
				// A name being written, in Latin-1 as the game's names; 23 characters
				// at most (what the game keeps, 0x536D90).
				if (new_game_typing >= 0) {
					std::string& name = new_game_name(new_game_typing);
					name += utf8_to_latin1(event.text.text);
					if (name.size() > 23) {
						name.resize(23);
					}
				}
				break;
			case SDL_KEYDOWN:
				if (new_game_typing >= 0) {
					const SDL_Keycode key = event.key.keysym.sym;
					std::string& name = new_game_name(new_game_typing);
					if (key == SDLK_BACKSPACE && !name.empty()) {
						name.pop_back();
					} else if (key == SDLK_RETURN || key == SDLK_ESCAPE) {
						new_game_end_typing();
					}
				}
				break;
			default:
				break;
			}
		}
		// The Vita's keyboard closed: its text came in its events.
		if (new_game_typing >= 0 && SDL_HasScreenKeyboardSupport() && !SDL_IsScreenKeyboardShown(window)) {
			new_game_end_typing();
		}
		if (!new_game_open || !start_open || !running) {
			return;
		}
		new_game_window->draw(renderer, new_game_view());
		if (text && !notices.empty()) {
			if (app_ticks() > notices.front().second) {
				notices.pop_front();
			} else {
				const int w = text->measure(notices.front().first);
				text->draw(notices.front().first, (kScreenWidth - w) / 2, kScreenHeight - 2 * text->line_height(),
					SDL_Color{255, 255, 255, 255});
			}
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
	};
	// The home town's window: the town under the cursor shows its panel; a tap
	// on a town puts the cursor on it, a second one on a Hanse town (or X)
	// chooses it (the game: the hover and the click, 0x49BFE0); Left and Right
	// go through the Hanse towns; Circle goes back without choosing (the game:
	// Esc or the right button).
	HomeTownWindow::Texts home_town_texts;
	auto home_town_view = [&]() {
		HomeTownWindow::View v;
		v.map = &new_game_map;
		v.places = &home_town_places;
		v.texts = &home_town_texts;
		v.cursor = home_town_cursor;
		v.pressed = home_town_pressed;
		return v;
	};
	auto home_town_choose = [&](int town) {
		if (town >= 0 && uint32_t(town) < new_game_map.count && (new_game_map.flags[size_t(town)] & 0x40)) {
			new_game_state.choices.home = uint32_t(town);
			home_town_open = false;
		}
	};
	auto home_town_frame = [&]() {
		home_town_texts.hanse = game_text(GameText::HomeTownHanse, tr("Ciudad de", "Hanseatic Town"));
		home_town_texts.office = game_text(GameText::HomeTownOffice, tr("Oficina comercial de", "Hanseatic trading office"));
		home_town_texts.station = game_text(GameText::HomeTownStation, tr("Estación comercial de", "Hanseatic trading station"));
		home_town_texts.effective = game_text(GameText::HomeTownEffective, tr("Producción alta", "effective"));
		home_town_texts.normal = game_text(GameText::HomeTownNormal, tr("media", "normal"));
		home_town_texts.hint = tr("Elige tu residencia tocando dos veces una de las ciudades hanseáticas.",
			"Choose your home town by tapping one of the Hanseatic towns twice.");
		for (size_t k = 0; k < home_town_texts.wares.size(); ++k) {
			home_town_texts.wares[k] = ware_name(int(k));
		}
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			if ((event.type == SDL_FINGERDOWN || event.type == SDL_FINGERMOTION || event.type == SDL_FINGERUP) &&
				rear_touch(event.tfinger.touchId)) {
				continue;
			}
			const int x = int(event.tfinger.x * kScreenWidth);
			const int y = int(event.tfinger.y * kScreenHeight);
			switch (event.type) {
			case SDL_QUIT:
				running = false;
				break;
			case SDL_CONTROLLERBUTTONDOWN: {
				const Uint8 button = event.cbutton.button;
				const uint32_t from = home_town_cursor >= 0 ? uint32_t(home_town_cursor) : new_game_state.choices.home;
				if (button == SDL_CONTROLLER_BUTTON_DPAD_LEFT || button == SDL_CONTROLLER_BUTTON_DPAD_UP) {
					home_town_cursor = int(previous_hanse_town(new_game_map, from));
				} else if (button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT || button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
					home_town_cursor = int(next_hanse_town(new_game_map, from));
				} else if (button == SDL_CONTROLLER_BUTTON_A) {
					home_town_choose(home_town_cursor);
				} else if (button == SDL_CONTROLLER_BUTTON_B) {
					home_town_open = false;
				}
				break;
			}
			case SDL_FINGERDOWN:
			case SDL_FINGERMOTION:
				home_town_pressed = home_town_window->town_at(x, y, home_town_view());
				break;
			case SDL_FINGERUP: {
				const int town = home_town_window->town_at(x, y, home_town_view());
				const int pressed = home_town_pressed;
				home_town_pressed = -1;
				if (town >= 0 && town == pressed) {
					if (town == home_town_cursor) {
						home_town_choose(town);
					} else {
						home_town_cursor = town;
					}
				}
				break;
			}
			default:
				break;
			}
		}
		if (!home_town_open || !running) {
			return;
		}
		home_town_window->draw(renderer, home_town_view());
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
	};
	// The «Details» window: a tap on a ball or a button, or the buttons (Up and
	// Down the rows, Left and Right a row's balls or «Cancel» and «OK», X,
	// Circle «Cancel»). «OK» gives the details back; either way the window
	// takes the level nearest to the details (0x498EFD).
	auto details_view = [&]() {
		p3::app::NewGameWindow::DetailsView v;
		v.details = &details_copy;
		v.texts = &new_game_texts;
		for (int k = 0; k < p3::app::kDetailCancel; ++k) {
			v.enabled[size_t(k)] = new_game_detail_enabled(details_copy, k / 2, k % 2 == 1);
		}
		v.enabled[size_t(p3::app::kDetailCancel)] = true;
		v.enabled[size_t(p3::app::kDetailOk)] = true;
		v.focus = details_focus;
		v.pressed = details_pressed;
		v.own_values = fix_game_bugs();
		return v;
	};
	auto details_press = [&](int control) {
		NewGameChoices& c = new_game_state.choices;
		if (control >= 0 && control < p3::app::kDetailCancel) {
			if (new_game_detail_enabled(details_copy, control / 2, control % 2 == 1)) {
				press_new_game_detail(details_copy, control / 2, control % 2 == 1);
			}
			return;
		}
		if (control != p3::app::kDetailCancel && control != p3::app::kDetailOk) {
			return;
		}
		if (control == p3::app::kDetailOk) {
			c.details = details_copy;
		}
		c.level = new_game_level_of(c.details);
		details_open = false;
	};
	auto details_frame = [&]() {
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			if ((event.type == SDL_FINGERDOWN || event.type == SDL_FINGERMOTION || event.type == SDL_FINGERUP) &&
				rear_touch(event.tfinger.touchId)) {
				continue;
			}
			const int x = int(event.tfinger.x * kScreenWidth);
			const int y = int(event.tfinger.y * kScreenHeight);
			switch (event.type) {
			case SDL_QUIT:
				running = false;
				break;
			case SDL_CONTROLLERBUTTONDOWN: {
				const Uint8 button = event.cbutton.button;
				const bool buttons_row = details_focus >= p3::app::kDetailCancel;
				if (button == SDL_CONTROLLER_BUTTON_DPAD_UP || button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
					const int row = buttons_row ? p3::app::kDetailRows : details_focus / 2;
					const int next = std::clamp(row + (button == SDL_CONTROLLER_BUTTON_DPAD_UP ? -1 : 1), 0,
						p3::app::kDetailRows);
					details_focus = next == p3::app::kDetailRows ? (buttons_row ? details_focus : p3::app::kDetailOk)
																 : 2 * next;
				} else if (button == SDL_CONTROLLER_BUTTON_DPAD_LEFT || button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) {
					const bool right = button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
					if (buttons_row) {
						details_focus = right ? p3::app::kDetailOk : p3::app::kDetailCancel;
					} else {
						details_press(2 * (details_focus / 2) + (right ? 1 : 0));
					}
				} else if (button == SDL_CONTROLLER_BUTTON_A) {
					if (buttons_row) {
						details_press(details_focus);
					}
				} else if (button == SDL_CONTROLLER_BUTTON_B) {
					details_press(p3::app::kDetailCancel);
				}
				break;
			}
			case SDL_FINGERDOWN:
			case SDL_FINGERMOTION:
				details_pressed = new_game_window->details_control_at(x, y, details_view());
				break;
			case SDL_FINGERUP: {
				const int k = new_game_window->details_control_at(x, y, details_view());
				const int pressed = details_pressed;
				details_pressed = -1;
				if (k >= 0 && k == pressed) {
					details_focus = k >= p3::app::kDetailCancel ? k : 2 * (k / 2);
					details_press(k);
				}
				break;
			}
			default:
				break;
			}
		}
		if (!details_open || !running) {
			return;
		}
		new_game_window->draw_details(renderer, details_view());
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
	};
	// The start screen (the game's main menu): the book, the entry under the
	// finger or the buttons' (Up and Down; X chooses), and over it the load
	// menu's slots. «Single player» opens the «Game settings» window, «Load
	// game» the slots, «Exit» quits; the others aren't in p3vita yet (a notice).
	auto start_choose = [&](int entry) {
		switch (p3::app::StartEntry(entry)) {
		case p3::app::StartEntry::Single:
			open_new_game();
			break;
		case p3::app::StartEntry::Load:
			open_menu(MenuScreen::Load, 0);
			break;
		case p3::app::StartEntry::Exit:
			running = false;
			break;
		default:
			notify(tr("Todavía no está en p3vita.", "Not in p3vita yet."));
			break;
		}
	};
	auto start_frame = [&]() {
		if (!start_screen) {
			start_screen = std::make_unique<p3::app::StartScreen>(renderer, *archive, spanish_mounted, kScreenWidth,
				kScreenHeight);
		}
		const int entries = p3::app::kStartEntries;
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			if ((event.type == SDL_FINGERDOWN || event.type == SDL_FINGERMOTION || event.type == SDL_FINGERUP) &&
				rear_touch(event.tfinger.touchId)) {
				continue;
			}
			const int x = int(event.tfinger.x * kScreenWidth);
			const int y = int(event.tfinger.y * kScreenHeight);
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
				if (menu.open()) {
					menu_button(event.cbutton.button);
				} else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP ||
					event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
					const int step = event.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP ? -1 : 1;
					start_lit = ((start_lit < 0 ? 0 : start_lit) + entries + step) % entries;
				} else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_A && start_lit >= 0) {
					start_choose(start_lit);
				}
				break;
			case SDL_FINGERDOWN:
				touch_start = {x, y};
				touch_moved = 0;
				if (!menu.open() && start_screen->entry_at(x, y) >= 0) {
					start_lit = start_screen->entry_at(x, y);
				}
				break;
			case SDL_FINGERMOTION:
				touch_moved += std::abs(int(event.tfinger.dx * kScreenWidth)) +
					std::abs(int(event.tfinger.dy * kScreenHeight));
				if (!menu.open() && start_screen->entry_at(x, y) >= 0) {
					start_lit = start_screen->entry_at(x, y);
				}
				break;
			case SDL_FINGERUP:
				if (menu.open()) {
					if (touch_moved < kTapDistance && small_text) {
						menu_tap(touch_start.x, touch_start.y);
					}
				} else if (start_screen->entry_at(x, y) >= 0) {
					start_choose(start_screen->entry_at(x, y));
				}
				break;
			default:
				break;
			}
			// Back from the slots goes to the book, not to the game's menu.
			if (menu.screen == MenuScreen::Main) {
				menu.screen = MenuScreen::Closed;
			}
		}
		if (!start_open || !running) {
			return;
		}
		start_screen->draw(renderer, menu.open() ? -1 : start_lit);
		if (menu.open() && small_text) {
			draw_menu(renderer, *small_text, menu, settings, town_names);
		}
		if (text && !notices.empty()) {
			if (app_ticks() > notices.front().second) {
				notices.pop_front();
			} else {
				const int w = text->measure(notices.front().first);
				text->draw(notices.front().first, (kScreenWidth - w) / 2, kScreenHeight - 2 * text->line_height(),
					SDL_Color{255, 255, 255, 255});
			}
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
	};
	while (running) {
		if (script) {
			script->start_frame();
			if (script->finished()) {
				break;
			}
		}
		if (start_open) {
			if (new_game_open && map_choice_open) {
				map_choice_frame();
			} else if (new_game_open && home_town_open) {
				home_town_frame();
			} else if (new_game_open && details_open) {
				details_frame();
			} else if (new_game_open) {
				new_game_frame();
			} else {
				start_frame();
			}
			continue;
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
		// The battle the player directs, on the screen ([0x6E59CC], the battles' [+0x3FC]): it takes the town's
		// part of the screen and its camera (the battles' piece 7, part 3); its graphics load a sheet a frame. At a
		// town, the town view shows the town's battle map under it (BattleTown).
		if (debug_nest_attack && has_save) {
			debug_nest_attack = p3::app::follow_nest_attack(save);
		}
		auto show_battle_town = [&](size_t b) {
			TownMap map;
			std::vector<uint8_t> owners;
			(void)b;
			battle_town.draw(save, view->tiles(), map, &owners);
			view->show(map, std::move(owners));
			live.forget_drawn();
		};
		{
			int on_screen = -1;
			if (sailing && has_save && save.player >= 0 && kBattleScreen) {
				const uint8_t slot = save.battles.get<uint8_t>(0, 0x3FC);
				const int b = slot != 0xFF ? save.battles.battle(slot) : -1;
				const uint32_t vtable = b >= 0 ? save.battles.get<uint32_t>(size_t(b), 0) : 0;
				if (vtable == Sailing::kSeaBattleVtable || vtable == Sailing::kTownBattleVtable) {
					on_screen = b;
				}
			}
			if (on_screen != battle_shown) {
				// The town's battle map: shown with a battle at a town; the town again when it goes.
				int town_now = -1;
				if (on_screen >= 0 && p3::app::BattleView::at_town(save, size_t(on_screen))) {
					const uint8_t town = save.battles.get<uint8_t>(size_t(on_screen), 0x1E98);
					std::string error;
					if (battle_town.load(*archive, save, size_t(on_screen), regions, error)) {
						show_battle_town(size_t(on_screen));
						town_now = town;
						if (debug_weak_towers) {
							p3::app::weaken_towers(save, size_t(on_screen));
						}
					} else {
						std::fprintf(stderr, "the battle map: %s\n", error.c_str());
					}
				}
				if (battle_town_index >= 0 && town_now < 0 && selection.mode == Mode::Saved) {
					reload = true;  // The town view shows the town again, the camera where it was.
					keep_camera = true;
				}
				battle_town_index = town_now;
				if (on_screen >= 0) {
					if (battle_shown < 0) {
						battle_saved_camera = {camera_x, camera_y};
					}
					// The town's windows close: the battle's screen takes the view (as the game's town view, 0x6102A9).
					market.open = false;
					hanse.open = false;
					place.open = Place::None;
					side.menu = PanelMenu::None;  // The messages close with it (below).
					side.citizens = false;
					if (!battle_view) {
						battle_view = std::make_unique<p3::app::BattleView>(renderer, *archive, view->tiles());
					}
					battle_centre = true;
					// At first all his ships in the battle are chosen: a touch on the water sends them.
					battle_selection = p3::app::BattleView::player_ships(save, size_t(on_screen), save.player);
					sailing->set_battle_selection(size_t(on_screen), battle_selection);
				} else {
					if (battle_view) {
						battle_view->clear();
					}
					camera_x = battle_saved_camera.x;
					camera_y = battle_saved_camera.y;
					battle_selection.clear();
				}
				battle_shown = on_screen;
				battle_hold = false;
				battle_long = false;
			}
			if (battle_shown >= 0) {
				battle_view->need(save, size_t(battle_shown));
				battle_view->load_step();
				// The chosen ships that can no longer take orders (out, sunk, sinking, taken) leave the selection.
				const std::vector<uint16_t> fit = p3::app::BattleView::player_ships(save, size_t(battle_shown), save.player);
				std::vector<uint16_t> kept;
				for (const uint16_t place : battle_selection) {
					if (std::find(fit.begin(), fit.end(), place) != fit.end()) {
						kept.push_back(place);
					}
				}
				if (kept != battle_selection) {
					battle_selection = kept;
					sailing->set_battle_selection(size_t(battle_shown), battle_selection);
				}
				// A finger held still on the battle (kBattleLongPressMs): on a ship of his, it is added to the
				// selection or taken out of it; on the water, with ships chosen, the held order starts there
				// (the PC's right button held: 0x608550 with 1, which the step sends again while it is held).
				if (finger_down && !battle_long && !battle_hold && std::abs(touch_now.x - touch_start.x) < kBattleStill &&
					std::abs(touch_now.y - touch_start.y) < kBattleStill && !battle_popup() && !menu.open() &&
					app_ticks() - battle_touch_ms >= kBattleLongPressMs &&
					touch_start.x < PanelLayout(side.folded).town_right) {
					battle_long = true;
					bool own = false;
					const int place = battle_view->ship_at(touch_start.x, touch_start.y, own);
					if (place >= 0 && own) {
						const auto at = std::find(battle_selection.begin(), battle_selection.end(), uint16_t(place));
						if (at != battle_selection.end()) {
							battle_selection.erase(at);
						} else {
							battle_selection.push_back(uint16_t(place));
						}
						sailing->set_battle_selection(size_t(battle_shown), battle_selection);
					} else if (place < 0 && !battle_selection.empty()) {
						battle_hold = true;
						sailing->fn_608550(size_t(battle_shown), uint32_t(camera_x + touch_start.x),
							uint32_t((camera_y + touch_start.y) * 34 / 22), true);
					}
				}
				// The camera on the player's ships, once they are placed.
				SDL_Point c;
				if (battle_centre && p3::app::BattleView::player_ships_centre(save, size_t(battle_shown), save.player, c)) {
					camera_x = c.x - kScreenWidth / 2;
					camera_y = c.y - kScreenHeight / 2;
					battle_centre = false;
				}
			}
		}
		if (battle_to_sea) {
			// The battle given back from its leave icon: once its screen has closed, the sea map (the PC's view
			// leaving it for the sea map).
			battle_to_sea = false;
			battle_leave = false;
			if (battle_shown < 0 && selection.mode == Mode::Saved) {
				selection.mode = Mode::Sea;
				reload = true;
				keep_camera = false;
			}
		}
		const bool in_battle = battle_shown >= 0;
		// The battle's keys (the user's choices): X fires (operation 0x92, the PC's Space), Triangle chooses all his
		// ships, L and R the previous or the next of them alone with the view on it (the PC's Tab), up and down
		// the sails (0x98: the selection's highest asked plus one, or its lowest minus one; the PC's Page Up and
		// Page Down). Square and Circle do nothing.
		auto battle_key = [&](Uint8 button) {
			const size_t b = size_t(battle_shown);
			switch (button) {
			case SDL_CONTROLLER_BUTTON_A:
				sailing->queue_fire(b);
				break;
			case SDL_CONTROLLER_BUTTON_Y:
				battle_selection = p3::app::BattleView::player_ships(save, b, save.player);
				sailing->set_battle_selection(b, battle_selection);
				break;
			case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
			case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: {
				const std::vector<uint16_t> ships = p3::app::BattleView::player_ships(save, b, save.player);
				if (ships.empty()) {
					break;
				}
				const int n = int(ships.size());
				const int step = button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER ? 1 : -1;
				int k = step > 0 ? 0 : n - 1;
				if (!battle_selection.empty()) {
					const auto at = std::find(ships.begin(), ships.end(), battle_selection.front());
					if (at != ships.end()) {
						k = (int(at - ships.begin()) + step + n) % n;
					}
				}
				battle_selection = {ships[size_t(k)]};
				sailing->set_battle_selection(b, battle_selection);
				SDL_Point p;
				if (p3::app::BattleView::ship_point(save, b, ships[size_t(k)], p)) {
					camera_x = p.x - g_town_right / 2;
					camera_y = p.y - kScreenHeight / 2;
				}
				break;
			}
			case SDL_CONTROLLER_BUTTON_BACK:
				side.folded = !side.folded;  // Select: the panel folded or unfolded.
				break;
			case SDL_CONTROLLER_BUTTON_DPAD_UP:
			case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
				if (!battle_selection.empty()) {
					sailing->fn_610579(b, p3::app::BattleView::sails_step(save, b, button == SDL_CONTROLLER_BUTTON_DPAD_UP));
				}
				break;
			default:
				break;
			}
		};
		// The point of the battle's area under the screen's (x, y): x and y · 34 / 22 (the town view's, 0x587E80).
		auto battle_point = [&](int x, int y) {
			return std::array<uint32_t, 2>{uint32_t(camera_x + x), uint32_t((camera_y + y) * 34 / 22)};
		};
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
		if (place.bathed && (place.open != Place::BathHouse || place.page != 0) && has_save && save.player >= 0) {
			// The bath taken: its 0x39 with the tick now, once the player goes on (0x5B19F1, 0x5B1E93).
			run_operations(save, sailing.get(), {bath_operation(uint32_t(save.player), save.globals.u32(global::kWorldTick))});
			place.bathed = false;
		}
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
		// (Not yet over a battle: its panel is part 3's.)
		const bool panel_shown = (selection.mode == Mode::Saved || (selection.mode == Mode::Sea && sea)) && has_save &&
			save.player >= 0 && small_text && panel_art && !in_battle;
		const bool sea_panel = panel_shown && selection.mode == Mode::Sea;  // On the sea map: its minimap and the ships.
		if (sea_panel && side.menu == PanelMenu::Build) {
			side.menu = PanelMenu::None;
		}
		const PanelLayout panel_layout(side.folded);
		// The battle's panel (draw_battle_panel) takes the side panel's place over the battle.
		const bool battle_panel = in_battle && small_text && panel_art && save.player >= 0;
		const int town_width = panel_shown || battle_panel ? panel_layout.town_right : kScreenWidth;
		// Its deck shows the first chosen ship of his, else his first one; its buttons' state (0x610D69).
		int battle_deck_place = -1;
		uint16_t battle_bits = 0;
		bool battle_any_ship = false;
		if (battle_panel) {
			const std::vector<uint16_t> own = p3::app::BattleView::player_ships(save, size_t(battle_shown), save.player);
			battle_any_ship = !own.empty();
			battle_deck_place = !battle_selection.empty() ? battle_selection.front() : !own.empty() ? own.front() : -1;
			battle_bits = p3::app::BattleView::panel_buttons(save, size_t(battle_shown), save.player);
		}
		// The battle panel's button (or the fire button, kBattleButtons) under a finger that is down, for its look.
		int battle_button_down = -1;
		if (battle_panel && finger_down && !side.folded && touch_moved < kTapDistance) {
			for (int k = 0; k < kBattleButtons; ++k) {
				if (inside(panel_layout.icons[k], touch_start.x, touch_start.y)) {
					battle_button_down = k;
				}
			}
			if (inside(BattleDeckLayout(panel_layout.room, small_text->line_height()).fire, touch_start.x, touch_start.y)) {
				battle_button_down = kBattleButtons;
			}
			if (inside(panel_layout.icons[kBattleLeaveIcon], touch_start.x, touch_start.y)) {
				battle_button_down = kBattleLeavePressed;
			}
		}
		g_town_right = town_width;
		// Whether (x, y) is on one of the town view's event symbols (draw_town_events).
		auto on_town_event = [&](int x, int y) {
			SDL_Rect where{};
			return selection.mode == Mode::Saved && has_save && town_index >= 0 && size_t(town_index) < save.towns.size() &&
				town_event_at(save.towns[size_t(town_index)].memory.u32(0x2C8), town_width, x, y, where) != 0;
		};
		if (!panel_shown) {
			side.focus = -1;
		}
		// The building in the middle of the town's part of the screen, the
		// buttons' cursor (as the game's mouse pointer): X goes into it.
		const bool town_free = selection.mode == Mode::Saved && has_save && save.player >= 0 && !trading && !in_hanse &&
			!in_place && !in_mail && !in_menu && !in_battle;
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
		MailLayout mail_layout = mail_in_panel ? MailLayout(small_text->line_height(), panel_layout.room)
											   : MailLayout(small_text ? small_text->line_height() : 20, mail.tab == 0);
		// The letter's buttons as the last frame drew them (MailUi::answers).
		mail_layout.with_answers(mail_in_panel && !mail.letter_open ? 0 : int(mail.answers.size()));
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
		// A button of the letter shown (X the first, Triangle the second): the operation it queues
		// (letter_window.hpp, rolling the game's rand() as the game's window does); its buttons gone (MailUi::answered).
		auto mail_answer = [&](int k) {
			if (k < 0 || k >= int(mail.answers.size()) || mail.shown >= save.mail.size() || mail.options) {
				return false;
			}
			if (std::optional<OperationBytes> op = letter_answer(save, mail.shown, uint32_t(k))) {
				queue_operation(save, *op);
				mail.answered = mail.shown;
				mail.answers.clear();
			}
			return true;
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
				if (mail.letter_open && mail_answer(0)) {
					return true;  // The letter's first button.
				}
				mail_select(mail.row);
				mail.letter_open = mail.tab == 0 || !mail_list.empty();
				return true;
			case SDL_CONTROLLER_BUTTON_Y:  // Triangle: the letter's second button.
				if (mail.letter_open) {
					mail_answer(1);
				}
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
			case SDL_CONTROLLER_BUTTON_A:  // The letter's buttons: X the first, Triangle the second.
			case SDL_CONTROLLER_BUTTON_Y:
				mail_answer(button == SDL_CONTROLLER_BUTTON_A ? 0 : 1);
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
			if (selection.mode == Mode::Saved && sea_switch) {
				selection.mode = Mode::Sea;
				reload = true;
			}
			sea_switch = true;
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
			std::vector<uint8_t> owners;
			std::string error;
			bool loaded = false;
			bool drawn = false;
			if (selection.mode == Mode::Saved) {
				drawn = live.drawn(save, selection.saved_index);
				loaded = drawn || live.load(*archive, save, selection.saved_index, regions, error);
				if (loaded && !drawn) {
					live.draw(save, view->tiles(), town, &owners);
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
				view->show(town, std::move(owners));
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
				// piece 6) and it may attack ships. Until piece 7 the computer fights those battles for the player
				// (as when he answers no to the battle's question), with the game's texts in the messages window
				// (the user's choice, 2026-11-07). 0x0D and 0x518750 are rewritten and checked.
				ship_pirate_ok = sailing && can_turn_pirate(save, *sailing, side.ship);
				const int rows = ship_goods_rows(save, side.ship);
				const ShipMenuLayout ship_layout(panel_layout.room, small_text->line_height());
				side.cargo_first = std::clamp(side.cargo_first, 0, std::max(0, rows - ship_layout.cargo_rows));
				// The trade route page: its state goes with its ship; the rows as they are now (the wares window
				// closes when its stop leaves the route).
				if (side.route_for != side.ship) {
					side.route_for = side.ship;
					side.route_first = 0;
					side.route_row = 0;
					side.route_column = 0;
					side.route_towns_row = -1;
					side.stop_window.reset();
					side.route_files = false;
				}
				const std::vector<RouteRow> route_now = route_rows(save, side.ship);
				const int route_count = route_page_rows(save, side.ship);
				const RoutePageLayout route_layout(panel_layout.room, small_text->line_height());
				side.route_first = std::clamp(side.route_first, 0, std::max(0, route_count - route_layout.rows));
				side.route_row = std::clamp(side.route_row, -1, route_count - 1);
				if (side.route_towns_row >= route_count) {
					side.route_towns_row = -1;
				}
				if (side.stop_window && std::none_of(route_now.begin(), route_now.end(),
											[&](const RouteRow& r) { return r.stop == side.stop_window->stop; })) {
					side.stop_window.reset();
				}
			}
		}
		if (side.menu != PanelMenu::Ship || side.ship_page != ShipPage::TradeRoute || side.folded || !panel_shown) {
			side.route_towns_row = -1;
			side.stop_window.reset();
			side.route_drag = -1;
			side.route_files = false;
		}
		if (!side.route_files && side.route_file_typing) {
			side.route_file_typing = false;
			SDL_StopTextInput();
		}
		if (!side.stop_window) {
			side.stop_drag = -1;
			if (side.stop_typing >= 0) {
				side.stop_typing = -1;
				SDL_StopTextInput();
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
			side.route_row = 0;
			side.route_column = 0;
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
		// The trade route page and its windows: what they queue runs at once (trade_route_window.hpp).
		auto route_run = [&](const std::optional<OperationBytes>& op) {
			if (op) {
				run_operations(save, sailing.get(), {*op});
			}
		};
		auto route_run_all = [&](const std::vector<OperationBytes>& ops) {
			if (!ops.empty()) {
				run_operations(save, sailing.get(), ops);
			}
		};
		// A row's town button: the towns' list, the pad on the row's town.
		auto route_open_towns = [&](int row) {
			side.route_towns_row = row;
			side.route_towns_pick = 0;
			const std::vector<RouteRow> rows = route_rows(save, side.ship);
			const std::vector<int> entries = route_town_entries(save, side.ship);
			if (row < int(rows.size())) {
				const auto at = std::find(entries.begin(), entries.end(), int(rows[size_t(row)].town));
				if (at != entries.end()) {
					side.route_towns_pick = int(at - entries.begin());
				}
			}
		};
		auto route_choose_town = [&](int entry) {
			const std::vector<int> entries = route_town_entries(save, side.ship);
			if (side.route_towns_row >= 0 && entry >= 0 && entry < int(entries.size())) {
				const int town = entries[size_t(entry)];
				route_run(route_town_operation(save, side.ship, side.route_towns_row, town < 0 ? uint8_t(0xFF) : uint8_t(town)));
			}
			side.route_towns_row = -1;
		};
		auto stop_end_typing = [&](bool commit) {
			if (side.stop_typing < 0) {
				return;
			}
			if (commit && side.stop_window && !side.stop_typed.empty()) {
				const int32_t value = int32_t(std::min(std::atol(side.stop_typed.c_str()), 1000000L));
				StopWindow& w = *side.stop_window;
				route_run(side.stop_typing_price ? set_stop_price(w, side.stop_typing, value)
												 : set_stop_amount(w, side.stop_typing, value));
			}
			side.stop_typing = -1;
			side.stop_typed.clear();
			SDL_StopTextInput();
		};
		// A row's «Mercancías»: the wares window of its stop (0x405A20 with the menu's ship and whether it shows a convoy).
		auto route_open_stop = [&](uint16_t stop) {
			stop_end_typing(false);
			side.stop_window = open_stop_window(save, side.ship, stop, ship_menu_convoy(save, side.ship));
			side.route_towns_row = -1;
		};
		// The route files' list («Archivo», 0x48C6A0): its files, the name «Route%d» with the ship's (or its guide's)
		// stop (none without a route), the pad on the box.
		auto route_files_open = [&]() {
			side.route_files = true;
			side.route_file_list = p3::app::list_route_files(saves_dir);
			const uint16_t own = save.ships[size_t(route_ship(save, side.ship))].memory.u16(0x132);
			side.route_file_name = own == 0xFFFF
				? std::string()
				: GameTexts::sprintf_raw(exe_text(0x6BA3A0, "%d", "Route%d"), {std::to_string(own)});
			side.route_file_typing = false;
			side.route_file_question = 0;
			side.route_file_first = 0;
			side.route_file_row = int(side.route_file_list.size());
			side.route_file_column = 0;
			side.route_towns_row = -1;
			side.stop_window.reset();
		};
		auto route_file_end_typing = [&]() {
			if (side.route_file_typing) {
				side.route_file_typing = false;
				if (side.route_file_name.empty()) {
					side.route_file_name = side.route_file_before;
				}
				SDL_StopTextInput();
			}
		};
		auto route_file_show = [&](int row) {
			const RouteFilesLayout fl(small_text->line_height());
			if (row < int(side.route_file_list.size())) {
				if (row < side.route_file_first) {
					side.route_file_first = row;
				} else if (row >= side.route_file_first + fl.rows) {
					side.route_file_first = row - fl.rows + 1;
				}
			}
		};
		// «Cargar» (0x48C92E): the file's stops (0x4D5EE0) to the queue object for the menu's ship, applied at once
		// (the end of the queue's loop, 0x5492D0).
		auto route_file_load = [&]() {
			const int i = route_file_match(side.route_file_list, side.route_file_name);
			if (i < 0) {
				return;
			}
			std::vector<uint8_t> bytes;
			std::optional<std::vector<TradeStop>> stops;
			if (p3::app::read_route_file(saves_dir, side.route_file_list[size_t(i)], bytes)) {
				stops = fn_4d5ee0(bytes);
			}
			load_route_file(save, side.ship, stops);
			if (sailing) {
				run_queued_operations(save, *sailing);
			}
			side.route_files = false;
		};
		// «Guardar» (0x48C9A0): the route of the ship (or its guide) written (0x4D5C50); the list closes.
		auto route_file_save = [&]() {
			const uint16_t stop = route_file_stop(save, side.ship);
			if (stop != 0xFFFF) {
				if (const std::optional<std::vector<uint8_t>> bytes = fn_4d5c50(save, stop)) {
					p3::app::write_route_file(saves_dir, side.route_file_name, *bytes);
				}
			}
			side.route_files = false;
		};
		auto route_file_answer = [&](bool yes) {
			const int question = side.route_file_question;
			side.route_file_question = 0;
			if (!yes) {
				return;
			}
			if (question == 1) {
				route_file_save();
			} else if (question == 2) {
				const int i = route_file_match(side.route_file_list, side.route_file_name);
				if (i >= 0) {
					p3::app::delete_route_file(saves_dir, side.route_file_list[size_t(i)]);  // (0x472470)
				}
				side.route_file_list = p3::app::list_route_files(saves_dir);
				side.route_file_row = std::min(side.route_file_row, int(side.route_file_list.size()));
				side.route_file_first = 0;
			}
		};
		// A file (its name into the box) or a bottom button: the box, «Cargar», «Guardar», «Borrar», close.
		auto route_file_press = [&](int row, int column) {
			route_file_end_typing();
			const int n = int(side.route_file_list.size());
			const bool exists = route_file_match(side.route_file_list, side.route_file_name) >= 0;
			if (row < n) {
				side.route_file_name = side.route_file_list[size_t(row)];
				return;
			}
			switch (column) {
			case 0:
				side.route_file_typing = true;
				side.route_file_before = side.route_file_name;
				side.route_file_name.clear();
				SDL_StartTextInput();
				break;
			case 1: route_file_load(); break;
			case 2:
				if (side.route_file_name.empty()) {
					break;
				}
				if (exists) {
					side.route_file_question = 1;  // (0x470E29: «overwrite?»)
				} else {
					route_file_save();
				}
				break;
			case 3:
				if (exists) {
					side.route_file_question = 2;  // (0x470D39: «delete?»)
				}
				break;
			default: side.route_files = false; break;
			}
		};
		// Its keys: Up and Down the files (below them the bottom row), Left and Right that row's buttons, X presses
		// the one lit (a file: its name), Circle closes it; a question: X yes, Circle no.
		auto route_files_button = [&](Uint8 button) {
			if (side.route_file_question) {
				if (button == SDL_CONTROLLER_BUTTON_A || button == SDL_CONTROLLER_BUTTON_B) {
					route_file_answer(button == SDL_CONTROLLER_BUTTON_A);
				}
				return button != SDL_CONTROLLER_BUTTON_START;
			}
			const int n = int(side.route_file_list.size());
			switch (button) {
			case SDL_CONTROLLER_BUTTON_DPAD_UP:
			case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
				route_file_end_typing();
				side.route_file_row = std::clamp(side.route_file_row + (button == SDL_CONTROLLER_BUTTON_DPAD_UP ? -1 : 1), 0, n);
				if (side.route_file_row < n) {
					side.route_file_name = side.route_file_list[size_t(side.route_file_row)];
				}
				route_file_show(side.route_file_row);
				return true;
			case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
			case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
				if (side.route_file_row >= n) {
					route_file_end_typing();
					side.route_file_column = std::clamp(
						side.route_file_column + (button == SDL_CONTROLLER_BUTTON_DPAD_LEFT ? -1 : 1), 0, kRouteFileColumns - 1);
				}
				return true;
			case SDL_CONTROLLER_BUTTON_A:
				route_file_press(side.route_file_row, side.route_file_column);
				return true;
			case SDL_CONTROLLER_BUTTON_B:
				if (side.route_file_typing) {
					route_file_end_typing();
				} else {
					side.route_files = false;
				}
				return true;
			default:
				return button != SDL_CONTROLLER_BUTTON_START;
			}
		};
		auto route_press = [&](int row, int column) {
			const std::vector<RouteRow> rows = route_rows(save, side.ship);
			const int stops = int(rows.size());
			if (row < 0) {
				if (column == 1) {
					route_run(route_active_operation(save, side.ship));
				} else {
					route_files_open();
				}
				return;
			}
			if (row > stops || (row == stops && stops >= 20)) {
				return;
			}
			if (row == stops || column == 0) {
				route_open_towns(row);
			} else if (column == 1) {
				side.stop_first = 0;
				side.stop_row = 0;
				side.stop_column = 0;
				route_open_stop(rows[size_t(row)].stop);
			} else {
				route_run(route_mode_operation(save, side.ship, row));
			}
		};
		auto route_show_row = [&](int row) {
			const RoutePageLayout rl(PanelLayout(side.folded).room, small_text->line_height());
			if (row >= 0 && row < side.route_first) {
				side.route_first = row;
			} else if (row >= side.route_first + rl.rows) {
				side.route_first = row - rl.rows + 1;
			}
		};
		// The wares window's previous or next stop («<», «>»: 0x405A20 again), round the route.
		auto stop_step_stop = [&](int step) {
			const std::vector<RouteRow> rows = route_rows(save, side.ship);
			const int n = int(rows.size());
			for (int k = 0; k < n && side.stop_window; ++k) {
				if (rows[size_t(k)].stop == side.stop_window->stop) {
					route_open_stop(rows[size_t((k + step + n) % n)].stop);
					break;
				}
			}
		};
		auto stop_show_row = [&](int row) {
			const StopWindowLayout sl(small_text->line_height());
			if (row < kStopRows && row < side.stop_first) {
				side.stop_first = row;
			} else if (row < kStopRows && row >= side.stop_first + sl.rows) {
				side.stop_first = row - sl.rows + 1;
			}
		};
		// A button of the wares window: a row's cell (the direction, −, a box, +) or a bottom button.
		auto stop_press = [&](int row, int column) {
			if (!side.stop_window) {
				return;
			}
			stop_end_typing(true);
			StopWindow& w = *side.stop_window;
			if (row == kStopRows) {
				switch (column) {
				case 0:
				case 1: stop_step_stop(column == 0 ? -1 : 1); break;
				case 2: route_run_all(press_stop_direction_all(w, save)); break;
				case 3:
				case 4: route_run_all(stop_amounts_all(w, column == 3)); break;
				case 5: route_run_all(undo_stop_window(w)); break;
				default: side.stop_window.reset(); break;
				}
				return;
			}
			const int ware = w.order[size_t(row)];
			if (ware >= kStopRows) {
				return;
			}
			const StopDirection d = w.state[size_t(ware)];
			switch (column) {
			case 0: route_run(press_stop_direction(w, save, ware)); break;
			case 1:
			case 3: route_run(step_stop_amount(w, ware, column == 1 ? -1 : 1)); break;
			case 4:
			case 6: route_run(step_stop_price(w, ware, column == 4 ? -1 : 1)); break;
			default: {
				// A box: written with the keyboard (the Vita's), committed with its 0x69.
				const bool price_box = column == 5;
				if (d == StopDirection::None || (price_box && d != StopDirection::Sell && d != StopDirection::Buy)) {
					break;
				}
				side.stop_typing = ware;
				side.stop_typing_price = price_box;
				side.stop_typed.clear();
				SDL_StartTextInput();
				break;
			}
			}
		};
		// The wares window's keys: Up and Down the rows (below them its bottom buttons), Left and Right the row's
		// cells, X presses the one lit, Square moves the row up, L and R the previous and next stop, Circle closes it.
		auto stop_button = [&](Uint8 button) {
			switch (button) {
			case SDL_CONTROLLER_BUTTON_DPAD_UP:
			case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
				stop_end_typing(true);
				side.stop_row = std::clamp(side.stop_row + (button == SDL_CONTROLLER_BUTTON_DPAD_UP ? -1 : 1), 0, kStopRows);
				side.stop_column = std::min(side.stop_column, (side.stop_row == kStopRows ? kStopBottomButtons : kStopColumns) - 1);
				stop_show_row(side.stop_row);
				return true;
			case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
			case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
				stop_end_typing(true);
				side.stop_column = std::clamp(side.stop_column + (button == SDL_CONTROLLER_BUTTON_DPAD_LEFT ? -1 : 1), 0,
					(side.stop_row == kStopRows ? kStopBottomButtons : kStopColumns) - 1);
				return true;
			case SDL_CONTROLLER_BUTTON_A:
				stop_press(side.stop_row, side.stop_column);
				return true;
			case SDL_CONTROLLER_BUTTON_X:
				stop_end_typing(true);
				if (side.stop_window && side.stop_row >= 1 && side.stop_row < kStopRows) {
					route_run(move_stop_row(*side.stop_window, side.stop_row, side.stop_row - 1));
					--side.stop_row;
					stop_show_row(side.stop_row);
				}
				return true;
			case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
			case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
				stop_end_typing(true);
				stop_step_stop(button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER ? -1 : 1);
				return true;
			case SDL_CONTROLLER_BUTTON_B:
				if (side.stop_typing >= 0) {
					stop_end_typing(false);  // First the box being written.
				} else {
					side.stop_window.reset();
				}
				return true;
			default:
				return button != SDL_CONTROLLER_BUTTON_START;  // The window takes the pad's other keys.
			}
		};
		// The page's keys: Up and Down its rows (above them «Archivo» and «Activa»), Left and Right the row's buttons,
		// X presses the one lit, Square moves the row's stop up (0x6B, after the row two above it or on top); the towns'
		// list: Up and Down its towns, Left and Right its columns, X chooses, Circle closes it.
		auto route_button = [&](Uint8 button) {
			if (side.route_files) {
				return route_files_button(button);
			}
			if (side.stop_window) {
				return stop_button(button);
			}
			if (side.route_towns_row >= 0) {
				const int n = int(route_town_entries(save, side.ship).size());
				const RouteTownsLayout tl(small_text->line_height(), n);
				switch (button) {
				case SDL_CONTROLLER_BUTTON_DPAD_UP:
				case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
					side.route_towns_pick = (side.route_towns_pick + n + (button == SDL_CONTROLLER_BUTTON_DPAD_UP ? -1 : 1)) % n;
					return true;
				case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
				case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
					side.route_towns_pick = std::clamp(side.route_towns_pick +
							(button == SDL_CONTROLLER_BUTTON_DPAD_LEFT ? -tl.rows_per_column : tl.rows_per_column), 0, n - 1);
					return true;
				case SDL_CONTROLLER_BUTTON_A:
					route_choose_town(side.route_towns_pick);
					return true;
				case SDL_CONTROLLER_BUTTON_B:
					side.route_towns_row = -1;
					return true;
				default:
					return button != SDL_CONTROLLER_BUTTON_START;
				}
			}
			const int count = route_page_rows(save, side.ship);
			const int stops = int(route_rows(save, side.ship).size());
			auto columns = [&]() { return side.route_row < 0 ? 2 : side.route_row >= stops ? 1 : 3; };
			switch (button) {
			case SDL_CONTROLLER_BUTTON_DPAD_UP:
			case SDL_CONTROLLER_BUTTON_DPAD_DOWN: {
				side.route_row = std::clamp(side.route_row + (button == SDL_CONTROLLER_BUTTON_DPAD_UP ? -1 : 1), -1, count - 1);
				side.route_column = std::min(side.route_column, columns() - 1);
				route_show_row(side.route_row);
				return true;
			}
			case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
			case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
				side.route_column = std::clamp(side.route_column + (button == SDL_CONTROLLER_BUTTON_DPAD_LEFT ? -1 : 1), 0,
					columns() - 1);
				return true;
			case SDL_CONTROLLER_BUTTON_A:
				route_press(side.route_row, side.route_column);
				return true;
			case SDL_CONTROLLER_BUTTON_X:
				if (side.route_row >= 1 && side.route_row < stops) {
					route_run(route_move_operation(save, side.ship, side.route_row, side.route_row - 2));
					--side.route_row;
					route_show_row(side.route_row);
				}
				return true;
			default:
				return false;
			}
		};
		// Its buttons while it is open: L and R the pages, Up and Down the rows (the goods page's list),
		// Left and Right the amount (the sailors to dismiss, the public convoy's destination), X does it,
		// Circle closes it.
		auto ship_button = [&](Uint8 button) {
			if (side.ship_page == ShipPage::TradeRoute && route_button(button)) {
				return true;
			}
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
			// While a battle has a human side (time mode 2, operation 0xC8) the real-time loop gives the world at
			// most one tick each 3375 ms ([0x673CF8], 0x546729), whatever the speed.
			const bool battle_time = sailing->time_mode() == 2;
			const Uint32 tick_ms = battle_time ? kBattleTickMs : speed.ms_per_tick;
			const int max_ticks = battle_time ? 1 : speed.max_ticks;
			const Uint32 now = app_ticks();
			const Uint32 elapsed = now - clock;
			clock = now - elapsed % tick_ms;
			const int ticks = std::min(int(elapsed / tick_ms), max_ticks) + (script ? script->extra_ticks() : 0);
			// What a step of the world leaves for the viewer: the battles' messages and the sea map's icons.
			auto after_step = [&]() {
				for (const std::string& message : sailing->take_player_messages()) {
					messages.push(message);
				}
				// The battle's sprites of the town view (the guns' smoke, the splashes, the hits).
				const std::vector<Sailing::BattleEffect> effects = sailing->take_battle_effects();
				if (battle_view && battle_shown >= 0 && save.battles.battle(save.battles.get<uint8_t>(0, 0x3FC)) == battle_shown) {
					battle_view->add_effects(save, size_t(battle_shown), effects, app_ticks(),
						battle_town_index >= 0 ? &battle_town_layer : nullptr);
				}
				for (const Sailing::BattleIcon& icon : sailing->take_sea_map_icons()) {
					battle_icons.push_back({icon.slot, icon.x, icon.y, save.battles.battle(icon.slot)});
				}
				battle_icons.erase(std::remove_if(battle_icons.begin(), battle_icons.end(),
									   [&](const SeaBattleIcon& icon) { return save.battles.battle(icon.slot) != icon.block; }),
					battle_icons.end());
				if (battle_info >= 0 && save.battles.battle(uint8_t(battle_info)) < 0) {
					battle_info = -1;  // Its battle is over.
				}
			};
			if (battle_time) {
				// Each frame, as the game's real-time loop: the battles' clock (0x612DB0, its operation 0x94) and
				// the queued operations (the battles' steps, the player's orders, the answers 0x96, 0xC8).
				sailing->fn_612db0(now, speed.ms_per_tick);
				run_queued_operations(save, *sailing);
				after_step();
			}
			for (int i = 0; i < ticks; ++i) {
				advance_game_tick(save, *sailing);
				after_step();
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
		// on screen, it is drawn again, with the camera where it was (under a battle at a town, its battle map).
		if (battle_town_index >= 0 && battle_shown >= 0 && battle_town.changed(save)) {
			show_battle_town(size_t(battle_shown));
		} else if (selection.mode == Mode::Saved && live.changed(save) && battle_town_index < 0) {
			TownMap town;
			std::vector<uint8_t> owners;
			live.draw(save, view->tiles(), town, &owners);
			view->show(town, std::move(owners));
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
			} else if (stats_no_days(side.stats_entry)) {
				// 6 months, 2 years and the playing time (with more than a year recorded).
				const int periods = stats_years_shown(save) ? 3 : 2;
				const int k = std::min(statistics_competition_period(side.stats_graph_period), periods) - 1;
				side.stats_graph_period = 1 + (k + periods + step) % periods;
			} else {
				const int periods = stats_years_shown(save) ? 4 : 3;
				side.stats_graph_period = (std::min(side.stats_graph_period, periods - 1) + periods + step) % periods;
			}
		};
		// The buttons an entry has: the four under the graph (0..3) and the one in the place of «4 weeks» (4).
		auto stats_slot_valid = [&](int entry, int slot) {
			return slot == 4 ? stats_choice_button(entry) : slot >= 0 && slot < 4 && stats_slot_buttons(entry);
		};
		// An entry shown (0x4A5DD0): the competition's and the towns' graphs turn the graphs' period 0 into 1; a
		// button's list closes; the town data's population sets the comparison's first town past the towns to 0, as the
		// game's (statistics_towns).
		auto stats_select = [&](int entry) {
			side.stats_entry = entry;
			side.stats_choice_slot = -1;
			if (stats_no_days(entry)) {
				side.stats_graph_period = statistics_competition_period(side.stats_graph_period);
			}
			if (!stats_slot_valid(entry, side.stats_slot)) {
				side.stats_slot = stats_slot_buttons(entry) ? 0 : 4;
			}
			if (entry == kStatisticsPopulation && g_texts) {
				statistics_towns(save, *g_texts, stats_town_names(), side.stats_towns, entry, side.stats_graph_period);
			}
		};
		// A button's list: opened sorted by name; a row puts its merchant, town or ware in the slot (the extra row:
		// empty).
		auto stats_open_choice = [&](int slot) {
			side.stats_slot = slot;
			side.stats_choice_slot = slot;
			side.stats_choice_by_key = false;
			side.stats_choice_row = 0;
		};
		auto stats_choose = [&](int row) {
			const std::vector<int> rows = stats_choice_rows(save, side);
			if (side.stats_choice_slot >= 0 && row >= 0 && row < int(rows.size())) {
				const int m = rows[size_t(row)];
				const size_t slot = size_t(side.stats_choice_slot);
				if (stats_competition(side.stats_entry)) {
					side.stats_merchants[slot] = m < 0 ? 0xFF : uint8_t(m);
				} else if (slot == 4 && stats_town_data(side.stats_entry)) {
					side.stats_towns.town = m < 0 ? side.stats_towns.town : uint8_t(m);  // [+0xBD]
				} else if (slot == 4) {
					side.stats_towns.ware = m < 0 ? side.stats_towns.ware : uint8_t(m);  // [+0xBC]
				} else if (stats_town_data(side.stats_entry)) {
					side.stats_towns.wares[slot] = m < 0 ? 0xFF : uint8_t(m);  // [+0xAC + slot]
				} else {
					side.stats_towns.towns[slot] = m < 0 ? 0xFF : uint8_t(m);  // [+0xB0 + slot]
				}
				side.stats_choice_slot = -1;
			}
		};
		auto stats_button = [&](Uint8 button) {
			if (side.stats_choice_slot >= 0) {
				// A button's list: Up and Down its rows, Cross picks, Square the sort, Circle closes it.
				const int count = int(stats_choice_rows(save, side).size());
				switch (button) {
				case SDL_CONTROLLER_BUTTON_DPAD_UP:
				case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
					if (count > 0) {
						side.stats_choice_row =
							(side.stats_choice_row + count + (button == SDL_CONTROLLER_BUTTON_DPAD_UP ? -1 : 1)) % count;
					}
					return true;
				case SDL_CONTROLLER_BUTTON_A:
					stats_choose(side.stats_choice_row);
					return true;
				case SDL_CONTROLLER_BUTTON_X:
					side.stats_choice_by_key = !side.stats_choice_by_key;
					side.stats_choice_row = 0;
					return true;
				case SDL_CONTROLLER_BUTTON_B:
					side.stats_choice_slot = -1;
					return true;
				default:
					return true;
				}
			}
			switch (button) {
			case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
			case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
				for (int k = 1; k < kStatisticsPages; ++k) {
					const int page = (side.stats_page + kStatisticsPages + (button == SDL_CONTROLLER_BUTTON_DPAD_LEFT ? -k : k)) %
						kStatisticsPages;
					if (kStatsPageDone[page]) {
						side.stats_page = page;
						stats_select(kStatisticsPageFirst[size_t(page)]);
						break;
					}
				}
				return true;
			case SDL_CONTROLLER_BUTTON_DPAD_UP:
			case SDL_CONTROLLER_BUTTON_DPAD_DOWN: {
				const int first = kStatisticsPageFirst[size_t(side.stats_page)];
				const int count = kStatisticsPageFirst[size_t(side.stats_page) + 1] - first;
				const int step = button == SDL_CONTROLLER_BUTTON_DPAD_UP ? -1 : 1;
				stats_select(first + (side.stats_entry - first + count + step) % count);
				return true;
			}
			case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
			case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
				stats_step_period(button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER ? -1 : 1);
				return true;
			case SDL_CONTROLLER_BUTTON_X: {
				// The competition and the towns' pages: Square the next merchant, town or ware button (then the one in
				// the place of «4 weeks»), Cross opens its list.
				const int entry = side.stats_entry;
				if (!stats_slot_buttons(entry) && !stats_choice_button(entry)) {
					return false;
				}
				for (int k = 1; k <= 5; ++k) {
					const int slot = (side.stats_slot + k) % 5;
					if (stats_slot_valid(entry, slot)) {
						side.stats_slot = slot;
						break;
					}
				}
				return true;
			}
			case SDL_CONTROLLER_BUTTON_A:
				if (!stats_slot_valid(side.stats_entry, side.stats_slot)) {
					return false;
				}
				stats_open_choice(side.stats_slot);
				return true;
			case SDL_CONTROLLER_BUTTON_B:
				side.menu = PanelMenu::None;
				return true;
			default:
				return false;
			}
		};
		// The wares window's − or + under a finger held still: again after 380 ms, then every 70 ms.
		auto stop_cell_at = [&](int x, int y, int& row, int& column) {
			const StopWindowLayout sl(small_text->line_height());
			const int k = sl.row_at(y);
			if (k < 0 || side.stop_first + k >= kStopRows) {
				return false;
			}
			for (int c = 0; c < kStopColumns; ++c) {
				if (inside(sl.cell(k, c), x, y)) {
					row = side.stop_first + k;
					column = c;
					return true;
				}
			}
			return false;
		};
		if (side.stop_window && finger_down && touch_moved < kTapDistance && side.stop_drag < 0 && small_text) {
			int row = 0;
			int column = 0;
			if (stop_cell_at(touch_start.x, touch_start.y, row, column) && column != 0 && column != 2 && column != 5) {
				const Uint32 held = app_ticks() - touch_down_ms;
				const int due = held >= 380 ? 1 + int((held - 380) / 70) : 0;
				for (; stop_repeats < due && side.stop_window; ++stop_repeats) {
					side.stop_row = row;
					side.stop_column = column;
					stop_press(row, column);
				}
			}
		}
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
				if (battle_button(event.cbutton.button)) {
					break;  // The battles' window, over the others.
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
				if (in_battle && event.cbutton.button != SDL_CONTROLLER_BUTTON_START) {
					battle_key(event.cbutton.button);  // The battle's screen: its keys; Start, the menu.
					break;
				}
				if (side.menu == PanelMenu::Ship && panel_shown && !side.folded && !in_mail && small_text &&
					(side.stop_window || side.route_towns_row >= 0 || side.route_files) && route_button(event.cbutton.button)) {
					break;  // The route page's windows, in the town and on the sea map.
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
				if (side.citizens && selection.mode == Mode::Saved && event.cbutton.button == SDL_CONTROLLER_BUTTON_B) {
					side.citizens = false;  // The citizens' window: Circle closes it.
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
				if (side.route_file_typing) {
					// A route file's name: 40 bytes at most, without what a file's name can't have.
					for (const char* c = event.text.text; *c; ++c) {
						if (uint8_t(*c) >= 0x20 && !std::strchr("\\/:*?\"<>|", *c) && side.route_file_name.size() < 40) {
							side.route_file_name += *c;
						}
					}
					break;
				}
				if (side.stop_typing >= 0) {
					// A box of the wares window: its digits (7 at most).
					for (const char* c = event.text.text; *c; ++c) {
						if (*c >= '0' && *c <= '9' && side.stop_typed.size() < 7) {
							side.stop_typed += *c;
						}
					}
					break;
				}
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
				if (side.route_file_typing) {
					const SDL_Keycode key = event.key.keysym.sym;
					std::string& name = side.route_file_name;
					if (key == SDLK_BACKSPACE && !name.empty()) {
						while (!name.empty() && (uint8_t(name.back()) & 0xC0) == 0x80) {
							name.pop_back();  // a whole UTF-8 character
						}
						if (!name.empty()) {
							name.pop_back();
						}
					} else if (key == SDLK_RETURN || key == SDLK_ESCAPE) {
						route_file_end_typing();
					}
					break;
				}
				if (side.stop_typing >= 0) {
					const SDL_Keycode key = event.key.keysym.sym;
					if (key == SDLK_BACKSPACE && !side.stop_typed.empty()) {
						side.stop_typed.pop_back();
					} else if (key == SDLK_RETURN || key == SDLK_ESCAPE) {
						stop_end_typing(key == SDLK_RETURN);
					}
					break;
				}
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
				{
					// The battles' window: Enter as X, Escape as Circle.
					const SDL_Keycode key = event.key.keysym.sym;
					if (battle_button(key == SDLK_RETURN ? SDL_CONTROLLER_BUTTON_A
							: key == SDLK_ESCAPE ? SDL_CONTROLLER_BUTTON_B
							: SDL_CONTROLLER_BUTTON_INVALID)) {
						break;
					}
				}
				if (end_window.open && (event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_ESCAPE)) {
					end_window.open = false;
					break;
				}
				if (messages.open && (event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_ESCAPE)) {
					messages.next();
					break;
				}
				if (in_battle && event.key.keysym.sym != SDLK_ESCAPE) {
					// The battle's screen, keyboard: Space fires, A all the ships, Tab the next, Page Up and Down the
					// sails (the PC game's); Escape, the menu.
					const SDL_Keycode key = event.key.keysym.sym;
					battle_key(key == SDLK_SPACE ? SDL_CONTROLLER_BUTTON_A
						: key == SDLK_a          ? SDL_CONTROLLER_BUTTON_Y
						: key == SDLK_TAB        ? SDL_CONTROLLER_BUTTON_RIGHTSHOULDER
						: key == SDLK_PAGEUP     ? SDL_CONTROLLER_BUTTON_DPAD_UP
						: key == SDLK_PAGEDOWN   ? SDL_CONTROLLER_BUTTON_DPAD_DOWN
												 : SDL_CONTROLLER_BUTTON_INVALID);
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
				touch_now = touch_start;
				touch_moved = 0;
				touch_scroll = 0;
				finger_down = true;
				touch_down_ms = app_ticks();
				stop_repeats = 0;
				side.route_drag = -1;
				side.stop_drag = -1;
				if (battle_popup()) {
					// Under the battles' window nothing moves: the touch is the window's.
					touch_on_side = false;
					touch_on_panel = true;
					touch_building = false;
					break;
				}
				if (in_battle && !in_menu) {
					// The battle's screen: dragging moves its view; a tap or a long press is an order (on lifting
					// it, and in the frame, kBattleLongPressMs). On its panel, the panel's.
					if (battle_panel && inside(panel_layout.panel, touch_start.x, touch_start.y)) {
						touch_on_side = true;
						touch_on_panel = true;
						touch_building = false;
						battle_long = true;  // No order from this touch.
						battle_hold = false;
						break;
					}
					touch_on_side = false;
					touch_on_panel = false;
					touch_building = false;
					battle_touch_ms = app_ticks();
					battle_long = false;
					battle_hold = false;
					break;
				}
				touch_on_side = panel_shown && !in_menu && (!in_mail || mail_in_panel) && !messages.open && !end_window.open &&
					inside(panel_layout.panel, touch_start.x, touch_start.y);
				touch_on_panel = touch_on_side ||
					(trading && inside(MarketLayout(small_text->line_height()).panel, touch_start.x, touch_start.y)) ||
					(in_hanse && inside(HanseLayout(small_text->line_height()).panel, touch_start.x, touch_start.y)) ||
					(in_place && inside(PlaceLayout(small_text->line_height()).panel, touch_start.x, touch_start.y)) ||
					(in_menu && (inside(MenuLayout(small_text->line_height(), menu.screen).panel, touch_start.x, touch_start.y) ||
						menu.ask != MenuAsk::None)) ||
					(in_mail && (!mail_in_panel || mail_parchment) && inside(mail_layout.panel, touch_start.x, touch_start.y)) ||
					(stats_open && inside(StatsLayout(small_text->line_height()).panel, touch_start.x, touch_start.y)) ||
					// The route page's windows over the town: the town doesn't move under them.
					(side.menu == PanelMenu::Ship && (side.stop_window || side.route_towns_row >= 0 || side.route_files) &&
						touch_start.x < panel_layout.town_right && touch_start.y >= kBarHeight);
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
				touch_now = {int(event.tfinger.x * kScreenWidth), int(event.tfinger.y * kScreenHeight)};
				if (in_battle && touch_on_side) {
					// On the battle's minimap, the view goes where the finger is.
					SDL_Point at;
					if (!side.folded && inside(panel_layout.map, touch_start.x, touch_start.y) &&
						p3::app::BattleView::minimap_point(save, size_t(battle_shown), panel_layout.map,
							int(event.tfinger.x * kScreenWidth),
							int(event.tfinger.y * kScreenHeight), at)) {
						camera_x = at.x - town_width / 2;
						camera_y = at.y - kScreenHeight / 2;
					}
					break;
				}
				if (in_battle && battle_hold) {
					// The held order follows the finger (the PC's mouse moved with its right button down, 0x58BF35).
					const auto p = battle_point(int(event.tfinger.x * kScreenWidth), int(event.tfinger.y * kScreenHeight));
					sailing->fn_608550(size_t(battle_shown), p[0], p[1], true);
					break;
				}
				// Arrastrar desplaza la ciudad con el dedo, salvo sobre el mercado;
				// sobre el minimapa del panel, la vista va donde está el dedo.
				if (touch_building) {
					// Dragging the building: it follows the finger, cell by cell.
					place_at(camera_x + int(event.tfinger.x * kScreenWidth), camera_y + int(event.tfinger.y * kScreenHeight));
				} else if (touch_on_side) {
					SDL_Point at;
					if (!side.folded && side.menu == PanelMenu::Ship && side.ship_page == ShipPage::TradeRoute &&
						side.route_towns_row < 0 && inside(panel_layout.room, touch_start.x, touch_start.y)) {
						// The route page: a stop's town button dragged (0x492EE0 when it is let go), or the rows
						// moved a row at a time.
						const RoutePageLayout rl(panel_layout.room, small_text->line_height());
						const int k = rl.row_at(touch_start.y);
						const int r = k >= 0 ? side.route_first + k : -1;
						if (side.route_drag < 0 && r >= 0 && r < int(route_rows(save, side.ship).size()) &&
							inside(rl.town(k), touch_start.x, touch_start.y) && touch_moved >= kTapDistance) {
							side.route_drag = r;
						}
						if (side.route_drag >= 0) {
							side.route_drag_top = rl.town(k).y + (touch_now.y - touch_start.y);
						} else if (k >= 0) {
							touch_scroll -= event.tfinger.dy * kScreenHeight;
							for (; touch_scroll >= float(rl.row_h); touch_scroll -= float(rl.row_h)) {
								++side.route_first;  // Clamped next frame.
							}
							for (; touch_scroll <= -float(rl.row_h); touch_scroll += float(rl.row_h)) {
								side.route_first = std::max(0, side.route_first - 1);
							}
						}
					} else if (!side.folded && side.menu == PanelMenu::Ship && side.ship_page == ShipPage::Goods &&
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
						(sea_panel || !side.sea_minimap) &&
						(sea_panel ? minimap_sea_point(panel_layout, *sea, int(event.tfinger.x * kScreenWidth),
										 int(event.tfinger.y * kScreenHeight), at)
								   : minimap_town_point(panel_layout, *view, int(event.tfinger.x * kScreenWidth),
										 int(event.tfinger.y * kScreenHeight), at))) {
						camera_x = at.x - town_width / 2;
						camera_y = at.y - kScreenHeight / 2;
					}
				} else if (side.route_files && side.menu == PanelMenu::Ship && small_text &&
					inside(RouteFilesLayout(small_text->line_height()).list, touch_start.x, touch_start.y)) {
					// The route files' list: dragged, its files move a row at a time.
					const RouteFilesLayout fl(small_text->line_height());
					const int most = std::max(0, int(side.route_file_list.size()) - fl.rows);
					touch_scroll -= event.tfinger.dy * kScreenHeight;
					for (; touch_scroll >= float(fl.row_h); touch_scroll -= float(fl.row_h)) {
						side.route_file_first = std::min(most, side.route_file_first + 1);
					}
					for (; touch_scroll <= -float(fl.row_h); touch_scroll += float(fl.row_h)) {
						side.route_file_first = std::max(0, side.route_file_first - 1);
					}
				} else if (side.stop_window && side.menu == PanelMenu::Ship && small_text &&
					inside(StopWindowLayout(small_text->line_height()).panel, touch_start.x, touch_start.y)) {
					// The wares window: a ware's name dragged (its row moves when it is let go), or the rows moved a row
					// at a time.
					const StopWindowLayout sl(small_text->line_height());
					const int k = sl.row_at(touch_start.y);
					const int r = k >= 0 ? side.stop_first + k : -1;
					if (side.stop_drag < 0 && r >= 0 && r < kStopRows && inside(sl.name(k), touch_start.x, touch_start.y) &&
						touch_moved >= kTapDistance) {
						side.stop_drag = r;
					}
					if (side.stop_drag >= 0) {
						side.stop_drag_top = sl.name(k).y + (touch_now.y - touch_start.y);
					} else if (k >= 0) {
						touch_scroll -= event.tfinger.dy * kScreenHeight;
						for (; touch_scroll >= float(sl.row_h); touch_scroll -= float(sl.row_h)) {
							side.stop_first = std::min(kStopRows - sl.rows, side.stop_first + 1);
						}
						for (; touch_scroll <= -float(sl.row_h); touch_scroll += float(sl.row_h)) {
							side.stop_first = std::max(0, side.stop_first - 1);
						}
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
				if (battle_popup()) {
					// A tap on one of the battles' window's buttons.
					if (touch_moved < kTapDistance && text && small_text) {
						const ChoiceBox box = popup_box();
						const int k = ChoiceBoxLayout(*text, *small_text, box).button_at(touch_start.x, touch_start.y);
						if (k >= 0 && box.buttons[size_t(k)].second) {
							battle_press(k);
						}
					}
					touch_on_panel = false;
					break;
				}
				if (in_battle && !in_menu && touch_on_side) {
					// The battle's panel: its tab folds it; its buttons (on, as the game's panel 0x42D3C9: a check
					// button turned to the other state), its minimap, the deck's fire button and rope.
					touch_on_side = false;
					touch_on_panel = false;
					battle_long = false;
					if (touch_moved >= kTapDistance) {
						break;
					}
					const size_t b = size_t(battle_shown);
					const int tx = touch_start.x, ty = touch_start.y;
					if (inside(panel_layout.tab, tx, ty)) {
						side.folded = !side.folded;
						break;
					}
					if (side.folded) {
						break;
					}
					SDL_Point at;
					if (p3::app::BattleView::minimap_point(save, size_t(battle_shown), panel_layout.map, tx, ty, at)) {
						camera_x = at.x - town_width / 2;
						camera_y = at.y - kScreenHeight / 2;
						break;
					}
					if (inside(panel_layout.icons[kBattleLeaveIcon], tx, ty)) {
						battle_leave = true;
						break;
					}
					for (int k = 0; k < kBattleButtons; ++k) {
						const BattleButtonState st = battle_button_state(BattleButton(k), battle_bits, battle_any_ship);
						if (!inside(panel_layout.icons[k], tx, ty) || !st.enabled) {
							continue;
						}
						switch (BattleButton(k)) {
						case BattleButton::SelectAll:
							battle_selection = p3::app::BattleView::player_ships(save, b, save.player);
							sailing->set_battle_selection(b, battle_selection);
							break;
						case BattleButton::Capture:
							sailing->fn_6103e9(b, false, !st.checked);
							break;
						case BattleButton::Plunder:
							sailing->fn_6103e9(b, !st.checked, false);
							break;
						case BattleButton::Flee:
							if (battle_bits & 0x8000) {
								sailing->queue_abandon(b);  // Leave (Abandonar): operation 0xA0.
							} else {
								sailing->fn_610c09(b);
							}
							break;
						case BattleButton::WhiteFlag:
							sailing->fn_610fd9(b, !st.checked);
							break;
						default:
							break;
						}
					}
					const int e = battle_deck_place >= 0
						? p3::app::BattleView::scene_entry(save, b, uint16_t(battle_deck_place))
						: -1;
					if (e >= 0 && small_text) {
						const BattleDeckLayout deck(panel_layout.room, small_text->line_height());
						const BattleGraph& g = save.battles;
						const uint32_t index = g.get<uint32_t>(size_t(e), 0x4);
						const bool loaded = g.get<uint8_t>(size_t(e), 0x130) == 0 || g.get<uint8_t>(size_t(e), 0x131) == 0;
						if (inside(deck.fire, tx, ty) && loaded && index < save.ships.size() &&
							save.ships[index].memory.s32(0x120) != 0) {
							sailing->queue_fire_one(b, uint16_t(battle_deck_place));
						}
						const int sails = deck.sails_at(tx, ty);
						if (sails >= 0) {
							sailing->queue_sails_one(b, uint16_t(battle_deck_place), uint8_t(sails));
						}
					}
					break;
				}
				if (in_battle && !in_menu) {
					touch_on_panel = false;
					const int lx = int(event.tfinger.x * kScreenWidth);
					const int ly = int(event.tfinger.y * kScreenHeight);
					const size_t b = size_t(battle_shown);
					if (battle_hold) {
						// The held order let go where the finger is (0x608550 with 0: the step stops sending it).
						const auto p = battle_point(lx, ly);
						sailing->fn_608550(b, p[0], p[1], false);
						battle_hold = false;
					} else if (!battle_long && touch_moved < kTapDistance) {
						// A tap: on a ship of his, it alone is chosen; on an enemy, the chosen ships attack it (0x93);
						// on the water, they go there (0x95: the PC's right button pressed and let go).
						bool own = false;
						const int place = battle_view->ship_at(touch_start.x, touch_start.y, own);
						if (place >= 0 && own) {
							battle_selection = {uint16_t(place)};
							sailing->set_battle_selection(b, battle_selection);
						} else if (place >= 0 && !battle_selection.empty()) {
							sailing->fn_608360(b, uint16_t(place));
							SDL_Point at;
							if (p3::app::BattleView::ship_point(save, b, uint16_t(place), at)) {
								battle_view->mark_order(at.x, at.y, app_ticks());
							}
						} else if (place < 0 && !battle_selection.empty()) {
							// At a town: on a tower, the chosen ships attack it (the PC's right button on its
							// building, 0x595564: 0x6016CB with its cell on the battle map); on another building
							// nothing (0x6016CB finds no tower there).
							const int ax = camera_x + touch_start.x, ay = camera_y + touch_start.y;
							const int tower = battle_town_index >= 0 ? p3::app::BattleView::tower_at(save, b, ax, ay) : -1;
							if (tower >= 0) {
								// The cell of the view where its building is drawn: the tower's own moved by the view's
								// origin (at a town 0x6016CB takes the map's ends off again). At a nest it doesn't
								// (it compares the view's cell with the nest's): on the map 253, whose file moves
								// the buildings by (1, 4), no tower is found, as on the PC; with fix_game_bugs() the
								// tower's own cell, so that it's found.
								const BattleGraph& g = save.battles;
								const SDL_Point o = battle_town.nest() && fix_game_bugs() ? SDL_Point{0, 0} : battle_town.origin();
								sailing->fn_6016cb(b, int32_t(g.get<uint8_t>(b, 0x1EAE + size_t(tower))) + o.x,
									int32_t(g.get<uint8_t>(b, 0x1EB2 + size_t(tower))) + o.y);
								battle_view->mark_order(g.get<uint16_t>(b, 0x1E9E + 2 * size_t(tower)),
									p3::app::BattleView::screen_y(g.get<uint16_t>(b, 0x1EA6 + 2 * size_t(tower))), app_ticks());
							} else if (battle_town_index < 0 || !view->building_at(ax, ay)) {
								const auto p = battle_point(touch_start.x, touch_start.y);
								sailing->fn_608550(b, p[0], p[1], true);
								sailing->fn_608550(b, p[0], p[1], false);
								battle_view->mark_order(ax, ay, app_ticks());
							}
						}
					}
					battle_long = false;
					break;
				}
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
						side.citizens = false;
						side.menu = PanelMenu::Statistics;
						side.folded = false;
						side.focus = -1;
						side.stats_page = 2;
						side.stats_entry = kStatisticsBalance;
						// The window's show (0x49D380) fills the competition's empty merchant slots.
						statistics_default_merchants(save, side.stats_merchants);
					statistics_default_towns(save, side.stats_towns);
						side.stats_choice_slot = -1;
					}
					break;
				}
				if (touch_moved < kTapDistance && !in_menu && !messages.open && !end_window.open && has_save &&
					save.player >= 0 && selection.mode == Mode::Saved && !trading && !in_hanse && !in_place &&
					(inside(g_people_button, touch_start.x, touch_start.y) ||
						inside(g_house_button, touch_start.x, touch_start.y))) {
					// The top bar's man: the citizens' window; its house: the competition's buildings shaded.
					touch_building = false;
					touch_on_side = false;
					touch_on_panel = false;
					if (inside(g_people_button, touch_start.x, touch_start.y)) {
						side.citizens = !side.citizens;
						if (side.citizens && side.menu == PanelMenu::Statistics) {
							side.menu = PanelMenu::None;
						}
					} else {
						side.shade_rivals = !side.shade_rivals;
					}
					break;
				}
				if (side.citizens && small_text && selection.mode == Mode::Saved && touch_moved < kTapDistance &&
					inside(CitizensLayout(small_text->line_height()).panel, touch_start.x, touch_start.y)) {
					// The citizens' window: its button closes it.
					touch_building = false;
					touch_on_side = false;
					touch_on_panel = false;
					if (inside(CitizensLayout(small_text->line_height()).close_button, touch_start.x, touch_start.y)) {
						side.citizens = false;
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
					const int slot = stats_choice_button(side.stats_entry) && inside(sl.periods[0], x, y) ? 4
						: stats_slot_buttons(side.stats_entry)											   ? sl.merchant_at(x, y)
																										   : -1;
					if (inside(sl.close_button, x, y)) {
						side.menu = PanelMenu::None;
					} else if (slot >= 0) {
						if (side.stats_choice_slot == slot) {
							side.stats_choice_slot = -1;
						} else {
							stats_open_choice(slot);
						}
					} else if (side.stats_choice_slot >= 0) {
						// A merchant's list: its sort button, a row, or closed by touching elsewhere.
						if (inside(sl.sort_button, x, y)) {
							side.stats_choice_by_key = !side.stats_choice_by_key;
							side.stats_choice_row = 0;
						} else if (inside(sl.choice, x, y)) {
							const int columns = sl.choice_columns(int(stats_choice_rows(save, side).size()));
							for (int r = 0; r < columns * sl.rows_per_column; ++r) {
								if (inside(sl.choice_row(r, columns), x, y)) {
									stats_choose(r);
									break;
								}
							}
						} else {
							side.stats_choice_slot = -1;
						}
					} else if (k >= 0 && stats_no_days(side.stats_entry)) {
						if (k >= 1 && (k < 3 || stats_years_shown(save))) {
							side.stats_graph_period = k;
						}
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
				if (side.route_drag >= 0 && small_text) {
					// A stop's town button let go (0x492EE0): its stop goes after the row before the first shown whose
					// top is at or below the button's (on top: it becomes the first; below them: after the last shown).
					const RoutePageLayout rl(panel_layout.room, small_text->line_height());
					const int stops = int(route_rows(save, side.ship).size());
					int target = std::min(stops, side.route_first + rl.rows) - 1;
					for (int k = 0; k < rl.rows && side.route_first + k < stops; ++k) {
						if (rl.top(k) >= side.route_drag_top) {
							target = side.route_first + k - 1;
							break;
						}
					}
					route_run(route_move_operation(save, side.ship, side.route_drag, target));
					side.route_drag = -1;
					touch_on_side = false;
					touch_on_panel = false;
					break;
				}
				if (side.stop_drag >= 0 && small_text) {
					// A ware's name let go: its row is the one under the name's middle.
					const StopWindowLayout sl(small_text->line_height());
					const int rel = side.stop_drag_top + sl.row_h / 2 - sl.rows_y;
					const int k = rel >= 0 ? rel / sl.row_h : -1 - (-rel - 1) / sl.row_h;
					const int to = std::clamp(side.stop_first + k, 0, kStopRows - 1);
					if (side.stop_window) {
						route_run(move_stop_row(*side.stop_window, side.stop_drag, to));
						side.stop_row = to;
					}
					side.stop_drag = -1;
					touch_on_side = false;
					touch_on_panel = false;
					break;
				}
				if (side.route_files && side.menu == PanelMenu::Ship && small_text) {
					// The route files' list: a file, the box, its buttons; a question's Yes and No; a tap elsewhere closes
					// it.
					if (touch_moved < kTapDistance) {
						const RouteFilesLayout fl(small_text->line_height());
						const int x = touch_start.x;
						const int y = touch_start.y;
						const int n = int(side.route_file_list.size());
						const int k = fl.row_at(x, y);
						if (side.route_file_question) {
							if (inside(fl.yes, x, y) || inside(fl.no, x, y)) {
								route_file_answer(inside(fl.yes, x, y));
							}
						} else if (!inside(fl.panel, x, y)) {
							route_file_end_typing();
							side.route_files = false;
						} else if (k >= 0 && side.route_file_first + k < n) {
							side.route_file_row = side.route_file_first + k;
							route_file_press(side.route_file_row, 0);
						} else {
							for (int c = 0; c < kRouteFileColumns; ++c) {
								if (inside(fl.cell(c), x, y)) {
									side.route_file_row = n;
									side.route_file_column = c;
									route_file_press(n, c);
									break;
								}
							}
						}
					}
					touch_building = false;
					touch_on_side = false;
					touch_on_panel = false;
					break;
				}
				if (side.route_towns_row >= 0 && side.menu == PanelMenu::Ship && small_text) {
					// The towns' list: a town (or «none»); a tap elsewhere closes it.
					if (touch_moved < kTapDistance) {
						const int n = int(route_town_entries(save, side.ship).size());
						const RouteTownsLayout tl(small_text->line_height(), n);
						const int e = tl.entry_at(touch_start.x, touch_start.y, n);
						if (e >= 0) {
							side.route_towns_pick = e;
							route_choose_town(e);
						} else {
							side.route_towns_row = -1;
						}
					}
					touch_building = false;
					touch_on_side = false;
					touch_on_panel = false;
					break;
				}
				if (side.stop_window && side.menu == PanelMenu::Ship && small_text && !touch_on_side) {
					// The wares window: its rows' cells and its bottom buttons (a − or + held has repeated already).
					if (touch_moved < kTapDistance) {
						const StopWindowLayout sl(small_text->line_height());
						int row = 0;
						int column = 0;
						bool hit = false;
						for (int b = 0; b < kStopBottomButtons && !hit; ++b) {
							if (inside(sl.bottom[b], touch_start.x, touch_start.y)) {
								side.stop_row = kStopRows;
								side.stop_column = b;
								stop_press(kStopRows, b);
								hit = true;
							}
						}
						if (!hit && stop_cell_at(touch_start.x, touch_start.y, row, column)) {
							side.stop_row = row;
							side.stop_column = column;
							if (stop_repeats == 0) {
								stop_press(row, column);
							}
						} else if (!hit) {
							const int k = sl.row_at(touch_start.y);
							if (k >= 0 && inside(sl.name(k), touch_start.x, touch_start.y)) {
								side.stop_row = side.stop_first + k;
							}
						}
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
					} else if (inside(panel_layout.map_switch, x, y)) {
						// The minimap's arrow (off on the sea map): the town's or the sea's minimap; the sea map
						// loads first if it isn't yet (as Triangle, with its notice, staying in the town).
						if (!sea_panel) {
							side.sea_minimap = !side.sea_minimap;
							if (side.sea_minimap && (!sea || !sea->ready()) && !sea_wanted) {
								sea_notice = tr("Cargando el mapa del mar...", "Loading the sea map...");
								notify(sea_notice);
								sea_wanted = true;
								sea_switch = false;
							}
						}
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
							stats_select(kStatisticsPageFirst[size_t(tab)]);
						} else if (row >= 0 && row < count && kStatsPageDone[side.stats_page]) {
							stats_select(first + row);
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
						} else if (side.ship_page == ShipPage::TradeRoute) {
							// The route page: «Activa», the scroll's arrows, a row's buttons.
							const RoutePageLayout rl(panel_layout.room, small_text->line_height());
							const int k = rl.row_at(y);
							const int r = k >= 0 ? side.route_first + k : -1;
							const int stops = int(route_rows(save, side.ship).size());
							if (inside(rl.files, x, y)) {
								side.route_row = -1;
								side.route_column = 0;
								route_press(-1, 0);
							} else if (inside(rl.active, x, y)) {
								side.route_row = -1;
								side.route_column = 1;
								route_press(-1, 1);
							} else if (inside(rl.up, x, y)) {
								side.route_first = std::max(0, side.route_first - 1);
							} else if (inside(rl.down, x, y)) {
								++side.route_first;  // Clamped next frame.
							} else if (r >= 0 && r < route_page_rows(save, side.ship)) {
								const int column = inside(r < stops ? rl.town(k) : rl.append(k), x, y) ? 0
									: inside(rl.wares(k), x, y)			 ? 1
									: inside(rl.mode(k), x, y)				 ? 2
																			 : -1;
								if (column >= 0 && (r < stops || column == 0)) {
									side.route_row = r;
									side.route_column = column;
									route_press(r, column);
								}
							}
						}
					} else if (sea_panel ? minimap_sea_point(panel_layout, *sea, x, y, at)
										 : !side.sea_minimap && minimap_town_point(panel_layout, *view, x, y, at)) {
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
					if (mail_answer(mail_layout.answer_at(x, y))) {
						// A button of the letter.
					} else if (inside(mail_layout.close_button, x, y)) {
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
					} else if (mail_answer(mail_layout.answer_at(x, y))) {
						// A button of the letter.
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
				// On the sea map, a tap on a battle's icon opens its window (0x4511FE → 0x611FC0).
				if (selection.mode == Mode::Sea && sea && touch_moved < kTapDistance && !(in_mail && touch_on_panel)) {
					for (const SeaBattleIcon& icon : battle_icons) {
						if (sea->battle_at(icon.x, icon.y, camera_x + touch_start.x, camera_y + touch_start.y)) {
							battle_info = icon.slot;
							break;
						}
					}
					if (battle_info >= 0) {
						break;
					}
				}
				// En el mapa del mar, tocar una ciudad la elige.
				if (selection.mode == Mode::Sea && sea && touch_moved < kTapDistance && !(in_mail && touch_on_panel)) {
					const int town = sea->town_at(save, camera_x + touch_start.x, camera_y + touch_start.y);
					if (town >= 0) {
						selection.saved_index = size_t(town);
					}
				}
				if (in_menu && touch_moved < kTapDistance) {
					menu_tap(touch_start.x, touch_start.y);
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
					save.player >= 0 && small_text && !in_menu && !in_mail && !messages.open && !end_window.open &&
					!on_town_event(touch_start.x, touch_start.y)) {
					// A tap in the town: a building to go into, as in the game, or one
					// of the player's own (demolish it or auction it); not on an event
					// symbol, which only shows its tooltip while the finger is on it.
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
				sea_switch = true;
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
		if (!trading && !in_hanse && !in_mail && !in_place && side.focus < 0 && side.menu == PanelMenu::None &&
			!battle_popup() && !in_battle) {  // Nothing moves under the battles' window.
			// (On the battle's screen the pad's up and down are the sails: part 2.)
			camera_x += kPadSpeed * (pad(SDL_CONTROLLER_BUTTON_DPAD_RIGHT) - pad(SDL_CONTROLLER_BUTTON_DPAD_LEFT));
			camera_y += kPadSpeed * (pad(SDL_CONTROLLER_BUTTON_DPAD_DOWN) - pad(SDL_CONTROLLER_BUTTON_DPAD_UP));
		}
		if (!trading && !in_hanse && !in_mail && !in_place) {
			const Uint8* keys = SDL_GetKeyboardState(nullptr);
			camera_x += kPadSpeed * (keys[SDL_SCANCODE_RIGHT] - keys[SDL_SCANCODE_LEFT]);
			camera_y += kPadSpeed * (keys[SDL_SCANCODE_DOWN] - keys[SDL_SCANCODE_UP]);
		}
		const bool at_sea = selection.mode == Mode::Sea && sea && !in_battle;
		int look_x = 0;
		int look_y = 0;
		if (script && !at_sea && !in_battle && script->look_at(look_x, look_y)) {
			camera_x = cell_screen_x(look_x, look_y) - town_width / 2;
			camera_y = cell_screen_y(look_y) - kScreenHeight / 2;
		}
		const int view_width = in_battle ? p3::app::BattleView::width(save, size_t(battle_shown))
			: at_sea                     ? sea->width()
										 : view->width();
		const int view_height = in_battle ? p3::app::BattleView::height(save, size_t(battle_shown))
			: at_sea                      ? sea->height()
										  : view->height();
		camera_x = std::clamp(camera_x, 0, std::max(0, view_width - town_width));  // The panel's part isn't the view's.
		camera_y = std::clamp(camera_y, 0, std::max(0, view_height - kScreenHeight));

		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
		SDL_RenderClear(renderer);
		const int course_ship = fleet_ship();
		if (in_battle) {
			// The sea battle the player directs: the water, the ships, the balls and the clouds; over each ship
			// its label (its name; its sailors, hull and guns).
			std::vector<p3::app::BattleLabel> labels;
			size_t town_cursor = view->first_object();
			p3::app::BattleTownLayer town_layer = battle_town_layer;
			town_layer.ground = [&](int x, int y) { view->draw_ground(x, y, true); };
			town_layer.objects = [&](int x, int y, int last_row) { view->draw_objects(town_cursor, last_row, x, y); };
			battle_view->draw(save, size_t(battle_shown), camera_x, camera_y, 0, town_width, kScreenHeight, app_ticks(),
				battle_selection, labels, battle_town_index >= 0 ? &town_layer : nullptr);
			if (small_text && panel_art) {
				for (const p3::app::BattleLabel& label : labels) {
					draw_battle_label(renderer, *small_text, *panel_art, label, battle_view->arrow());
				}
			}
			if (battle_panel) {
				draw_battle_panel(renderer, *small_text, *panel_art, side, save, size_t(battle_shown), *battle_view,
					battle_town_index >= 0 ? view->minimap() : nullptr,
					{camera_x, camera_y, town_width, kScreenHeight}, battle_deck_place, battle_bits, battle_any_ship,
					battle_button_down < kBattleButtons ? battle_button_down : -1, battle_button_down == kBattleButtons,
					battle_button_down == kBattleLeavePressed);
				if (battle_button_down == kBattleLeavePressed) {
					// The game's tooltip of that icon is «Weigh anchor» (20026), which misleads here: the viewer's own.
					draw_tooltip(renderer, *small_text, tr("Dejar la batalla al ordenador", "Leave the battle to the computer"),
						panel_layout.icons[kBattleLeaveIcon]);
				}
				if (battle_button_down >= 0 && battle_button_down < kBattleButtons) {
					// Its button's tooltip under the finger (the scripts' ToolTip).
					const BattleButtonState st = battle_button_state(BattleButton(battle_button_down), battle_bits, true);
					draw_tooltip(renderer, *small_text, tooltip_text(st.tooltip), panel_layout.icons[battle_button_down]);
				}
			}
			// Method +0x20 (0x60EDC6): once the order to go is let go ([+0x12E0] 0), each chosen ship's line is
			// cleared after this frame ([+0xAE0 + 8 · i] = −1).
			{
				BattleGraph& g = save.battles;
				const size_t b = size_t(battle_shown);
				if (g.get<int32_t>(b, 0xAE0) != -1 && g.get<uint8_t>(b, 0x12E0) == 0) {
					for (uint16_t i = 0; i < g.get<uint16_t>(b, 0xAD0) && i < 0xF0; ++i) {
						g.set<int32_t>(b, 0xAE0 + 8 * size_t(i), -1);
					}
				}
			}
			if (battle_view->missing_looks() && small_text) {
				// A ship's graphics still loading at the start (a sheet a frame): the notice. Later looks (a new hull
				// state) load under the old one, without it.
				const std::string line = tr("Cargando la batalla...", "Loading the battle...");
				const int w = small_text->measure(line) + 16;
				const SDL_Rect box = {town_width / 2 - w / 2, kScreenHeight / 2 - 16, w, small_text->line_height() + 8};
				draw_panel_box(renderer, box);
				small_text->draw(line, box.x + 8, box.y + 4, kTextColor);
			}
		} else if (at_sea) {
			sea->draw(save, small_text.get(), town_names, camera_x, camera_y, selection.saved_index,
				course_ship >= 0 ? &save.ships[size_t(course_ship)] : nullptr);
			for (const SeaBattleIcon& icon : battle_icons) {
				sea->draw_battle(icon.x, icon.y, camera_x, camera_y);
			}
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
					draw_route_towns(renderer, *small_text, save, side);
					draw_route_files(renderer, *small_text, side);
					draw_stop_window(renderer, *small_text, *panel_art, save, side);
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
			view->draw(camera_x, camera_y, pointed, side.shade_rivals && save.player >= 0 ? save.player : -1,
				int(save.merchant_count));
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
			if (selection.mode == Mode::Saved && has_save && panel_art) {
				// The town's event symbols (0x58BF3A): one for each bit of its [+0x2C8] that is set, at the top right.
				draw_town_events(renderer, panel_art->events, save.towns[size_t(town_index)].memory.u32(0x2C8), town_width);
			}
			if (panel_shown) {
				draw_side_panel(renderer, *small_text, *panel_art, side, panel_state(save), view.get(),
					{camera_x, camera_y, town_width, kScreenHeight}, sea && sea->ready() ? sea.get() : nullptr, &save);
				if (side.menu == PanelMenu::ShipList && !side.folded) {
					draw_ship_list(renderer, *small_text, save, town_names, side);
				}
				if (side.menu == PanelMenu::Build && !side.folded) {
					draw_build_room(renderer, *small_text, *panel_art, side, build_now, build_tabs_now,
						build_choice_now ? &*build_choice_now : nullptr, side.cell_x >= 0, build_fits_now);
				}
				if (side.menu == PanelMenu::Ship && side.ship >= 0 && !side.folded) {
					draw_ship_menu(renderer, *small_text, *panel_art, save, side, ship_now, ship_pirate_ok);
					draw_route_towns(renderer, *small_text, save, side);
					draw_route_files(renderer, *small_text, side);
					draw_stop_window(renderer, *small_text, *panel_art, save, side);
				}
				if (side.menu == PanelMenu::Statistics && !side.folded) {
					draw_stats_room(renderer, *small_text, *panel_art, side);
					draw_stats_window(renderer, *small_text, *panel_art, save, town_names, side);
				}
			}
			if (side.citizens && selection.mode == Mode::Saved && has_save && small_text && panel_art && save.player >= 0) {
				draw_citizens_window(renderer, *small_text, *panel_art, save, uint32_t(selection.saved_index));
			}
		}
		if (text) {
			std::vector<std::string> right_texts;
			if (battle_popup() || messages.open || end_window.open) {
				// A window over the screen (a battle's, a message, the end): its buttons say what the keys do now,
				// so the bar's actions, which the keys don't do while it is open, aren't shown.
			} else if (side.menu == PanelMenu::Ship && panel_shown && !side.folded && !in_mail && !in_menu &&
				(side.stop_window || side.route_towns_row >= 0 || side.route_files)) {
				// So do the trade route page's windows.
			} else if (in_battle) {
				// The battle's screen: its own keys.
				right_texts = {tr(BTN_CROSS " disparar   " BTN_TRIANGLE " todos   L/R: barco   arriba/abajo: velas",
								   BTN_CROSS " fire   " BTN_TRIANGLE " all   L/R: ship   up/down: sails"),
					tr(BTN_CROSS " disparar   " BTN_TRIANGLE " todos", BTN_CROSS " fire   " BTN_TRIANGLE " all")};
			} else if (side.menu == PanelMenu::Statistics && panel_shown && !side.folded && !in_mail && !in_menu &&
				side.stats_choice_slot >= 0) {
				right_texts = {tr(BTN_CROSS " elegir   " BTN_SQUARE " orden   " BTN_CIRCLE " volver",
								   BTN_CROSS " choose   " BTN_SQUARE " order   " BTN_CIRCLE " back"),
					tr(BTN_CIRCLE " volver", BTN_CIRCLE " back")};
			} else if (side.menu == PanelMenu::Statistics && panel_shown && !side.folded && !in_mail && !in_menu &&
				(stats_slot_buttons(side.stats_entry) || stats_choice_button(side.stats_entry))) {
				const int entry = side.stats_entry;
				right_texts = {stats_competition(entry)
						? tr("L/R: periodo   " BTN_SQUARE " " BTN_CROSS " comerciante   " BTN_CIRCLE " cerrar",
							  "L/R: period   " BTN_SQUARE " " BTN_CROSS " merchant   " BTN_CIRCLE " close")
						: stats_town_data_wares(entry)
						? tr("L/R: periodo   " BTN_SQUARE " " BTN_CROSS " mercancía   " BTN_CIRCLE " cerrar",
							  "L/R: period   " BTN_SQUARE " " BTN_CROSS " ware   " BTN_CIRCLE " close")
						: stats_town_data(entry)
						? tr("L/R: periodo   " BTN_CROSS " ciudad   " BTN_CIRCLE " cerrar",
							  "L/R: period   " BTN_CROSS " town   " BTN_CIRCLE " close")
						: tr("L/R: periodo   " BTN_SQUARE " " BTN_CROSS " ciudad   " BTN_CIRCLE " cerrar",
							  "L/R: period   " BTN_SQUARE " " BTN_CROSS " town   " BTN_CIRCLE " close"),
					tr(BTN_CIRCLE " cerrar", BTN_CIRCLE " close")};
			} else if (side.menu == PanelMenu::Statistics && panel_shown && !side.folded && !in_mail && !in_menu) {
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
					selection.mode == Mode::Saved
						? std::to_string(own_building_count(save, uint32_t(save.player), uint32_t(selection.saved_index)))
						: std::string(),
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
			MailLayout layout = in_panel ? MailLayout(small_text->line_height(), PanelLayout(side.folded).room)
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
			// The letter's buttons now.
			mail.answers.clear();
			if (mail.answered != entry) {
				mail.answered = 0xFFFF;
			}
			if (mail.tab != 0 && entry < save.mail.size() && mail.answered != entry) {
				for (const std::string& label : letter_buttons(save, g_texts, entry)) {
					mail.answers.push_back(clean_game_text(label));
				}
			}
			layout.with_answers(int(mail.answers.size()));
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
					// What the composer queues by itself (a letter 0x79 whose loan is no longer to decide).
					if (std::optional<OperationBytes> op = letter_shown_operation(save, entry);
						op && !operation_queued(save, *op)) {
						queue_operation(save, *op);
					}
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
		if (small_text && has_save && !in_menu && finger_down && touch_moved < kTapDistance && touch_start.x < town_width &&
			touch_start.y >= kBarHeight) {
			// A town's event symbol's tooltip, under the finger: in the town view (0x4A6290), on the sea map (0x452973).
			SDL_Rect where{};
			std::string tip;
			if (selection.mode == Mode::Saved) {
				tip = town_event_tooltip(town_event_at(save.towns[size_t(town_index)].memory.u32(0x2C8), town_width,
					touch_start.x, touch_start.y, where));
			} else if (selection.mode == Mode::Sea && sea && sea->ready()) {
				tip = sea->symbol_tooltip_at(save, camera_x + touch_start.x, camera_y + touch_start.y, camera_x, camera_y,
					where);
			}
			draw_tooltip(renderer, *small_text, tip, where);
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
		if (text && small_text) {
			// The battles' window over the others: the question (or an end), else a battle icon's window.
			if (battle_window_up()) {
				draw_choice_box(renderer, *text, *small_text, battle_window_box());
			} else if (battle_info >= 0) {
				draw_choice_box(renderer, *text, *small_text, battle_info_box());
			} else if (battle_leave) {
				draw_choice_box(renderer, *text, *small_text, battle_leave_box());
			}
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
	start_screen.reset();
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
