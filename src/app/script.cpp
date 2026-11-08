#include "script.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#ifndef __vita__
#include "stb_image_write.h"
#endif

namespace p3::app {

namespace {

constexpr int kTicksPerDay = 256;
constexpr int kScreenWidth = 960;
constexpr int kScreenHeight = 544;

struct ButtonName {
	const char* name;
	SDL_GameControllerButton button;
};

// The Vita's names; SDL maps them as in main.cpp (Triangle = Y, X = A...).
constexpr ButtonName kButtons[] = {
	{"x", SDL_CONTROLLER_BUTTON_A},
	{"circle", SDL_CONTROLLER_BUTTON_B},
	{"square", SDL_CONTROLLER_BUTTON_X},
	{"triangle", SDL_CONTROLLER_BUTTON_Y},
	{"l", SDL_CONTROLLER_BUTTON_LEFTSHOULDER},
	{"r", SDL_CONTROLLER_BUTTON_RIGHTSHOULDER},
	{"select", SDL_CONTROLLER_BUTTON_BACK},
	{"start", SDL_CONTROLLER_BUTTON_START},
	{"up", SDL_CONTROLLER_BUTTON_DPAD_UP},
	{"down", SDL_CONTROLLER_BUTTON_DPAD_DOWN},
	{"left", SDL_CONTROLLER_BUTTON_DPAD_LEFT},
	{"right", SDL_CONTROLLER_BUTTON_DPAD_RIGHT},
};

std::string lower(std::string s) {
	for (char& c : s) {
		c = char(std::tolower(static_cast<unsigned char>(c)));
	}
	return s;
}

std::string trim(const std::string& s) {
	size_t begin = 0;
	size_t end = s.size();
	while (begin < end && std::isspace(static_cast<unsigned char>(s[begin]))) {
		++begin;
	}
	while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
		--end;
	}
	return s.substr(begin, end - begin);
}

// A whole non-negative number, or -1.
int number(const std::string& s) {
	if (s.empty() || s.size() > 9) {
		return -1;
	}
	for (char c : s) {
		if (!std::isdigit(static_cast<unsigned char>(c))) {
			return -1;
		}
	}
	return std::atoi(s.c_str());
}

}  // namespace

const char kStepsHelp[] =
	"Steps (separated by commas, played one frame at a time; a frame is 1/60 s\n"
	"of game time):\n"
	"  X, Circle, Square, Triangle, L, R, Select, Start, Up, Down, Left, Right\n"
	"               press the Vita button (down one frame, up the next)\n"
	"  BUTTON*N     hold it down for N frames (the pad moves the camera 12 px a\n"
	"               frame)\n"
	"  frames:N     N frames without input (time passes at the game's speed)\n"
	"  days:N       N frames of fast forward, one game day (256 ticks) each\n"
	"  ticks:N      one frame of fast forward, N ticks\n"
	"  tap:X:Y      touch the screen at (X, Y) and let go\n"
	"  touch:X:Y    put a finger on the screen at (X, Y); lift:X:Y lifts it there\n"
	"  move:X:Y     slide the finger that is down to (X, Y) in one motion\n"
	"  cell:X:Y     center the camera on the town's cell (X, Y)\n"
	"  text:TEXT    type TEXT (an SDL_TEXTINPUT, as the keyboard gives it)\n"
	"  shot:FILE    save the screen as a PNG\n";

std::unique_ptr<InputScript> InputScript::parse(const std::string& steps, std::string& error) {
	auto script = std::make_unique<InputScript>();
	size_t pos = 0;
	while (pos <= steps.size()) {
		size_t end = steps.find(',', pos);
		if (end == std::string::npos) {
			end = steps.size();
		}
		const std::string text = trim(steps.substr(pos, end - pos));
		pos = end + 1;
		if (text.empty()) {
			continue;
		}
		Step step;
		const size_t colon = text.find(':');
		const std::string head = lower(colon == std::string::npos ? text : text.substr(0, colon));
		const std::string rest = colon == std::string::npos ? "" : text.substr(colon + 1);
		if (head == "shot" && colon != std::string::npos && !rest.empty()) {
			step.kind = Kind::Shot;
			step.file = rest;
		} else if (head == "text" && colon != std::string::npos && !rest.empty()) {
			step.kind = Kind::Text;
			step.file = rest;
		} else if ((head == "frames" || head == "days") && colon != std::string::npos) {
			step.kind = head == "frames" ? Kind::Frames : Kind::Days;
			step.count = number(rest);
			if (step.count < 0) {
				error = "wrong number in step \"" + text + "\"";
				return nullptr;
			}
		} else if (head == "ticks" && colon != std::string::npos) {
			step.kind = Kind::Ticks;
			step.ticks = number(rest);
			if (step.ticks < 0) {
				error = "wrong number in step \"" + text + "\"";
				return nullptr;
			}
		} else if ((head == "tap" || head == "cell" || head == "touch" || head == "lift" || head == "move") &&
			colon != std::string::npos) {
			const size_t second = rest.find(':');
			step.kind = head == "tap" ? Kind::Tap
				: head == "touch"     ? Kind::Touch
				: head == "lift"      ? Kind::Lift
				: head == "move"      ? Kind::Move
									  : Kind::Cell;
			step.x = second == std::string::npos ? -1 : number(rest.substr(0, second));
			step.y = second == std::string::npos ? -1 : number(rest.substr(second + 1));
			const bool off_screen = step.kind != Kind::Cell && (step.x >= kScreenWidth || step.y >= kScreenHeight);
			if (step.x < 0 || step.y < 0 || off_screen) {
				error = "wrong position in step \"" + text + "\"" + (off_screen ? " (the screen is 960x544)" : "");
				return nullptr;
			}
		} else if (colon == std::string::npos) {
			const size_t star = head.find('*');
			const std::string name = head.substr(0, star);
			for (const ButtonName& b : kButtons) {
				if (name == b.name) {
					step.button = b.button;
				}
			}
			if (star != std::string::npos) {
				step.kind = Kind::Hold;
				step.count = number(head.substr(star + 1));
			}
			if (step.button == SDL_CONTROLLER_BUTTON_INVALID || step.count < 1) {
				error = "unknown step \"" + text + "\"";
				return nullptr;
			}
		} else {
			error = "unknown step \"" + text + "\"";
			return nullptr;
		}
		script->steps_.push_back(step);
	}
	return script;
}

