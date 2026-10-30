// La cola de eventos (events.cpp): los tipos 0x2C (0x4DEA20, 0x4DEBA0), 0x05 (0x4E5B84) y 0x16 (0x4E8234).

#include "p3data/town_tick/internal.hpp"

namespace p3::data::tick {

// Los tipos de este grupo: por traducir.
int events_big(SaveGame& save, uint16_t event, uint16_t type) {
	(void)save;
	(void)event;
	(void)type;
	return -1;
}

}  // namespace p3::data::tick
