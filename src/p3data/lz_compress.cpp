#include "p3data/lz_compress.hpp"

#include <array>
#include <cstring>

namespace p3::data {

// The game's compressor, 0x54C410 → 0x6381E0 (Patrician3.exe, statically
// linked; its state lives in globals at 0x6C7B20..0x6C7CB8). The format is
// the one lz_decompress reads (lz.cpp): s32 size and a bit stream, LSB first,
// in u32 words. Each token: bit 0 literal (8 bits) or 1 match: 3 bits of
// distance class c, c + 1 bits, distance = bits + (2 << c) − 1 (1..510), and
// the length − 2 in fields of 2, 3, 4... bits, the next one while a field is
// all ones.
//
// How the game chooses (the rewrite must keep it to give the same bytes):
// a hash table over the u16 of the two bytes at each position (0x10000
// heads, 0x6C7B30) with chains of the last 510 positions (0x1FE nodes of 12
// bytes in a ring, 0x6C7B34: position, older, newer), newest first.
// 0x6384D4 at each position: if its two bytes are equal, the first one is a
// literal and then the run from the next byte is a match at distance 1 (if
// it is 2 bytes or more); if not, the longest match of the chain (the newest
// on a tie; the first that reaches the end of the input wins at once), or a
// literal. Every position covered is inserted in the table (0x638434). The
// last byte, if no match covered it, is a literal. When the output reaches
// the size of the input, the game stores it instead (n < 0, 0x638274), and
// it does the same below 16 bytes.

namespace {

constexpr size_t kWindow = 0x1FE;

std::vector<uint8_t> stored(const uint8_t* data, size_t size) {
	std::vector<uint8_t> out(4 + size);
	const int32_t n = -int32_t(size);
	std::memcpy(out.data(), &n, 4);
	if (size) {
		std::memcpy(out.data() + 4, data, size);
	}
	return out;
}

class Compressor {
public:
	Compressor(const uint8_t* data, size_t size) : data_(data), size_(size), head_(0x10000, -1) {}

	// False if the output reached the size of the input (the game then stores).
	bool run(std::vector<uint8_t>& out) {
		out_.reserve(size_ / 2);
		size_t p = 0;
		if (!put(uint32_t(size_), 32) || !put(uint32_t(data_[p]) << 1, 9)) {
			return false;
		}
		insert(p);
		++p;
		++advanced_;
		while (p < size_ - 1) {
			size_t length = 0;
			if (!find_and_emit(p, length)) {
				return false;
			}
			for (; length > 0; --length) {
				insert(p);
				++p;
				++advanced_;
			}
		}
		if (p != size_ && !put(uint32_t(data_[p]) << 1, 9)) {
			return false;
		}
		// The last word, even if empty, without the check of put.
		word(accumulator_);
		out = std::move(out_);
		return true;
	}

private:
	struct Node {
		size_t pos = 0;
		int older = -1;  // [+0x4]
		int newer = -1;  // [+0x8]
	};

	// The u16 of the two bytes at p (the game reads one byte past the end at
	// the last position; it doesn't change the output).
	uint16_t key(size_t p) const {
		return uint16_t(data_[p] | (p + 1 < size_ ? data_[p + 1] : 0) << 8);
	}

	// 0x638434.
	void insert(size_t p) {
		if (advanced_ >= kWindow) {
			if (next_node_ == int(kWindow)) {
				next_node_ = 0;
			}
			Node& old = nodes_[size_t(next_node_)];
			if (old.newer < 0) {
				head_[key(old.pos)] = -1;
			} else {
				nodes_[size_t(old.newer)].older = -1;
			}
		}
		const uint16_t k = key(p);
		const int first = head_[k];
		Node& node = nodes_[size_t(next_node_)];
		if (first >= 0) {
			nodes_[size_t(first)].newer = next_node_;
		}
		head_[k] = next_node_;
		node.pos = p;
		node.older = first;
		node.newer = -1;
		++next_node_;
	}