void InputScript::push_button(SDL_GameControllerButton button, bool down) const {
	SDL_Event event{};
	event.type = down ? SDL_CONTROLLERBUTTONDOWN : SDL_CONTROLLERBUTTONUP;
	event.cbutton.button = Uint8(button);
	event.cbutton.state = down ? SDL_PRESSED : SDL_RELEASED;
	SDL_PushEvent(&event);
}

void InputScript::push_finger(Uint32 type, int x, int y, int dx, int dy) const {
	SDL_Event event{};
	event.type = type;
	event.tfinger.x = float(x) / kScreenWidth;
	event.tfinger.y = float(y) / kScreenHeight;
	event.tfinger.dx = float(dx) / kScreenWidth;
	event.tfinger.dy = float(dy) / kScreenHeight;
	event.tfinger.pressure = type == SDL_FINGERUP ? 0.0f : 1.0f;
	SDL_PushEvent(&event);
}

void InputScript::start_frame() {
	++frame_;
	extra_ticks_ = 0;
	screenshot_.clear();
	look_x_ = -1;
	look_y_ = -1;
	// The end of a press or a hold: the button goes up in the frame after.
	if (held_ != SDL_CONTROLLER_BUTTON_INVALID && progress_ == 0) {
		push_button(held_, false);
		held_ = SDL_CONTROLLER_BUTTON_INVALID;
	}
	if (current_ >= steps_.size()) {
		finished_ = held_ == SDL_CONTROLLER_BUTTON_INVALID;
		return;
	}
	const Step& step = steps_[current_];
	const int length = step.kind == Kind::Tap ? 2 : step.count;
	switch (step.kind) {
	case Kind::Press:
	case Kind::Hold:
		if (progress_ == 0) {
			push_button(step.button, true);
		}
		held_ = step.button;
		break;
	case Kind::Frames:
		break;
	case Kind::Days:
		extra_ticks_ = kTicksPerDay;
		break;
	case Kind::Ticks:
		extra_ticks_ = step.ticks;
		break;
	case Kind::Shot:
		screenshot_ = step.file;
		break;
	case Kind::Tap:
		push_finger(progress_ == 0 ? SDL_FINGERDOWN : SDL_FINGERUP, step.x, step.y);
		finger_x_ = step.x;
		finger_y_ = step.y;
		break;
	case Kind::Touch:
	case Kind::Lift:
		push_finger(step.kind == Kind::Touch ? SDL_FINGERDOWN : SDL_FINGERUP, step.x, step.y);
		finger_x_ = step.x;
		finger_y_ = step.y;
		break;
	case Kind::Move:
		// The finger slides from where it is to (x, y) in one motion.
		push_finger(SDL_FINGERMOTION, step.x, step.y, step.x - finger_x_, step.y - finger_y_);
		finger_x_ = step.x;
		finger_y_ = step.y;
		break;
	case Kind::Cell:
		look_x_ = step.x;
		look_y_ = step.y;
		break;
	case Kind::Text: {
		// Typed text (as SDL's keyboard, or the Vita's, gives it).
		SDL_Event event{};
		event.type = SDL_TEXTINPUT;
		std::strncpy(event.text.text, step.file.c_str(), sizeof(event.text.text) - 1);
		SDL_PushEvent(&event);
		break;
	}
	}
	if (++progress_ >= length) {
		progress_ = 0;
		++current_;
	}
}

bool InputScript::held(SDL_GameControllerButton button) const {
	return held_ == button;
}

bool save_screenshot(SDL_Renderer* renderer, int width, int height, const std::string& path, std::string& error) {
#ifdef __vita__
	(void)renderer;
	(void)width;
	(void)height;
	(void)path;
	error = "screenshots are only saved on the PC";
	return false;
#else
	std::vector<unsigned char> pixels(size_t(width) * size_t(height) * 4);
	if (SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_RGBA32, pixels.data(), width * 4) != 0) {
		error = SDL_GetError();
		return false;
	}
	// Opaque: the PNG shows the screen, not the alpha left by the blending.
	for (size_t i = 3; i < pixels.size(); i += 4) {
		pixels[i] = 255;
	}
	std::vector<unsigned char> png;
	auto append = [](void* context, void* data, int size) {
		auto* out = static_cast<std::vector<unsigned char>*>(context);
		out->insert(out->end(), static_cast<unsigned char*>(data), static_cast<unsigned char*>(data) + size);
	};
	std::FILE* file = nullptr;
	if (!stbi_write_png_to_func(append, &png, width, height, 4, pixels.data(), width * 4) ||
		!(file = std::fopen(path.c_str(), "wb"))) {
		error = "can't write " + path;
		return false;
	}
	const bool written = std::fwrite(png.data(), 1, png.size(), file) == png.size();
	if (std::fclose(file) != 0 || !written) {
		error = "can't write " + path;
		return false;
	}
	return true;
#endif
}

}  // namespace p3::app
