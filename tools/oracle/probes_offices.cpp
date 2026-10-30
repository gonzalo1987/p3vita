// Sondas de src/p3data/town_tick/offices.cpp.
//
// Las empresas de las oficinas producen, contratan y despiden con funciones de
// businesses.cpp (0x50E690..., 0x4D4A40, 0x50E380): sus sondas están en
// probes_businesses.cpp y aceptan también las oficinas.

#include "p3data/town_tick/internal.hpp"
#include "probe_registry.hpp"

// Lo que offices.cpp expone para las sondas.
namespace p3::data::tick::offices_detail {

void fn_4d3670(SaveGame& save, int record, uint32_t id);
void fn_4d4400(SaveGame& save, int record, int office, int32_t made, int32_t made2);
void fn_4d3780(SaveGame& save, int record, int office, uint32_t& active);
void fn_4d3560(SaveGame& save, int record, int merchant, int office);
void fn_4f8690(SaveGame& save, int merchant, uint32_t town, uint32_t ware, int32_t money, int32_t amount, bool flag);
void fn_4f8840(SaveGame& save, int merchant, uint32_t town, uint32_t ware, int32_t money, int32_t amount, bool flag);
int32_t fn_52ea80(const SaveGame& save, uint32_t ware, int town, int32_t price);
int32_t fn_52ec50(const SaveGame& save, uint32_t ware, int town, int32_t price, uint32_t quality);
void fn_4ffa30(SaveGame& save, int office);
void fn_4ffc20(SaveGame& save, int office);
void fn_4ff780(SaveGame& save, int office);

}  // namespace p3::data::tick::offices_detail

namespace p3::probe {

using namespace p3::data;
namespace od = tick::offices_detail;

namespace {

// Registro de 0x14 bytes del mundo (empresa de una oficina) en `address`, o -1.
int world_record(const ProbeCall& c, uint32_t address) {
	const Location l = c.locate(address);
	return l.kind == Location::WorldRecord && l.offset == 0 ? l.index : -1;
}

}  // namespace

void register_probes_offices(ProbeRegistry& r) {
	r[0x4FFD40] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		c.eax_mask = 0xFFFF;  // Devuelve un u16 en ax.
		return tick::fn_4ffd40(s, c.office(c.ecx));
	};
	r[0x4FFF20] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		tick::fn_4fff20(s, c.office(c.ecx), c.args[0]);
		return std::nullopt;
	};
	r[0x4FFA30] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		od::fn_4ffa30(s, c.office(c.ecx));
		return std::nullopt;
	};
	r[0x4FFC20] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		od::fn_4ffc20(s, c.office(c.ecx));
		return std::nullopt;
	};
	r[0x4FF780] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		od::fn_4ff780(s, c.office(c.ecx));
		return std::nullopt;
	};
	r[0x4D3780] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		const Location l = c.locate(c.args[1]);  // [0x40 + 4·ciudad] del comerciante
		od::fn_4d3780(s, world_record(c, c.ecx), c.office(c.args[0]), s.merchants[size_t(l.index)].memory.u32(l.offset));
		return std::nullopt;
	};
	r[0x4D3560] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		od::fn_4d3560(s, world_record(c, c.ecx), c.merchant(c.args[0]), c.office(c.args[1]));
		return std::nullopt;
	};
	r[0x4D3670] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		od::fn_4d3670(s, world_record(c, c.ecx), c.args[0]);
		return std::nullopt;
	};
	r[0x4D4400] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		od::fn_4d4400(s, world_record(c, c.ecx), c.office(c.args[0]), int32_t(c.args[1]), int32_t(c.args[2]));
		return std::nullopt;
	};
	// 0x4D49D0 lee de las variables locales de 0x4FFD40 (los edificios por
	// tipo), no de sus argumentos: no se puede repetir sola; la comprueba la
	// sonda de 0x4FFD40.
	r[0x4F8690] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		od::fn_4f8690(s, c.merchant(c.ecx), c.args[0], c.args[1], int32_t(c.args[2]), int32_t(c.args[3]),
			(c.args[4] & 0xFF) != 0);
		return std::nullopt;
	};
	r[0x4F8840] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		od::fn_4f8840(s, c.merchant(c.ecx), c.args[0], c.args[1], int32_t(c.args[2]), int32_t(c.args[3]),
			(c.args[4] & 0xFF) != 0);
		return std::nullopt;
	};
	r[0x52EA80] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		return uint32_t(od::fn_52ea80(s, c.args[0], int(c.args[1]), int32_t(c.args[2])));
	};
	r[0x52EC50] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		return uint32_t(od::fn_52ec50(s, c.args[0], int(c.args[1]), int32_t(c.args[2]), c.args[3]));
	};
}

}  // namespace p3::probe
