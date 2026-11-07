#pragma once

// The game's interface texts, read at run time from Patrician3.exe: they
// aren't in the CPR but inside the executable of each language, as strings
// reached through tables of pointers in its .data section (see
// docs/cloud/alderman-viewer-2.md). p3vita keeps only where each text is in
// each known build (the address of its pointer), never the texts.

#include <array>
#include <cstdint>
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
	// The report of a town battle (0x5FE7E0, 0x6A227C..0x6A2294 and, for a
	// pirate's nest, 0x6AA1A4..0x6AA1AC): the result for each side, then the
	// same lists of ships as at sea.
	TownReportProtected,  // «\l\f1_You have protected the town of %s by putting the attackers to flight.\n»
	TownReportSaved,  // «... You have beaten the attackers and saved the town of %s from plunder.\n»
	TownReportPlundered,  // «... The pirates have beaten you and plundered the town of %s.\n»
	TownReportBrokeThrough,  // «... You have broken through the defence of %s and relieved the city's bank of %d\C.\n»
	TownReportUndefended,  // «... The town of %s was not defended so you could therefore rob the city's bank of %d\C...»
	TownReportAttackFled,  // «... Your attack on %s was a failure! Your ships were defeated and had to flee.\n»
	TownReportAttackLost,  // «... Your attack on %s was a total failure! The town was too well defended.\n»
	NestReportRuined,  // «... You reduced the hideaway of the pirate %s %s to wreck and ruin and have liberated %d\C.\n»
	NestReportAttackFled,  // «... Your attack on the pirate %s %s hideout has failed! ... had to flee.\n»
	NestReportAttackLost,  // «... Your attack on the pirate %s %s hideout was an abject failure! ...\n»
	// The report of an expedition (event 0x12, 0x4E6A54; the letter 0x63),
	// 0x6B95E0..0x6B9630 in the English exe.
	ExpeditionLedBy,  // «\n\lThe convoy led by the »
	ExpeditionReturned,  // « has success_fully returned from an expe_dition and is now heading for »
	ExpeditionDot,  // «.»
	ExpeditionDiscoveryOf,  // « The disco_very of »
	ExpeditionDiscoveryEnd,  // « has brought about new and profi_table possi_bili_ties of trade.»
	ExpeditionIn,  // «\nIn »
	ExpeditionTransactionMade,  // « the following transaction was made:\n»
	ExpeditionTransactionTook,  // « the following transaction took place:\n»
	ExpeditionSold,  // «%u\%c %s sold at %u\C (total: %u\C)\n»
	ExpeditionBought,  // «%u\%c %s bought at %u\C (total: %u\C)\n»
	ExpeditionGivenCaptain,  // «\nYou have given your captain »
	ExpeditionTakeWith,  // « to take with him on his voyage. He returns with »
	ExpeditionEnd,  // « .»
	ExpeditionIndianTribe,  // «\nWith the Indian tribe»
	ExpeditionNoCentres,  // «\nNo new trading centres could be made accessible.»
	ExpeditionTwoNew,  // «\nYou owe to the trained eyes of your lookout ... namely »
	ExpeditionAnd,  // « and »
	ExpeditionCouldBeDiscovered,  // «, could be discovered.\n»
	ExpeditionNewWorld,  // «\nAfter the strains of a long journey to the new world »
	ExpeditionOneVillage,  // «an Indian village could be discovered that is interested in ...\n»
	ExpeditionTwoVillages,  // «two Indian villages could be discovered that are interested in ...\n»
	// 0x6B1750..0x6B1794: more of the expedition, and its contracts (0x52C6C0).
	ExpeditionOpportunities,  // « where new and lucrative trade opportunities have been discovered.\n»
	ExpeditionFoundInNewWorld,  // «were found in the new world »
	ExpeditionVillagesFound,  // « where indian villages interested in trading have been discovered.\n»
	ExpeditionThree,  // « three»
	ExpeditionFour,  // « four»
	ContractPromised,  // «A trader in %s has promised to %s %d\%c %s by %d.%d.%d at a price of %d\C.\n »
	ContractBuy,  // «buy»
	ContractSell,  // «sell»
	ContractNoNew,  // «No new contracts have been signed.\n»
	ContractsHeader,  // «\nContracts:\n»
	ContractTraded,  // «%$1$d\%$2$c %$3$s was %$6$s at a price of %$4$d\C in %$5$s.»
	ContractBought,  // «bought»
	ContractSold,  // «sold»
	ExpeditionStorm,  // «The expedition has been caught by a storm.\n»
	ExpeditionPiratesFailed,  // «The expedition has been unsuccessfully attacked by pirates.\n»
	ExpeditionPiratesStole,  // «The expedition was successfully attacked by pirates. %d\C has been stolen.\n»
	ContractMustDeliver,  // « %d\%c must be delivered by %d.%d.%d. to complete the Contract.\n»
	ContractMet,  // « The quantity required by the contract was therefore met.\n»
	// The 30 places of the new world an expedition can find (0x6B1F20, by the
	// world object's [+0x4]: «Bordeaux»... «Tobruk»).
	ExpeditionPlace0,
	ExpeditionPlace29 = ExpeditionPlace0 + 29,
	WareSword,  // The 4 weapons after the 20 trade wares («sword», «bow», «crossbow», «carbine»; 0x6C0C00).
	WareCarbine = WareSword + 3,
	ShipUnnamed,  // The name of a ship without one, by type (0x512B20; «snaikka»...; 0x6C0CB4).
	// The letters 0x47 of the elections (0x6B72DC..0x6B7300): the mayor's
	// (0x528E60) and the Alderman's at the Hanseatic conference (0x5299F0).
	MayorElectionIn,  // «\cLord Mayor's election in »
	MayorElectionVotes,  // «\n\l\nThe votes cast are divided as follows among the councillors ...:\n\n»
	MayorElectionCandidate,  // «\l%s %s:\n\c%u votes\n» (first name, family name, votes)
	MayorElectionTherefore,  // «\l\nTherefore, »
	MayorElectionElected,  // « has been elected as Lord Mayor.»
	ConferenceIn,  // «\cHanseatic conference in »
	ConferenceVotes,  // «\n\l\nThe Lord Mayors standing for election got the following number of votes:\n\n»
	ConferenceTherefore,  // «\l\nTherefore »
	ConferenceElected,  // « has been elected.»
	ConferenceCandidate,  // «\l%s %s from %s:\n\c%u votes\n» (names, town, votes)
	// The menus and the saved games, for the viewer: the game's options panel
	// in a game (0x69AE20..0x69AE50 in the English exe), its main menu, its
	// buttons and its dialog of saved games (0x69B15C..0x69B170).
	SettingOn,  // «on» (Spanish «Sí»)
	SettingOff,  // «off» («No»)
	MenuSaveGame,  // «Save game»
	MenuLoadGame,  // «Load game»
	MenuOptions,  // «Options»
	QuitAsk,  // «Do you really want»
	QuitAskGame,  // «to quit the game?»
	QuitGame,  // The main menu's «Leave» (Spanish «Salir del juego»)
	ButtonOk,  // «OK»
	ButtonContinue,  // «Continue»
	ButtonCancel,  // «Cancel»
	ButtonBack,  // «Back»
	DeleteSavegame,  // «Delete savegame»
	DeleteAsk,  // «Do you really want to»
	DeleteAskSavegame,  // «delete this savegame?»
	OverwriteAsk,  // «Do you really want to overwrite»
	OverwriteAskSavegame,  // «the savegame '%s'?»
	LoadingGame,  // «Loading game...» (Spanish «Iniciando partida...»)
	LoadDamaged,  // «This save game appears to be damaged» (Spanish «No se puede cargar la partida.»)
	LoadDamagedCannot,  // «and can't be loaded.» («El fichero está dañado.»)
	// The tavern's window (0x5CD540; its texts 0x69D8A8.. in the English exe,
	// 0x580 lower in the Spanish one): hiring sailors and a captain.
	TavernHireSailors,  // «Hire sailors»
	TavernHireCaptain,  // «Hire captain»
	TavernOffersService,  // «offers his service to you.» (Spanish «: te ofrece sus servicios.»)
	TavernAge,  // «Age:»
	TavernDemands,  // «He demands»
	TavernDailyWage,  // «as daily wage.»
	TavernNoShip,  // «You have no ship in port»
	TavernChosenShip,  // «Chosen ship:»
	TavernThereAre,  // «There are»
	TavernSailorsIn,  // «sailors in the tavern.»
	TavernHowMany,  // «How many do you want to hire?»
	TavernNoShipForSailors,  // «on which you can hire sailors.» («debes tener uno fondeado para poder contratar marineros.»)
	TavernCaptain,  // «Captain» (the tavern's people, 0x69C9E0)
	TavernSailors,  // «Sailors»
	// The ship's window (0x493ED0).
	ShipDismissSailors,  // «dismiss sailors»
	ShipDismissCaptain,  // «dismiss captain»
	ShipCaptain,  // «Captain»
	// The shipyard's repairs (0x5F8F50; 0x6A5CB0.. in the English exe).
	ShipyardRepair,  // «Repair»
	ShipyardExecute,  // «Execute»
	ShipyardRepairsHere,  // «For repairs at this shipyard» («Encargos de reparación en este astillero:»)
	ShipyardConvoy,  // «Convoy»
	ShipyardRepairCost,  // «\c\f1_The cost of repairs amounts to %i\C.»
	ShipyardNoShips,  // «are no further ships available.» («No hay barcos disponibles en el puerto.»)
	// The buildings' names (0x6A57C8 + 4 · the building's type in the English exe).
	BuildingRepairDock,  // 0x01 «Repair Dock» (Spanish «Muelle»)
	BuildingTownHall,  // 0x20 «Town Hall»
	BuildingMarketHall,  // 0x23 «Market Hall» («Lonja»)
	BuildingTavern,  // 0x24 «Tavern»
	BuildingLender,  // 0x25 «Lender's House» («Prestamista»)
	BuildingShipyard,  // 0x2E «Shipyard»
	// The player's office in the market window (the ship's panel's «%s <-> %s», 0x48AD01).
	Office,  // «Office» («Oficina»)
	ShipWeapons,  // «Ship's weapons» («Armas de barco»; the armoury's, 0x6AA33C)
	HandWeapons,  // «Hand weapons» («Armas de mano»)
	// The convoy window (0x40C170, 0x494DC0; 0x699xxx in the English exe,
	// 0xAE4 lower in the Spanish one).
	ConvoyFormPrivate,  // «Form private convoy»
	ConvoyFormPublic,  // «Form public convoy»
	ConvoyJoin,  // «Join convoy %s»
	ConvoyDisbandAsk,  // «Disband convoy ... Should the convoy %s be disbanded?» (with the window's codes)
	ConvoyLeaveAsk,  // «Leave convoy ... Should the ship %s leave the convoy?»
	ConvoyCannotForm,  // «Convoy cannot be formed ... You have not chosen a suitable convoy leader...»
	ConvoyPublicLeaving,  // «The public convoy %s will be leaving in %u days with the destination %s.»
	// The shipyard's other pages (0x6A5CB0.. in the English exe, 0xC8 higher in the Spanish one).
	ShipyardCancelContract,  // «Cancel contract»
	ShipyardUpgradeShip,  // «Upgrade ship»
	ShipyardChangeName,  // «Change name»
	ShipyardMaterials,  // «Raw material requirement»
	ShipyardInStock,  // «in stock»
	ShipyardBuy,  // «buy»
	ShipyardCosts,  // «Costs»
	ShipyardTotalCost,  // «Total cost amounts to %i» (with the window's codes)
	ShipyardOrdered,  // « was ordered.»
	ShipyardCompletion,  // «Completion is on »
	ShipyardTrader,  // «Trader»
	ShipyardType,  // «Type»
	ShipyardNewName,  // «New name»
	ShipyardCancelAsk,  // «...The raw materials and 10% of wage costs will be retained!...cancel this order?»
	ShipyardWorkStarted,  // «Note ... Work is already being done on this ship.»
	ShipyardForUpgrade,  // «For an upgrade at this shipyard»
	ShipyardNoOrders,  // «No ships were commissioned at this shipyard.»
	ShipyardNewLevel,  // «New upgrade level»
	ShipyardLevel,  // «Level»
	Count
};

