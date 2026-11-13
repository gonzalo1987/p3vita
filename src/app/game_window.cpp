// The buildings' windows as the game draws them (game_window.hpp).

#include "game_window.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "p3data/font.hpp"
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
	: renderer_(renderer), archive_(archive) {}

GameWindowArt::~GameWindowArt() {
	free_animations();
	free_face();
	for (auto& [id, t] : texs_) {
		if (t.texture) {
			SDL_DestroyTexture(t.texture);
		}
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

SDL_Texture* GameWindowArt::load(uint32_t tex, SDL_Rect* frames, int count) {
	const std::string section = "TEX" + std::to_string(tex);
	const std::string file = ini_value(textures_ini_, section, "TextureFile");
	p3::data::Image image;
	if (file.empty() || !read_cpr_image(archive_, file.c_str(), image)) {
		return nullptr;
	}
	if (ini_value(textures_ini_, section, "UseColorKey") == "true") {
		// Its colour key («ColorKey=0xAARRGGBB»), transparent.
		const uint32_t key = uint32_t(std::strtoul(ini_value(textures_ini_, section, "ColorKey").c_str(), nullptr, 16));
		for (size_t i = 0; i + 3 < image.rgba.size(); i += 4) {
			if (image.rgba[i] == uint8_t(key >> 16) && image.rgba[i + 1] == uint8_t(key >> 8) && image.rgba[i + 2] == uint8_t(key)) {
				image.rgba[i + 3] = 0;
			}
		}
	}
	for (int k = 0; k < count; ++k) {
		frames[k] = {0, 0, image.width, image.height};
	}
	const std::vector<uint8_t> ini(textures_ini_.begin(), textures_ini_.end());
	read_texture_frames(ini, ("[" + section + "]").c_str(), frames, count);
	return make_texture(renderer_, image);
}

SDL_Texture* GameWindowArt::load_frames(uint32_t tex, std::vector<SDL_Rect>& frames) {
	const int count = std::max(1, std::atoi(ini_value(textures_ini_, "TEX" + std::to_string(tex), "Count").c_str()));
	frames.assign(size_t(count), SDL_Rect{});
	return load(tex, frames.data(), count);
}

void GameWindowArt::free_animations() {
	for (Animation& a : animations_) {
		if (a.texture) {
			SDL_DestroyTexture(a.texture);
		}
	}
	animations_.clear();
}

void GameWindowArt::set_interior(int k, uint32_t now) {
	ensure_common();
	if (k == interior_) {
		return;
	}
	if (picture_) {
		SDL_DestroyTexture(picture_);
		picture_ = nullptr;
	}
	free_animations();
	interior_ = k;
	if (k < 0) {
		return;
	}
	load_interior(now);
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

void GameWindowArt::load_interior(uint32_t now) {
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
		a.texture = load_frames(uint32_t(value("")), a.frames);
		a.x = value("X");
		a.y = value("Y");
		a.time = value("Time");
		a.count = std::max(1, value("Frames"));
		animations_.push_back(std::move(a));
	}
	opened(now);
}

namespace {

// The talking faces of animation.ini's [GebaeudeAni] (0x46B110's names, its case for each: the frame's number with two
// or one zero before it below 10 or from 10, and its place over the portrait, 0x46B7C4..0x46B8C2, from the window's
// middle and the portrait's top).
struct FaceFiles {
	const char* below_ten;
	const char* from_ten;
	int x;  // From the window's middle.
	int y;  // From the portrait's top.
};
constexpr FaceFiles kFaces[19] = {
	{"Matrose_start/Matrose_Start000%i.bmp", "Matrose_start/Matrose_Start00%i.bmp", -109, 0},
	{"Matrose_Loop/Matrose_Loop000%i.bmp", "Matrose_Loop/Matrose_Loop00%i.bmp", -109, 0},
	{"Informant_start/Matrose_00%i.bmp", "Informant_start/Matrose_0%i.bmp", -90, 13},
	{"Informant_Loop/Matrose_Loop00%i.bmp", "Informant_Loop/Matrose_Loop0%i.bmp", -90, 13},
	{"Pirat_start/Pirat_start00%i.bmp", "Pirat_start/Pirat_start0%i.bmp", -97, 8},
	{"Pirat_loop/Pirat_loop00%i.bmp", "Pirat_loop/Pirat_loop0%i.bmp", -97, 8},
	{"", "", -140, 0},  // 6..9: the church's priest (images/Gebaeude_innen/Kirche/...), with the church.
	{"", "", -140, 0},
	{"", "", -140, 0},
	{"", "", -140, 0},
	{"Matrose_spuckt/Matrose_spuckt000%i.bmp", "Matrose_spuckt/Matrose_spuckt00%i.bmp", -109, 0},
	{"Kapitaen_Start/Kapitaen_Start000%i.bmp", "Kapitaen_Start/Kapitaen_Start00%i.bmp", -140, 0},
	{"Kapitaen_Loop/Kapitaen_Loop000%i.bmp", "Kapitaen_Loop/Kapitaen_Loop00%i.bmp", -140, 0},
	{"Waffenhaendler_Start/Start000%i.bmp", "Waffenhaendler_Start/Start00%i.bmp", -37, 0},
	{"Waffenhaendler_Loop/Loop000%i.bmp", "Waffenhaendler_Loop/Loop00%i.bmp", -37, 0},
	{"Einbrecher/Start/Einbrecher_Start000%i.bmp", "Einbrecher/Start/Einbrecher_Start00%i.bmp", -140, 0},
	{"Einbrecher/Loop/Einbrecher_Loop000%i.bmp", "Einbrecher/Loop/Einbrecher_Loop00%i.bmp", -140, 0},
	{"Reisender/Start/Reisender_Start000%i.bmp", "Reisender/Start/Reisender_Start00%i.bmp", -140, 0},
	{"Reisender/Loop/Reisender_Loop000%i.bmp", "Reisender/Loop/Reisender_Loop00%i.bmp", -140, 0},
};

// Each portrait's picture and its faces (the start, then the loop; -1 none).
struct PortraitInfo {
	uint32_t tex;
	int start;
	int loop;
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
			const std::string path = std::string("images/Gebaeude_innen/Kneipe/") + name;
			face_.frames.push_back(read_cpr_image(archive_, path.c_str(), image) ? make_texture(renderer_, image) : nullptr);
		}
		// On to the next frame when its time comes and it is read (0x46B530); at the start's end, the loop.
		const int time = std::max(1, std::atoi(ini_value(animation_ini_, "GebaeudeAni",
			("FrameTime" + std::to_string(face_.animation)).c_str()).c_str()));
		if (int32_t(now - face_.next) >= 0) {
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
	t.texture = load_frames(id, t.frames);
	texs_.push_back({id, std::move(t)});
	return texs_.back().second;
}

SDL_Point GameWindowArt::tex_size(uint32_t id, int frame) {
	const Tex& t = tex(id);
	if (!t.texture || frame < 0 || size_t(frame) >= t.frames.size()) {
		return {0, 0};
	}
	return {t.frames[size_t(frame)].w, t.frames[size_t(frame)].h};
}

SDL_Point GameWindowArt::draw_tex(uint32_t id, int frame, int x, int y) {
	const Tex& t = tex(id);
	if (!t.texture || frame < 0 || size_t(frame) >= t.frames.size()) {
		return {0, 0};
	}
	const SDL_Rect& src = t.frames[size_t(frame)];
	const SDL_Rect dst = {x, y, src.w, src.h};
	SDL_RenderCopy(renderer_, t.texture, &src, &dst);
	return {src.w, src.h};
}

double GameWindowArt::chance() {
	seed_ = seed_ * 214013u + 2531011u;  // The CRT's own recipe, on a generator of the viewer's.
	return double((seed_ >> 16) & 0x7FFF) / 32768.0;
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
		if (a.frame < 0) {
			a.frame = 0;  // Going back, it stays on its first frame (0x46691D: the end is only checked forward).
		}
		if (a.frame < a.count) {
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
// shipyard 0x5AD9D8, the guild 0x5A3A7A, the bath house 0x5A03A7, the market hall 0x5ACAC2; the church's depend on
// the town's church, 0x5A56A5, still to do; the others start none).
void GameWindowArt::opened(uint32_t now) {
	timer_ = now + 5000;  // The town hall's 0x5A9710: its animation 4 first 5 s after.
	auto start = [&](std::initializer_list<int> ks, bool back_and_forth = false) {
		for (int k : ks) {
			start_animation(k, back_and_forth, now);
		}
	};
	switch (interior_) {
	case kTavernInterior: start({0, 1, 2, 6, 10}); break;
	case kShipyardInterior: start({0, 1, 2, 3, 4}); break;
	case kGuildInterior: start({0, 5, 6}); break;
	case kBathHouseInterior: start({0, 4}); start({2}, true); break;
	case kMarketHallInterior: start({1}); break;
	default: break;
	}
}

// Each frame, what a side menu starts at random while its building is open. The town hall's (0x5A94E0..0x5A96FC):
// the woman's two (0 when 1 isn't on, 1 when 0 isn't, each with a chance over 0.99), the man's (2 with one over 0.5
// when 3 isn't on; 3 over 0.99, after 2 if 2 is on), each once; the 4th every 10 to 20 s.
void GameWindowArt::tick(uint32_t now) {
	if (interior_ == kTavernInterior) {
		tavern_tick(now);
		return;
	}
	if (interior_ != kTownHallInterior) {
		return;
	}
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
// (0.99); 10 and 11 one after the other for ever when 11 is off (0.995; 10 starts with the window); 13 there and back
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

void GameWindowArt::begin(const SDL_Rect& on_screen, int parchment_top, uint32_t now) {
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
	const int x = kFrameLeft;
	const int y = kFrameTop;
	if (picture_) {
		const SDL_Rect dst = {x, y, picture_frame_.w, picture_frame_.h};
		SDL_RenderCopy(renderer_, picture_, &picture_frame_, &dst);
	}
	// The animations on it (0x465CC9: their frame at their place).
	for (const Animation& a : animations_) {
		if (a.on && a.texture && a.frame >= 0 && size_t(a.frame) < a.frames.size()) {
			const SDL_Rect& src = a.frames[size_t(a.frame)];
			const SDL_Rect dst = {x + a.x, y + a.y, src.w, src.h};
			SDL_RenderCopy(renderer_, a.texture, &src, &dst);
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

}  // namespace p3::app
