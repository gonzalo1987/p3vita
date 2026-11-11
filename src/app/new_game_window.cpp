// The viewer's «Game settings» window (new_game_window.hpp).

#include "new_game_window.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "p3data/cpr.hpp"
#include "p3data/font.hpp"
#include "p3data/image.hpp"
#include "text_renderer.hpp"
#include "ui_images.hpp"

namespace p3::app {

namespace {

using p3::data::CprArchive;
using p3::data::FontAtlas;
using p3::data::Image;
using p3::data::NewGameBall;
using p3::data::NewGameChoices;

// Where the window (0x497130) puts each control on its 800 · 600: from the
// top at 115 (0x6BA5E4), each under the one before with its gap; the left
// column at 509 (0x6BA5DC), the right balls where the text fields end (0x6BA5E0:
// 509 − 29 + 233); «Back» and «Start» at (453, 544) and (614, 544). The text
// fields are TextBox0 (233 · 24), the buttons BUTTON3 (233 · 36) and BUTTON4
// (149 · 36), the balls 29 · 28 (scripts/options.ini).
constexpr SDL_Rect kPlaces[kNewGameControls] = {
	{509, 115, 233, 24},  // first name
	{509, 141, 233, 24},  // family name
	{509, 173, 29, 28}, {713, 173, 29, 28},  // gender
	{509, 217, 233, 36},  // world map
	{509, 255, 233, 36},  // home town
	{509, 293, 29, 28}, {713, 293, 29, 28},  // starting year
	{509, 359, 29, 28}, {713, 359, 29, 28},  // level of difficulty
	{509, 387, 233, 36},  // details
	{509, 431, 29, 28}, {713, 431, 29, 28},  // objective conditions
	{509, 461, 29, 28}, {713, 461, 29, 28},  // the goal's time
	{509, 497, 29, 28}, {713, 497, 29, 28},  // game speed
	{453, 544, 149, 36},  // back
	{614, 544, 149, 36},  // start
};

// The balls' frames in Kugel_komplett.tga (TEX15) by kind, from options.ini:
// neutral, pressed, lit, disabled (BUTTON7 minus, BUTTON8 plus, BUTTON9 left,
// BUTTON10 right).
struct BallFrames {
	int neutral, pressed, lit, disabled;
};
constexpr BallFrames kMinus = {3, 4, 5, 6};
constexpr BallFrames kPlus = {7, 8, 9, 10};
constexpr BallFrames kLeft = {11, 12, 13, 14};
constexpr BallFrames kRight = {15, 16, 17, 18};

// The buttons' frames in Hauptbttn_komplett.tga (TEX7): BUTTON3 neutral and
// pressed, BUTTON4 neutral and pressed, and the text fields' (ANIM33).
constexpr int kWideNeutral = 4;
constexpr int kWidePressed = 5;
constexpr int kSmallNeutral = 6;
constexpr int kSmallPressed = 7;
constexpr int kFieldFrame = 8;

// The colours: the text white (0x66E10C), the lit buttons' text (0x66E110),
// «Start» without the names (0xFF404040, [+0x9CC]); the title in the gold of
// elgreco72.aim.
constexpr SDL_Color kWhite = {255, 255, 255, 255};
constexpr SDL_Color kLit = {241, 160, 0, 255};
constexpr SDL_Color kGrey = {64, 64, 64, 255};
constexpr SDL_Color kTitle = {176, 100, 28, 255};
constexpr SDL_Color kDimmed = {255, 255, 255, 110};

// The game's fonts (scripts/fonts.ini): 5 elgreco72 (75 high), 4 tiepolo_bold24
// (24), 2 tiepolo_black20 (20), here their TrueType faces.
constexpr char kTitleFace[] = "fonts/ELGRECO_.TTF";
constexpr char kLabelFace[] = "fonts/Tiepolo_bold.ttf";
constexpr char kValueFace[] = "fonts/Tiepolo_black.ttf";

bool is_ball(NewGameControl c) {
	return new_game_ball(c) != NewGameBall::Count;
}

BallFrames ball_frames(NewGameControl c) {
	switch (c) {
	case NewGameControl::GenderLeft:
	case NewGameControl::GoalLeft:
		return kLeft;
	case NewGameControl::GenderRight:
	case NewGameControl::GoalRight:
		return kRight;
	case NewGameControl::YearMinus:
	case NewGameControl::LevelMinus:
	case NewGameControl::TimeMinus:
	case NewGameControl::SpeedMinus:
		return kMinus;
	default:
		return kPlus;
	}
}

std::unique_ptr<TextRenderer> load_font(SDL_Renderer* renderer, const CprArchive& archive, const char* face,
	float height) {
	if (auto font = FontAtlas::load(archive, face, height)) {
		return std::make_unique<TextRenderer>(renderer, std::move(font));
	}
	return nullptr;
}

}  // namespace

NewGameBall new_game_ball(NewGameControl control) {
	switch (control) {
	case NewGameControl::GenderLeft:
		return NewGameBall::GenderLeft;
	case NewGameControl::GenderRight:
		return NewGameBall::GenderRight;
	case NewGameControl::YearMinus:
		return NewGameBall::YearMinus;
	case NewGameControl::YearPlus:
		return NewGameBall::YearPlus;
	case NewGameControl::LevelMinus:
		return NewGameBall::LevelMinus;
	case NewGameControl::LevelPlus:
		return NewGameBall::LevelPlus;
	case NewGameControl::GoalLeft:
		return NewGameBall::GoalLeft;
	case NewGameControl::GoalRight:
		return NewGameBall::GoalRight;
	case NewGameControl::TimeMinus:
		return NewGameBall::TimeMinus;
	case NewGameControl::TimePlus:
		return NewGameBall::TimePlus;
	case NewGameControl::SpeedMinus:
		return NewGameBall::SpeedMinus;
	case NewGameControl::SpeedPlus:
		return NewGameBall::SpeedPlus;
	default:
		return NewGameBall::Count;
	}
}

NewGameWindow::NewGameWindow(SDL_Renderer* renderer, const CprArchive& archive, int screen_width, int screen_height) {
	Image background;
	if (!read_cpr_image(archive, "images/background/spieleinstellungen.jpg", background) || background.height <= 0) {
		return;
	}
	background_ = make_texture(renderer, background);
	Image image;
	if (read_cpr_image(archive, "images/options/Hauptbttn_komplett.tga", image)) {
		buttons_ = make_texture(renderer, image);
	}
	if (read_cpr_image(archive, "images/options/Kugel_komplett.tga", image)) {
		balls_ = make_texture(renderer, image);
	}
	std::vector<uint8_t> ini;
	if (read_cpr_file(archive, "scripts/textures.ini", ini)) {
		read_texture_frames(ini, "[TEX7]", button_frames_, 9);
		read_texture_frames(ini, "[TEX15]", ball_frames_, 22);
	}
	// The window's 800 · 600 over the screen's height, in the middle.
	scale_ = float(screen_height) / 600.0f;
	left_ = (screen_width - int(std::lround(800.0f * scale_))) / 2;
	title_font_ = load_font(renderer, archive, kTitleFace, 75.0f * scale_);
	label_font_ = load_font(renderer, archive, kLabelFace, 24.0f * scale_);
	value_font_ = load_font(renderer, archive, kValueFace, 20.0f * scale_);
}

NewGameWindow::~NewGameWindow() {
	for (SDL_Texture* t : {background_, buttons_, balls_}) {
		if (t) {
			SDL_DestroyTexture(t);
		}
	}
}

SDL_Rect NewGameWindow::place(int x, int y, int w, int h) const {
	const int x0 = left_ + int(std::lround(float(x) * scale_));
	const int y0 = int(std::lround(float(y) * scale_));
	return {x0, y0, left_ + int(std::lround(float(x + w) * scale_)) - x0, int(std::lround(float(y + h) * scale_)) - y0};
}

SDL_Rect NewGameWindow::rect(NewGameControl control) const {
	return place(kPlaces[int(control)]);
}

int NewGameWindow::control_at(int x, int y, const View& view) const {
	const SDL_Point p = {x, y};
	for (int k = 0; k < kNewGameControls; ++k) {
		const SDL_Rect r = place(kPlaces[k]);
		if (view.shown[size_t(k)] && SDL_PointInRect(&p, &r)) {
			return k;
		}
	}
	// A little more around: the text fields and buttons 4 pixels (of the game's
	// 800), the balls their half of the row and 6 pixels above and below.
	for (int k = 0; k < kNewGameControls; ++k) {
		if (!view.shown[size_t(k)]) {
			continue;
		}
		const SDL_Rect& at = kPlaces[k];
		SDL_Rect r;
		if (is_ball(NewGameControl(k))) {
			const bool left = at.x == 509;
			r = place(left ? 509 : 626, at.y - 6, left ? 117 : 116, at.h + 12);
		} else {
			r = place(at.x - 4, at.y - 4, at.w + 8, at.h + 8);
		}
		if (SDL_PointInRect(&p, &r)) {
			return k;
		}
	}
	return -1;
}

void NewGameWindow::draw(SDL_Renderer* renderer, const View& view) const {
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_RenderClear(renderer);
	if (!background_ || !view.choices || !view.texts) {
		return;
	}
	const NewGameChoices& c = *view.choices;
	const NewGameTexts& t = *view.texts;
	const SDL_Rect whole = place(0, 0, 800, 600);
	SDL_RenderCopy(renderer, background_, nullptr, &whole);
	auto shown = [&](NewGameControl k) { return view.shown[size_t(k)]; };
	auto enabled = [&](NewGameControl k) { return view.enabled[size_t(k)]; };
	// The title (0x498135: font 5, right-aligned at (757, 11)).
	if (title_font_) {
		const SDL_Rect at = place(757, 11, 0, 0);
		title_font_->draw(t.title, at.x - title_font_->measure(t.title), at.y, kTitle);
	}
	// The labels (font 4, right-aligned 16 pixels before their control, in the
	// middle of its height).
	auto label = [&](NewGameControl k, const std::string& text) {
		if (!label_font_ || !shown(k)) {
			return;
		}
		const SDL_Rect r = rect(k);
		const SDL_Rect at = place(kPlaces[int(k)].x - 16, 0, 0, 0);
		label_font_->draw(text, at.x - label_font_->measure(text), r.y + (r.h - label_font_->line_height()) / 2, kWhite);
	};
	label(NewGameControl::FirstName, t.first_name);
	label(NewGameControl::FamilyName, t.surname);
	label(NewGameControl::GenderLeft, t.gender);
	label(NewGameControl::Map, t.world_map);
	label(NewGameControl::HomeTown, t.home_town);
	label(NewGameControl::YearMinus, t.year);
	label(NewGameControl::LevelMinus, t.level);
	label(NewGameControl::GoalLeft, t.objective);
	label(NewGameControl::SpeedMinus, t.speed);
	// The text fields: their frame and the name (white, font 2), with the
	// cursor while it is being written; the buttons' cursor around it.
	for (NewGameControl k : {NewGameControl::FirstName, NewGameControl::FamilyName}) {
		const SDL_Rect r = rect(k);
		if (buttons_ && button_frames_[kFieldFrame].w > 0) {
			SDL_RenderCopy(renderer, buttons_, &button_frames_[kFieldFrame], &r);
		}
		const std::string& name = k == NewGameControl::FirstName ? c.first_name : c.family_name;
		const std::string text = p3::data::latin1_to_utf8(name);
		const int x = r.x + int(std::lround(6.0f * scale_));
		int end = x;
		if (value_font_) {
			const int y = r.y + (r.h - value_font_->line_height()) / 2;
			value_font_->draw(text, x, y, kWhite);
			end = x + value_font_->measure(text);
		}
		if (view.typing == int(k)) {
			const SDL_Rect cursor = {end + 1, r.y + (r.h - int(16.0f * scale_)) / 2, std::max(2, int(2.0f * scale_)),
				int(16.0f * scale_)};
			SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
			SDL_RenderFillRect(renderer, &cursor);
		}
		if (view.focus == int(k) || view.typing == int(k)) {
			SDL_SetRenderDrawColor(renderer, kLit.r, kLit.g, kLit.b, 255);
			SDL_RenderDrawRect(renderer, &r);
		}
	}
	// The buttons: their frame (pressed under the finger) and text (font 2, in
	// the middle; lit with the buttons' cursor, dimmed when they can't be pressed).
	auto button = [&](NewGameControl k, const std::string& text) {
		if (!shown(k)) {
			return;
		}
		const SDL_Rect r = rect(k);
		const bool small = kPlaces[int(k)].w == 149;
		const bool pressed = view.pressed == int(k) && enabled(k);
		const int frame = small ? (pressed ? kSmallPressed : kSmallNeutral) : (pressed ? kWidePressed : kWideNeutral);
		if (buttons_ && button_frames_[frame].w > 0) {
			SDL_RenderCopy(renderer, buttons_, &button_frames_[frame], &r);
		}
		if (!value_font_) {
			return;
		}
		SDL_Color color = enabled(k) ? (view.focus == int(k) || pressed ? kLit : kWhite) : kDimmed;
		if (k == NewGameControl::Start && !enabled(k)) {
			color = kGrey;  // Without both names (0x49A906..).
		}
		const int w = value_font_->measure(text);
		value_font_->draw(text, r.x + (r.w - w) / 2, r.y + (r.h - value_font_->line_height()) / 2, color);
	};
	button(NewGameControl::Map, t.standard_map + (c.production == 0 ? " (*)" : ""));
	button(NewGameControl::HomeTown, view.home_town);
	const bool custom = c.level < p3::data::kNewGameLevelDetails.size() &&
		c.details != p3::data::kNewGameLevelDetails[c.level];
	button(NewGameControl::Details, t.details + (custom ? " (*)" : ""));
	button(NewGameControl::Back, t.back);
	button(NewGameControl::Start, t.start);
	// The balls: neutral, pressed under the finger, lit with the buttons' cursor
	// on their row, or disabled.
	for (int k = 0; k < kNewGameControls; ++k) {
		const NewGameControl control = NewGameControl(k);
		if (!is_ball(control) || !shown(control) || !balls_) {
			continue;
		}
		const BallFrames f = ball_frames(control);
		const bool row_lit = view.focus >= 0 && kPlaces[view.focus].y == kPlaces[k].y;
		const int frame = !enabled(control) ? f.disabled
			: view.pressed == k            ? f.pressed
			: row_lit                      ? f.lit
										   : f.neutral;
		const SDL_Rect r = rect(control);
		if (ball_frames_[frame].w > 0) {
			SDL_RenderCopy(renderer, balls_, &ball_frames_[frame], &r);
		}
	}
	// The values between the balls (font 2, in the middle of the row, 0x4984C0..).
	auto value = [&](NewGameControl k, const std::string& text) {
		if (!value_font_ || !shown(k)) {
			return;
		}
		const SDL_Rect r = rect(k);
		const SDL_Rect mid = place(625, 0, 0, 0);
		const int w = value_font_->measure(text);
		value_font_->draw(text, mid.x - w / 2, r.y + (r.h - value_font_->line_height()) / 2,
			enabled(k) || enabled(NewGameControl(int(k) + 1)) ? kWhite : kDimmed);
	};
	value(NewGameControl::GenderLeft, c.female ? t.female : t.male);
	value(NewGameControl::YearMinus, std::to_string(c.year));
	value(NewGameControl::LevelMinus, c.level < t.levels.size() ? t.levels[c.level] : std::string());
	value(NewGameControl::GoalLeft, c.goal == 6 ? t.hanse_population : c.goal < t.goals.size() ? t.goals[c.goal] : "");
	value(NewGameControl::SpeedMinus, c.speed < t.speeds.size() ? t.speeds[c.speed] : std::string());
}

// --- The home town's window. ---

namespace {

// The town buttons' frames in p_global_stadt01.tga (TEX30021), by kind
// (Stadtauswahl.ini BUTTON0 red, BUTTON1 blue, BUTTON2 blue and red): neutral,
// lit (the hover), pressed.
constexpr int kTownFrames[3][3] = {{0, 1, 2}, {3, 4, 5}, {6, 7, 8}};
constexpr int kTownButton = 26;
// The panel's boxes (Stadtauswahl.ini): the header TextBox0 (250 · 30), the
// columns' titles TextBox2 (125 · 30), the rows TextBox1 (125 · 15), black at
// half (0x80000000); the hint right-aligned at (775, 555), 20 high (0x49BD80).
constexpr int kHeaderW = 250;
constexpr int kHeaderH = 30;
constexpr int kColumnW = 125;
constexpr int kRowH = 15;
constexpr SDL_Color kBox = {0, 0, 0, 0x80};

}  // namespace

HomeTownWindow::HomeTownWindow(SDL_Renderer* renderer, const CprArchive& archive, int screen_width, int screen_height) {
	Image map;
	if (!read_cpr_image(archive, "images/Vollansichtskarte800.bmp", map) || map.height <= 0) {
		return;
	}
	map_ = make_texture(renderer, map);
	Image image;
	if (read_cpr_image(archive, "images/p_global_stadt01.tga", image)) {
		buttons_ = make_texture(renderer, image);
	}
	std::vector<uint8_t> ini;
	if (read_cpr_file(archive, "scripts/textures.ini", ini)) {
		read_texture_frames(ini, "[TEX30021]", frames_, 9);
	}
	scale_ = float(screen_height) / 600.0f;
	left_ = (screen_width - int(std::lround(800.0f * scale_))) / 2;
	header_font_ = load_font(renderer, archive, kTitleFace, 24.0f * scale_);  // font 3, elgreco24
	entry_font_ = load_font(renderer, archive, kLabelFace, 16.0f * scale_);  // font 1, tiepolo_bold16
}

HomeTownWindow::~HomeTownWindow() {
	for (SDL_Texture* t : {map_, buttons_}) {
		if (t) {
			SDL_DestroyTexture(t);
		}
	}
}

SDL_Rect HomeTownWindow::place(int x, int y, int w, int h) const {
	const int x0 = left_ + int(std::lround(float(x) * scale_));
	const int y0 = int(std::lround(float(y) * scale_));
	return {x0, y0, left_ + int(std::lround(float(x + w) * scale_)) - x0, int(std::lround(float(y + h) * scale_)) - y0};
}

SDL_Rect HomeTownWindow::button_place(const View& view, int town) const {
	const uint8_t number = view.map->numbers[size_t(town) % view.map->numbers.size()];
	const SDL_Point at = number < view.places->size() ? (*view.places)[number] : SDL_Point{0, 0};
	return {at.x / 3 - 9, at.y / 3 - 9, kTownButton, kTownButton};  // 0x49C146..0x49C197
}

int HomeTownWindow::town_at(int x, int y, const View& view) const {
	if (!view.map || !view.places) {
		return -1;
	}
	// The nearest button's middle within 22 of the game's pixels.
	int best = -1;
	float nearest = 22.0f * scale_;
	for (uint32_t i = 0; i < view.map->count; ++i) {
		const SDL_Rect b = place(button_place(view, int(i)).x, button_place(view, int(i)).y, kTownButton, kTownButton);
		const float d = std::hypot(float(x - (b.x + b.w / 2)), float(y - (b.y + b.h / 2)));
		if (d <= nearest) {
			nearest = d;
			best = int(i);
		}
	}
	return best;
}

void HomeTownWindow::draw(SDL_Renderer* renderer, const View& view) const {
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_RenderClear(renderer);
	if (!map_ || !view.map || !view.places || !view.texts) {
		return;
	}
	const p3::data::GameMap& m = *view.map;
	const Texts& t = *view.texts;
	const SDL_Rect whole = place(0, 0, 800, 600);
	SDL_RenderCopy(renderer, map_, nullptr, &whole);
	for (uint32_t i = 0; i < m.count && buttons_; ++i) {
		const int kind = (m.flags[i] & 0x40) ? 1 : (m.flags[i] & 0x80) ? 2 : 0;
		const int state = view.pressed == int(i) ? 2 : view.cursor == int(i) ? 1 : 0;
		const SDL_Rect& f = frames_[kTownFrames[kind][state]];
		const SDL_Rect b = button_place(view, int(i));
		const SDL_Rect to = place(b.x, b.y, b.w, b.h);
		if (f.w > 0) {
			SDL_RenderCopy(renderer, buttons_, &f, &to);
		}
	}
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	// A box of the panel with its text: left-aligned after its first space, or
	// in the middle (the header), in the middle of its height.
	auto box = [&](int x, int y, int w, int h, const std::string& text, TextRenderer* font, bool centered) {
		const SDL_Rect r = place(x, y, w, h);
		SDL_SetRenderDrawColor(renderer, kBox.r, kBox.g, kBox.b, kBox.a);
		SDL_RenderFillRect(renderer, &r);
		if (font && !text.empty()) {
			const int tw = font->measure(text);
			font->draw(text, centered ? r.x + (r.w - tw) / 2 : r.x, r.y + (r.h - font->line_height()) / 2, kWhite);
		}
	};
	if (view.cursor >= 0 && uint32_t(view.cursor) < m.count) {
		// The panel (0x49C410): the kind and the name; the two columns, each
		// sorted by name (strcmp on the game's Latin-1) and as long as the other.
		const uint8_t flags = m.flags[size_t(view.cursor)];
		const std::string& kind = (flags & 0x40) ? t.hanse : (flags & 0x80) ? t.office : t.station;
		// The kind's text ends in a space (GameTexts trims it).
		const std::string header =
			kind + " " + p3::data::latin1_to_utf8(p3::data::game_map_town_name(m, uint32_t(view.cursor)));
		std::array<std::vector<std::string>, 2> columns;
		for (int k = 0; k < 2; ++k) {
			for (uint8_t ware : p3::data::game_map_town_wares(m, uint32_t(view.cursor), k == 0)) {
				columns[size_t(k)].push_back(p3::data::utf8_to_latin1(" " + t.wares[ware % t.wares.size()]));
			}
			std::stable_sort(columns[size_t(k)].begin(), columns[size_t(k)].end());
		}
		const int rows = int(std::max(columns[0].size(), columns[1].size()));
		const SDL_Rect b = button_place(view, view.cursor);
		const int x = b.x < 400 ? b.x + 36 : b.x - 10 - kHeaderW;
		const int y = b.y < 300 ? b.y + 10 : b.y - 10 - kRowH * rows - kHeaderH - kHeaderH;
		box(x, y, kHeaderW, kHeaderH, header, header_font_.get(), true);
		box(x, y + kHeaderH, kColumnW, kHeaderH, " " + t.effective, header_font_.get(), false);
		box(x + kColumnW, y + kHeaderH, kColumnW, kHeaderH, " " + t.normal, header_font_.get(), false);
		for (int r = 0; r < rows; ++r) {
			for (int k = 0; k < 2; ++k) {
				const std::vector<std::string>& column = columns[size_t(k)];
				const std::string text = size_t(r) < column.size() ? p3::data::latin1_to_utf8(column[size_t(r)]) : "";
				box(x + kColumnW * k, y + 2 * kHeaderH + kRowH * r, kColumnW, kRowH, text, entry_font_.get(), false);
			}
		}
	}
	// The hint at the foot, as wide as its text.
	if (entry_font_ && !t.hint.empty()) {
		const int w = int(std::lround(float(entry_font_->measure(t.hint)) / scale_)) + 8;
		box(775 - w, 555, w, 20, " " + t.hint, entry_font_.get(), false);
	}
}

}  // namespace p3::app
