// La cola de eventos (events.cpp): los tipos 0x0E (0x4E4A44), 0x12 (0x4E6A54), 0x1C (0x4E9094), 0x1E (0x4DFD44), 0x1F (0x4E0334), 0x2E (0x4E9A94), 0x30 (0x4EA594), 0x32 (0x4EAFE4) y 0x35 (0x4E94A4).

#include "p3data/town_tick/internal.hpp"

namespace p3::data::tick {

// Los tipos de este grupo: por traducir.
int events_rare(SaveGame& save, uint16_t event, uint16_t type) {
	(void)save;
	(void)event;
	(void)type;
	return -1;
}

}  // namespace p3::data::tick