	// How far the bytes at a and b agree from `from` on, without passing the
	// end of the input from b.
	size_t extend(size_t a, size_t b, size_t from) const {
		size_t n = from;
		while (b + n < size_ && data_[b + n] == data_[a + n]) {
			++n;
		}
		return n;
	}

	// 0x6384D4: a literal or a match at p; `length` is how many positions
	// the main loop must then insert.
	bool find_and_emit(size_t& p, size_t& length) {
		size_t best_pos = 0;
		size_t best_len = 0;
		bool found = false;
		if (data_[p] == data_[p + 1]) {
			// A run: [p + 1 + k] against [p + k], from the byte after p.
			const size_t run = 1 + extend(p + 1, p + 2, 0);
			if (run >= 2) {
				best_pos = p;
				best_len = run;
				if (!put(uint32_t(data_[p]) << 1, 9)) {
					return false;
				}
				insert(p);
				++p;
				++advanced_;
				found = true;
			}
		}
		if (!found) {
			int node = head_[key(p)];
			if (node < 0) {
				length = 1;
				return put(uint32_t(data_[p]) << 1, 9);
			}
			for (; node >= 0; node = nodes_[size_t(node)].older) {
				const size_t candidate = nodes_[size_t(node)].pos;
				const size_t n = extend(candidate, p, 2);
				if (p + n >= size_) {
					best_pos = candidate;
					best_len = n;
					break;
				}
				if (best_len < n) {
					best_pos = candidate;
					best_len = n;
				}
			}
		}
		// The distance − 1 in its class.
		static constexpr uint32_t kClassTop[7] = {1, 5, 0xD, 0x1D, 0x3D, 0x7D, 0xFD};
		static constexpr uint32_t kClassBase[8] = {0, 2, 6, 0xE, 0x1E, 0x3E, 0x7E, 0xFE};
		const uint32_t d = uint32_t(p - best_pos - 1);
		uint32_t c = 0;
		while (c < 7 && d > kClassTop[c]) {
			++c;
		}
		if (!put((2 * c + 1) | (d - kClassBase[c]) << 4, int(c) + 5)) {
			return false;
		}
		// The length − 2, in fields of 2, 3, 4... bits.
		uint32_t rest = uint32_t(best_len - 2);
		int width = 2;
		for (;;) {
			const uint32_t all = (uint32_t(1) << width) - 1;
			if (rest < all) {
				break;
			}
			rest -= all;
			if (!put(all, width)) {
				return false;
			}
			++width;
		}
		if (!put(rest, width)) {
			return false;
		}
		length = best_len;
		return true;
	}

	// 0x638728.
	bool put(uint32_t value, int bits) {
		accumulator_ |= value << bits_;
		bits_ += bits;
		if (bits_ < 32) {
			return true;
		}
		word(accumulator_);
		if (out_.size() >= size_) {
			return false;
		}
		bits_ -= 32;
		accumulator_ = bits_ == 0 ? 0 : value >> (bits - bits_);
		return true;
	}

	void word(uint32_t v) {
		for (int i = 0; i < 4; ++i) {
			out_.push_back(uint8_t(v >> (8 * i)));
		}
	}

	const uint8_t* data_;
	size_t size_;
	std::vector<int> head_;
	std::array<Node, kWindow> nodes_{};
	int next_node_ = 0;
	size_t advanced_ = 0;  // [0x6C7B4C]
	std::vector<uint8_t> out_;
	uint32_t accumulator_ = 0;
	int bits_ = 0;
};

}  // namespace

std::vector<uint8_t> lz_compress(const uint8_t* data, size_t size) {
	if (size < 16) {
		return stored(data, size);
	}
	Compressor compressor(data, size);
	std::vector<uint8_t> out;
	if (!compressor.run(out)) {
		return stored(data, size);
	}
	return out;
}

}  // namespace p3::data
