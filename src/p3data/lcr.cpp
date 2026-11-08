#include "p3data/lcr.hpp"

#include <cstring>

namespace p3::data {

namespace {

bool fail(std::string* error, const char* what) {
	if (error) {
		*error = what;
	}
	return false;
}

struct Reader {
	const uint8_t* data;
	size_t size;
	size_t at = 0;

	bool u32(uint32_t& v) {
		if (at + 4 > size) {
			return false;
		}
		std::memcpy(&v, data + at, 4);
		at += 4;
		return true;
	}
	bool s32(int& v) {
		uint32_t u = 0;
		if (!u32(u)) {
			return false;
		}
		v = int(int32_t(u));
		return true;
	}
	// A string: its length (with its NUL; 0 for none) and its bytes; '\' made '/'.
	bool text(std::string& s) {
		uint32_t n = 0;
		if (!u32(n) || n > 512 || at + n > size || (n > 0 && data[at + n - 1] != 0)) {
			return false;
		}
		s.assign(reinterpret_cast<const char*>(data + at), n > 0 ? n - 1 : 0);
		for (char& c : s) {
			if (c == '\\') {
				c = '/';
			}
		}
		at += n;
		return true;
	}
};

}  // namespace

// The layout (docs/formats/lcr.md; checked on the 80 ships' looks and the 108
// effects of the GOG 1.1 CPR): u16 0x0600, u16 the id, u32 the class, u32 the
// key, u32, u32, at 0x14 u32 the directions, u32 the frames per second, u8,
// six u32; at 0x35 the shadow image's name (u32 its length with the NUL, 0 for
// none, the bytes); then each direction: u32 its frames, each u32 the anchor
// x, y, with a shadow u32 its x, y, w, h and where its corner goes, x, y, then
// u32 the strips n and n × (x, y, w, h), u32 the body's x1, y1, x2, y2
// (inclusive); after the last direction the main image's name. Some files go
// on after it (a ship's look of hull state 0: the icons the town view puts
// over a ship, the white flag, the marker arrow...).
bool parse_lcr(const uint8_t* data, size_t size, LcrAnim& out, std::string* error) {
	out = LcrAnim();
	if (size < 0x39) {
		return fail(error, "too short");
	}
	uint16_t version = 0;
	std::memcpy(&version, data, 2);
	std::memcpy(&out.id, data + 2, 2);
	std::memcpy(&out.kind, data + 4, 2);
	std::memcpy(&out.key, data + 8, 4);
	uint32_t directions = 0;
	std::memcpy(&directions, data + 0x14, 4);
	std::memcpy(&out.fps, data + 0x18, 4);
	std::memcpy(out.words, data + 0x1D, sizeof(out.words));
	if (version != 0x0600 || directions == 0 || directions > 64) {
		return fail(error, "not an animation");
	}
	Reader r{data, size, 0x35};
	if (!r.text(out.shadow_image)) {
		return fail(error, "bad shadow image name");
	}
	const bool shadowed = !out.shadow_image.empty();
	out.directions.resize(directions);
	for (std::vector<LcrFrame>& frames : out.directions) {
		uint32_t n = 0;
		if (!r.u32(n) || n > 256) {
			return fail(error, "bad frame count");
		}
		frames.resize(n);
		for (LcrFrame& f : frames) {
			uint32_t strips = 0;
			int x2 = 0;
			int y2 = 0;
			f.shadowed = shadowed;
			if (!r.s32(f.anchor_x) || !r.s32(f.anchor_y)) {
				return fail(error, "bad frame");
			}
			if (shadowed && (!r.s32(f.shadow.x) || !r.s32(f.shadow.y) || !r.s32(f.shadow.w) || !r.s32(f.shadow.h) ||
								!r.s32(f.shadow_x) || !r.s32(f.shadow_y))) {
				return fail(error, "bad frame");
			}
			if (!r.u32(strips) || strips > 64 || r.at + 16 * size_t(strips) > size) {
				return fail(error, "bad frame");
			}
			r.at += 16 * size_t(strips);
			if (!r.s32(f.body.x) || !r.s32(f.body.y) || !r.s32(x2) || !r.s32(y2)) {
				return fail(error, "bad frame");
			}
			f.body.w = x2 - f.body.x + 1;
			f.body.h = y2 - f.body.y + 1;
		}
	}
	if (!r.text(out.image) || out.image.empty()) {
		return fail(error, "bad image name");
	}
	return true;
}

std::string lcr_ship_path(int type, int hull, int sails) {
	static constexpr const char* kTypes[4] = {"schnigge", "kraier", "kogge", "holk"};
	return std::string("iso/lcr/") + kTypes[type & 3] + "_schaden0" + char('0' + (hull & 3)) + "_segel0" +
		char('0' + sails % 5) + ".lcr";
}

}  // namespace p3::data