class GameTexts {
public:
	// Reads the texts from the executable at `path`. nullptr and `error` if it
	// can't be read or isn't a known build (by its PE time stamp).
	static std::unique_ptr<GameTexts> load(const std::string& path, std::string* error = nullptr);
	// The same from the executable's bytes.
	static std::unique_ptr<GameTexts> parse(const std::vector<uint8_t>& exe, std::string* error = nullptr);

	// A small file with the texts read (save_cache), so that the next start
	// reads it instead of the 3 MB of the executable (load_cache): the build's
	// time stamp, a hash of p3vita's slots for it and each raw text. Made from
	// the player's own executable; load_cache gives nullptr (and `error`) if
	// the file isn't such a cache, or was made for other slots (another version
	// of p3vita) or a build p3vita doesn't know.
	bool save_cache(const std::string& path) const;
	static std::unique_ptr<GameTexts> load_cache(const std::string& path, std::string* error = nullptr);
	// Whether both hold the same build and texts (the cache's check: p3save
	// texts-cache); the number of texts by address in `addresses`.
	bool same_as(const GameTexts& other, size_t* addresses = nullptr) const;

	// The text in UTF-8, without the game's control codes (\f1_, \c, \l, \r...);
	// "\n" stays as a line break.
	const std::string& get(GameText id) const { return texts_[size_t(id)]; }
	// The text as the executable has it: Windows-1252 bytes with the game's
	// control codes, as the game copies it into its memory.
	const std::string& raw(GameText id) const { return raw_[size_t(id)]; }
	// A text by its address in the English exe (a slot, or the string itself),
	// read from this build at its own address (game_text_addresses.inc: the
	// texts of the letters' composer...); empty if it isn't in the table.
	const std::string& raw_at(uint32_t english) const;
	// A string of the exe's string table by its number (its resources; the buttons' tooltips 20000..20063, as
	// the scripts' «ToolTip=» give them), raw (Windows-1252); empty if it isn't there.
	static constexpr uint32_t kResourceKey = 0x20000000;  // Their keys among the texts by address.
	const std::string& resource(uint32_t id) const { return raw_at(kResourceKey | id); }
	// The game's sprintf (0x64D410) on raw texts: %s (and %d, %i, %u, %c, with
	// the number or the character already written by the caller) with the bytes of `args` in
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
	// Whether it is the Spanish build: its code passes the arguments of a few
	// texts in another order, to fit the sentence (ContractPromised, 0x52D487).
	bool spanish() const { return spanish_; }

private:
	std::array<std::string, size_t(GameText::Count)> texts_;
	std::array<std::string, size_t(GameText::Count)> raw_;
	std::vector<std::pair<uint32_t, std::string>> by_address_;  // Sorted by the English address.
	std::string build_;
	uint32_t time_stamp_ = 0;  // The build's PE time stamp.
	bool spanish_ = false;
};

// The control codes out and Windows-1252 to UTF-8 (exposed for the tests).
std::string clean_game_text(const std::string& raw);

// A Windows-1252 byte (the game's texts) appended in UTF-8 ('?' for the
// five bytes the code page leaves undefined).
void append_cp1252(std::string& out, uint8_t c);

}  // namespace p3::data
