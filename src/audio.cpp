// Copyright 2026 Ian Karlsson
// WAV and AIFF headers (docs/formats.md, "WAV and AIFF files")
#include "audio.hpp"

#include <cctype>
#include <cmath>

namespace sfs {

namespace {

constexpr int CHUNKS = 256;

struct Loop {
	bool found = false;
	uint32_t type = 0;
	uint64_t start = 0, end = 0;
};

bool wav(const FileRead &read, uint64_t bytes, samplerfs_sample &out)
{
	uint8_t h[12];
	if (bytes < 12 || !read(0, h, 12) || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4))
		return false;
	uint32_t block = 0;
	bool fmt = false, data = false;
	Loop loop;
	uint64_t at = 12;
	for (int n = 0; n < CHUNKS && at + 8 <= bytes; n++) {
		uint8_t c[8];
		if (!read(at, c, 8))
			break;
		uint64_t size = le32(c + 4), body = at + 8;
		uint64_t room = bytes - body;
		if (!memcmp(c, "fmt ", 4) && size >= 16) {
			uint8_t f[16];
			if (!read(body, f, 16))
				return false;
			out.rate = le32(f + 4);
			block = le16(f + 12);
			fmt = true;
		} else if (!memcmp(c, "data", 4)) {
			out.frames = std::min(size, room);
			data = true;
		} else if (!memcmp(c, "smpl", 4) && size >= 36) {
			uint8_t s[36 + 24];
			if (read(body, s, 36)) {
				if (le32(s + 12) <= 127)
					out.key = int32_t(le32(s + 12));
				if (le32(s + 28) && size >= 60 && read(body + 36, s + 36, 24)) {
					loop.found = true;
					loop.type = le32(s + 36 + 4);
					loop.start = le32(s + 36 + 8);
					loop.end = uint64_t(le32(s + 36 + 12)) + 1;
				}
			}
		}
		at = body + size + (size & 1);
	}
	if (!fmt || !data || !block)
		return false;
	out.frames /= block;
	out.end = out.frames;
	if (loop.found) {
		out.mode = int(loop.type);
		out.loop = loop.type == 1 ? SAMPLERFS_LOOP_ALTERNATE : SAMPLERFS_LOOP_FORWARD;
		out.loop_start = loop.start;
		out.loop_end = loop.end;
	}
	return true;
}

// an 80-bit IEEE extended number, as AIFF gives the rate
double extended(const uint8_t *p)
{
	int exponent = (p[0] & 0x7F) << 8 | p[1];
	uint64_t mantissa = uint64_t(be32(p + 2)) << 32 | be32(p + 6);
	if (!exponent && !mantissa)
		return 0;
	double v = std::ldexp(double(mantissa), exponent - 16383 - 63);
	return p[0] & 0x80 ? -v : v;
}

bool aiff(const FileRead &read, uint64_t bytes, samplerfs_sample &out)
{
	uint8_t h[12];
	if (bytes < 12 || !read(0, h, 12) || memcmp(h, "FORM", 4) || (memcmp(h + 8, "AIFF", 4) && memcmp(h + 8, "AIFC", 4)))
		return false;
	bool comm = false;
	uint16_t mode = 0, begin = 0, end = 0;
	bool inst = false;
	std::vector<std::pair<uint16_t, uint32_t>> markers;
	uint64_t at = 12;
	for (int n = 0; n < CHUNKS && at + 8 <= bytes; n++) {
		uint8_t c[8];
		if (!read(at, c, 8))
			break;
		uint64_t size = be32(c + 4), body = at + 8;
		if (!memcmp(c, "COMM", 4) && size >= 18) {
			uint8_t f[18];
			if (!read(body, f, 18))
				return false;
			out.frames = be32(f + 2);
			double rate = extended(f + 8);
			out.rate = rate > 0 && rate < 4e9 ? uint32_t(std::lround(rate)) : 0;
			comm = true;
		} else if (!memcmp(c, "INST", 4) && size >= 20) {
			uint8_t f[20];
			if (read(body, f, 20)) {
				if (f[0] <= 127)
					out.key = f[0];
				mode = be16(f + 8);
				begin = be16(f + 10);
				end = be16(f + 12);
				inst = true;
			}
		} else if (!memcmp(c, "MARK", 4) && size >= 2 && size <= 65536) {
			std::vector<uint8_t> m(size);
			if (read(body, m.data(), m.size())) {
				uint16_t count = be16(&m[0]);
				size_t p = 2;
				for (uint16_t k = 0; k < count && p + 7 <= m.size(); k++) {
					markers.emplace_back(be16(&m[p]), be32(&m[p + 2]));
					p += 6 + ((m[p + 6] + 2) & ~size_t(1));
				}
			}
		}
		at = body + size + (size & 1);
	}
	if (!comm)
		return false;
	out.end = out.frames;
	if (inst && (mode == 1 || mode == 2)) {
		auto position = [&](uint16_t id) -> int64_t {
			for (auto [mid, pos] : markers)
				if (mid == id)
					return pos;
			return -1;
		};
		int64_t b = position(begin), e = position(end);
		if (b >= 0 && e > b) {
			out.mode = mode;
			out.loop = mode == 2 ? SAMPLERFS_LOOP_ALTERNATE : SAMPLERFS_LOOP_FORWARD;
			out.loop_start = uint64_t(b);
			out.loop_end = uint64_t(e);
		}
	}
	return true;
}

}

bool audio_name(std::string_view name)
{
	size_t dot = name.rfind('.');
	if (dot == std::string_view::npos)
		return false;
	std::string ext;
	for (char c : name.substr(dot + 1))
		ext += char(tolower(uint8_t(c)));
	return ext == "wav" || ext == "aif" || ext == "aiff" || ext == "aifc";
}

bool audio_sample(const FileRead &read, uint64_t bytes, samplerfs_sample &out)
{
	for (auto *reader : { wav, aiff }) {
		samplerfs_sample s {};
		s.key = -1;
		if (reader(read, bytes, s)) {
			out = s;
			return true;
		}
	}
	return false;
}

}
