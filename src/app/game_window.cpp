// The buildings' windows as the game draws them (game_window.hpp).

#include "game_window.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "p3data/font.hpp"
#include "p3data/game_fixes.hpp"
#include "p3data/image.hpp"
#include "text_renderer.hpp"
#include "ui_images.hpp"

namespace p3::app {

namespace {

std::string read_text(const p3::data::CprArchive& archive, const char* path) {
	std::vector<uint8_t> bytes;
	return read_cpr_file(archive, path, bytes) ? std::string(bytes.begin(), bytes.end()) : std::string();
}

}  // namespace

GameWindowArt::GameWindowArt(SDL_Renderer* renderer, const p3::data::CprArchive& archive)
	: renderer_(renderer), archive_(archive) {
	// The bath house's side menu as its constructor leaves it (0x59FCAC..0x59FCD2): the player alone in each town's
	// bath until the tick then plus a day. It is made when the game starts, before a game is loaded: here the first
	// visit to each town's bath rolls its guests.
	for (uint32_t t = 0; t < kTownSlots; ++t) {
		bath_mask_[t] = 1;
		bath_until_[t] = 0;
	}
}

GameWindowArt::~GameWindowArt() {
	free_animations();
	free_face();
	for (auto& [id, t] : texs_) {
		free_strip(t);
	}
	for (SDL_Texture* t : {portrait_picture_, torn_across_, torn_down_}) {
		if (t) {
			SDL_DestroyTexture(t);
		}
	}
	for (SDL_Texture* t : {picture_, parchment_, pill_, close_, frame_, target_, banner_, paper_, corners_, seal_}) {
		if (t) {
			SDL_DestroyTexture(t);
		}
	}
}

namespace {

// An ini's value `word` in any case (textures.ini has «UseAlpha=False» too).
bool ini_is(const std::string& value, const char* word) {
	std::string v;
	for (char c : value) {
		v += char(std::tolower(static_cast<unsigned char>(c)));
	}
	return v == word;
}

// The largest texture a picture may go in: the Vita's (its GXM renderer's 4096), or the renderer's if smaller.
int max_texture_side(SDL_Renderer* renderer) {
	constexpr int kVitaMax = 4096;
	SDL_RendererInfo info;
	int side = kVitaMax;
	if (SDL_GetRendererInfo(renderer, &info) == 0) {
		if (info.max_texture_width > 0) {
			side = std::min(side, info.max_texture_width);
		}
		if (info.max_texture_height > 0) {
			side = std::min(side, info.max_texture_height);
		}
	}
	return side;
}

}  // namespace

bool GameWindowArt::read_tex_image(uint32_t tex, p3::data::Image& image) {
	const std::string section = "TEX" + std::to_string(tex);
	const std::string file = ini_value(textures_ini_, section, "TextureFile");
	if (file.empty() || !read_cpr_image(archive_, file.c_str(), image)) {
		return false;
	}
	if (ini_is(ini_value(textures_ini_, section, "UseAlpha"), "true")) {
		apply_bmp_palette_alpha(archive_, file.c_str(), image);  // An 8-bit BMP's alpha in its palette (the tally marks).
	}
	if (ini_is(ini_value(textures_ini_, section, "UseAlpha"), "false")) {
		// Drawn without the file's alpha (the palette AIMs' is 0 throughout: the bath house's steam, water and drinking
		// man were invisible), opaque but for its colour key.
		for (size_t i = 3; i < image.rgba.size(); i += 4) {
			image.rgba[i] = 255;
		}
	}
	if (ini_is(ini_value(textures_ini_, section, "UseColorKey"), "true")) {
		// Its colour key («ColorKey=0xAARRGGBB»), transparent.
		const uint32_t key = uint32_t(std::strtoul(ini_value(textures_ini_, section, "ColorKey").c_str(), nullptr, 16));
		for (size_t i = 0; i + 3 < image.rgba.size(); i += 4) {
			if (image.rgba[i] == uint8_t(key >> 16) && image.rgba[i + 1] == uint8_t(key >> 8) && image.rgba[i + 2] == uint8_t(key)) {
				image.rgba[i + 3] = 0;
			}
		}
	}
	return true;
}

SDL_Texture* GameWindowArt::load(uint32_t tex, SDL_Rect* frames, int count) {
	p3::data::Image image;
	if (!read_tex_image(tex, image)) {
		return nullptr;
	}
	for (int k = 0; k < count; ++k) {
		frames[k] = {0, 0, image.width, image.height};
	}
	const std::vector<uint8_t> ini(textures_ini_.begin(), textures_ini_.end());
	read_texture_frames(ini, ("[TEX" + std::to_string(tex) + "]").c_str(), frames, count);
	return make_texture(renderer_, image);
}

bool GameWindowArt::load_strip(uint32_t tex, Strip& strip) {
	free_strip(strip);
	p3::data::Image image;
	if (!read_tex_image(tex, image)) {
		return false;
	}
	const int count = std::max(1, std::atoi(ini_value(textures_ini_, "TEX" + std::to_string(tex), "Count").c_str()));
	strip.frames.assign(size_t(count), SDL_Rect{0, 0, image.width, image.height});
	const std::vector<uint8_t> ini(textures_ini_.begin(), textures_ini_.end());
	read_texture_frames(ini, ("[TEX" + std::to_string(tex) + "]").c_str(), strip.frames.data(), count);
	const int side = max_texture_side(renderer_);
	if (image.width <= side && image.height <= side) {
		strip.texture = make_texture(renderer_, image);
		return strip.texture != nullptr;
	}
	// Too big for one texture: a texture for each frame (the same rectangle's made once: the strips going and coming
	// back, as the bath house's resting man's, repeat theirs).
	strip.pieces.assign(size_t(count), nullptr);
	for (int k = 0; k < count; ++k) {
		SDL_Rect r = strip.frames[size_t(k)];
		SDL_Rect inside;
		const SDL_Rect whole = {0, 0, image.width, image.height};
		if (!SDL_IntersectRect(&r, &whole, &inside)) {
			continue;
		}
		strip.frames[size_t(k)] = inside;
		for (int j = 0; j < k; ++j) {
			const SDL_Rect& o = strip.frames[size_t(j)];
			if (strip.pieces[size_t(j)] && o.x == inside.x && o.y == inside.y && o.w == inside.w && o.h == inside.h) {
				strip.pieces[size_t(k)] = strip.pieces[size_t(j)];
				break;
			}
		}
		if (strip.pieces[size_t(k)]) {
			continue;
		}
		p3::data::Image piece;
		piece.width = inside.w;
		piece.height = inside.h;
		piece.rgba.resize(size_t(inside.w) * size_t(inside.h) * 4);
		for (int y = 0; y < inside.h; ++y) {
			const uint8_t* from = image.rgba.data() + (size_t(inside.y + y) * size_t(image.width) + size_t(inside.x)) * 4;
			std::copy(from, from + size_t(inside.w) * 4, piece.rgba.begin() + std::ptrdiff_t(size_t(y) * size_t(inside.w) * 4));
		}
		strip.pieces[size_t(k)] = make_texture(renderer_, piece);
	}
	return true;
}

void GameWindowArt::free_strip(Strip& strip) {
	if (strip.texture) {
		SDL_DestroyTexture(strip.texture);
	}
	// The pieces shared between frames destroyed once.
	for (size_t k = 0; k < strip.pieces.size(); ++k) {
		SDL_Texture* t = strip.pieces[k];
		const auto before = strip.pieces.begin() + std::ptrdiff_t(k);
		if (t && std::find(strip.pieces.begin(), before, t) == before) {
			SDL_DestroyTexture(t);
		}
	}
	strip = Strip();
}

SDL_Point GameWindowArt::draw_strip(const Strip& strip, int frame, int x, int y, int width, uint8_t alpha) const {
	if (frame < 0 || size_t(frame) >= strip.frames.size()) {
		return {0, 0};
	}
	SDL_Rect src = strip.frames[size_t(frame)];
	if (width > 0 && width < src.w) {
		src.w = width;
	}
	const SDL_Rect dst = {x, y, src.w, src.h};
	SDL_Texture* t = strip.texture;
	if (!strip.pieces.empty()) {
		t = strip.pieces[size_t(frame)];
		src = {0, 0, src.w, src.h};
	}
	if (!t) {
		return {0, 0};
	}
	if (alpha != 255) {
		SDL_SetTextureAlphaMod(t, alpha);
	}
	SDL_RenderCopy(renderer_, t, &src, &dst);
	if (alpha != 255) {
		SDL_SetTextureAlphaMod(t, 255);
	}
	return {src.w, src.h};
}

void GameWindowArt::free_animations() {
	for (Animation& a : animations_) {
		free_strip(a.strip);
	}
	animations_.clear();
}

void GameWindowArt::set_interior(int k, uint32_t now, const InteriorState& state) {
	ensure_common();
	if (k != interior_) {
		leave();  // Another building entered straight from this one.
	}
	state_ = state;
	if (k != interior_) {
		if (picture_) {
			SDL_DestroyTexture(picture_);
			picture_ = nullptr;
		}
		free_animations();
		interior_ = k;
		inside_ = false;
		if (k >= 0) {
			load_interior();
		}
	}
	if (!inside_) {
		inside_ = true;
		opened(now);
	}
}

void GameWindowArt::leave() {
	if (!inside_) {
		return;
	}
	inside_ = false;
	if (interior_ == kBathHouseInterior) {
		// The bath house's side menu closed (0x5A0567..0x5A05AF): its animations 1..4 stopped, the guests kept a day.
		for (int k = 1; k <= 4; ++k) {
			stop_animation(k);
		}
		if (state_.town < kTownSlots) {
			bath_until_[state_.town] = state_.tick + 0x100;
		}
	}
}

void GameWindowArt::ensure_common() {
	if (!common_) {
		common_ = true;
		textures_ini_ = read_text(archive_, "scripts/textures.ini");
		animation_ini_ = read_text(archive_, "scripts/animation.ini");
		const int back = std::atoi(ini_value(animation_ini_, "Animation", "BackTexID").c_str());
		parchment_ = load(uint32_t(back > 0 ? back : 31105), &parchment_frame_, 1);
		pill_ = load(16032, pill_frames_, 9);
		close_ = load(16023, close_frames_, 3);
		const int frame = std::atoi(ini_value(animation_ini_, "Rahmen", "RahmenID").c_str());
		frame_ = load(uint32_t(frame > 0 ? frame : 31002), frame_frames_, 8);
		banner_ = load(31006, &banner_frame_, 1);
		paper_ = load(16001, paper_frames_, 39);
		corners_ = load(16058, corner_frames_, 4);
		seal_ = load(16059, &seal_frame_, 1);
		// The Spanish game's TrueType fonts (ttffonts.ini, «Params=width height», the em square): the titles' font 3
		// (Elgreco, 22 × 20) and the pages' and buttons' font 1 (Tiepolo bold, 13 × 15), with the texts' pictures. Its
		// rasterizer (WalType.dll) draws them narrower than that width: the lines on photos of the game's screen
		// (re/emu/sesion31/pc_shots) measure 0.93 to 0.95 of the face at those sizes, so a pixel or so less.
		if (auto font = p3::data::FontAtlas::load_em(archive_, "fonts/Elgreco_.ttf", 21, 20)) {
			title_ = std::make_unique<TextRenderer>(renderer_, std::move(font));
		}
		// The tables' heads' font 0 (Tiepolo black, 14 × 15; 0x6DCD28), a pixel narrower as the others.
		if (auto font = p3::data::FontAtlas::load_em(archive_, "fonts/Tiepolo_black.ttf", 13, 15)) {
			head_ = std::make_unique<TextRenderer>(renderer_, std::move(font));
			head_->set_archive(&archive_);
		}
		// (The side menus' buttons too: 0x5A8F2C sets font 2, but each button draws in its style's font, buttons.ini's
		// FontID=1.)
		if (auto font = p3::data::FontAtlas::load_em(archive_, "fonts/Tiepolo_bold.ttf", 12, 15)) {
			page_ = std::make_unique<TextRenderer>(renderer_, std::move(font));
			page_->set_archive(&archive_);
		}
	}
}

void GameWindowArt::load_interior() {
	const std::string section = "Anim" + std::to_string(interior_);
	const int id = std::atoi(ini_value(animation_ini_, section, "BGID").c_str());
	if (id > 0) {
		picture_ = load(uint32_t(id), &picture_frame_, 1);
	}
	const int count = std::atoi(ini_value(animation_ini_, section, "Animations").c_str());
	for (int n = 0; n < count; ++n) {
		const std::string key = "Anim" + std::to_string(n);
		auto value = [&](const char* what) { return std::atoi(ini_value(animation_ini_, section, (key + what).c_str()).c_str()); };
		Animation a;
		load_strip(uint32_t(value("")), a.strip);
		a.x = value("X");
		a.y = value("Y");
		a.time = value("Time");
		a.count = std::max(1, value("Frames"));
		animations_.push_back(std::move(a));
	}
}

namespace {

// The talking faces of animation.ini's [GebaeudeAni] (0x46B110's names under images/Gebaeude_innen/, its case for
// each: the frame's number with two or one zero before it below 10 or from 10, and its place over the portrait,
// 0x46B7C4..0x46B8C2 by the table 0x46B94C, from the window's middle and the portrait's top).
struct FaceFiles {
	const char* below_ten;
	const char* from_ten;
	int x;  // From the window's middle.
	int y;  // From the portrait's top.
};
constexpr FaceFiles kFaces[19] = {
	{"Kneipe/Matrose_start/Matrose_Start000%i.bmp", "Kneipe/Matrose_start/Matrose_Start00%i.bmp", -109, 0},
	{"Kneipe/Matrose_Loop/Matrose_Loop000%i.bmp", "Kneipe/Matrose_Loop/Matrose_Loop00%i.bmp", -109, 0},
	{"Kneipe/Informant_start/Matrose_00%i.bmp", "Kneipe/Informant_start/Matrose_0%i.bmp", -90, 13},
	{"Kneipe/Informant_Loop/Matrose_Loop00%i.bmp", "Kneipe/Informant_Loop/Matrose_Loop0%i.bmp", -90, 13},
	{"Kneipe/Pirat_start/Pirat_start00%i.bmp", "Kneipe/Pirat_start/Pirat_start0%i.bmp", -97, 8},
	{"Kneipe/Pirat_loop/Pirat_loop00%i.bmp", "Kneipe/Pirat_loop/Pirat_loop0%i.bmp", -97, 8},
	// 6..9: the church's priest (258 × 160 over the whole portrait): talking, thanking; with the altar the same.
	{"Kirche/ohneAltar/Priester_loop_ohneAltar000%i.bmp", "Kirche/ohneAltar/Priester_loop_ohneAltar00%i.bmp", -140, 0},
	{"Kirche/Dank_ohneAltar/Priester_Dank_ohneAltar000%i.bmp", "Kirche/Dank_ohneAltar/Priester_Dank_ohneAltar00%i.bmp",
		-140, 0},
	{"Kirche/mitAltar/Priester_loop_mitAltar000%i.bmp", "Kirche/mitAltar/Priester_loop_mitAltar00%i.bmp", -140, 0},
	{"Kirche/Dank_mitAltar/Priester_Dank_mitAltar000%i.bmp", "Kirche/Dank_mitAltar/Priester_Dank_mitAltar00%i.bmp",
		-140, 0},
	{"Kneipe/Matrose_spuckt/Matrose_spuckt000%i.bmp", "Kneipe/Matrose_spuckt/Matrose_spuckt00%i.bmp", -109, 0},
	{"Kneipe/Kapitaen_Start/Kapitaen_Start000%i.bmp", "Kneipe/Kapitaen_Start/Kapitaen_Start00%i.bmp", -140, 0},
	{"Kneipe/Kapitaen_Loop/Kapitaen_Loop000%i.bmp", "Kneipe/Kapitaen_Loop/Kapitaen_Loop00%i.bmp", -140, 0},
	{"Kneipe/Waffenhaendler_Start/Start000%i.bmp", "Kneipe/Waffenhaendler_Start/Start00%i.bmp", -37, 0},
	{"Kneipe/Waffenhaendler_Loop/Loop000%i.bmp", "Kneipe/Waffenhaendler_Loop/Loop00%i.bmp", -37, 0},
	{"Kneipe/Einbrecher/Start/Einbrecher_Start000%i.bmp", "Kneipe/Einbrecher/Start/Einbrecher_Start00%i.bmp", -140, 0},
	{"Kneipe/Einbrecher/Loop/Einbrecher_Loop000%i.bmp", "Kneipe/Einbrecher/Loop/Einbrecher_Loop00%i.bmp", -140, 0},
	{"Kneipe/Reisender/Start/Reisender_Start000%i.bmp", "Kneipe/Reisender/Start/Reisender_Start00%i.bmp", -140, 0},
	{"Kneipe/Reisender/Loop/Reisender_Loop000%i.bmp", "Kneipe/Reisender/Loop/Reisender_Loop00%i.bmp", -140, 0},
};

// Each portrait's picture and its faces (the start, then the loop; -1 none); the priest's loop goes back and forth
// and his thanks plays when asked (priest_thanks).
struct PortraitInfo {
	uint32_t tex;
	int start;
	int loop;
	int thanks = -1;
};
PortraitInfo portrait_info(Portrait p) {
	switch (p) {
	case Portrait::Sailors: return {31009, 0, 1};
	case Portrait::Captain: return {31008, 11, 12};
	case Portrait::Informer: return {31007, 2, 3};
	case Portrait::Traveller: return {31100, 17, 18};
	case Portrait::WeaponsDealer: return {31011, -1, -1};
	case Portrait::Burglar: return {31099, 15, 16};
	case Portrait::Pirate: return {31010, 4, 5};
	case Portrait::Client: return {31011, 13, 14};
	// The church's (0x5C98FA: by the town's poor fed, [+0x1D5E] = 0x4FE360; its faces by the same, 0x5C958F, 0x5CA170).
	case Portrait::Priest: return {31062, 6, 6, 7};
	case Portrait::PriestAltar: return {31063, 8, 8, 9};
	default: return {0, -1, -1};
	}
}

constexpr int kPortraitTop = 55;  // 0x66E100.
// The sailor's spit (the face 10, Matrose_spuckt) every 7 s at his loop's end (the page 2's update, 0x5CD613..0x5CD775).
constexpr int kSpitFace = 10;
constexpr uint32_t kSpitWait = 7000;

}  // namespace

void GameWindowArt::free_face() {
	for (SDL_Texture* t : face_.frames) {
		if (t) {
			SDL_DestroyTexture(t);
		}
	}
	face_ = Face();
}

void GameWindowArt::set_face(int animation, uint32_t now) {
	free_face();
	face_.animation = animation;
	if (animation < 0 || animation >= 19) {
		return;
	}
	const std::string n = std::to_string(animation);
	face_.count = std::max(1, std::atoi(ini_value(animation_ini_, "GebaeudeAni", ("FramesAni" + n).c_str()).c_str()));
	const int time = std::atoi(ini_value(animation_ini_, "GebaeudeAni", ("FrameTime" + n).c_str()).c_str());
	face_.next = now + uint32_t(time > 0 ? time : 100);
}

void GameWindowArt::draw_portrait(Portrait p, uint32_t now) {
	ensure_common();
	const PortraitInfo info = portrait_info(p);
	if (p != portrait_) {
		// Another person: his picture, his start's face (0x46B710 at the page's opening, 0x5CF138).
		portrait_ = p;
		if (portrait_picture_) {
			SDL_DestroyTexture(portrait_picture_);
			portrait_picture_ = nullptr;
		}
		if (info.tex != 0) {
			portrait_picture_ = load(info.tex, &portrait_frame_, 1);
		}
		set_face(info.start, now);
		spit_at_ = now + kSpitWait;  // The sailors' page opened (0x5CF240).
	}
	if (!torn_across_) {
		torn_across_ = load(31013, torn_across_frames_, 2);
		torn_down_ = load(31014, torn_down_frames_, 2);
	}
	if (!portrait_picture_) {
		return;
	}
	const int middle = kFrameLeft + kInteriorWidth / 2;
	const int left = middle - 140;
	const int top = kFrameTop + kPortraitTop;
	SDL_Rect dst = {left, top, portrait_frame_.w, portrait_frame_.h};
	SDL_RenderCopy(renderer_, portrait_picture_, &portrait_frame_, &dst);
	if (face_.animation >= 0 && kFaces[face_.animation].below_ten[0] != '\0') {
		const FaceFiles& files = kFaces[face_.animation];
		// The next frames read (two a frame at most).
		for (int k = 0; k < 2 && int(face_.frames.size()) < face_.count; ++k) {
			const int i = int(face_.frames.size());
			char name[160];
			std::snprintf(name, sizeof name, i < 10 ? files.below_ten : files.from_ten, i);
			p3::data::Image image;
			const std::string path = std::string("images/Gebaeude_innen/") + name;
			face_.frames.push_back(read_cpr_image(archive_, path.c_str(), image) ? make_texture(renderer_, image) : nullptr);
		}
		// On to the next frame when its time comes and it is read (0x46B530); at the start's end, the loop.
		const int time = std::max(1, std::atoi(ini_value(animation_ini_, "GebaeudeAni",
			("FrameTime" + std::to_string(face_.animation)).c_str()).c_str()));
		// 0x46B530(k, 0, 1), back and forth: a frame on its way (or back from the last one), turned at the first.
		auto back_and_forth = [&] {
			if (int32_t(now - face_.next) < 0) {
				return;
			}
			if (face_.frame < face_.count - 1) {
				const int to = face_.frame + (face_.forward ? 1 : -1);
				if (to < int(face_.frames.size())) {
					face_.frame = to;
					face_.next = now + uint32_t(time);
				}
			} else if (face_.frame > 0) {
				--face_.frame;
				face_.forward = false;
				face_.next = now + uint32_t(time);
			}
			if (face_.frame == 0) {
				face_.forward = true;
			}
		};
		if (info.thanks >= 0) {
			// The priest (the church window's update, 0x5C958F..0x5C964D): asked to thank, he starts it when his loop is
			// back at its first frame and plays it to its last (the request then over, 0x5C9621), then his loop again.
			if (thanks_ && face_.animation != info.thanks && face_.frame == 0) {
				set_face(info.thanks, now);
			} else if (thanks_ && face_.animation == info.thanks) {
				back_and_forth();
				if (face_.frame == face_.count - 1) {
					thanks_ = false;
				}
			} else if (!thanks_ && face_.animation == info.thanks) {
				set_face(info.loop, now);
			} else {
				back_and_forth();
			}
		} else if (int32_t(now - face_.next) >= 0) {
			if (face_.frame < face_.count - 1) {
				if (face_.frame + 1 < int(face_.frames.size())) {
					++face_.frame;
					face_.next = now + uint32_t(time);
				}
			} else if (face_.animation == info.start && info.loop >= 0) {
				set_face(info.loop, now);
			} else if (p == Portrait::Sailors && face_.animation == info.loop && int32_t(now - spit_at_) >= 0) {
				set_face(kSpitFace, now);  // The sailor spits at his loop's end once 7 s have gone (0x5CD6D9; and a sound).
			} else if (face_.animation == kSpitFace) {
				set_face(info.loop, now);  // Back to his loop, the next one 7 s on (0x5CD75F).
				spit_at_ = now + kSpitWait;
			} else {
				face_.frame = 0;  // The loop again.
				face_.next = now + uint32_t(time);
			}
		}
		if (face_.frame < int(face_.frames.size()) && face_.frames[size_t(face_.frame)]) {
			SDL_Texture* t = face_.frames[size_t(face_.frame)];
			int w = 0;
			int h = 0;
			SDL_QueryTexture(t, nullptr, nullptr, &w, &h);
			dst = {middle + files.x, top + files.y, w, h};
			SDL_RenderCopy(renderer_, t, nullptr, &dst);
		}
	}
	// The torn paper frame (0x46B960).
	if (torn_across_) {
		const SDL_Rect& a = torn_across_frames_[0];
		const SDL_Rect& b = torn_across_frames_[1];
		dst = {left, top, a.w, 8};
		SDL_Rect src = {a.x, a.y, a.w, 8};
		SDL_RenderCopy(renderer_, torn_across_, &src, &dst);
		dst = {left, top + 152, b.w, 12};
		src = {b.x, b.y, b.w, 12};
		SDL_RenderCopy(renderer_, torn_across_, &src, &dst);
	}
	if (torn_down_) {
		const SDL_Rect& a = torn_down_frames_[0];
		const SDL_Rect& b = torn_down_frames_[1];
		dst = {left, top + 8, a.w, a.h};
		SDL_RenderCopy(renderer_, torn_down_, &a, &dst);
		dst = {middle + 132, top + 8, b.w, b.h};
		SDL_RenderCopy(renderer_, torn_down_, &b, &dst);
	}
}

const GameWindowArt::Tex& GameWindowArt::tex(uint32_t id) {
	ensure_common();
	for (const auto& [known, t] : texs_) {
		if (known == id) {
			return t;
		}
	}
	Tex t;
	load_strip(id, t);
	texs_.push_back({id, std::move(t)});
	return texs_.back().second;
}

SDL_Point GameWindowArt::tex_size(uint32_t id, int frame) {
	const Tex& t = tex(id);
	if ((!t.texture && t.pieces.empty()) || frame < 0 || size_t(frame) >= t.frames.size()) {
		return {0, 0};
	}
	return {t.frames[size_t(frame)].w, t.frames[size_t(frame)].h};
}

SDL_Point GameWindowArt::draw_tex(uint32_t id, int frame, int x, int y, int width, uint8_t alpha) {
	return draw_strip(tex(id), frame, x, y, width, alpha);
}

int GameWindowArt::menu_rand() {
	seed_ = seed_ * 214013u + 2531011u;  // The CRT's own recipe, on a generator of the viewer's.
	return int((seed_ >> 16) & 0x7FFF);
}

double GameWindowArt::chance() {
	return double(menu_rand()) / 32768.0;
}

bool GameWindowArt::animation_on(int k) const {
	return k >= 0 && size_t(k) < animations_.size() && animations_[size_t(k)].on;
}

void GameWindowArt::start_animation(int k, bool back_and_forth, uint32_t now) {
	if (k < 0 || size_t(k) >= animations_.size() || animations_[size_t(k)].on) {
		return;
	}
	Animation& a = animations_[size_t(k)];
	a.frame = 0;
	a.start = now;
	a.on = true;
	a.stop_at_end = false;
	a.next = -1;
	a.back_and_forth = back_and_forth;
	a.forward = true;
}

void GameWindowArt::stop_at_loop_end(int k) {
	if (k >= 0 && size_t(k) < animations_.size()) {
		animations_[size_t(k)].stop_at_end = true;
	}
}

void GameWindowArt::stop_animation(int k) {
	if (k < 0 || size_t(k) >= animations_.size() || !animations_[size_t(k)].on) {
		return;
	}
	Animation& a = animations_[size_t(k)];
	a.on = false;
	a.next = -1;
	a.stop_at_end = false;
	a.back_and_forth = false;
}

void GameWindowArt::chain_animation(int k, int next) {
	if (k >= 0 && size_t(k) < animations_.size()) {
		animations_[size_t(k)].next = next;
		animations_[size_t(k)].forward = true;
	}
	if (next >= 0 && size_t(next) < animations_.size()) {
		animations_[size_t(next)].forward = true;  // 0x4667EE: the next one forward too.
	}
}

void GameWindowArt::update_animations(uint32_t now) {
	for (size_t k = 0; k < animations_.size(); ++k) {
		Animation& a = animations_[k];
		if (!a.on) {
			continue;
		}
		const int step = std::max(1, a.time / a.count);  // The ms of a frame.
		const int done = int((now - a.start) / uint32_t(step));
		a.frame = a.forward ? done : a.count - done - 1;
		// Going back, it stays on its first frame, on, for ever (0x46691D clamps the frame at 0 before 0x46694C
		// checks the backward end, back and forth and frame < 0: never): a game bug, display only, reproduced. Its
		// only one going back and forth, the tavern's informer turning round (13), so stays on after his first turn,
		// and his other gestures (3, 4, 5), which wait for 13 to be off, never come again while the game runs. Behind
		// fix_game_bugs (the user's word, 2026-11-14), the backward end is checked before the frame is clamped.
		const bool back_end = a.back_and_forth && a.frame < 0 && p3::data::fix_game_bugs();
		if (a.frame < 0) {
			a.frame = 0;
		}
		if (a.frame < a.count && !back_end) {
			continue;
		}
		// The loop's end (0x466970): again from now; the next one started and this one stopped; stopped if asked to,
		// unless going and coming back; back and forth turns round.
		a.start = now;
		a.frame = 0;
		if (a.next >= 0 && size_t(a.next) < animations_.size()) {
			Animation& b = animations_[size_t(a.next)];
			b.frame = 0;
			b.start = now;
			b.on = true;
			stop_animation(int(k));
		}
		if (a.stop_at_end && (!a.forward || !a.back_and_forth)) {
			stop_animation(int(k));
		}
		if (a.back_and_forth) {
			a.forward = !a.forward;
			a.frame = a.count - 1;
		}
	}
}

// What each building's side menu starts when it opens (the calls after its 0x466430: the tavern 0x5A649F, the
// shipyard 0x5AD9D8, the guild 0x5A3A7A, the bath house 0x5A03A7 / 0x5A0624, the market hall 0x5ACAC2, the church
// 0x5A56A5), and the timers its window sets then (the town hall, the barn, the town gate). No building starts one
// back and forth when it opens (not the bath house's 2: 0x5A03C9 pushes the flag 0 before choosing 1 or 2); only
// the tavern's 13 goes so, at random.
void GameWindowArt::opened(uint32_t now) {
	auto start = [&](std::initializer_list<int> ks) {
		for (int k : ks) {
			start_animation(k, false, now);
		}
	};
	switch (interior_) {
	case kTavernInterior: start({0, 1, 2, 6, 10}); break;
	case kShipyardInterior: start({0, 1, 2, 3, 4}); break;
	case kGuildInterior: start({0, 5, 6}); break;
	case kMarketHallInterior: start({1}); break;
	case kChurchInterior:
		// 0x5A56AA..0x5A57B8: the animation k when the town's poor are fed over k (its window's update keeps it so).
		for (int k = 0; k < 5; ++k) {
			if (state_.church_level > k) {
				start({k});
			}
		}
		break;
	case kBathHouseInterior: {
		// Its side menu opened (0x5A0830): who is in the bath (the player, and each councillor of the town with a chance
		// of 7 in 100 once the day after the last visit has gone; rand() % 100 < 7), then (0x5A0629..0x5A065F) the
		// steam (0), and the water (1) with the player alone in the bath, or else the water with a man in it (2) and the
		// man resting (4).
		const uint32_t t = state_.town < kTownSlots ? state_.town : 0;
		bath_count_ = 1;
		if (bath_until_[t] < state_.tick) {
			bath_mask_[t] = 1;
			for (int k = 1; k <= 4; ++k) {
				if (menu_rand() % 100 < 7) {
					bath_mask_[t] = uint8_t(bath_mask_[t] | (1 << k));
					++bath_count_;
				}
			}
		} else {
			for (int k = 1; k <= 4; ++k) {
				bath_count_ += (bath_mask_[t] >> k) & 1;
			}
		}
		start({0});
		if (bath_count_ == 1) {
			start({1});
		} else {
			start({2, 4});
		}
		break;
	}
	case kTownHallInterior: timer_ = now + 5000; break;  // 0x5A973B: its animation 4 first 5 s after.
	case kBarnInterior: timer_ = now + 5000 + uint32_t(menu_rand() % 5000); break;  // Its window laid out, 0x5E9C8F.
	case kTownGateInterior: timer_ = now + 2000 + uint32_t(menu_rand() % 2000); break;  // Its window laid out, 0x5EF4DA.
	default: break;  // The lender's, the office's and the weaponsmith's start none.
	}
}

// Each frame, what a building's side menu (or its window) starts while it is open: at random, at times, or by the
// game's state.
void GameWindowArt::tick(uint32_t now) {
	switch (interior_) {
	case kTavernInterior: tavern_tick(now); break;
	case kTownHallInterior: town_hall_tick(now); break;
	case kGuildInterior: guild_tick(now); break;
	case kLenderInterior: pair_tick(now, false); break;
	case kOfficeInterior: pair_tick(now, true); break;
	case kMarketHallInterior: market_hall_tick(now); break;
	case kChurchInterior: church_tick(now); break;
	case kBathHouseInterior: bath_tick(now); break;
	case kBarnInterior: barn_tick(now); break;
	case kTownGateInterior: gate_tick(now); break;
	default: break;  // The shipyard's and the weaponsmith's side menus start none.
	}
}

// The guild's side menu's update (0x5A3E03..0x5A3FD7): the two men's gestures in pairs (Mann_A_Teil1/2 the one in
// red, 1 and 2; Mann_B_Teil1/2 the one in blue, 3 and 4; the fire 0 and its light on a stool and a corner, 5 and 6,
// start with the window), each chance rand() / 32768 over 0.99 ([0x677FA8]), each played once: 1 (after 2 if 2 is
// on), 2 (after 1 if 1 is on), 3 (after 4 if 4 is on), 4 (after 3 if 3 is on); a chance rolled for each every frame.
void GameWindowArt::guild_tick(uint32_t now) {
	auto pair = [&](int k, int other) {
		if (chance() > 0.99) {
			if (animation_on(other)) {
				chain_animation(other, k);
			} else {
				start_animation(k, false, now);
			}
			stop_at_loop_end(k);
		}
	};
	pair(1, 2);
	pair(2, 1);
	pair(3, 4);
	pair(4, 3);
}

// The lender's side menu's update (0x5A3042..0x5A310D: the lender writing, 0, or resting, 1) and the office's
// (0x5A4CC9..0x5A4D8A: its two lights, kontor_TP and kontor_TP2): two animations in one place, each once with a
// chance over 0.99: 0 when 1 is off, 1 when 0 is off. The lender rolls a chance only when the other is off, the
// office every frame.
void GameWindowArt::pair_tick(uint32_t now, bool always_roll) {
	for (int k = 0; k < 2; ++k) {
		const int other = 1 - k;
		if (always_roll) {
			if (chance() > 0.99 && !animation_on(other)) {
				start_animation(k, false, now);
				stop_at_loop_end(k);
			}
		} else if (!animation_on(other) && chance() > 0.99) {
			start_animation(k, false, now);
			stop_at_loop_end(k);
		}
	}
}

// The market hall's side menu's update (0x5ACDCC..0x5ACEEC; the notes took it for the church's): the women (0) once
// with a chance over 0.99 every frame; the man's shadow (3) once when the woman's (2) is off, hers once when his is
// off (0.99 each); the trader (1) starts with the window.
void GameWindowArt::market_hall_tick(uint32_t now) {
	if (chance() > 0.99) {
		start_animation(0, false, now);
		stop_at_loop_end(0);
	}
	if (!animation_on(2) && chance() > 0.99) {
		start_animation(3, false, now);
		stop_at_loop_end(3);
	}
	if (!animation_on(3) && chance() > 0.99) {
		start_animation(2, false, now);
		stop_at_loop_end(2);
	}
}

// The church's window's update (0x5C9652..0x5C97F1), each frame while it is open: its animation k (0..4: the four
// statues with their candles, Status01..04, and the altar) on while the town's poor are fed over k (0x4FE360),
// stopped otherwise; nothing at random.
void GameWindowArt::church_tick(uint32_t now) {
	for (int k = 0; k < 5; ++k) {
		if (state_.church_level > k) {
			start_animation(k, false, now);
		} else if (animation_on(k)) {
			stop_animation(k);
		}
	}
}

// The bath house's side menu's update (0x5A0797..0x5A07EB): with someone else in the bath, a chance over 0.995
// ([0x677D28]) each frame chains the man resting (4) and drinking (3) to each other: at 4's loop's end 3 once, then 4
// again (each stopped one loses its chain, 0x4666FC).
void GameWindowArt::bath_tick(uint32_t /*now*/) {
	if (bath_count_ > 1 && chance() > 0.995) {
		chain_animation(4, 3);
		chain_animation(3, 4);
	}
}

// The barn's window's update (0x5E9E77..0x5E9ED2; the land trade's, *0x6E55A0): the horse (0) once every 8 to 13 s
// (rand() % 5000 more).
void GameWindowArt::barn_tick(uint32_t now) {
	if (int32_t(now - timer_) >= 0) {
		start_animation(0, false, now);
		stop_at_loop_end(0);
		timer_ = now + 8000 + uint32_t(menu_rand() % 5000);
	}
}

// The town gate's window's update (0x5EF7A0..0x5EF8B9; the sieges' window): the gate closed (InteriorState), its
// armoured captain (3) alone, 0, 1 and 2 stopped; else the guard at the open gate (0) and, every 8 to 10 s
// (rand() % 2000 more), one of his two gestures (1 with an odd rand(), else 2) once, and 3 stopped.
void GameWindowArt::gate_tick(uint32_t now) {
	if (state_.gate_closed) {
		stop_animation(0);
		stop_animation(1);
		stop_animation(2);
		start_animation(3, false, now);
		return;
	}
	if (int32_t(now - timer_) >= 0) {
		const int k = (menu_rand() & 1) != 0 ? 1 : 2;
		start_animation(k, false, now);
		stop_at_loop_end(k);
		timer_ = now + 8000 + uint32_t(menu_rand() % 2000);
	}
	start_animation(0, false, now);
	stop_animation(3);
}

// The town hall's side menu's update (0x5A94E0..0x5A96FC): the woman's two (0 when 1 isn't on, 1 when 0 isn't, each
// with a chance over 0.99), the man's (2 with one over 0.5 when 3 isn't on; 3 over 0.99, after 2 if 2 is on), each
// once; the 4th every 10 to 20 s.
void GameWindowArt::town_hall_tick(uint32_t now) {
	if (!animation_on(1) && chance() > 0.99) {
		start_animation(0, false, now);
		stop_at_loop_end(0);
	}
	if (!animation_on(0) && chance() > 0.99) {
		start_animation(1, false, now);
		stop_at_loop_end(1);
	}
	if (!animation_on(3) && chance() > 0.5) {
		start_animation(2, false, now);
		stop_at_loop_end(2);
	}
	const double r = chance();
	if (r > 0.99) {
		if (!animation_on(2)) {
			start_animation(3, false, now);
		} else {
			chain_animation(2, 3);
		}
		stop_at_loop_end(3);
	}
	if (int32_t(now - timer_) >= 0) {
		start_animation(4, false, now);
		stop_at_loop_end(4);
		timer_ = now + 10000 + uint32_t(chance() * 10000);
	}
}

// The tavern's people at random each frame (its side menu's update, 0x5A6A30..0x5A6D90), each chance rand() / 32768
// over its mark: 7 and 8 once each (0.99); 9 once (0.999), or after 12 if 12 is on (0.99); 12 once when 9 is off
// (0.99); 11 once after 10, then 10 again, when 11 is off (0.995; 10 starts with the window); 13 there and back
// once when 3, 4 and 5 are off (0.99); 3 then 4 when 13, 4 and 5 are off (0.99); while 4 is on, 5 after it once (0.9).
void GameWindowArt::tavern_tick(uint32_t now) {
	auto once = [&](int k, bool back_and_forth = false) {
		start_animation(k, back_and_forth, now);
		stop_at_loop_end(k);
	};
	if (chance() > 0.99) {
		once(7);
	}
	if (chance() > 0.99) {
		once(8);
	}
	const double r = chance();
	if (!animation_on(12)) {
		if (r > 0.999) {
			once(9);
		}
	} else if (r > 0.99) {
		chain_animation(12, 9);
		stop_at_loop_end(9);
	}
	if (!animation_on(9) && chance() > 0.99) {
		once(12);
	}
	if (!animation_on(11) && chance() > 0.995) {
		chain_animation(10, 11);
		chain_animation(11, 10);
	}
	if (!animation_on(5) && !animation_on(4) && !animation_on(3) && chance() > 0.99) {
		once(13, true);
	}
	if (!animation_on(13) && !animation_on(4) && !animation_on(5) && chance() > 0.99) {
		start_animation(3, false, now);
		chain_animation(3, 4);
	}
	if (animation_on(4) && chance() > 0.9) {
		chain_animation(4, 5);
		stop_at_loop_end(5);
	}
}

void GameWindowArt::begin(const SDL_Rect& on_screen, int parchment_top, uint32_t now, bool framed) {
	tick(now);
	update_animations(now);
	on_screen_ = on_screen;
	if (!target_ && !target_failed_) {
		target_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, kWindowWidth, kWindowHeight);
		target_failed_ = !target_;
		if (target_) {
			SDL_SetTextureBlendMode(target_, SDL_BLENDMODE_BLEND);
			SDL_SetTextureScaleMode(target_, SDL_ScaleModeLinear);
		}
	}
	drawing_into_target_ = target_ && SDL_SetRenderTarget(renderer_, target_) == 0;
	if (drawing_into_target_) {
		SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 0);
		SDL_RenderClear(renderer_);
	} else {
		SDL_RenderSetViewport(renderer_, &on_screen_);
		SDL_RenderSetScale(renderer_, float(on_screen_.w) / kWindowWidth, float(on_screen_.h) / kWindowHeight);
	}
	SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
	if (!framed) {
		ensure_common();
		return;
	}
	const int x = kFrameLeft;
	const int y = kFrameTop;
	if (picture_) {
		const SDL_Rect dst = {x, y, picture_frame_.w, picture_frame_.h};
		SDL_RenderCopy(renderer_, picture_, &picture_frame_, &dst);
	}
	// The animations on it (0x465CC9: their frame at their place).
	for (const Animation& a : animations_) {
		if (a.on) {
			draw_strip(a.strip, a.frame, x + a.x, y + a.y);
		}
	}
	if (parchment_top >= 0 && parchment_) {
		if (!picture_) {
			const SDL_Rect dst = {x, y, parchment_frame_.w, parchment_frame_.h};
			SDL_RenderCopy(renderer_, parchment_, &parchment_frame_, &dst);
		} else {
			// The 160 lines above the top fading in, a line at a time (alpha 0 to 159), then the rest at 160.
			const int top = std::min(parchment_top, parchment_frame_.h);
			for (int i = 0; i < kParchmentOverPicture; ++i) {
				const int row = top - kParchmentOverPicture + i;
				if (row < 0) {
					continue;
				}
				SDL_SetTextureAlphaMod(parchment_, Uint8(i));
				const SDL_Rect src = {parchment_frame_.x, parchment_frame_.y + row, parchment_frame_.w, 1};
				const SDL_Rect dst = {x, y + row, parchment_frame_.w, 1};
				SDL_RenderCopy(renderer_, parchment_, &src, &dst);
			}
			SDL_SetTextureAlphaMod(parchment_, kParchmentOverPicture);
			const SDL_Rect src = {parchment_frame_.x, parchment_frame_.y + top, parchment_frame_.w, parchment_frame_.h - top};
			const SDL_Rect dst = {x, y + top, parchment_frame_.w, parchment_frame_.h - top};
			SDL_RenderCopy(renderer_, parchment_, &src, &dst);
			SDL_SetTextureAlphaMod(parchment_, 255);
		}
	}
	draw_frame();
}

