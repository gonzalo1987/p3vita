// Sondas de src/p3data/town_tick/events_rare.cpp.

#include "p3data/town_tick/internal.hpp"
#include "probe_registry.hpp"

#include <vector>

// Lo que events_rare.cpp expone para las sondas.
namespace p3::data::tick::events_rare_detail {

void fn_5098b0(SaveGame& save, uint16_t& captain);
uint32_t fn_4e4a44(SaveGame& save, uint16_t event);
uint32_t fn_4e9094(SaveGame& save);
void fn_4e94a4(SaveGame& save);
uint32_t fn_4dfd44(SaveGame& save, uint16_t event);
uint32_t fn_4e0334(SaveGame& save, uint16_t event);
void fn_4f91c0(SaveGame& save, int merchant);
uint32_t fn_4ea594(SaveGame& save);
uint32_t fn_4fac70(const SaveGame& save, int town);
void fn_52a150(SaveGame& save, int town, uint8_t rate);
void fn_522b20(SaveGame& save, int town, uint32_t side);
struct WallPiece {
	uint8_t x = 0;
	uint8_t y = 0;
	uint8_t type = 0;
	uint8_t param = 0;
};
void fn_4fa1d0(SaveGame& save, int town, uint32_t side, std::vector<WallPiece>& out, uint32_t& corners);
uint32_t fn_4e9a94(SaveGame& save);
bool fn_532c50(const SaveGame& save, uint32_t index);
uint32_t fn_4eafe4(SaveGame& save);
struct MapPoint {
	uint16_t x = 0;
	uint16_t y = 0;
};
int32_t fn_52d950(MapPoint p);
int32_t fn_52db50(MapPoint a, MapPoint b);
uint32_t fn_4e6a54(SaveGame& save, uint16_t event);
bool fn_534990(SaveGame& save, int merchant, int16_t x, int16_t y, uint8_t flag);
bool fn_4f93c0(SaveGame& save, int merchant, int16_t x, int16_t y, uint8_t flag);
void fn_534960(SaveGame& save, int merchant, uint32_t object);
void fn_4f9400(SaveGame& save, int merchant, uint32_t object);
void fn_4f8b50(SaveGame& save, int merchant);
void fn_4f8bd0(SaveGame& save, int merchant);
void fn_5319d0(SaveGame& save, int convoy);
bool fn_52be90(const SaveGame& save, uint32_t object, MapPoint p, uint8_t near);
bool fn_52d0b0(SaveGame& save, uint32_t object, uint16_t convoy);
void fn_517fe0(SaveGame& save, int ship, uint32_t ware, int32_t amount, int32_t price);
int32_t fn_52d4f0(SaveGame& save, uint32_t object, uint32_t side, int32_t& cargo, uint8_t flag);
int32_t fn_52d6b0(const SaveGame& save, uint32_t object, uint32_t side, int32_t amount, uint8_t flag);
uint8_t fn_52c6c0(SaveGame& save, uint32_t object, uint16_t convoy, uint16_t mode, uint8_t may_create,
	uint8_t deliver, std::string* text);
uint8_t fn_52bfe0(SaveGame& save, uint32_t object, uint16_t convoy, uint32_t point, uint16_t mode, uint8_t first,
	std::string* trades, std::string* contracts);

}  // namespace p3::data::tick::events_rare_detail

