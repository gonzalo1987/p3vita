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
	// The sea battle's boarding messages (0x60C1C9), which the game writes
	// into the battle ([+0x1310]): the simulation uses them raw (raw()).
	BattleBoardingCaptured,  // «\c\f3_Boarding battle ... The crew of the "%s" has captured %s "%s" %s.»
	BattleBoardingLost,  // «... Your ship "%s" lost the boarding fight against %s "%s" %s.»
	BattleSeizurePlundered,  // «\c\f3_Seizure ... The crew of the "%s" has plundered %s "%s" %s.»
	BattleSeizureWasPlundered,  // «... Your ship "%s" was plundered by %s "%s" %s.»
	BattleOfMerchant,  // «of %s %s» (first name, family name)
	BattleOfPirate,  // «of the pirate %s %s»
	BattleOfUnknownPirate,  // «of the unknown pirate»
	BattleTakenSnaikka,  // The 4 ship types as the one taken («the snaikka»...; 0x6A171C).
	BattleTakenHulk = BattleTakenSnaikka + 3,
	BattleWinnerSnaikka,  // The 4 ship types as the winner («the snaikke»...; 0x6A175C).
	BattleWinnerHulk = BattleWinnerSnaikka + 3,
	// The battle's question to the player (0x605420, 0x60659B..0x606AF5).
	BattleAttacking,  // «\c\f3_Sea battle ... %s "%s" is attacking %s "%s" %s.» (the player attacks)
	BattleAttacked,  // «... %s "%s" is being attacked by %s "%s" %s.» (the player is attacked)
	BattleOfAnUnknownPirate,  // «of an unknown pirate»
	BattleYourSnaikka,  // «Your snaikka»... (0x6A1784, filled at start by 0x46BFC0 in the English exe)
	BattleYourHulk = BattleYourSnaikka + 3,
	BattleYourConvoy,  // «Your convoy» (0x6A1794)
	BattleTheConvoyAttacking,  // «the convoy» that attacks the player (0x6A179C)
	BattleTheConvoyAttacked,  // «the convoy» the player attacks (0x6A17A0)
	// The report of a sea battle (0x60D299, 0x6A2240..0x6A2278), sent to each
	// side of the computer as a letter 0x29: the result for each side...
	BattleReportPutToFlight,  // «\l\f1_You have put the attackers to flight!\n»
	BattleReportDefeatedEnemy,  // «... You have defeated the enemy!»
	BattleReportEscaped,  // «... You were defeated by the attacker, but managed to escape!»
	BattleReportLost,  // «... You lost the sea battle!»
	BattleReportAttackSucceeded,  // «... Your attack was successful!»
	BattleReportEnemyEscaped,  // «... Your enemy escaped!»
	BattleReportAttackFled,  // «... Your attack was a failure! Your ships were defeated and had to flee!»
	BattleReportAttackLost,  // «... Your attack was a total failure! You lost the sea battle!»
	BattleReportOwnShips,  // «\n\f0_Own ships:\n\f1_»
	BattleReportCapturedShips,  // «\n\f0_Captured ships:\n\f1_»
	BattleReportEnemyShips,  // «\n\f0_Enemy ships:\n\f1_»
	BattleReportShip,  // «%s %s: %d%%\n» (type, name, hull)
	BattleReportSunk,  // «%s %s: sunk\n»
	BattleReportCaptured,  // «%s %s: %d%%, captured\n»
	BattleReportFled,  // «%s %s: %d%%, fled\n»
	BattleReportPlundered,  // «%s %s: %d%%, plundered\n» (0x6AA180)
	BattleShipSnaikka,  // The 4 ship types, bare («snaikka»...; 0x6A174C).
	BattleShipHulk = BattleShipSnaikka + 3,
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
	// The text as the executable has it: Windows-1252 bytes with the game's
	// control codes, as the game copies it into its memory.
	const std::string& raw(GameText id) const { return raw_[size_t(id)]; }
	// The game's sprintf (0x64D410) on raw texts: %s (and %d, %i, %u, with
	// the number already written by the caller) with the bytes of `args` in
	// order, %% and the positional %$N$s of the Spanish texts (the N-th
	// argument); nothing collapsed or trimmed.
	static std::string sprintf_raw(const std::string& text, const std::vector<std::string>& args);
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
	std::array<std::string, size_t(GameText::Count)> raw_;
	std::string build_;
};

// The control codes out and Windows-1252 to UTF-8 (exposed for the tests).
std::string clean_game_text(const std::string& raw);

}  // namespace p3::data