void GameWindowArt::end() {
	if (drawing_into_target_) {
		SDL_SetRenderTarget(renderer_, nullptr);
		SDL_RenderCopy(renderer_, target_, nullptr, &on_screen_);
	} else {
		SDL_RenderSetScale(renderer_, 1.0f, 1.0f);
		SDL_RenderSetViewport(renderer_, nullptr);
	}
	drawing_into_target_ = false;
}

void GameWindowArt::draw_frame() const {
	if (!frame_) {
		return;
	}
	auto put = [&](int k, int x, int y) {
		const SDL_Rect& src = frame_frames_[k];
		const SDL_Rect dst = {x, y, src.w, src.h};
		SDL_RenderCopy(renderer_, frame_, &src, &dst);
	};
	const int w = kWindowWidth;
	const int h = kWindowHeight;
	// As 0x465FA0..0x4662F4: the corners, the top and bottom 17 times from x 12, the left and right sides 34 times
	// from y 12.
	put(0, 0, 0);
	put(1, w - frame_frames_[1].w, 0);
	put(2, w - frame_frames_[2].w, h - frame_frames_[2].h);
	put(3, 0, h - frame_frames_[3].h);
	for (int k = 0; k < 17; ++k) {
		put(4, kFrameLeft + k * frame_frames_[4].w, 0);
		put(5, kFrameLeft + k * frame_frames_[5].w, h - frame_frames_[5].h);
	}
	for (int k = 0; k < 34; ++k) {
		put(7, 0, kFrameTop + k * frame_frames_[7].h);
		put(6, w - frame_frames_[6].w, kFrameTop + k * frame_frames_[6].h);
	}
}

