// Copyright 2026 Ian Karlsson
// WAV and AIFF headers (docs/formats.md, "WAV and AIFF files")
#include "audio.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace syfs {

namespace {

constexpr int CHUNKS = 256;
constexpr size_t READ_BYTES = 1 << 16;

struct Loop {
	bool found = false;
	uint32_t type = 0;
	uint64_t start = 0, end = 0;
};

// a format tag's encoding: 1 integers, 3 floating point; WAVE_FORMAT_EXTENSIBLE (0xFFFE) names one in its subformat
AudioLayout::Code wav_code(uint16_t tag, uint32_t width)
{
	if (tag == 1 && width >= 1 && width <= 4)
		return width == 1 ? AudioLayout::UNSIGNED : AudioLayout::SIGNED;
	if (tag == 3 && (width == 4 || width == 8))
		return AudioLayout::FLOAT;
	return AudioLayout::NONE;
}

bool wav(const FileRead &read, uint64_t bytes, syfs::Sample &out, AudioLayout &layout)
{
	uint8_t h[12];
	if (bytes < 12 || !read(0, h, 12) || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4))
		return false;
	uint32_t block = 0;
	uint16_t tag = 0;
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
			uint8_t f[26];
			if (!read(body, f, size >= 26 ? 26 : 16))
				return false;
			tag = le16(f);
			out.channels = le16(f + 2);
			out.rate = le32(f + 4);
			block = le16(f + 12);
			out.bits = le16(f + 14);
			if (tag == 0xFFFE && size >= 26)
				tag = le16(f + 24);
			fmt = true;
		} else if (!memcmp(c, "data", 4)) {
			out.frames = std::min(size, room);
			layout.data = body;
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
	layout.block = block;
	if (out.channels && block % out.channels == 0) {
		layout.width = block / out.channels;
		layout.code = wav_code(tag, layout.width);
	}
	if (!out.bits || out.bits > 8 * layout.width)
		out.bits = 8 * layout.width;
	out.end = out.frames;
	if (loop.found) {
		out.mode = int(loop.type);
		out.loop = loop.type == 1 ? SYFS_LOOP_ALTERNATE : SYFS_LOOP_FORWARD;
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

// an AIFC's compression type: none and twos big-endian integers, sowt little-endian ones, fl32 and fl64 floating point
AudioLayout::Code aifc_code(const uint8_t *type, uint32_t width, bool &big)
{
	auto is = [&](const char *t) { return !memcmp(type, t, 4); };
	big = !is("sowt");
	if ((is("NONE") || is("twos") || is("sowt")) && width >= 1 && width <= 4)
		return AudioLayout::SIGNED;
	if (((is("fl32") || is("FL32")) && width == 4) || ((is("fl64") || is("FL64")) && width == 8))
		return AudioLayout::FLOAT;
	return AudioLayout::NONE;
}

bool aiff(const FileRead &read, uint64_t bytes, syfs::Sample &out, AudioLayout &layout)
{
	uint8_t h[12];
	if (bytes < 12 || !read(0, h, 12) || memcmp(h, "FORM", 4) || (memcmp(h + 8, "AIFF", 4) && memcmp(h + 8, "AIFC", 4)))
		return false;
	bool aifc = !memcmp(h + 8, "AIFC", 4);
	uint8_t compression[4] = { 'N', 'O', 'N', 'E' };
	bool comm = false, ssnd = false;
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
			uint8_t f[22];
			if (!read(body, f, aifc && size >= 22 ? 22 : 18))
				return false;
			if (aifc && size >= 22)
				memcpy(compression, f + 18, 4);
			out.channels = be16(f);
			out.frames = be32(f + 2);
			out.bits = be16(f + 6);
			double rate = extended(f + 8);
			out.rate = rate > 0 && rate < 4e9 ? uint32_t(std::lround(rate)) : 0;
			comm = true;
		} else if (!memcmp(c, "SSND", 4) && size >= 8) {
			uint8_t f[4];
			if (read(body, f, 4)) {
				layout.data = body + 8 + be32(f);
				ssnd = true;
			}
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
	layout.width = (out.bits + 7) / 8;
	layout.block = layout.width * out.channels;
	if (ssnd && layout.block)
		layout.code = aifc_code(compression, layout.width, layout.big);
	// an AIFC's floating point gives no sample size of its own
	if (layout.code == AudioLayout::FLOAT)
		out.bits = 8 * layout.width;
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
			out.loop = mode == 2 ? SYFS_LOOP_ALTERNATE : SYFS_LOOP_FORWARD;
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

bool audio_sample(const FileRead &read, uint64_t bytes, syfs::Sample &out, AudioLayout *layout)
{
	for (auto *reader : { wav, aiff }) {
		syfs::Sample s = blank_sample();
		s.channels = 0;
		AudioLayout l;
		if (reader(read, bytes, s, l) && s.channels) {
			// frames the file holds whole, where its header claims more
			if (l.code != AudioLayout::NONE && (l.data > bytes || s.frames > (bytes - l.data) / l.block))
				s.frames = l.data > bytes ? 0 : (bytes - l.data) / l.block;
			out = s;
			if (layout)
				*layout = l;
			return true;
		}
	}
	return false;
}

namespace {

int16_t decoded(const uint8_t *p, const AudioLayout &l)
{
	auto byte = [&](uint32_t k) { return p[l.big ? k : l.width - 1 - k]; };  // k from the most significant
	switch (l.code) {
	case AudioLayout::UNSIGNED:
		return int16_t((byte(0) - 128) * 256);
	case AudioLayout::SIGNED:
		return int16_t(byte(0) << 8 | (l.width > 1 ? byte(1) : 0));
	case AudioLayout::FLOAT: {
		uint64_t bits = 0;
		for (uint32_t k = 0; k < l.width; k++)
			bits = bits << 8 | byte(k);
		double v;
		if (l.width == 4) {
			float f;
			uint32_t b = uint32_t(bits);
			memcpy(&f, &b, 4);
			v = f;
		} else
			memcpy(&v, &bits, 8);
		v = std::nearbyint(v * 32768);
		return int16_t(v != v ? 0 : std::clamp(v, -32768.0, 32767.0));
	}
	default:
		return 0;
	}
}

}

size_t audio_wave(const FileRead &read, uint64_t bytes, uint64_t first, int16_t *out, size_t frames)
{
	syfs::Sample s;
	AudioLayout l;
	if (!audio_sample(read, bytes, s, &l) || l.code == AudioLayout::NONE || first >= s.frames)
		return 0;
	frames = size_t(std::min<uint64_t>(frames, s.frames - first));
	size_t per = std::max<size_t>(1, READ_BYTES / l.block), done = 0;
	std::vector<uint8_t> buf;
	while (done < frames) {
		size_t n = std::min(per, frames - done);
		buf.resize(n * l.block);
		if (!read(l.data + (first + done) * l.block, buf.data(), buf.size()))
			break;
		for (size_t k = 0; k < n * s.channels; k++)
			*out++ = decoded(&buf[k * l.width], l);
		done += n;
	}
	return done;
}

}
