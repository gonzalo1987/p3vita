// La cola de eventos (events.cpp): los tipos 0x03 (0x4DDA40), 0x01 (0x4DB4E0), 0x2A (0x4DE4F0) y 0x2B (0x4DE790).

#include "p3data/town_tick/internal.hpp"

namespace p3::data::tick {

// Los tipos de este grupo: por traducir.
int events_towns(SaveGame& save, uint16_t event, uint16_t type) {
	(void)save;
	(void)event;
	(void)type;
	return -1;
}

}  // namespace p3::data::tick