void GameWindowArt::draw_pill(const SDL_Rect& r, PillState state) const {
	if (!pill_) {
		return;
	}
	// The rows' frames give 18 lines from their top (the buttons' Size; the ini's fourth number isn't a height there).
	const int row = state == PillState::Pressed ? 3 : state == PillState::Disabled ? 6 : 0;
	const int h = 18;
	const int y = r.y + (r.h - h) / 2;
	const SDL_Rect& left = pill_frames_[row];
	const SDL_Rect& middle = pill_frames_[row + 1];
	const SDL_Rect& right = pill_frames_[row + 2];
	const int end = std::min(left.w, r.w / 2);
	SDL_Rect src = {left.x, left.y, end, h};
	SDL_Rect dst = {r.x, y, end, h};
	SDL_RenderCopy(renderer_, pill_, &src, &dst);
	src = {right.x + right.w - end, right.y, end, h};
	dst = {r.x + r.w - end, y, end, h};
	SDL_RenderCopy(renderer_, pill_, &src, &dst);
	// The middle, stretched no wider than its own frame at a time.
	for (int x = r.x + end; x < r.x + r.w - end;) {
		const int w = std::min(middle.w, r.x + r.w - end - x);
		src = {middle.x, middle.y, w, h};
		dst = {x, y, w, h};
		SDL_RenderCopy(renderer_, pill_, &src, &dst);
		x += w;
	}
}

