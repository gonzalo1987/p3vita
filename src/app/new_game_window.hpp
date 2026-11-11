#pragma once

// The game's «Game settings» window (0x496C90, *0x6D1C7C) as the viewer's (the
// user's choice, 2026-11-11: as the PC game's, adapted to the Vita; roadmap,
// phase 2, «Partida nueva desde cero»): its background (TEX22,
// images/background/spieleinstellungen.jpg), its title, labels, values and
// controls where the game puts them (0x497130: the text fields on frame 8 of
// Hauptbttn_komplett.tga, the balls of Kugel_komplett.tga and the buttons of
// Hauptbttn_komplett.tga, as scripts/options.ini has them), drawn as its paint
// 0x498060 does, in the game's fonts 5 (the title), 4 (the labels) and 2 (the
// values and the buttons), all scaled to the screen's height. What the
// controls do to the choices is p3data's (new_game.hpp).

#include <SDL.h>

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "p3data/new_game.hpp"

namespace p3::data {
class CprArchive;
}

namespace p3::app {

class TextRenderer;

// The window's controls, from the top (0x497130).
enum class NewGameControl {
	FirstName, FamilyName, GenderLeft, GenderRight, Map, HomeTown, YearMinus, YearPlus, LevelMinus, LevelPlus, Details,
	GoalLeft, GoalRight, TimeMinus, TimePlus, SpeedMinus, SpeedPlus, Back, Start, Count
};
constexpr int kNewGameControls = int(NewGameControl::Count);

// The ball of a control (or Count if it isn't one).
p3::data::NewGameBall new_game_ball(NewGameControl control);

// The window's texts in the language: the game's (from its exe), or the
// viewer's without it.
struct NewGameTexts {
	std::string title;  // «Game settings»
	std::string first_name, surname, gender, world_map, home_town, year, level, objective, speed;  // The labels.
	std::string male, female;
	std::string standard_map;  // The map's button: «Standard Map»
	std::string details;  // «Details» (with « (*)» when they aren't the level's)
	std::array<std::string, 5> levels;
	std::array<std::string, 8> goals;  // 0x69AA10: none .. capacity
	std::string hanse_population;  // The goal 6's name (0x6B2D3C)
	std::array<std::string, 6> speeds;
	std::string back, start;
};

class NewGameWindow {
public:
	NewGameWindow(SDL_Renderer* renderer, const p3::data::CprArchive& archive, int screen_width, int screen_height);
	~NewGameWindow();
	NewGameWindow(const NewGameWindow&) = delete;
	NewGameWindow& operator=(const NewGameWindow&) = delete;

	bool ok() const { return background_ != nullptr; }

	// What the window shows: the choices, the home town's name (UTF-8), which
	// controls show and can be pressed, the one the buttons point at, the one
	// under the finger and the name field being written (-1: none).
	struct View {
		const p3::data::NewGameChoices* choices = nullptr;
		const NewGameTexts* texts = nullptr;
		std::string home_town;
		std::array<bool, kNewGameControls> shown{};
		std::array<bool, kNewGameControls> enabled{};
		int focus = -1;
		int pressed = -1;
		int typing = -1;
	};
	void draw(SDL_Renderer* renderer, const View& view) const;
	// The control at a point of the screen, or -1: its picture (a little
	// bigger), and for a row of balls each half of the row (the finger needs
	// more than the balls' 26 pixels).
	int control_at(int x, int y, const View& view) const;
	// Where a control is on the screen.
	SDL_Rect rect(NewGameControl control) const;

private:
	SDL_Rect place(int x, int y, int w, int h) const;
	SDL_Rect place(const SDL_Rect& r) const { return place(r.x, r.y, r.w, r.h); }

	SDL_Texture* background_ = nullptr;
	SDL_Texture* buttons_ = nullptr;
	SDL_Texture* balls_ = nullptr;
	SDL_Rect button_frames_[9]{};
	SDL_Rect ball_frames_[22]{};
	std::unique_ptr<TextRenderer> title_font_;
	std::unique_ptr<TextRenderer> label_font_;
	std::unique_ptr<TextRenderer> value_font_;
	float scale_ = 1.0f;
	int left_ = 0;  // Where the window's 800 · 600 starts on the screen.
};

// The home town's window, opened by the «Game settings» window's home town
// button (0x49C0D0 on *0x6D2F60; scripts/Stadtauswahl.ini): the whole map
// (TEX5, images/Vollansichtskarte800.bmp) with a button for each of the map's
// towns where its symbol is on the sea map (StadtDaten.ini's StadtSymbolXPos and
// YPos, ÷ 3 − 9), red, blue for the Hanse towns and blue and red for those with
// a trading office (p_global_stadt01.tga, TEX30021); the town under the cursor
// with its panel beside it (0x49C410: its kind and name, and the wares it
// produces well and normally, sorted by name, in boxes of half-transparent
// black: TextBox0, TextBox2 and TextBox1, in the game's fonts 3 and 1); a hint
// at the foot. A Hanse town chosen becomes the home town.
class HomeTownWindow {
public:
	HomeTownWindow(SDL_Renderer* renderer, const p3::data::CprArchive& archive, int screen_width, int screen_height);
	~HomeTownWindow();
	HomeTownWindow(const HomeTownWindow&) = delete;
	HomeTownWindow& operator=(const HomeTownWindow&) = delete;

	bool ok() const { return map_ != nullptr; }

	// The texts (UTF-8): the towns' kinds before their names (without the
	// space that follows), the columns' titles, the hint and the wares' names
	// (0x6C0BB0).
	struct Texts {
		std::string hanse, office, station, effective, normal, hint;
		std::array<std::string, 20> wares;
	};
	// What it shows: the map, each town's place on the sea map (by its number),
	// the texts, the town the panel is for (the cursor) and the one under the
	// finger (-1: none).
	struct View {
		const p3::data::GameMap* map = nullptr;
		const std::vector<SDL_Point>* places = nullptr;
		const Texts* texts = nullptr;
		int cursor = -1;
		int pressed = -1;
	};
	void draw(SDL_Renderer* renderer, const View& view) const;
	// The town whose button is at a point of the screen (a little bigger than
	// its 26 pixels), or -1.
	int town_at(int x, int y, const View& view) const;

private:
	SDL_Rect place(int x, int y, int w, int h) const;
	SDL_Rect button_place(const View& view, int town) const;  // In the game's 800 · 600.

	SDL_Texture* map_ = nullptr;
	SDL_Texture* buttons_ = nullptr;
	SDL_Rect frames_[9]{};
	std::unique_ptr<TextRenderer> header_font_;
	std::unique_ptr<TextRenderer> entry_font_;
	float scale_ = 1.0f;
	int left_ = 0;
};

}  // namespace p3::app
