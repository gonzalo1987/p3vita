// Sondas de src/p3data/town_tick/construction.cpp.
//
// Las funciones que reciben punteros a la pila del llamador (0x4FA740,
// 0x4F9C80, 0x4FA7F0, 0x5250D0 y 0x4D9FC0) no se pueden repetir solas: sus
// entradas y salidas no están en el estado que graba el oráculo. Se
// comprueban a través de quien las llama (0x524920, 0x51FAB0, 0x51D870,
// 0x51E070 y 0x51E270), que encolan lo que encuentran.
//
// 0x524D60 y 0x51F100 devuelven un byte o un u16 con el resto de eax sin
// definir: se compara solo el estado.

#include "p3data/town_tick/internal.hpp"
#include "probe_registry.hpp"

namespace p3::data::tick::construction_detail {

uint32_t fn_4d9f60(const SaveGame& save, uint32_t town);
uint16_t fn_51f100(SaveGame& save, int town);
void fn_5254c0(SaveGame& save, int town, uint32_t index);
uint32_t fn_51fab0(SaveGame& save, int town, uint8_t type, bool immediate);
void fn_51e270(SaveGame& save, int town);
bool fn_524d60(SaveGame& save, int town, uint32_t x, uint32_t y);
void fn_524f00(SaveGame& save, int town);
void fn_524cd0(SaveGame& save, int town);
void fn_524e20(SaveGame& save, int town);
bool fn_4fcbd0(const SaveGame& save, int town, uint8_t x, uint8_t y, uint8_t type, uint8_t param);
bool fn_521900(SaveGame& save, int town, uint8_t owner, uint8_t x, uint8_t y, uint8_t type, uint8_t param);
void fn_523830(SaveGame& save, int town, uint8_t x, uint8_t y, uint8_t owner);
void fn_522c70(SaveGame& save, int town, uint32_t owner, uint32_t kind);

}  // namespace p3::data::tick::construction_detail

namespace p3::probe {

using namespace p3::data;
namespace detail = tick::construction_detail;

void register_probes_construction(ProbeRegistry& r) {
	r[0x521900] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// Sobre la ciudad: (dueño, x, y, tipo, parámetro); devuelve 0 o 1.
		const int town = c.town(c.ecx);
		if (town < 0) {
			return 0xDEAD;
		}
		return uint32_t(detail::fn_521900(s, town, uint8_t(c.args[0]), uint8_t(c.args[1]), uint8_t(c.args[2]),
			uint8_t(c.args[3]), uint8_t(c.args[4])));
	};
	r[0x522C70] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// Sobre la ciudad: (dueño, clase).
		const int town = c.town(c.ecx);
		if (town < 0) {
			return 0xDEAD;
		}
		detail::fn_522c70(s, town, c.args[0], c.args[1]);
		return std::nullopt;
	};
	r[0x523830] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// Sobre la ciudad: (x, y, dueño).
		const int town = c.town(c.ecx);
		if (town < 0) {
			return 0xDEAD;
		}
		detail::fn_523830(s, town, uint8_t(c.args[0]), uint8_t(c.args[1]), uint8_t(c.args[2]));
		return std::nullopt;
	};
	r[0x4FCBD0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// Sobre town_class1 ([0x7A4] de la ciudad); devuelve un byte.
		const Location l = c.locate(c.ecx);
		if (l.kind != Location::Town || l.offset != 0x7A4) {
			return 0xDEAD;
		}
		c.eax_mask = 0xFF;
		return uint32_t(detail::fn_4fcbd0(s, l.index, uint8_t(c.args[0]), uint8_t(c.args[1]), uint8_t(c.args[2]),
			uint8_t(c.args[3])));
	};
	r[0x524920] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		tick::fn_524920(s, c.town(c.ecx), int32_t(c.args[0]));
		return std::nullopt;
	};
	r[0x51E070] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		tick::fn_51e070(s, c.town(c.ecx));
		return std::nullopt;
	};
	r[0x51D870] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		tick::fn_51d870(s, c.town(c.ecx));
		return std::nullopt;
	};
	// Cola de eventos: ecx = 0x6DD73C.
	r[0x4D9F60] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		return detail::fn_4d9f60(s, c.args[0]);
	};
	r[0x51F100] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		detail::fn_51f100(s, c.town(c.ecx));
		return std::nullopt;
	};
	r[0x5254C0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		detail::fn_5254c0(s, c.town(c.ecx), c.args[0]);
		return std::nullopt;
	};
	r[0x51FAB0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		return detail::fn_51fab0(s, c.town(c.ecx), uint8_t(c.args[0]), uint8_t(c.args[1]) != 0);
	};
	r[0x51E270] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		detail::fn_51e270(s, c.town(c.ecx));
		return std::nullopt;
	};
	r[0x524D60] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		detail::fn_524d60(s, c.town(c.ecx), c.args[0], c.args[1]);
		return std::nullopt;
	};
	r[0x524F00] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		detail::fn_524f00(s, c.town(c.ecx));
		return std::nullopt;
	};
	r[0x524CD0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		detail::fn_524cd0(s, c.town(c.ecx));
		return std::nullopt;
	};
	r[0x524E20] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		detail::fn_524e20(s, c.town(c.ecx));
		return std::nullopt;
	};
}

}  // namespace p3::probe
