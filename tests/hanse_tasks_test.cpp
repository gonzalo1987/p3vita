// The operations the viewer queues for the path to Alderman, byte for byte as
// the game's UI builds them (docs/cloud/alderman-viewer.md), without game data.

#include <cstdio>
#include <cstdlib>

#include "p3data/hanse_tasks.hpp"

using namespace p3::data;

namespace {

int failures = 0;

void check(bool condition, const char* what) {
	if (!condition) {
		std::fprintf(stderr, "FAILED: %s\n", what);
		failures++;
	}
}

}  // namespace

int main() {
	// 0x5E8A3D: {u32 0x4B, u32 player, u32 event}, the rest 0.
	const OperationBytes take = take_task_operation(2, 0x0123);
	const OperationBytes take_expected = {0x4B, 0, 0, 0, 2, 0, 0, 0, 0x23, 0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	check(take == take_expected, "operation 0x4B");

	// 0x5F2D9A: {u32 0x3B, u16 event, u16 amount, u8 town, u8 player, u8 ware}, the rest 0.
	const OperationBytes road = road_materials_operation(0x0234, 50, 7, 2, 0x13);
	const OperationBytes road_expected = {0x3B, 0, 0, 0, 0x34, 0x02, 50, 0, 7, 2, 0x13, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	check(road == road_expected, "operation 0x3B");

	// The road window's amount: min(chosen, available), or what is still needed.
	check(road_window_amount(5, 12, 100) == 5, "amount: the chosen one");
	check(road_window_amount(50, 12, 100) == 12, "amount: what is available");
	check(road_window_amount(0xFFFFFFFF, 12, 7) == 7, "amount: what is still needed");
	check(road_window_amount(1, 0, 7) == 0, "amount: nothing available");

	RoadMaterials m;
	m.total = {360, 480, 120, 240};
	m.done = {30, 40, 10, 20};
	m.delivered = {5, 0, 10, 0};
	check(m.needed(0) == 325 && m.needed(2) == 100 && m.percent() == 8, "still needed and progress");

	if (failures == 0) {
		std::printf("hanse_tasks_test: all correct\n");
	}
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
