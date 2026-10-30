// La cola de eventos (events.cpp): los tipos 0x02 (0x4DBB00, elección del alcalde 0x528E60), 0x0A y 0x36 (subastas, 0x4E2CD4), 0x20 (0x4DBBD0) y 0x21 (0x4DC1F0).

#include "p3data/town_tick/internal.hpp"

namespace p3::data::tick {

// Los tipos de este grupo: por traducir.
int events_mayor(SaveGame& save, uint16_t event, uint16_t type) {
	(void)save;
	(void)event;
	(void)type;
	return -1;
}

}  // namespace p3::data::tick
