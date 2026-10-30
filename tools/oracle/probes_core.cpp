// Sondas de src/p3data/town_tick/core.cpp.

#include "p3data/town_tick/internal.hpp"
#include "probe_registry.hpp"

// Funciones internas de core.cpp.
namespace p3::data::tick::core_detail {
void fn_51cd80(SaveGame& save, int town);
void fn_527cd0(SaveGame& save, int town);
void fn_527d40(SaveGame& save, int town);
void fn_527d20(SaveGame& save, int town);
void fn_4fa6f0(SaveGame& save, int town);
void fn_50fe40(SaveGame& save, int town, int node);
uint32_t fn_51bdb0(const SaveGame& save, int town);
void fn_525390(SaveGame& save, int town);
void fn_4fe877(SaveGame& save, int church_town, uint8_t town_index);
void fn_525f30(SaveGame& save, int town);
void fn_528630(SaveGame& save, int town);
void fn_52a170(SaveGame& save, int town);
}  // namespace p3::data::tick::core_detail

namespace p3::probe {

using namespace p3::data;

namespace {

// Funciones de la ciudad en ecx, sin argumentos.
template <void (*F)(SaveGame&, int)>
ProbeFunction on_town() {
	return [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		F(s, c.town(c.ecx));
		return std::nullopt;
	};
}

}  // namespace

void register_probes_core(ProbeRegistry& r) {
	namespace d = tick::core_detail;
	r[0x51BA10] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		tick::fn_51ba10(s, c.town(c.ecx), c.args[0]);
		return std::nullopt;
	};
	r[0x51CD80] = on_town<d::fn_51cd80>();
	r[0x527CD0] = on_town<d::fn_527cd0>();
	r[0x527D40] = on_town<d::fn_527d40>();
	r[0x527D20] = on_town<d::fn_527d20>();
	r[0x525390] = on_town<d::fn_525390>();
	r[0x525F30] = on_town<d::fn_525f30>();
	r[0x528630] = on_town<d::fn_528630>();
	r[0x52A170] = on_town<d::fn_52a170>();
	r[0x51BDB0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		return d::fn_51bdb0(s, c.town(c.ecx));
	};
	r[0x4FA6F0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		const Location l = c.locate(c.ecx);  // town_class1, en [0x7A4] de la ciudad
		d::fn_4fa6f0(s, l.index);
		return std::nullopt;
	};
	r[0x50FE40] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		const Location l = c.locate(c.ecx);  // Nodo de la lista [0x9B4]
		d::fn_50fe40(s, l.index, l.item);
		return std::nullopt;
	};
	r[0x4FE877] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		const Location l = c.locate(c.ecx);  // La iglesia, en [0x794] de la ciudad
		d::fn_4fe877(s, l.index, uint8_t(c.args[0]));
		return std::nullopt;
	};
}

}  // namespace p3::probe
