// Visor de ciudades: dibuja las ciudades del juego original y permite
// recorrerlas. Es el primer paso del motor; todavía no hay simulación.

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
#include "p3data/image.hpp"
#include "p3data/prices.hpp"
#include "p3data/savegame.hpp"
#include "p3data/seamap.hpp"
#include "p3data/town.hpp"
#include "p3data/town_buildings.hpp"
#include "p3data/trade.hpp"
#include "p3data/wares.hpp"

using namespace p3::data;

namespace {

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

std::unique_ptr<CprArchive> open_archive(const char* dir_override, std::string& tried) {
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
		const SeaMapTown& t = towns_[size_t(save.towns[town].town_id)];
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

	void draw(const SaveGame& save, TextRenderer* names_text, const std::vector<std::string>& names, int camera_x,
		int camera_y, size_t selected) {
		const SDL_Rect src = {camera_x, camera_y, std::min(kScreenWidth, width_ - camera_x),
			std::min(kScreenHeight, height_ - camera_y)};
		const SDL_Rect dst = {0, 0, src.w, src.h};
		SDL_RenderCopy(renderer_, map_, &src, &dst);

		// Ciudades: azules las que tienen oficina del jugador; la elegida, resaltada.
		for (size_t i = 0; i < save.towns.size(); ++i) {
			const int id = save.towns[i].town_id;
			const SeaMapTown& t = towns_[size_t(id)];
			bool kontor = false;
			for (const SavedOffice& office : save.offices) {
				kontor |= office.used() && office.owner == save.player && office.town == i;
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

		// Barcos: los que navegan, con su rumbo; los del jugador, con su marca
		// (la de los atracados, sobre su ciudad).
		for (const SavedShip& ship : save.ships) {
			if (!ship.in_use()) {
				continue;
			}
			int x = ship.x >> 16;
			int y = ship.y >> 16;
			const bool sailing = ship.status == 0x0F || ship.status == 0x12;
			if (sailing) {
				const int frame = ship.heading & 15;
				const SDL_Rect ship_src = {(frame % 4) * kShipSpriteWidth, (frame / 4) * kShipSpriteHeight,
					kShipSpriteWidth, kShipSpriteHeight};
				const SDL_Rect ship_dst = {x - kShipSpriteWidth / 2 - camera_x, y - kShipSpriteHeight / 2 - camera_y,
					kShipSpriteWidth, kShipSpriteHeight};
				SDL_RenderCopy(renderer_, ships_[ship.type & 3], &ship_src, &ship_dst);
			}
			if (ship.owner != save.player || (!sailing && ship.town >= save.towns.size())) {
				continue;
			}
			if (!sailing) {
				const SDL_Point c = town_center(save, ship.town);
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

bool load_selection(const CprArchive& archive, const TileSet& tiles, const SaveGame& save,
	const std::vector<int>& regions, const std::vector<int>& template_ids, const Selection& selection,
	TownMap& out, std::string& error) {
	if (selection.mode == Mode::Saved) {
		return load_saved_town(archive, save, selection.saved_index, tiles, regions, out, &error);
	}
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

std::string bar_title(const SaveGame& save, const std::vector<std::string>& names,
	const std::vector<int>& template_ids, const Selection& selection) {
	if (selection.mode == Mode::Saved || selection.mode == Mode::Sea) {
		const SavedTown& town = save.towns[selection.saved_index];
		const std::string name = town.town_id < int(names.size()) ? names[size_t(town.town_id)]
																	: "Ciudad " + std::to_string(town.town_id);
		if (selection.mode == Mode::Sea) {
			return "Mapa del mar - " + name + " - año " + std::to_string(save.start_year);
		}
		return name + " - año " + std::to_string(save.start_year) + " - " + std::to_string(town.citizens) +
			" habitantes";
	}
	return "Plantilla " + std::to_string(template_ids[selection.template_index]) +
		(selection.mode == Mode::Designer ? " - todos los edificios del diseñador" : " - solo el terreno");
}

void draw_bar(SDL_Renderer* renderer, TextRenderer& text, const std::string& title, const std::string& right_text) {
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 160);
	const SDL_Rect bar = {0, 0, kScreenWidth, kBarHeight};
	SDL_RenderFillRect(renderer, &bar);
	const int y = (kBarHeight - text.line_height()) / 2;
	text.draw(title, kBarMargin, y, kTextColor);
	text.draw(right_text, kScreenWidth - kBarMargin - text.measure(right_text), y, kDimColor);
}

// Monedas con separador de miles: 30.000.
std::string format_money(int64_t value) {
	std::string digits = std::to_string(value < 0 ? -value : value);
	for (int i = int(digits.size()) - 3; i > 0; i -= 3) {
		digits.insert(size_t(i), ".");
	}
	return value < 0 ? "-" + digits : digits;
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
		if (ship.docked() && ship.owner == save.player && ship.town == town) {
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
	return buying ? 1000 * 1000 * 1000 : save.ships[size_t(ship)].cargo[size_t(ui.ware)];
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
	ui.message_until = SDL_GetTicks() + kMessageTime;
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
	} else if (int32_t(market.wares[size_t(ui.ware)]) < ware_scale(ware.unit)) {
		ui.message = "La ciudad no tiene " + name + ".";
	} else if (save.merchants[size_t(save.player)].money <
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
		std::snprintf(hold, sizeof(hold), ": %.1f de %.1f cargas", ship->used / 2000.0,
			(ship->capacity - ship->equipment) / 2000.0);
		text.draw(ship->name + " (" + kShipTypes[ship->type & 3] + ")" + hold, left, layout.header_y, kTextColor);
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
		right(std::to_string(units(int32_t(town.wares[size_t(w)]), ware.unit)) + short_unit, 0, y, kTextColor);
		right(buy ? std::to_string(buy) : "-", 1, y, kTextColor);
		right(std::to_string(sell), 2, y, kTextColor);
		if (ship) {
			const int n = units(ship->cargo[size_t(w)], ware.unit);
			right(n ? std::to_string(n) + short_unit : "-", 3, y, n ? kTextColor : kDimColor);
		}
	}

	// El resultado de la última operación o, si no, lo que costaría comerciar
	// ahora con la cantidad elegida.
	const bool show_message = !ui.message.empty() && !SDL_TICKS_PASSED(SDL_GetTicks(), ui.message_until);
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

}  // namespace

int main(int argc, char** argv) {
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
		return 1;
	}
	SDL_Window* window = SDL_CreateWindow(kTitle, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		kScreenWidth, kScreenHeight, 0);
	SDL_Renderer* renderer =
		window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : nullptr;
	if (!renderer) {
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, kTitle, SDL_GetError(), window);
		SDL_Quit();
		return 1;
	}
	show_loading(renderer);

	std::string tried;
	auto archive = open_archive(argc > 1 ? argv[1] : nullptr, tried);
	const std::vector<int> template_ids = archive ? list_town_ids(*archive) : std::vector<int>{};
	if (template_ids.empty()) {
		const std::string message = archive
			? std::string("El archivo CPR no contiene mapas de ciudad.")
			: "No se encuentran los datos del juego. Copia p2arch0_eng.cpr (y p2arch0_spa.cpr para la "
			  "versión española) en:" + tried;
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, kTitle, message.c_str(), window);
		SDL_Quit();
		return 1;
	}

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
	SaveGame save;
	const bool has_save = load_savegame(*archive, kStartSave, save) && !save.towns.empty();
	const std::vector<int> regions = load_town_regions(*archive);
	const NameLists names = load_name_lists(*archive);
	Selection selection;
	if (has_save) {
		for (size_t i = 0; i < save.towns.size(); ++i) {
			if (save.towns[i].town_id == kLuebeck) {
				selection.saved_index = i;
			}
		}
	} else {
		selection.mode = Mode::Designer;
	}

	SDL_GameController* controller = nullptr;
	bool reload = true;
	bool keep_camera = false;
	bool running = true;
	int camera_x = 0;
	int camera_y = 0;
	// Toque en curso: dónde empezó y cuánto se ha movido el dedo.
	SDL_Point touch_start = {0, 0};
	int touch_moved = 0;
	bool touch_on_panel = false;
	while (running) {
		// El mercado solo existe en las ciudades de la partida, con el jugador cargado.
		const bool trading = market.open && selection.mode == Mode::Saved && save.player >= 0 && small_text;
		const int town_index = int(selection.saved_index);
		auto select_ware = [&](int step) { market.ware = (market.ware + kTradeWareCount + step) % kTradeWareCount; };
		auto change_step = [&](int step) { market.step = (market.step + kTradeStepCount + step) % kTradeStepCount; };
		if (reload && selection.mode == Mode::Sea) {
			if (!sea) {
				show_loading(renderer);
				sea = std::make_unique<SeaMapView>(renderer, *archive);
			}
			if (!sea->ok()) {
				SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, kTitle, "No se encuentra el mapa del mar.", window);
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
			if (!load_selection(*archive, view->tiles(), save, regions, template_ids, selection, town, error)) {
				SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, kTitle, error.c_str(), window);
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
				case SDL_CONTROLLER_BUTTON_A:  // X en la Vita: abre el mercado y compra.
					if (trading) {
						trade(save, town_index, market, true);
					} else if (selection.mode == Mode::Saved) {
						market.open = true;
					}
					break;
				case SDL_CONTROLLER_BUTTON_X:  // Cuadrado: vende.
					if (trading) {
						trade(save, town_index, market, false);
					}
					break;
				case SDL_CONTROLLER_BUTTON_B:  // Círculo: cierra el mercado.
					market.open = false;
					break;
				case SDL_CONTROLLER_BUTTON_BACK:  // Select: siguiente barco del puerto o vista de plantilla.
					if (trading) {
						market.ship++;
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
				switch (event.key.keysym.sym) {
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
					break;
				case SDLK_c:
				case SDLK_v:
					if (trading) {
						trade(save, town_index, market, event.key.keysym.sym == SDLK_c);
					}
					break;
				case SDLK_TAB:
					market.ship++;
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
					trading && inside(MarketLayout(small_text->line_height()).panel, touch_start.x, touch_start.y);
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
				if (touch_on_panel && touch_moved < kTapDistance) {
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
				const int map = save.towns[selection.saved_index].template_map;
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
		if (controller) {
			camera_x += stick_speed(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX));
			camera_y += stick_speed(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY));
			if (!trading) {
				camera_x += kPadSpeed * (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) -
					SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_LEFT));
				camera_y += kPadSpeed * (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_DOWN) -
					SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_UP));
			}
		}
		if (!trading) {
			const Uint8* keys = SDL_GetKeyboardState(nullptr);
			camera_x += kPadSpeed * (keys[SDL_SCANCODE_RIGHT] - keys[SDL_SCANCODE_LEFT]);
			camera_y += kPadSpeed * (keys[SDL_SCANCODE_DOWN] - keys[SDL_SCANCODE_UP]);
		}
		const bool at_sea = selection.mode == Mode::Sea && sea;
		const int view_width = at_sea ? sea->width() : view->width();
		const int view_height = at_sea ? sea->height() : view->height();
		camera_x = std::clamp(camera_x, 0, std::max(0, view_width - kScreenWidth));
		camera_y = std::clamp(camera_y, 0, std::max(0, view_height - kScreenHeight));

		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
		SDL_RenderClear(renderer);
		if (at_sea) {
			sea->draw(save, small_text.get(), town_names, camera_x, camera_y, selection.saved_index);
		} else {
			view->draw(camera_x, camera_y);
		}
		if (text) {
			const std::string right_text = trading
				? merchant_name(save, names, save.player) + ": " +
					format_money(save.merchants[size_t(save.player)].money) + " monedas"
				: at_sea ? "L/R: ciudad   Triángulo: entrar"
				: "L/R: ciudad   Triángulo: mapa   X: mercado";
			draw_bar(renderer, *text, bar_title(save, town_names, template_ids, selection), right_text);
		}
		if (trading) {
			draw_market(renderer, *small_text, save, town_index, market);
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
	SDL_Quit();
	return 0;
}
