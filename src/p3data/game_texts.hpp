#pragma once

// The game's interface texts, read at run time from Patrician3.exe: they
// aren't in the CPR but inside the executable of each language, as strings
// reached through tables of pointers in its .data section (see
// docs/cloud/alderman-viewer-2.md). p3vita keeps only where each text is in
// each known build (the address of its pointer), never the texts.

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace p3::data {

// The texts the viewer uses, by meaning.
enum class GameText {
	Tasks,  // «Tasks»
	TasksChoose,  // «You may choose one of the following tasks:»
	TaskFoundTown,  // The description of each kind of Hanse task (a %s: the region).
	TaskRoad,
	TaskPirateShip,
	TaskPirateNest,
	TaskGrowTown,
	ThanksFoundTown,  // What the Hanse says when the player takes each kind.
	ThanksRoad,
	ThanksPirateShip,
	ThanksPirateNest,
	ThanksGrowTown,
	NoTasks,  // «The Hanseatic League has no contracts on offer right now...»
	RegionWest,  // The regions of StadtDaten.ini (0..4), for the %s of the descriptions.
	RegionNorthSea,
	RegionBaltic,
	RegionNorth,
	RegionEast,
	AldermanOffice,  // «Alderman's office»
	AldermanIs,  // «Alderman of the Hanseatic League is %s %s %s %s from %s.»
	Nominated,  // «The following Lord Mayors have been nominated for the next Alderman election on %d. %s %d in %s:»
	ElectionTitle,  // «Election of Alderman»
	CandidatesIntro,  // «The lord mayors of the following Hanseatic League towns are standing for election:»
	VotePrompt,  // «Choose a candidate and cast your vote.»
	Elected,  // «%s %s %s %s from %s was elected Alderman of the Hanseatic League by %d votes.»
	MonthJanuary,  // The 12 months, for the dates in the texts.
	MonthDecember = MonthJanuary + 11,
	WareGrain,  // The 20 trade wares, in lower case («grain»...), in the order of kWares.
	WareBricks = WareGrain + 19,
	ShipSnaikka,  // The 4 ship types («Snaikka», «Crayer», «Cog», «Hulk»).
	ShipHulk = ShipSnaikka + 3,
	MarketGoods,  // The trade window's columns: «Goods», «Town», «Buy», «Sell», «Ship».
	MarketTown,
	MarketBuy,
	MarketSell,
	MarketShip,
	AmountLoads,  // «%.0f load», «%.0f barrel», «%.0f pieces».
	AmountBarrels,
	AmountPieces,
	AmountLoadsDecimal,  // «%.1f load»
	NotEnoughMoney,  // «Not enough money!»
	Inhabitants,  // «%.0f inhabitants»
	Date,  // «%u. %s %u» (day, month, year)
	AtSea,  // «at sea»
	Sunk,  // «Sunk»
	ReachedDestination,  // «The %s has reached its destination.»
	SailNoCrew,  // «The ship cannot sail because there are not enough crew members.»
	SailOverloaded,  // «The ship cannot sail because it is overloaded.»
	SailDamaged,  // «Because of the bad condition of the ship, the captain refuses to set sail.»
	SailMutiny,  // «The crew is mutinous and refuses to sail...»
	GameSpeed,  // «Game speed»
	SpeedVerySlow,  // «very slow», «slow», «normal», «fast», «very fast», «slowdown».
	SpeedSlow,
	SpeedNormal,
	SpeedFast,
	SpeedVeryFast,
	SpeedSlowdown,
	Count
};

class GameTexts {
public:
	// Reads the texts from the executable at `path`. nullptr and `error` if it
	// can't be read or isn't a known build (by its PE time stamp).
	static std::unique_ptr<GameTexts> load(const std::string& path, std::string* error = nullptr);
	// The same from the executable's bytes.
	static std::unique_ptr<GameTexts> parse(const std::vector<uint8_t>& exe, std::string* error = nullptr);

	// The text in UTF-8, without the game's control codes (\f1_, \c, \l, \r...);
	// "\n" stays as a line break.
	const std::string& get(GameText id) const { return texts_[size_t(id)]; }
	// The text with its printf specifiers (%s, %d, %u, %i, %%; and the
	// positional %$N$d of some texts) replaced by `args` in order (missing
	// ones: empty), and runs of spaces collapsed.
	std::string format(GameText id, const std::vector<std::string>& args) const { return format(get(id), args); }
	static std::string format(const std::string& text, const std::vector<std::string>& args);
	// The month 0..11 in the texts' language.
	const std::string& month(int m) const { return get(GameText(int(GameText::MonthJanuary) + ((m % 12) + 12) % 12)); }

	// «English GOG 1.1», «Spanish»...
	const std::string& build() const { return build_; }

private:
	std::array<std::string, size_t(GameText::Count)> texts_;
	std::string build_;
};

// The control codes out and Windows-1252 to UTF-8 (exposed for the tests).
std::string clean_game_text(const std::string& raw);

}  // namespace p3::data
