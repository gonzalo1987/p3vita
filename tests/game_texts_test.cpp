// The game's texts: control codes, Windows-1252 and the printf formats, without game data.

#include <cstdio>
#include <cstdlib>

#include "p3data/game_texts.hpp"

using namespace p3::data;

namespace {

int failures = 0;

void check(bool condition, const char* what) {
	if (!condition) {
		std::fprintf(stderr, "FAILED: %s\n", what);
		failures++;
	}
}

}  // namespace

int main() {
	check(clean_game_text("\\c\\f1_Choose a candidate.") == "Choose a candidate.", "alignment and font codes");
	check(clean_game_text("\\f1_An extra tax of %d\\C has been levied.") == "An extra tax of %d has been levied.",
		"the coin symbol");
	check(clean_game_text("\\t16057_x\\f0y") == "xy", "tab code and a font code without '_'");
	check(clean_game_text("Hanse\xe1tica \x96 \x80") == "Hanse\xc3\xa1tica \xe2\x80\x93 \xe2\x82\xac", "Windows-1252");
	check(clean_game_text("line\none") == "line\none", "line breaks stay");

	check(GameTexts::format("%s %s %s %s from %s by %d votes.", {"", "", "Georg Stromberg", "", "Lubeck", "18"}) ==
			"Georg Stromberg from Lubeck by 18 votes.",
		"empty name parts collapse");
	check(GameTexts::format("at least %$5$d people by %$2$d. %$3$s %$4$d.", {"Visby", "1", "May", "1301", "3,300"}) ==
			"at least 3,300 people by 1. May 1301.",
		"positional arguments");
	check(GameTexts::format("%d%% of %s", {"50"}) == "50% of", "%% and a missing argument");

	if (failures == 0) {
		std::printf("game_texts_test: all correct\n");
	}
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