void GameWindowArt::draw_close(const SDL_Rect& r, bool pressed) const {
	if (!close_) {
		return;
	}
	const SDL_Rect& src = close_frames_[pressed ? 1 : 0];
	const SDL_Rect dst = {r.x + (r.w - src.w) / 2, r.y + (r.h - src.h) / 2, src.w, src.h};
	SDL_RenderCopy(renderer_, close_, &src, &dst);
}

void GameWindowArt::draw_title(TextRenderer& fallback, int x, int y, int w, const std::string& title) const {
	if (banner_) {
		const SDL_Rect dst = {x + w / 2 - 146, y + 4, banner_frame_.w, banner_frame_.h};
		SDL_RenderCopy(renderer_, banner_, &banner_frame_, &dst);
	}
	TextRenderer& text = title_ ? *title_ : fallback;
	const int centre = x + w / 2 + 9;
	const int top = y + 12 + (title_ ? 0 : (20 - text.line_height()) / 2);
	const int left = centre - text.measure(title) / 2;
	text.draw(title, left + 1, top + 1, {0xEF, 0xD7, 0xC0, 255});
	text.draw(title, left, top, {0, 0, 0, 255});
}

SDL_Rect GameWindowArt::draw_paper(int x, int y, int cols, int rows) const {
	for (int row = 0; row < rows; ++row) {
		for (int col = 0; col < cols; ++col) {
			SDL_Texture* texture = paper_;
			const SDL_Rect* src = nullptr;
			const bool first = col == 0;
			const bool last = col == cols - 1;
			if (row == 0 || row == rows - 1) {
				if (first || last) {
					texture = corners_;
					src = &corner_frames_[(row == 0 ? 0 : 2) + (last ? 1 : 0)];
				} else {
					src = &paper_frames_[col % 3 + (row == 0 ? 10 : 21)];
				}
			} else if (first) {
				src = &paper_frames_[row % 3 + 14];
			} else if (last) {
				src = &paper_frames_[row % 3 + 17];
			} else {
				src = &paper_frames_[30 + 3 * (row % 3) + col % 3];
			}
			if (texture) {
				const SDL_Rect cell = {src->x, src->y, 32, 32};
				const SDL_Rect dst = {x + 32 * col, y + 32 * row, 32, 32};
				SDL_RenderCopy(renderer_, texture, &cell, &dst);
			}
		}
	}
	const int right = x + 32 * cols - 16;
	if (seal_) {
		const SDL_Rect dst = {right - 64, y + 32 * (rows - 3) + 16, 64, 64};
		SDL_RenderCopy(renderer_, seal_, &seal_frame_, &dst);
	}
	return {x + 16, y + 16, right - (x + 16), 32 * rows - 32};
}

