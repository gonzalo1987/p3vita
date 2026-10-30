// Sondas de src/p3data/town_tick/merchants.cpp.

#include "p3data/town_tick/internal.hpp"
#include "probe_registry.hpp"

// Lo que merchants.cpp expone para las sondas.
namespace p3::data::tick::merchants_detail {

void fn_4d9080(SaveGame& save, uint32_t merchant, int32_t& value, int32_t& debt);
int32_t fn_52a7d0(const SaveGame& save, int ship);
uint32_t fn_517f70(const SaveGame& save, int ship);
int32_t fn_51f220(const SaveGame& save, int town, uint32_t record);
int32_t fn_500330(const SaveGame& save, int office, uint32_t hometown);
uint8_t fn_4f9430(const SaveGame& save, int merchant);
void fn_4f7510(SaveGame& save, int merchant, uint32_t me, uint32_t town, uint32_t rank);
void fn_4f75f0(SaveGame& save, int merchant, uint32_t town);

}  // namespace p3::data::tick::merchants_detail

namespace p3::probe {

using namespace p3::data;
namespace md = tick::merchants_detail;

namespace {

// El s32 de un comerciante al que apunta `address`.
int32_t& merchant_field(SaveGame& s, const ProbeCall& c, uint32_t address) {
	const Location l = c.locate(address);
	return s.merchants[size_t(l.index)].memory.s32(l.offset);
}

}  // namespace

void register_probes_merchants(ProbeRegistry& r) {
	r[0x4F7BB0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// thiscall sobre el comerciante, con su índice de argumento.
		tick::fn_4f7bb0(s, int(c.args[0]));
		return std::nullopt;
	};
	r[0x4D9080] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		// ecx = la cola de eventos (0x6DD73C); punteros a [+0x46C] y [+0x498] del comerciante.
		md::fn_4d9080(s, c.args[0], merchant_field(s, c, c.args[1]), merchant_field(s, c, c.args[2]));
		return std::nullopt;
	};
	r[0x52A7D0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		return uint32_t(md::fn_52a7d0(s, c.ship(c.args[0])));
	};
	r[0x517F70] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		return md::fn_517f70(s, c.ship(c.ecx));
	};
	r[0x51F220] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		return uint32_t(md::fn_51f220(s, c.town(c.ecx), c.args[0]));
	};
	r[0x500330] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		return uint32_t(md::fn_500330(s, c.office(c.ecx), c.args[0]));
	};
	r[0x4F9430] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		c.eax_mask = 0xFF;  // Devuelve un u8 en al.
		return md::fn_4f9430(s, c.merchant(c.ecx));
	};
	r[0x4F7510] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		md::fn_4f7510(s, c.merchant(c.ecx), c.args[0], c.args[1], c.args[2]);
		return std::nullopt;
	};
	r[0x4F75F0] = [](SaveGame& s, const ProbeCall& c) -> std::optional<uint32_t> {
		md::fn_4f75f0(s, c.merchant(c.ecx), c.args[0]);
		return std::nullopt;
	};
}

}  // namespace p3::probe
