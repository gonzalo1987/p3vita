// Visor de ciudades: dibuja las ciudades del juego original y permite
// recorrerlas. Es el primer paso del motor; todavía no hay simulación.

#include <SDL.h>

#include <algorithm>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "p3data/aim.hpp"
#include "p3data/cpr.hpp"
#include "p3data/font.hpp"
#include "p3data/image.hpp"
#include "p3data/savegame.hpp"
#include "p3data/town.hpp"
#include "p3data/town_buildings.hpp"

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
constexpr int kBarHeight = 34;
constexpr int kBarMargin = 12;

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

// Qué se ve: una ciudad de la partida de inicio, o una plantilla de mapa con
// todos los edificios del diseñador (.editor) o solo con el terreno (.town).
enum class Mode { Saved, Designer, Terrain };

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
	if (selection.mode == Mode::Saved) {
		const SavedTown& town = save.towns[selection.saved_index];
		const std::string name = town.town_id < int(names.size()) ? names[size_t(town.town_id)]
																	: "Ciudad " + std::to_string(town.town_id);
		return name + " - año " + std::to_string(save.start_year) + " - " + std::to_string(town.citizens) +
			" habitantes";
	}
	return "Plantilla " + std::to_string(template_ids[selection.template_index]) +
		(selection.mode == Mode::Designer ? " - todos los edificios del diseñador" : " - solo el terreno");
}

void draw_bar(SDL_Renderer* renderer, TextRenderer& text, const std::string& title) {
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 160);
	const SDL_Rect bar = {0, 0, kScreenWidth, kBarHeight};
	SDL_RenderFillRect(renderer, &bar);
	const int y = (kBarHeight - text.line_height()) / 2;
	text.draw(title, kBarMargin, y, {255, 236, 190, 255});
	const std::string help = "L/R: ciudad    Triángulo: vista";
	text.draw(help, kScreenWidth - kBarMargin - text.measure(help), y, {200, 200, 200, 255});
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
	const std::vector<std::string> town_names = load_town_names(*archive);
	std::unique_ptr<TextRenderer> text;
	if (auto font = FontAtlas::load(*archive, kUiFont, kUiFontHeight)) {
		text = std::make_unique<TextRenderer>(renderer, std::move(font));
	}
	SaveGame save;
	const bool has_save = load_savegame(*archive, kStartSave, save) && !save.towns.empty();
	const std::vector<int> regions = load_town_regions(*archive);
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
	while (running) {
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
		bool next_mode = false;
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
				if (event.cbutton.button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER) {
					town_step = -1;
				} else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) {
					town_step = 1;
				} else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_Y) {
					next_mode = true;  // Triángulo en la Vita.
				} else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_START) {
					running = false;
				}
				break;
			case SDL_KEYDOWN:
				if (event.key.keysym.sym == SDLK_PAGEUP) {
					town_step = -1;
				} else if (event.key.keysym.sym == SDLK_PAGEDOWN) {
					town_step = 1;
				} else if (event.key.keysym.sym == SDLK_e) {
					next_mode = true;
				} else if (event.key.keysym.sym == SDLK_ESCAPE) {
					running = false;
				}
				break;
			case SDL_FINGERMOTION:
				// Arrastrar desplaza la ciudad con el dedo.
				camera_x -= int(event.tfinger.dx * kScreenWidth);
				camera_y -= int(event.tfinger.dy * kScreenHeight);
				break;
			}
		}
		if (town_step != 0) {
			if (selection.mode == Mode::Saved) {
				const size_t count = save.towns.size();
				selection.saved_index = (selection.saved_index + count + size_t(town_step)) % count;
			} else {
				const size_t count = template_ids.size();
				selection.template_index = (selection.template_index + count + size_t(town_step)) % count;
			}
			reload = true;
			continue;
		}
		if (next_mode) {
			// Al pasar de la partida a las plantillas se sigue en el mismo mapa,
			// con la cámara donde estaba.
			if (selection.mode == Mode::Saved) {
				const int map = save.towns[selection.saved_index].template_map;
				auto it = std::find(template_ids.begin(), template_ids.end(), map);
				selection.template_index = it == template_ids.end() ? 0 : size_t(it - template_ids.begin());
				selection.mode = Mode::Designer;
			} else if (selection.mode == Mode::Designer) {
				selection.mode = Mode::Terrain;
			} else {
				selection.mode = has_save ? Mode::Saved : Mode::Designer;
			}
			reload = true;
			keep_camera = true;
			continue;
		}

		if (controller) {
			camera_x += stick_speed(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX));
			camera_y += stick_speed(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY));
			camera_x += kPadSpeed * (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) -
				SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_LEFT));
			camera_y += kPadSpeed * (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_DOWN) -
				SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_UP));
		}
		const Uint8* keys = SDL_GetKeyboardState(nullptr);
		camera_x += kPadSpeed * (keys[SDL_SCANCODE_RIGHT] - keys[SDL_SCANCODE_LEFT]);
		camera_y += kPadSpeed * (keys[SDL_SCANCODE_DOWN] - keys[SDL_SCANCODE_UP]);
		camera_x = std::clamp(camera_x, 0, std::max(0, view->width() - kScreenWidth));
		camera_y = std::clamp(camera_y, 0, std::max(0, view->height() - kScreenHeight));

		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
		SDL_RenderClear(renderer);
		view->draw(camera_x, camera_y);
		if (text) {
			draw_bar(renderer, *text, bar_title(save, town_names, template_ids, selection));
		}
		SDL_RenderPresent(renderer);
	}

	// Las texturas tienen que liberarse antes de cerrar SDL.
	text.reset();
	view.reset();
	if (controller) {
		SDL_GameControllerClose(controller);
	}
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return 0;
}