namespace p3::probe {

using namespace p3::data;
namespace rd = tick::events_rare_detail;

namespace {

constexpr uint32_t kEvents = 0x6DD73C;

// La ciudad de un town_class1 ([+0x7A4] de la ciudad) o -1.
int town_of_class1(const ProbeCall& c, uint32_t address) {
	const Location l = c.locate(address);
	return l.kind == Location::Town && l.offset == 0x7A4 ? l.index : -1;
}

// El punto (u16 x, u16 y) de un convoy en `address`.
std::optional<rd::MapPoint> convoy_point(const SaveGame& s, const ProbeCall& c, uint32_t address) {
	const Location l = c.locate(address);
	if (l.kind != Location::Convoy || l.index < 0 || size_t(l.index) >= s.convoys.size() || l.offset + 4 > 0x3C) {
		return std::nullopt;
	}
	const GameMemory<0x3C>& v = s.convoys[size_t(l.index)];
	return rd::MapPoint{v.u16(l.offset), v.u16(l.offset + 2)};
}

// Índice del evento al que apunta `address` (en el array *0x6DD73C del juego).
uint16_t event_index(const SaveGame& s, uint32_t address) {
	return uint16_t((address - s.globals.u32(kEvents)) / 0x18);
}

// El objeto de los 15 del mundo (0x6DEFB0 + 0x2C·i) en `address`, o -1.
int world_object(uint32_t address) {
	constexpr uint32_t kObjects = 0x6DEFB0;
	if (address < kObjects || address >= kObjects + 15 * 0x2C || (address - kObjects) % 0x2C) {
		return -1;
	}
	return int((address - kObjects) / 0x2C);
}

// Los bits de expediciones en `address`: -1 el objeto 0x6DF278, el
// comerciante si es su [+0x4D8], o -2.
int expedition_bits(const ProbeCall& c, uint32_t address) {
	if (address == 0x6DF278) {
		return -1;
	}
	const Location l = c.locate(address);
	return l.kind == Location::Merchant && l.offset == 0x4D8 ? l.index : -2;
}

// Los barcos (Sailing) de tick_probe --cpr para la vuelta del evento 0x12.
void use_sailing() {
	tick::set_event_sailing(probe_sailing());
}

}  // namespace

void register_probes_events_rare(ProbeRegistry& r) {
	// Los manejadores sobre la cola (ecx = 0x6DD73C): con el evento (puntero)
	// en la pila (0x0E, 0x1E), con su índice (0x1F) o sin nada, el de la cabeza.
	r[0x4E4A44] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		return rd::fn_4e4a44(s, event_index(s, c.args[0]));
	};
	r[0x4E9094] = [](SaveGame& s, const ProbeCall&) -> std::optional<uint32_t> {
		return rd::fn_4e9094(s);
	};
	r[0x4E94A4] = [](SaveGame& s, const ProbeCall&) -> std::optional<uint32_t> {
		rd::fn_4e94a4(s);
		return std::nullopt;
	};
	r[0x4DFD44] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		return rd::fn_4dfd44(s, event_index(s, c.args[0]));
	};
	r[0x4E0334] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		return rd::fn_4e0334(s, uint16_t(c.args[0]));
	};
	// Evento 0x30.
	r[0x4EA594] = [](SaveGame& s, const ProbeCall&) -> std::optional<uint32_t> {
		return rd::fn_4ea594(s);
	};
	r[0x4F91C0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		rd::fn_4f91c0(s, c.merchant(c.ecx));
		return std::nullopt;
	};
	// Evento 0x2E.
	r[0x4E9A94] = [](SaveGame& s, const ProbeCall&) -> std::optional<uint32_t> {
		return rd::fn_4e9a94(s);
	};
	r[0x4FAC70] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		const int town = town_of_class1(c, c.ecx);
		return town < 0 ? 0xDEAD : rd::fn_4fac70(s, town);
	};
	r[0x52A150] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		rd::fn_52a150(s, c.town(c.ecx), uint8_t(c.args[0]));
		return std::nullopt;
	};
	r[0x4FB9C0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		const int town = town_of_class1(c, c.ecx);
		if (town < 0) {
			return 0xDEAD;
		}
		tick::fn_4fb9c0(s, town, c.args[0], c.args[1], uint8_t(c.args[3]), uint8_t(c.args[4]));
		return std::nullopt;
	};
	r[0x4FA1D0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// La lista que llena va en la pila del llamador: solo se compara la rejilla.
		const int town = town_of_class1(c, c.ecx);
		if (town < 0) {
			return 0xDEAD;
		}
		std::vector<rd::WallPiece> pieces;
		uint32_t corners = 0;
		rd::fn_4fa1d0(s, town, c.args[0], pieces, corners);
		return std::nullopt;
	};
	r[0x522B20] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		rd::fn_522b20(s, c.town(c.ecx), c.args[0]);
		return std::nullopt;
	};
	// Evento 0x32.
	r[0x4EAFE4] = [](SaveGame& s, const ProbeCall&) -> std::optional<uint32_t> {
		return rd::fn_4eafe4(s);
	};
	r[0x532C50] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		c.eax_mask = 0xFF;
		return rd::fn_532c50(s, c.args[0]) ? 1 : 0;
	};
	// Evento 0x12 (cdecl con punteros a puntos (u16 x, u16 y) de un convoy).
	r[0x4E6A54] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		use_sailing();
		return rd::fn_4e6a54(s, event_index(s, c.args[0]));
	};
	// La llegada: los bits de expediciones (sobre 0x6DF278 o el [+0x4D8] de
	// un comerciante, con u16 x, u16 y o el objeto).
	r[0x534990] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		const int bits = expedition_bits(c, c.ecx);
		if (bits < -1) {
			return 0xDEAD;
		}
		return rd::fn_534990(s, bits, int16_t(c.args[0]), int16_t(c.args[1]), uint8_t(c.args[2])) ? 1 : 0;
	};
	r[0x4F93C0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		c.eax_mask = 0xFF;
		return rd::fn_4f93c0(s, c.merchant(c.ecx), int16_t(c.args[0]), int16_t(c.args[1]), uint8_t(c.args[2])) ? 1 : 0;
	};
	r[0x534960] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		const int bits = expedition_bits(c, c.ecx);
		if (bits < -1) {
			return 0xDEAD;
		}
		rd::fn_534960(s, bits, c.args[0]);
		return std::nullopt;
	};
	r[0x4F9400] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		rd::fn_4f9400(s, c.merchant(c.ecx), c.args[0]);
		return std::nullopt;
	};
	r[0x4F8B50] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		rd::fn_4f8b50(s, c.merchant(c.ecx));
		return std::nullopt;
	};
	r[0x4F8BD0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		rd::fn_4f8bd0(s, c.merchant(c.ecx));
		return std::nullopt;
	};
	r[0x5319D0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		rd::fn_5319d0(s, c.convoy(c.args[0]));
		return std::nullopt;
	};
	// Los 15 objetos del mundo (thiscall sobre 0x6DEFB0 + 0x2C·i).
	r[0x52BE90] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		const int object = world_object(c.ecx);
		const std::optional<rd::MapPoint> p = convoy_point(s, c, c.args[0]);
		if (object < 0 || !p) {
			return 0xDEAD;
		}
		c.eax_mask = 0xFF;
		return rd::fn_52be90(s, uint32_t(object), *p, uint8_t(c.args[1])) ? 1 : 0;
	};
	r[0x52D0B0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		const int object = world_object(c.ecx);
		if (object < 0) {
			return 0xDEAD;
		}
		c.eax_mask = 0xFF;
		return rd::fn_52d0b0(s, uint32_t(object), uint16_t(c.args[0])) ? 1 : 0;
	};
	r[0x52BFE0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		const int object = world_object(c.ecx);
		if (object < 0) {
			return 0xDEAD;
		}
		c.eax_mask = 0xFF;
		return rd::fn_52bfe0(s, uint32_t(object), uint16_t(c.args[0]), c.args[1], uint16_t(c.args[4]),
			uint8_t(c.args[5]), nullptr, nullptr);  // The texts (the game's CStrings) aren't compared.
	};
	r[0x52C6C0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		const int object = world_object(c.ecx);
		if (object < 0) {
			return 0xDEAD;
		}
		c.eax_mask = 0xFF;
		return rd::fn_52c6c0(s, uint32_t(object), uint16_t(c.args[0]), uint16_t(c.args[3]), uint8_t(c.args[4]),
			uint8_t(c.args[5]), nullptr);
	};
	r[0x52D4F0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// La carga es la de una mercancía de un barco.
		const int object = world_object(c.ecx);
		const Location l = c.locate(c.args[1]);
		if (object < 0 || l.kind != Location::Ship || l.offset < 0x54 || l.offset >= 0xB4 || (l.offset - 0x54) % 4) {
			return 0xDEAD;
		}
		int32_t& cargo = s.ships[size_t(l.index)].cargo(int(l.offset - 0x54) / 4);
		return uint32_t(rd::fn_52d4f0(s, uint32_t(object), c.args[0], cargo, uint8_t(c.args[2])));
	};
	r[0x52D6B0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		const int object = world_object(c.ecx);
		if (object < 0) {
			return 0xDEAD;
		}
		return uint32_t(rd::fn_52d6b0(s, uint32_t(object), c.args[0], int32_t(c.args[1]), uint8_t(c.args[2])));
	};
	r[0x517FE0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		rd::fn_517fe0(s, c.ship(c.ecx), c.args[0], int32_t(c.args[1]), int32_t(c.args[2]));
		return std::nullopt;
	};
	// La vuelta (miembro de Sailing: necesita tick_probe --cpr).
	r[0x502B20] = [](SaveGame&, const ProbeCall& c) -> std::optional<uint32_t> {
		probe_sailing()->fn_502b20(c.convoy(c.ecx), c.args[0]);
		return std::nullopt;
	};
	r[0x52D950] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		const std::optional<rd::MapPoint> p = convoy_point(s, c, c.args[0]);
		return p ? std::optional<uint32_t>(uint32_t(rd::fn_52d950(*p))) : std::optional<uint32_t>(0xDEAD);
	};
	r[0x52DB50] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		const std::optional<rd::MapPoint> a = convoy_point(s, c, c.args[0]);
		const std::optional<rd::MapPoint> b = convoy_point(s, c, c.args[1]);
		return a && b ? std::optional<uint32_t>(uint32_t(rd::fn_52db50(*a, *b))) : std::optional<uint32_t>(0xDEAD);
	};
	r[0x5098B0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// thiscall sobre el objeto de los barcos con un puntero a un u16: el
		// [+0x2F2] de una oficina o una variable del llamador (0x502B20), de
		// la que solo se compara lo que cambia en los capitanes.
		const Location l = c.locate(c.args[0]);
		if (l.kind == Location::Office && l.offset == 0x2F2) {
			rd::fn_5098b0(s, s.offices[size_t(l.index)].memory.u16(0x2F2));
			return std::nullopt;
		}
		if (const std::optional<uint32_t> local = c.stack(c.args[0])) {
			uint16_t captain = uint16_t(*local);
			rd::fn_5098b0(s, captain);
			return std::nullopt;
		}
		return 0xDEAD;
	};
}

}  // namespace p3::probe
