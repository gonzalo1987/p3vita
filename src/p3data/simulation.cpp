#include "p3data/simulation.hpp"

#include <cstring>

#include "p3data/town_tick/internal.hpp"
#include "p3data/town_tick/votes.hpp"

namespace p3::data {

int town_for_tick(const SaveGame& save, uint32_t tick) {
	const uint32_t t = tick & 0xFF;
	int town = -1;
	if ((t & 7) == 7) {
		town = int(t >> 3);
	} else if ((t & 7) == 3) {
		town = int((t + 0xFF) >> 3);
	}
	return town >= 0 && size_t(town) < save.towns.size() ? town : -1;
}

int merchant_for_tick(const SaveGame& save, uint32_t tick) {
	const uint32_t t = tick & 0xFF;
	int merchant = -1;
	if ((t & 7) == 1) {
		merchant = int(t >> 3);
	} else if ((t & 7) == 6) {
		merchant = int((t + 0x100) >> 3);
	}
	return merchant >= 0 && merchant < save.globals.u16(global::kWorldMerchantCount) ? merchant : -1;
}

void town_tick(SaveGame& save, int town) {
	tick::fn_51ba10(save, town, save.globals.u16(global::kWorldDayOfYear));
}

void advance_game_tick(SaveGame& save, Sailing& sailing) {
	sailing.begin_tick();
	const uint32_t t = save.tick() & 0xFF;
	if (t == 0) {
		// Al empezar el día (las estadísticas de 0x54DE90 no se reproducen).
		tick::fn_5315c0(save, save.tick() % 0x700 == 0);
		tick::fn_535450(save, save.tick());
		uint8_t& countdown = save.globals.u8(global::kWorld + 0x8B);
		if (countdown > 0) {
			--countdown;
		}
	}
	const int town = town_for_tick(save, save.tick());
	if (town >= 0) {
		town_tick(save, town);
		if ((t & 7) == 7 && size_t(town) < save.sieges.size()) {
			tick::fn_6349f0(save, town);
		}
	}
	if ((t & 7) == 5 && (t >> 3) < 15) {
		tick::fn_52d7d0(save, int(t >> 3));
	}
	const int merchant = merchant_for_tick(save, save.tick());
	if (merchant >= 0) {
		tick::fn_4f7bb0(save, merchant);
	}
	sailing.update_ships();
	tick::set_event_navigation(&sailing.nav());
	tick::set_event_sailing(&sailing);
	tick::fn_4d85c0(save, save.tick());
	for (size_t i = 0; i < save.sieges.size(); ++i) {
		tick::fn_633a90(save, int(i));
	}
	run_queued_operations(save, sailing);
}

void run_queued_operations(SaveGame& save, Sailing& sailing) {
	std::vector<std::vector<uint8_t>> operations;
	operations.swap(save.queued_operations);
	for (const std::vector<uint8_t>& op : operations) {
		run_operation(save, &sailing, op.data());
	}
}

void run_operation(SaveGame& save, Sailing* sailing, const uint8_t* operation) {
	int32_t fields[5];
	std::memcpy(fields, operation, sizeof(fields));
	switch (uint32_t(fields[0])) {
	case 0x00:  // Zarpar (0x537670): {0, barco, ciudad, x, y}.
		if (sailing) {
			sailing->set_sail(fields[1], fields[2], fields[3], fields[4]);
		}
		break;
	case 0x0A:  // A ship is deleted (0x535866, 0x507FB0).
		tick::fn_535866(save, sailing, operation);
		break;
	case 0x12:  // El capitán de un barco mejora (0x538A80).
		tick::fn_538a80(save, operation);
		break;
	case 0x13:  // El capitán de un barco de un jugador humano se retira: el evento 0x27 (0x538C40).
		tick::fn_538c40(save, operation);
		break;
	case 0x1E:  // Un convoy sale de expedición (0x539B50).
		tick::fn_539b50(save, sailing, operation);
		break;
	case 0x1F:  // Un punto de la ruta de expedición de un convoy (0x539C60).
		tick::fn_539c60(save, operation);
		break;
	case 0x22:  // Construir (0x53CE30): las obras de las ciudades y de los comerciantes.
		tick::construction_detail::fn_53ce30(save, operation);
		break;
	case 0x23:  // Empezar la muralla de un lado (0x535B12).
		tick::construction_detail::fn_535b12(save, operation);
		break;
	case 0x24:  // La siguiente pieza de muralla pendiente (0x535B44, 0x522C70).
		tick::construction_detail::fn_535b44(save, operation);
		break;
	case 0x27:  // Materiales para las obras de una ciudad (0x535BBC).
		tick::construction_detail::fn_535bbc(save, operation);
		break;
	case 0x28:  // Un comerciante pone en venta un edificio (0x53D190, 0x4DAB00).
		tick::fn_53d190(save, operation);
		break;
	case 0x29:  // Un préstamo: el evento 0x01 (0x539DC0).
		tick::fn_539dc0(save, operation);
		break;
	case 0x2C:  // Un barco encargado al astillero de una ciudad (0x535C70, 0x52A360).
		tick::fn_535c70(save, operation);
		break;
	case 0x37:  // El comerciante paga por una ciudad (0x535E04, 0x4F8560).
		tick::fn_535e04(save, operation);
		break;
	case 0x38:  // Una subasta de barco: el evento 0x0A (0x535E3B).
		tick::fn_535e3b(save, operation);
		break;
	case 0x3F:  // The vote of a human player in an election or the diet (0x53AA10).
		tick::fn_53aa10(save, operation);
		break;
	case 0x3B:  // Materiales para la carretera de un encargo de la Hansa (0x53A3E0).
		tick::fn_53a3e0(save, operation);
		break;
	case 0x46:  // Alcalde para una ciudad nueva (0x535FD6).
		tick::fn_535fd6(save, operation);
		break;
	case 0x48:  // Una propuesta para una ciudad: la votación o el impuesto (0x53B010).
		tick::fn_53b010(save, operation);
		break;
	case 0x4B:  // Un candidato coge un encargo de la Hansa (0x53B440).
		tick::fn_53b440(save, operation);
		break;
	case 0x60:  // A merchant's wedding is arranged: event 0x15 (0x53DA90).
		tick::fn_53da90(save, operation);
		break;
	case 0x61:  // La fiesta de un comerciante: el evento 0x07 (0x53DBF0).
		tick::fn_53dbf0(save, operation);
		break;
	case 0x64:  // Un robo en una oficina: el evento 0x33 (0x5363EE).
		tick::fn_5363ee(save, operation);
		break;
	case 0x81:  // Un encargo de la dieta de la Hansa: el evento 0x05 (0x53FA80).
		tick::fn_53fa80(save, operation);
		break;
	case 0x87:  // A child is born to a married merchant (0x53FEA0).
		tick::fn_53fea0(save, operation);
		break;
	case 0x8E:  // Un asedio: el ejército sitiador (0x536A67, 0x633AF0).
		tick::fn_536a67(save, operation);
		break;
	case 0x65:  // An AI merchant goes bankrupt (0x53DD70, 0x4F5D80).
		tick::fn_53dd70(save, operation);
		break;
	case 0x67:  // Un administrador mejora (0x53DDF0).
		tick::fn_53ddf0(save, operation);
		break;
	case 0x78:  // Un barco nuevo en las entradas de [0x6DD7C0] (0x5051E0).
		tick::fn_5051e0(save, sailing, operation);
		break;
	case 0x79:  // La dieta de la Hansa: un byte por ciudad (0x536535).
		tick::fn_536535(save, operation);
		break;
	case 0x7A:  // La dieta de la Hansa: [0x834..0x83F] de una ciudad (0x536570).
		tick::fn_536570(save, operation);
		break;
	case 0x7B:  // Capitanes y administradores que buscan trabajo (0x5365E3).
		tick::fn_5365e3(save, operation);
		break;
	case 0x7C:  // La peste: el evento 0x1C (0x536642).
		tick::fn_536642(save, operation);
		break;
	case 0x7D:  // Un incendio en una ciudad: el evento 0x1D (0x5366A0).
		tick::fn_5366a0(save, operation);
		break;
	case 0x7E:  // La temporada de piratas que viene: el evento 0x1F (0x53F8B0).
		tick::fn_53f8b0(save, operation);
		break;
	case 0x7F:  // Ciudades con el bit 0x4000000 y eventos 0x35 (0x5366E8).
		tick::fn_5366e8(save, operation);
		break;
	case 0x80:  // Dos campos del registro 0x80 de la cola, si es un encargo (0x536756).
		tick::fn_536756(save, operation);
		break;
	case 0x83:  // Un evento 0x00 (0x5367B5).
		tick::fn_5367b5(save, operation);
		break;
	case 0x84:  // El encargo del jefe de la Hansa: el evento 0x30 (0x53FCA0).
		tick::fn_53fca0(save, operation);
		break;
	case 0x85:  // Crear un encargo de la Hansa (0x53FD10).
		tick::fn_53fd10(save, operation);
		break;
	case 0x86:  // Quitar los encargos de la Hansa que ya no valen (en 0x536801).
		tick::fn_53fe20(save);
		break;
	case 0xB8:  // Network games' sync checks (0x542F10).
		tick::fn_542f10(save, operation);
		break;
	default:  // Las demás aún no se reproducen.
		break;
	}
}

}  // namespace p3::data
