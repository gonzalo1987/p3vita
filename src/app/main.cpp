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
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "p3data/aim.hpp"
#include "p3data/cpr.hpp"
#include "p3data/font.hpp"
#include "p3data/hanse_tasks.hpp"
#include "p3data/image.hpp"
#include "p3data/navigation.hpp"
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

using namespace p3::data;
using p3::app::InputScript;

namespace {

// With --steps the viewer runs without a window and its time is the script's
// (one frame, 1/60 s), so that every run comes out the same.
const InputScript* g_script = nullptr;

Uint32 app_ticks() {
	return g_script ? g_script->now() : SDL_GetTicks();
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

// `cpr_file` (--cpr): the English archive itself; the language one is mounted
// if it is in the same folder.
std::unique_ptr<CprArchive> open_archive(const char* dir_override, const char* cpr_file, std::string& tried) {
	if (cpr_file) {
		tried += std::string("\n") + cpr_file;
		auto archive = CprArchive::open(cpr_file);
		if (archive) {
			const std::string path = cpr_file;
			const size_t slash = path.find_last_of('/');
			const std::string dir = slash == std::string::npos ? "" : path.substr(0, slash + 1);
			if (path.substr(dir.size()) != kLanguageArchive) {
				archive->mount(dir + kLanguageArchive);  // Optional.
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
			archive->mount(dir + kLanguageArchive);  // Opcional.
			return archive;
		}
		// Sin la base, el archivo español solo también sirve para ver las ciudades.
		if (auto archive = CprArchive::open(dir + kLanguageArchive)) {
			return archive;
		}
	}
	return nullptr;
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

	int measure(const std::string& text) const { return font_->measure(text); }
	int line_height() const { return font_->line_height(); }

	void draw(const std::string& text, int x, int y, SDL_Color color) {
		SDL_SetTextureColorMod(texture_, color.r, color.g, color.b);
		for (const FontAtlas::Quad& q : font_->layout(text, x, y)) {
			const SDL_Rect src = {q.src_x, q.src_y, q.width, q.height};
			const SDL_Rect dst = {q.dst_x, q.dst_y, q.width, q.height};
			SDL_RenderCopy(renderer_, texture_, &src, &dst);
		}
	}

private:
	SDL_Renderer* renderer_;
	std::unique_ptr<FontAtlas> font_;
	SDL_Texture* texture_ = nullptr;
};

// Mapa de una ciudad listo para dibujar: los trozos de sprite con su textura.
class TownView {
public:
	TownView(SDL_Renderer* renderer, const CprArchive& archive)
		: renderer_(renderer), archive_(archive), tiles_(load_tileset(archive)) {}

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
		for (const TownSprite& sprite : build_town_sprites(town, tiles_)) {
			SDL_Texture* texture = atlas_texture(sprite.tile->atlas);
			if (!texture) {
				continue;
			}
			items_.push_back({texture, {sprite.src_x, sprite.src_y, sprite.width, sprite.height},
				{sprite.dst_x, sprite.dst_y, sprite.width, sprite.height}});
			width_ = std::max(width_, sprite.dst_x + sprite.width);
			height_ = std::max(height_, sprite.dst_y + sprite.height);
		}
	}

	// Dibuja lo que cae dentro de la pantalla con la cámara en (camera_x, camera_y).
	void draw(int camera_x, int camera_y) {
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
	}

	int width() const { return width_; }
	int height() const { return height_; }

private:
	struct Item {
		SDL_Texture* texture;
		SDL_Rect src;
		SDL_Rect dst;
	};

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

// Mapa del mar: la imagen del juego con las ciudades y los barcos de la
// partida, en las mismas coordenadas que guarda la partida.
class SeaMapView {
public:
	SeaMapView(SDL_Renderer* renderer, const CprArchive& archive)
		: renderer_(renderer), towns_(load_sea_map_towns(archive)) {
		map_ = load_texture(renderer, archive, kSeaMapImage, &width_, &height_);
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
	SDL_Renderer* renderer_;
	std::vector<SeaMapTown> towns_;
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
		index_ = index;
		loaded_ = false;
		if (index >= save.towns.size()) {
			error = "the save has no town " + std::to_string(index);
			return false;
		}
		loaded_ = load_saved_town_template(archive, save.towns[index], regions, base_, &error);
		return loaded_;
	}

	// The town as it is now.
	void draw(const SaveGame& save, const TileSet& tiles, TownMap& out) {
		place_saved_town(base_, save, index_, tiles, out);
		key_ = drawn_buildings_key(save.towns[index_]);
	}

	// Whether its buildings have changed since the last draw.
	bool changed(const SaveGame& save) const {
		return loaded_ && drawn_buildings_key(save.towns[index_]) != key_;
	}

private:
	SavedTownTemplate base_;
	size_t index_ = 0;
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

// Monedas con separador de miles: 30.000.
std::string format_money(int64_t value) {
	std::string digits = std::to_string(value < 0 ? -value : value);
	for (int i = int(digits.size()) - 3; i > 0; i -= 3) {
		digits.insert(size_t(i), ".");
	}
	return value < 0 ? "-" + digits : digits;
}

constexpr const char* kMonthNames[12] = {"enero", "febrero", "marzo", "abril", "mayo", "junio", "julio", "agosto",
	"septiembre", "octubre", "noviembre", "diciembre"};

// «13 de mayo»
std::string day_and_month(int day, int month) {
	return std::to_string(day) + " de " + kMonthNames[month % 12];
}

std::string town_name(const SaveGame& save, const std::vector<std::string>& names, int town) {
	const int id = save.towns[size_t(town)].town_id();
	return id < int(names.size()) ? names[size_t(id)] : "Ciudad " + std::to_string(id);
}

std::string bar_title(const SaveGame& save, const std::vector<std::string>& names,
	const std::vector<int>& template_ids, const Selection& selection) {
	if (selection.mode == Mode::Saved || selection.mode == Mode::Sea) {
		const std::string date = day_and_month(save.day(), save.month()) + " de " + std::to_string(save.year());
		const std::string name = town_name(save, names, int(selection.saved_index));
		if (selection.mode == Mode::Sea) {
			return date + " - Mapa del mar - " + name;
		}
		return date + " - " + name + " - " + format_money(save.towns[selection.saved_index].citizens()) +
			" habitantes";
	}
	return "Plantilla " + std::to_string(template_ids[selection.template_index]) +
		(selection.mode == Mode::Designer ? " - todos los edificios del diseñador" : " - solo el terreno");
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
	Uint32 ms_per_tick;
	int max_ticks;
};
constexpr GameSpeed kGameSpeeds[] = {
	{"ralentizada", 3515, 8},  // La de la tecla Pausa del juego.
	{"muy lenta", 468, 8},
	{"lenta", 351, 8},
	{"normal", 234, 8},  // Un día por minuto.
	{"rápida", 117, 8},
	{"muy rápida", 78, 8},
	{"avance rápido", 2, 256},  // F12 del juego.
};
constexpr int kGameSpeedCount = int(sizeof(kGameSpeeds) / sizeof(kGameSpeeds[0]));
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
		return "en el puerto de " + here;
	case kShipWaiting:
		return "esperando en " + here;
	case kShipLeaving:
		return "saliendo de " + here + (there.empty() ? "" : " hacia " + there);
	case kShipArriving:
		return "entrando en el puerto de " + here;
	case kShipSailing: {
		if (!ship.route) {
			return "sin rumbo en el mar";
		}
		std::string text = there.empty() ? "navegando" : "navegando hacia " + there;
		if (ship.arrival_tick() > save.tick()) {
			const GameDate date = date_from_tick(ship.arrival_tick());
			text += ", llegada prevista el " + day_and_month(date.day, date.month);
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
		return one ? "barril" : "barriles";
	case WareUnit::Load:
		return one ? "carga" : "cargas";
	default:
		return one ? "pieza" : "piezas";
	}
}

std::string amount_text(int32_t internal, WareUnit unit) {
	const int n = units(internal, unit);
	return std::to_string(n) + " " + unit_name(unit, n);
}

// Lo que se compra o vende de cada vez, en barriles o cargas; 0 = todo lo posible.
constexpr int kTradeSteps[] = {1, 5, 10, 0};
constexpr int kTradeStepCount = int(sizeof(kTradeSteps) / sizeof(kTradeSteps[0]));
constexpr Uint32 kMessageTime = 4000;  // Milisegundos que se ve el resultado de una operación.

constexpr const char* kShipTypes[] = {"Snaikka", "Craier", "Kogge", "Holk"};

// Estado del panel de mercado.
struct MarketUi {
	bool open = false;
	int ware = 0;  // Fila seleccionada.
	int step = 0;  // Índice en kTradeSteps.
	size_t ship = 0;  // Entre los barcos del jugador atracados en la ciudad.
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

int32_t trade_amount(const SaveGame& save, int ship, const MarketUi& ui, bool buying) {
	const WareUnit unit = kWares[size_t(ui.ware)].unit;
	const int step = kTradeSteps[ui.step];
	if (step > 0) {
		return step * ware_scale(unit);
	}
	// Todo: el juego ya limita la compra al almacén, la bodega y el dinero.
	return buying ? 1000 * 1000 * 1000 : save.ships[size_t(ship)].cargo(size_t(ui.ware));
}

std::string step_text(const MarketUi& ui) {
	const int step = kTradeSteps[ui.step];
	return step > 0 ? std::to_string(step) + " " + unit_name(kWares[size_t(ui.ware)].unit, step) : "todo";
}

void trade(SaveGame& save, int town, MarketUi& ui, bool buying) {
	const int ship = market_ship(save, town, ui);
	const WareInfo& ware = kWares[size_t(ui.ware)];
	std::string name = ware.name;
	name[0] = char(std::tolower(uint8_t(name[0])));
	ui.message_until = app_ticks() + kMessageTime;
	if (ship < 0) {
		ui.message = "No tienes ningún barco en este puerto.";
		return;
	}
	// Sin mercado los precios del juego son 0: no se deja comerciar.
	if (!save.towns[size_t(town)].has_market()) {
		ui.message = "Esta ciudad no tiene mercado.";
		return;
	}
	const int32_t amount = trade_amount(save, ship, ui, buying);
	const TradeResult result = buying ? buy_to_ship(save, ship, town, ui.ware, amount)
									  : sell_from_ship(save, ship, town, ui.ware, amount, save.trade_difficulty);
	const SavedTown& market = save.towns[size_t(town)];
	if (result.amount > 0) {
		ui.message = std::string(buying ? "Compra: " : "Venta: ") + amount_text(result.amount, ware.unit) + " de " +
			name + " por " + format_money(result.price) + " monedas.";
	} else if (!buying) {
		ui.message = "No llevas " + name + " en la bodega.";
	} else if (int32_t(market.stock(size_t(ui.ware))) < ware_scale(ware.unit)) {
		ui.message = "La ciudad no tiene " + name + ".";
	} else if (save.merchants[size_t(save.player)].money() <
		buy_price(ui.ware, market.market(ui.ware), ware_scale(ware.unit))) {
		ui.message = "No tienes dinero suficiente.";
	} else {
		ui.message = "No queda sitio en la bodega.";
	}
}

// Disposición del panel, compartida por el dibujo y el táctil.
struct MarketLayout {
	static constexpr int kWidth = 560;
	static constexpr int kPadding = 10;
	static constexpr int kRowHeight = 19;
	static constexpr int kGap = 8;
	static constexpr int kColumns[4] = {250, 320, 390, 548};  // Bordes derechos de las columnas.
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
void draw_market(SDL_Renderer* renderer, TextRenderer& text, const SaveGame& save, int town_index,
	const MarketUi& ui) {
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
		char hold[96];
		std::snprintf(hold, sizeof(hold), ": %.1f de %.1f cargas", ship->used() / 2000.0,
			(ship->capacity() - ship->equipment()) / 2000.0);
		text.draw(ship->name() + " (" + kShipTypes[ship->type() & 3] + ")" + hold, left, layout.header_y, kTextColor);
	} else {
		text.draw("No tienes ningún barco en este puerto.", left, layout.header_y, kDimColor);
	}
	const std::string close = "Círculo: cerrar";
	text.draw(close, panel.x + panel.w - MarketLayout::kPadding - text.measure(close), layout.header_y, kDimColor);

	text.draw("Mercancía", left, layout.titles_y, kDimColor);
	right("Ciudad", 0, layout.titles_y, kDimColor);
	right("Compra", 1, layout.titles_y, kDimColor);
	right("Venta", 2, layout.titles_y, kDimColor);
	right("Bodega", 3, layout.titles_y, kDimColor);
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
		const char* short_unit = ware.unit == WareUnit::Barrel ? " barr." : " cargas";
		text.draw(ware.name, left, y, kTextColor);
		right(std::to_string(units(int32_t(town.stock(size_t(w))), ware.unit)) + short_unit, 0, y, kTextColor);
		right(buy ? std::to_string(buy) : "-", 1, y, kTextColor);
		right(std::to_string(sell), 2, y, kTextColor);
		if (ship) {
			const int n = units(ship->cargo(size_t(w)), ware.unit);
			right(n ? std::to_string(n) + short_unit : "-", 3, y, n ? kTextColor : kDimColor);
		}
	}

	// El resultado de la última operación o, si no, lo que costaría comerciar
	// ahora con la cantidad elegida.
	const bool show_message = !ui.message.empty() && !SDL_TICKS_PASSED(app_ticks(), ui.message_until);
	std::string footer = show_message ? ui.message : "";
	if (!show_message && ship) {
		const TradeResult buy =
			quote_buy(save, ship_index, town_index, ui.ware, trade_amount(save, ship_index, ui, true));
		const TradeResult sell =
			quote_sell(save, ship_index, town_index, ui.ware, trade_amount(save, ship_index, ui, false),
				save.trade_difficulty);
		footer = buy.amount > 0 ? "Comprar " + amount_text(buy.amount, selected_unit) + ": " +
				format_money(buy.price) + " monedas"
								: "No se puede comprar";
		if (sell.amount > 0) {
			footer += "    vender " + std::to_string(units(sell.amount, selected_unit)) + ": " + format_money(sell.price);
		}
	}
	text.draw(footer, left, layout.footer_y, kTextColor);
	draw_button(renderer, text, layout.step_button, "Izq./Der.: " + step_text(ui));
	if (ship) {
		draw_button(renderer, text, layout.buy_button, "X: comprar");
		draw_button(renderer, text, layout.sell_button, "Cuadrado: vender");
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
	int row = 0;  // Selected task or material.
	int amount = 0;  // Index in kRoadAmounts.
	std::string message;
	Uint32 message_until = 0;
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

	int rows() const { return task != 0xFFFF ? (road_here ? 4 : 0) : int(offered.size()); }
};

HanseView hanse_view(const SaveGame& save, int town) {
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
	return v;
}

// Name of a town by its number in StadtDaten.ini (not its index in the save).
std::string numbered_town_name(const std::vector<std::string>& names, int number) {
	return number >= 0 && number < int(names.size()) ? names[size_t(number)] : "la ciudad " + std::to_string(number);
}

std::string full_date(uint32_t tick) {
	const GameDate date = date_from_tick(tick);
	return day_and_month(date.day, date.month) + " de " + std::to_string(date.year);
}

// One line for a task: what to do. The game's descriptions (one per type,
// «An overland trade route is to be established between two towns...») are
// longer and in the executable.
std::string hanse_task_text(const SaveGame& save, const HanseTask& t, const std::vector<std::string>& town_names) {
	auto index_name = [&](int town) {
		return town < int(save.towns.size()) ? town_name(save, town_names, town) : "la ciudad " + std::to_string(town);
	};
	switch (HanseTaskType(t.type)) {
	case HanseTaskType::FoundTown:
		return "Fundar una ciudad nueva: " + numbered_town_name(town_names, t.data[4]);
	case HanseTaskType::Road:
		return "Carretera entre " + index_name(t.data[0]) + " y " + index_name(t.data[1]);
	case HanseTaskType::PirateShip: {
		const size_t ship = size_t(t.data[0] | t.data[1] << 8);
		return ship < save.ships.size() ? "Cazar el barco pirata " + save.ships[ship].name()
										: "Cazar un barco pirata";
	}
	case HanseTaskType::PirateNest:
		return "Acabar con la guarida pirata de " + numbered_town_name(town_names, t.data[1]);
	case HanseTaskType::GrowTown:
		return "Que " + index_name(t.data[2]) + " llegue a " + format_money(t.data[0] | t.data[1] << 8) +
			" habitantes";
	default:
		return "Encargo anulado";
	}
}

// How the player's task is going.
std::string hanse_progress_text(const SaveGame& save, const HanseTask& t, const HanseView& v,
	const std::vector<std::string>& town_names) {
	switch (HanseTaskType(t.type)) {
	case HanseTaskType::FoundTown:
		return t.started && t.data[6] < save.towns.size()
			? "Se cumple cuando " + town_name(save, town_names, t.data[6]) + " tenga mercado."
			: "La ciudad se funda al empezar el encargo.";
	case HanseTaskType::Road:
		return "Tramos hechos: " + std::to_string(v.road.sections - v.road.sections_left) + " de " +
			std::to_string(v.road.sections) + " (" + std::to_string(v.road.percent()) + " %)." +
			(v.road_here || t.data[2] == 0 ? "" : " Lleva los materiales a una de las dos ciudades.");
	case HanseTaskType::PirateShip:
		return "Hunde el barco pirata (las batallas aún no están en p3vita).";
	case HanseTaskType::PirateNest:
		return t.data[2] == 0 ? "Acerca un barco a la guarida." : "Ataca la guarida (las batallas aún no están).";
	case HanseTaskType::GrowTown:
		return t.data[2] < save.towns.size() ? "Habitantes ahora: " + format_money(save.towns[t.data[2]].citizens()) + "."
											 : "";
	default:
		return "";
	}
}

std::string road_amount_text(int amount) {
	const uint32_t n = kRoadAmounts[size_t(amount)];
	return n == 0xFFFFFFFF ? "todo" : std::to_string(n);
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

// X or the act button: take the selected task (0x4B) or give the selected
// material (0x3B), as the game's windows do. The executor runs it after the
// next tick, as the game runs its queue between two calls to advance_time.
void hanse_act(SaveGame& save, int town, HanseUi& ui, const HanseView& v) {
	ui.message_until = app_ticks() + kMessageTime;
	if (v.task == 0xFFFF && ui.row < int(v.offered.size())) {
		queue_operation(save, take_task_operation(uint32_t(save.player), v.offered[size_t(ui.row)]));
		ui.message = "Has aceptado el encargo.";  // The game: «Many thanks for taking on this important task.»
		ui.row = 0;
		return;
	}
	if (v.road_here && ui.row < 4) {
		const int k = ui.row;
		const uint16_t needed = v.road.needed(k);
		if (needed == 0 || v.available[size_t(k)] <= 0) {
			ui.message = needed == 0 ? "Ya no hace falta más." : "No tienes en esta ciudad.";
			return;
		}
		const uint16_t amount = road_window_amount(kRoadAmounts[size_t(ui.amount)], uint32_t(v.available[size_t(k)]),
			needed);
		queue_operation(save, road_materials_operation(v.task, amount, uint8_t(town), uint8_t(save.player),
			kRoadWares[size_t(k)]));
		const WareInfo& ware = kWares[kRoadWares[size_t(k)]];
		std::string name = ware.name;
		name[0] = char(std::tolower(uint8_t(name[0])));
		ui.message = "Entregas " + std::to_string(amount) + " " + unit_name(ware.unit, amount) + " de " + name + ".";
		return;
	}
	ui.message = v.level == 0 ? "Aún no eres candidato." : "No hay nada que hacer aquí.";
}

// The Alderman: a merchant, or a citizen of the town [0x6DE52E].
std::string alderman_name(const SaveGame& save, const NameLists& names, const std::vector<std::string>& town_names) {
	const int chief = alderman(save);
	if (chief < save.merchant_count && chief < int(save.merchants.size())) {
		return merchant_name(save, names, chief);
	}
	const int town = save.globals.u8(0x6DE52E);
	return town < int(save.towns.size()) ? "un ciudadano de " + town_name(save, town_names, town) : "un ciudadano";
}

void draw_hanse(SDL_Renderer* renderer, TextRenderer& text, const SaveGame& save, const HanseUi& ui,
	const HanseView& v, const std::vector<std::string>& town_names, const NameLists& names) {
	const HanseLayout layout(text.line_height());
	const SDL_Rect& panel = layout.panel;
	const int left = panel.x + HanseLayout::kPadding;
	const int lh = text.line_height();
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(renderer, 30, 22, 14, 225);
	SDL_RenderFillRect(renderer, &panel);
	SDL_SetRenderDrawColor(renderer, 150, 120, 70, 255);
	SDL_RenderDrawRect(renderer, &panel);

	text.draw("La Hansa", left, layout.header_y, kTextColor);
	const std::string close = "Círculo: cerrar";
	text.draw(close, panel.x + panel.w - HanseLayout::kPadding - text.measure(close), layout.header_y, kDimColor);

	// The Alderman, the election and the player's candidacy.
	std::vector<std::pair<std::string, SDL_Color>> info;
	info.push_back({"Gobernador: " + alderman_name(save, names, town_names), kTextColor});
	if (const uint32_t election = alderman_election_tick(save)) {
		info.push_back({"Próxima elección: " + full_date(election), kTextColor});
	}
	if (v.is_alderman) {
		info.push_back({"¡Eres Gobernador de la Hansa!", kTextColor});
	} else if (v.level == 0) {
		const std::vector<int> mayor = mayor_towns(save, save.player);
		info.push_back({"No eres candidato a Gobernador.", kDimColor});
		info.push_back({mayor.empty() ? "Hace falta ser alcalde de una ciudad." : "Alcalde de " +
			town_name(save, town_names, mayor[0]) + ".", kDimColor});
		info.push_back({"Reputación para optar: " + std::to_string(candidacy_reputation(save, save.player)) + " de " +
			std::to_string(kCandidacyReputation) + ".", kDimColor});
	} else if (v.level <= 3) {
		info.push_back({"Candidato a Gobernador: " + std::to_string(v.level - 1) + " de 3 encargos cumplidos.",
			kTextColor});
	} else {
		info.push_back({"Has cumplido los encargos: puedes presentarte a la elección.", kTextColor});
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
	if (v.task == 0xFFFF && !v.offered.empty()) {
		text.draw("Encargos de la Hansa (elige uno):", left, layout.title_y, kDimColor);
		for (size_t i = 0; i < v.offered.size(); ++i) {
			const int y = layout.rows_y + int(i) * HanseLayout::kRowHeight;
			if (int(i) == ui.row) {
				row_box(int(i));
			}
			const HanseTask t = hanse_task(save, v.offered[i]);
			text.draw(hanse_task_text(save, t, town_names), left, y, kTextColor);
		}
		if (ui.row < int(v.offered.size())) {
			const HanseTask t = hanse_task(save, v.offered[size_t(ui.row)]);
			text.draw("Plazo: hasta el " + full_date(t.deadline) + ".", left, layout.detail_y, kTextColor);
		}
	} else if (v.task != 0xFFFF) {
		const HanseTask t = hanse_task(save, v.task);
		text.draw("Tu encargo: " + hanse_task_text(save, t, town_names), left, layout.title_y, kTextColor);
		if (v.road_here) {
			static constexpr int kColumns[2] = {330, 470};  // Right edges.
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
				text.draw(ware.name, left, y, enabled ? kTextColor : kDimColor);
				const char* unit = ware.unit == WareUnit::Barrel ? " barr." : " cargas";
				right("falta " + std::to_string(needed) + unit, 0, y, enabled ? kTextColor : kDimColor);
				right("tienes " + std::to_string(v.available[size_t(k)]) + unit, 1, y, enabled ? kTextColor : kDimColor);
			}
		}
		text.draw("Plazo: hasta el " + full_date(t.deadline) + ".", left, layout.detail_y, kTextColor);
		text.draw(hanse_progress_text(save, t, v, town_names), left, layout.detail_y + lh, kTextColor);
	}

	const bool show_message = !ui.message.empty() && !SDL_TICKS_PASSED(app_ticks(), ui.message_until);
	if (show_message) {
		text.draw(ui.message, left, layout.footer_y, kTextColor);
	}
	if (v.road_here) {
		draw_button(renderer, text, layout.amount_button, "Izq./Der.: " + road_amount_text(ui.amount));
		draw_button(renderer, text, layout.act_button, "X: entregar");
	} else if (v.task == 0xFFFF && !v.offered.empty()) {
		draw_button(renderer, text, layout.act_button, "X: aceptar el encargo");
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

// The command line (only on the PC: the Vita starts the viewer without one).
struct Options {
	const char* data_dir = nullptr;
	const char* cpr = nullptr;
	const char* steps = nullptr;
	const char* screenshot = nullptr;
	const char* debug_state = nullptr;
};

void print_usage() {
	std::printf(
		"usage: p3vita [DATA_DIR] [--cpr FILE] [--steps STEPS] [--screenshot FILE.png]\n"
		"              [--debug-state STATES]\n"
		"\n"
		"Shows the towns of Patrician III. DATA_DIR is the folder with p2arch0_eng.cpr\n"
		"(and p2arch0_spa.cpr for the Spanish version, optional); by default data/ and\n"
		"the current folder. --cpr gives the English archive itself.\n"
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

	std::string tried;
	auto archive = open_archive(options.data_dir, options.cpr, tried);
	const std::vector<int> template_ids = archive ? list_town_ids(*archive) : std::vector<int>{};
	if (template_ids.empty()) {
		const std::string message = archive
			? std::string("El archivo CPR no contiene mapas de ciudad.")
			: "No se encuentran los datos del juego. Copia p2arch0_eng.cpr (y p2arch0_spa.cpr para la "
			  "versión española) en:" + tried;
		show_error(window, message);
		SDL_Quit();
		return 1;
	}
	bool screenshot_failed = false;

	auto view = std::make_unique<TownView>(renderer, *archive);
	std::unique_ptr<SeaMapView> sea;  // Se carga la primera vez que se abre.
	const std::vector<std::string> town_names = load_town_names(*archive);
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
	SaveGame save;
	const bool has_save = load_savegame(*archive, kStartSave, save) && !save.towns.empty();
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
	const std::vector<int> regions = load_town_regions(*archive);
	const NameLists names = load_name_lists(*archive);
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
	}
	size_t fleet_index = 0;  // Barco del jugador al que se dan las órdenes en el mapa del mar.
	int game_speed = kNormalSpeed;
	Uint32 clock = app_ticks();  // Hasta dónde se ha convertido el tiempo real en ticks.
	// Aviso que se ve unos segundos bajo la barra superior.
	std::string notice;
	Uint32 notice_until = 0;
	auto notify = [&](const std::string& message) {
		notice = message;
		notice_until = app_ticks() + kMessageTime;
	};
	auto fleet_ship = [&]() -> int {
		const std::vector<int> fleet = player_fleet(save);
		return fleet.empty() ? -1 : fleet[fleet_index % fleet.size()];
	};
	// X en el mapa del mar: el barco elegido zarpa hacia la ciudad elegida (operación 0x00).
	auto order_sail = [&]() {
		const int index = fleet_ship();
		if (!sailing || index < 0) {
			notify("No tienes ningún barco.");
			return;
		}
		const SavedShip& ship = save.ships[size_t(index)];
		const int town = int(selection.saved_index);
		const std::string there = town_name(save, town_names, town);
		const Sailing::SailCheck check = ship.status() < 4 ? sailing->sail_check(ship) : Sailing::SailCheck::Ok;
		if ((ship.status() == kShipDocked || ship.status() == kShipWaiting) && ship.town() == town) {
			notify(ship.name() + " ya está en " + there + ".");
		} else if (check == Sailing::SailCheck::FewSailors) {
			notify("Faltan marineros para zarpar.");
		} else if (check == Sailing::SailCheck::Tired) {
			notify("La tripulación necesita descansar.");
		} else if (check == Sailing::SailCheck::Overloaded) {
			notify("Lleva más carga de la que cabe en la bodega.");
		} else if (check == Sailing::SailCheck::Damaged) {
			notify("El casco está demasiado dañado para zarpar sin capitán.");
		} else if (check == Sailing::SailCheck::PortClosed) {
			notify("El puerto está bloqueado.");
		} else if (check != Sailing::SailCheck::Ok) {
			notify(ship.name() + " no puede zarpar ahora.");
		} else {
			const char* verb = ship.status() == kShipSailing ? " pone rumbo a "
				: ship.status() == kShipLeaving && ship.town() == town ? " vuelve a "
				: " zarpa hacia ";
			if (!sailing->set_sail(index, town)) {
				notify(ship.name() + " no puede zarpar ahora.");
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
		// El mercado solo existe en las ciudades de la partida, con el jugador cargado.
		const bool trading = market.open && selection.mode == Mode::Saved && save.player >= 0 && small_text;
		const int town_index = int(selection.saved_index);
		// The Hanse panel, in the save's towns like the market (never both open).
		const bool in_hanse = hanse.open && !trading && selection.mode == Mode::Saved && save.player >= 0 && small_text;
		const HanseView hanse_now = in_hanse ? hanse_view(save, town_index) : HanseView();
		auto hanse_select = [&](int step) {
			const int rows = hanse_now.rows();
			hanse.row = rows > 0 ? (hanse.row + rows + step) % rows : 0;
		};
		auto change_amount = [&](int step) {
			const int count = int(kRoadAmounts.size());
			hanse.amount = (hanse.amount + count + step) % count;
		};
		auto open_hanse = [&](bool open) {
			hanse.open = open;
			hanse.row = 0;
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
				hanse_act(save, town_index, hanse, hanse_now);
				return true;
			case SDL_CONTROLLER_BUTTON_B:
			case SDL_CONTROLLER_BUTTON_X:
				open_hanse(false);
				return true;
			default:
				return false;
			}
		};
		auto select_ware = [&](int step) { market.ware = (market.ware + kTradeWareCount + step) % kTradeWareCount; };
		auto change_step = [&](int step) { market.step = (market.step + kTradeStepCount + step) % kTradeStepCount; };
		if (reload && selection.mode == Mode::Sea) {
			if (!sea) {
				show_loading(renderer);
				sea = std::make_unique<SeaMapView>(renderer, *archive);
			}
			if (!sea->ok()) {
				show_error(window, "No se encuentra el mapa del mar.");
				break;
			}
			const SDL_Point focus = sea->town_center(save, selection.saved_index);
			camera_x = focus.x - kScreenWidth / 2;
			camera_y = focus.y - kScreenHeight / 2;
			reload = false;
		}
		if (reload) {
			show_loading(renderer);
			TownMap town;
			std::string error;
			bool loaded = false;
			if (selection.mode == Mode::Saved) {
				loaded = live.load(*archive, save, selection.saved_index, regions, error);
				if (loaded) {
					live.draw(save, view->tiles(), town);
				}
			} else {
				loaded = load_template(*archive, template_ids, selection, town, error);
			}
			if (!loaded) {
				show_error(window, error);
				break;
			}
			view->show(town);
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
		if (sailing) {
			const GameSpeed& speed = kGameSpeeds[game_speed];
			const Uint32 now = app_ticks();
			const Uint32 elapsed = now - clock;
			clock = now - elapsed % speed.ms_per_tick;
			const int ticks =
				std::min(int(elapsed / speed.ms_per_tick), speed.max_ticks) + (script ? script->extra_ticks() : 0);
			ticks_run = ticks;
			for (int i = 0; i < ticks; ++i) {
				advance_game_tick(save, *sailing);
				for (const ShipEvent& event : sailing->events()) {
					const SavedShip& ship = save.ships[size_t(event.ship)];
					if (ship.owner() != save.player) {
						continue;
					}
					if (event.kind == ShipEvent::Arrived) {
						notify(ship.name() + " ha llegado a " + town_name(save, town_names, event.town) + ".");
						// En el mapa del mar queda elegida la ciudad: Triángulo entra en ella.
						if (selection.mode == Mode::Sea && event.ship == fleet_ship()) {
							selection.saved_index = size_t(event.town);
						}
					} else if (event.kind == ShipEvent::Stopped) {
						notify(ship.name() + " se ha detenido en el mar.");
					} else {
						notify(ship.name() + " se ha hundido.");
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
					notify(same ? "Sigues siendo Gobernador de la Hansa." : "¡Has sido elegido Gobernador de la Hansa!");
				} else {
					notify(std::string(same ? "Elección de Gobernador: sigue " : "Nuevo Gobernador de la Hansa: ") +
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
				if (in_hanse && hanse_button(event.cbutton.button)) {
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
				case SDL_CONTROLLER_BUTTON_A:  // X en la Vita: abre el mercado y compra; en el mar, zarpar.
					if (trading) {
						trade(save, town_index, market, true);
					} else if (selection.mode == Mode::Saved) {
						market.open = true;
						hanse.open = false;
					} else if (selection.mode == Mode::Sea) {
						order_sail();
					}
					break;
				case SDL_CONTROLLER_BUTTON_X:
					// Cuadrado: vende; en el mar, el siguiente barco del jugador; en la ciudad, the Hanse panel.
					if (trading) {
						trade(save, town_index, market, false);
					} else if (selection.mode == Mode::Sea) {
						fleet_index++;
					} else if (selection.mode == Mode::Saved) {
						open_hanse(true);
					}
					break;
				case SDL_CONTROLLER_BUTTON_B:  // Círculo: cierra el mercado.
					market.open = false;
					break;
				case SDL_CONTROLLER_BUTTON_BACK:
					// Select: siguiente barco del puerto, velocidad del juego en el mar o vista de plantilla.
					if (trading) {
						market.ship++;
					} else if (selection.mode == Mode::Sea) {
						game_speed = (game_speed + 1) % kGameSpeedCount;
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
				case SDL_CONTROLLER_BUTTON_START:
					running = false;
					break;
				}
				break;
			case SDL_KEYDOWN:
				if (in_hanse) {
					// Keyboard: the arrows, C or Enter to act, H or Escape to close.
					const SDL_Keycode key = event.key.keysym.sym;
					const Uint8 button = key == SDLK_UP ? SDL_CONTROLLER_BUTTON_DPAD_UP
						: key == SDLK_DOWN ? SDL_CONTROLLER_BUTTON_DPAD_DOWN
						: key == SDLK_LEFT ? SDL_CONTROLLER_BUTTON_DPAD_LEFT
						: key == SDLK_RIGHT ? SDL_CONTROLLER_BUTTON_DPAD_RIGHT
						: key == SDLK_c || key == SDLK_RETURN ? SDL_CONTROLLER_BUTTON_A
						: key == SDLK_h || key == SDLK_ESCAPE ? SDL_CONTROLLER_BUTTON_B
						: SDL_CONTROLLER_BUTTON_INVALID;
					if (button != Uint8(SDL_CONTROLLER_BUTTON_INVALID) && hanse_button(button)) {
						break;
					}
				}
				switch (event.key.keysym.sym) {
				case SDLK_h:
					if (selection.mode == Mode::Saved) {
						open_hanse(true);
					}
					break;
				case SDLK_PAGEUP:
					town_step = -1;
					break;
				case SDLK_PAGEDOWN:
					town_step = 1;
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
						trade(save, town_index, market, event.key.keysym.sym == SDLK_c);
					}
					break;
				case SDLK_TAB:
					market.ship++;
					fleet_index++;
					break;
				case SDLK_RETURN:
					if (selection.mode == Mode::Sea) {
						order_sail();
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
						running = false;
					}
					break;
				}
				break;
			case SDL_FINGERDOWN:
				touch_start = {int(event.tfinger.x * kScreenWidth), int(event.tfinger.y * kScreenHeight)};
				touch_moved = 0;
				touch_on_panel =
					(trading && inside(MarketLayout(small_text->line_height()).panel, touch_start.x, touch_start.y)) ||
					(in_hanse && inside(HanseLayout(small_text->line_height()).panel, touch_start.x, touch_start.y));
				break;
			case SDL_FINGERMOTION:
				touch_moved += std::abs(int(event.tfinger.dx * kScreenWidth)) +
					std::abs(int(event.tfinger.dy * kScreenHeight));
				// Arrastrar desplaza la ciudad con el dedo, salvo sobre el mercado.
				if (!touch_on_panel) {
					camera_x -= int(event.tfinger.dx * kScreenWidth);
					camera_y -= int(event.tfinger.dy * kScreenHeight);
				}
				break;
			case SDL_FINGERUP:
				// En el mapa del mar, tocar una ciudad la elige.
				if (selection.mode == Mode::Sea && sea && touch_moved < kTapDistance) {
					const int town = sea->town_at(save, camera_x + touch_start.x, camera_y + touch_start.y);
					if (town >= 0) {
						selection.saved_index = size_t(town);
					}
				}
				if (touch_on_panel && in_hanse && touch_moved < kTapDistance) {
					const HanseLayout layout(small_text->line_height());
					const int row = layout.row_at(touch_start.x, touch_start.y, hanse_now.rows());
					if (hanse_now.road_here && inside(layout.amount_button, touch_start.x, touch_start.y)) {
						change_amount(1);
					} else if (inside(layout.act_button, touch_start.x, touch_start.y)) {
						hanse_act(save, town_index, hanse, hanse_now);
					} else if (row >= 0) {
						hanse.row = row;
					}
				} else if (touch_on_panel && touch_moved < kTapDistance) {
					const MarketLayout layout(small_text->line_height());
					const int row = layout.row_at(touch_start.x, touch_start.y);
					if (inside(layout.step_button, touch_start.x, touch_start.y)) {
						change_step(1);
					} else if (inside(layout.buy_button, touch_start.x, touch_start.y)) {
						trade(save, town_index, market, true);
					} else if (inside(layout.sell_button, touch_start.x, touch_start.y)) {
						trade(save, town_index, market, false);
					} else if (row >= 0) {
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
			reload = true;
			continue;
		}
		if (next_mode && has_save) {
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
		if (controller) {
			camera_x += stick_speed(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX));
			camera_y += stick_speed(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY));
		}
		if (!trading && !in_hanse) {
			camera_x += kPadSpeed * (pad(SDL_CONTROLLER_BUTTON_DPAD_RIGHT) - pad(SDL_CONTROLLER_BUTTON_DPAD_LEFT));
			camera_y += kPadSpeed * (pad(SDL_CONTROLLER_BUTTON_DPAD_DOWN) - pad(SDL_CONTROLLER_BUTTON_DPAD_UP));
		}
		if (!trading && !in_hanse) {
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
			view->draw(camera_x, camera_y);
		}
		if (text) {
			std::vector<std::string> right_texts;
			if (trading || in_hanse) {
				const std::string money = format_money(save.merchants[size_t(save.player)].money()) + " monedas";
				right_texts = {merchant_name(save, names, save.player) + ": " + money, money};
			} else if (at_sea) {
				right_texts = {"L/R: ciudad   X: zarpar   Triángulo: entrar", "X: zarpar   Triángulo: entrar"};
			} else if (selection.mode == Mode::Saved) {
				right_texts = {"L/R: ciudad   Triángulo: mapa   X: mercado   Cuadrado: Hansa",
					"Triángulo: mapa   X: mercado   Cuadrado: Hansa", "X: mercado   Cuadrado: Hansa",
					"Triángulo: mapa   X: mercado"};
			} else {
				right_texts = {"L/R: ciudad   Triángulo: mapa   X: mercado", "Triángulo: mapa   X: mercado"};
			}
			draw_bar(renderer, *text, 0, bar_title(save, town_names, template_ids, selection), right_texts);
		}
		// En el mapa del mar, abajo: el barco al que se dan las órdenes y la velocidad del juego.
		if (at_sea && small_text) {
			std::string ship_text = "No tienes ningún barco.";
			if (course_ship >= 0) {
				const SavedShip& ship = save.ships[size_t(course_ship)];
				ship_text = ship.name() + " (" + kShipTypes[ship.type() & 3] + "): " + ship_activity(save, town_names, ship);
			}
			const std::string speed = std::string("velocidad ") + kGameSpeeds[game_speed].name;
			draw_bar(renderer, *small_text, kScreenHeight - kBarHeight, ship_text,
				{"Cuadrado: barco   Select: " + speed, "Select: " + speed, speed});
		}
		if (in_hanse) {
			// After this frame's ticks and operations: what the panel shows now.
			const HanseView view_now = hanse_view(save, town_index);
			hanse.row = std::min(hanse.row, std::max(0, view_now.rows() - 1));
			draw_hanse(renderer, *small_text, save, hanse, view_now, town_names, names);
		}
		if (trading) {
			draw_market(renderer, *small_text, save, town_index, market);
		}
		if (small_text && !notice.empty() && !SDL_TICKS_PASSED(app_ticks(), notice_until)) {
			const int padding = 8;
			const SDL_Rect box = {8, kBarHeight + 6, small_text->measure(notice) + 2 * padding,
				small_text->line_height() + padding};
			SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
			SDL_SetRenderDrawColor(renderer, 30, 22, 14, 225);
			SDL_RenderFillRect(renderer, &box);
			SDL_SetRenderDrawColor(renderer, 150, 120, 70, 255);
			SDL_RenderDrawRect(renderer, &box);
			small_text->draw(notice, box.x + padding, box.y + padding / 2, kTextColor);
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
