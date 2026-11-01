// The end of a sea battle (docs/cloud/battles-2.md §4): method +0x1C
// (0x60D299) and the battle's destructor (method +0x4, 0x611740 → 0x603BC0).
// Being rewritten: until they are, the step (0x606CC0) is not wired into
// the simulation (Sailing::fn_536bbf).

#include "p3data/sailing.hpp"

namespace p3::data {

// 0x60D299 (method +0x1C of a sea battle; whether side 1 has a ship with
// sailors): the end of the battle. Not rewritten yet.
void Sailing::fn_60d299(size_t battle, bool sailors) {
	(void)battle;
	(void)sailors;
}

// 0x611740 (method +0x4 of a sea battle; free): the destructor 0x603BC0 and
// the block freed. Not rewritten yet.
void Sailing::fn_611740(size_t battle, bool free) {
	(void)battle;
	(void)free;
}

}  // namespace p3::data
