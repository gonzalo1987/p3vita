// Sondas de src/p3data/town_tick/population.cpp.

#include "p3data/town_tick/internal.hpp"
#include "probe_registry.hpp"

namespace p3::data::tick::population_detail {
void fn_524b70(SaveGame& save, int town, uint32_t index);
void fn_524c30(SaveGame& save, int town);
uint32_t fn_4d4a90(const SaveGame& save, uint32_t list);
int32_t fn_51c360(SaveGame& save, int town, int32_t workers);
void fn_520840(SaveGame& save, int town, uint16_t* occupancy, uint32_t head);
}  // namespace p3::data::tick::population_detail

namespace p3::probe {

using namespace p3::data;

void register_probes_population(ProbeRegistry& r) {
	using Town = void (*)(SaveGame&, int);
	auto town = [](Town fn) {
		return [fn](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
			fn(s, c.town(c.ecx));
			return std::nullopt;
		};
	};
	r[0x51C830] = town(tick::fn_51c830);
	r[0x51CBE0] = town(tick::fn_51cbe0);
	r[0x51C650] = town(tick::fn_51c650);
	r[0x520BD0] = town(tick::fn_520bd0);
	r[0x51C0E0] = town(tick::fn_51c0e0);
	r[0x524C30] = town(tick::population_detail::fn_524c30);
	r[0x524B70] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		tick::population_detail::fn_524b70(s, c.town(c.ecx), c.args[0]);
		return std::nullopt;
	};
	r[0x4D4A90] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		return tick::population_detail::fn_4d4a90(s, c.ecx);  // ecx: la lista global (0x6DD628)
	};
	r[0x51C360] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		return uint32_t(tick::population_detail::fn_51c360(s, c.town(c.ecx), int32_t(c.args[0])));
	};
	r[0x520840] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// La ocupación es [0x778] de la ciudad o [+0x2DE] de una oficina.
		const Location l = c.locate(c.args[0]);
		uint16_t* occupancy = l.kind == Location::Town ? &s.towns[size_t(l.index)].memory.u16(l.offset)
			: &s.offices[size_t(l.index)].memory.u16(l.offset);
		tick::population_detail::fn_520840(s, c.town(c.ecx), occupancy, c.args[1]);
		return std::nullopt;
	};
}

}  // namespace p3::probe
