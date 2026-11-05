#pragma once

// Scripted input for the viewer: a list of steps given on the command line
// (--steps) that the main loop plays one frame at a time, as if they came from
// the Vita's buttons and touch screen. Used to drive the viewer without a
// window and save screenshots on the PC.

#include <SDL.h>

#include <memory>
#include <string>
#include <vector>

namespace p3::app {

// Milliseconds of game time per scripted frame: the Vita's 60 Hz.
constexpr Uint32 kScriptFrameMs = 1000 / 60;

class InputScript {
public:
	// Steps separated by commas (see kStepsHelp). nullptr and `error` if a step is wrong.
	static std::unique_ptr<InputScript> parse(const std::string& steps, std::string& error);

	// Starts the next frame: pushes its SDL events (button presses, taps) and
	// sets what the frame holds down, the extra ticks and the screenshot.
	void start_frame();

	// Whether the button is held down during this frame (the pad moves the camera).
	bool held(SDL_GameControllerButton button) const;
	// Ticks to run in this frame on top of the real-time ones (days:N).
	int extra_ticks() const { return extra_ticks_; }
	// File to save the screen into at the end of this frame, or empty.
	const std::string& screenshot() const { return screenshot_; }
	// Town cell to center the camera on in this frame (cell:X:Y), if any.
	bool look_at(int& x, int& y) const {
		x = look_x_;
		y = look_y_;
		return look_x_ >= 0;
	}
	// All the steps have been played (after the last frame's screenshot).
	bool finished() const { return finished_; }

	// Game time of the current frame in milliseconds (replaces SDL_GetTicks).
	Uint32 now() const { return Uint32(frame_) * kScriptFrameMs; }

private:
	enum class Kind { Press, Hold, Frames, Days, Shot, Tap, Cell, Text };
	struct Step {
		Kind kind = Kind::Press;
		SDL_GameControllerButton button = SDL_CONTROLLER_BUTTON_INVALID;
		int count = 1;  // Frames held, frames waited or days.
		int x = 0;  // Tap position on the screen, or town cell.
		int y = 0;
		std::string file;
	};

	void push_button(SDL_GameControllerButton button, bool down) const;
	void push_finger(Uint32 type, int x, int y) const;

	std::vector<Step> steps_;
	size_t current_ = 0;
	int progress_ = 0;  // Frames already spent in the current step.
	long frame_ = 0;
	SDL_GameControllerButton held_ = SDL_CONTROLLER_BUTTON_INVALID;
	int extra_ticks_ = 0;
	std::string screenshot_;
	int look_x_ = -1;
	int look_y_ = -1;
	bool finished_ = false;
};

// Help for --steps, for the usage text.
extern const char kStepsHelp[];

// Saves what the renderer has drawn (width × height) as a PNG; false and
// `error` if it can't (the Vita doesn't save screenshots).
bool save_screenshot(SDL_Renderer* renderer, int width, int height, const std::string& path, std::string& error);

}  // namespace p3::app