// A middle row of a dialog's tiles (0x41C73E..0x41C79B): its left edge, its middle, its right edge.
void GameWindowArt::roll_dialog_row(DialogTiles& d, int row) {
	uint8_t* t = d.tiles.data() + size_t(row) * size_t(d.cols);
	t[0] = uint8_t(0x0E + menu_rand() % 3);
	for (int k = 1; k < d.cols - 1; ++k) {
		t[k] = uint8_t(0x1E + menu_rand() % 9);
	}
	t[d.cols - 1] = uint8_t(0x11 + menu_rand() % 3);
}

void GameWindowArt::draw_dialog(int x, int y, int cols, int rows, bool rolls, int key) {
	if (cols < 2 || rows < 2) {
		return;
	}
	ensure_common();
	auto found = std::find_if(dialogs_.begin(), dialogs_.end(), [&](const auto& e) { return e.first == key; });
	if (found == dialogs_.end()) {
		dialogs_.push_back({key, DialogTiles{}});
		found = dialogs_.end() - 1;
	}
	DialogTiles& d = found->second;
	if (d.cols != cols || d.rolls != rolls || d.tiles.empty()) {
		// 0x41C5A0: a new grid (its corners, the top and bottom edges column by column, then the middle rows).
		d = DialogTiles{cols, rows, rolls, std::vector<uint8_t>(size_t(cols) * size_t(rows))};
		const int top = rolls ? 4 : 9;
		const int bottom = rolls ? 0x19 : 0x14;
		uint8_t* first = d.tiles.data();
		uint8_t* last = d.tiles.data() + size_t(rows - 1) * size_t(cols);
		first[0] = uint8_t(top);
		first[cols - 1] = uint8_t(top + 4);
		last[0] = uint8_t(bottom);
		last[cols - 1] = uint8_t(bottom + 4);
		for (int k = 1; k < cols - 1; ++k) {
			first[k] = uint8_t(top + 1 + menu_rand() % 3);
			last[k] = uint8_t(bottom + 1 + menu_rand() % 3);
		}
		for (int r = 1; r < rows - 1; ++r) {
			roll_dialog_row(d, r);
		}
	} else if (d.rows != rows) {
		// 0x41C420: the rows above the last kept, the last row too, the new middle ones rolled.
		std::vector<uint8_t> tiles(size_t(cols) * size_t(rows));
		const int kept = std::min(rows, d.rows) - 1;
		std::copy(d.tiles.begin(), d.tiles.begin() + std::ptrdiff_t(size_t(kept) * size_t(cols)), tiles.begin());
		std::copy(d.tiles.end() - cols, d.tiles.end(), tiles.end() - cols);
		const int old_rows = d.rows;
		d.tiles = std::move(tiles);
		d.rows = rows;
		for (int r = old_rows - 1; r < rows - 1; ++r) {
			roll_dialog_row(d, r);
		}
	}
	if (!paper_) {
		return;
	}
	// 0x41CAA0: with the rolls, their ends at the window's corners (frames 0..3: left top, right top, left bottom,
	// right bottom), the tiles from their width in.
	int left = x;
	if (rolls) {
		const SDL_Rect* ends = paper_frames_;
		const int w = 32 * cols + 2 * ends[0].w;
		const int h = 32 * rows;
		const SDL_Point at[4] = {{x, y}, {x + w - ends[1].w, y}, {x, y + h - ends[2].h}, {x + w - ends[3].w, y + h - ends[3].h}};
		for (int k = 0; k < 4; ++k) {
			const SDL_Rect dst = {at[k].x, at[k].y, ends[k].w, ends[k].h};
			SDL_RenderCopy(renderer_, paper_, &ends[k], &dst);
		}
		left += ends[3].w;
	}
	for (int r = 0; r < rows; ++r) {
		for (int c = 0; c < cols; ++c) {
			const SDL_Rect& src = paper_frames_[std::min<int>(d.tiles[size_t(r) * size_t(cols) + size_t(c)], 38)];
			const SDL_Rect dst = {left + 32 * c, y + 32 * r, src.w, src.h};
			SDL_RenderCopy(renderer_, paper_, &src, &dst);
		}
	}
}

}  // namespace p3::app
