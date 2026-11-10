// Copyright 2026 Ian Karlsson
#include "internal.hpp"

#include <algorithm>
#include <cstdio>

namespace sfs {

static const uint8_t sync[12] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };

Image::Image(const samplerfs_io &io) : io_(io), size_(io.size)
{
	uint8_t head[16];
	if (io_.read && io_.size >= 2352 && io_.read(io_.user, 0, head, 16) == 16 && !memcmp(head, sync, 12)
		&& io_.size % 2352 == 0 && (head[15] == 1 || head[15] == 2)) {
		raw_ = true;
		data_at_ = head[15] == 1 ? 16 : 24;
		size_ = io_.size / 2352 * 2048;
	}
}

bool Image::read(uint64_t offset, void *buf, size_t n) const
{
	if (!io_.read || offset > size_ || n > size_ - offset)
		return false;
	if (!raw_)
		return io_.read(io_.user, offset, buf, n) == n;
	auto *out = static_cast<uint8_t *>(buf);
	while (n) {
		uint64_t block = offset / 2048;
		size_t at = size_t(offset % 2048), k = std::min(n, 2048 - at);
		if (io_.read(io_.user, block * 2352 + data_at_ + at, out, k) != k)
			return false;
		out += k;
		offset += k;
		n -= k;
	}
	return true;
}

std::vector<uint8_t> Image::read(uint64_t offset, size_t n) const
{
	std::vector<uint8_t> v(n);
	if (!read(offset, v.data(), n))
		v.clear();
	return v;
}

size_t Medium::add(samplerfs_kind kind, samplerfs_format fmt, uint32_t number, size_t parent, std::string_view name,
	uint64_t bytes, uint64_t at)
{
	samplerfs_entry e {};
	e.kind = kind;
	e.format = fmt;
	e.number = number;
	e.parent = parent;
	std::string n = trimmed(name);
	snprintf(e.name, sizeof e.name, "%s", n.c_str());
	e.bytes = bytes;
	entries.push_back(e);
	where.push_back(at);
	return entries.size() - 1;
}

void Medium::tag(size_t i, std::string_view t)
{
	snprintf(entries[i].tag, sizeof entries[i].tag, "%.*s", int(t.size()), t.data());
}

size_t Medium::child(size_t parent, samplerfs_kind kind, uint32_t number) const
{
	for (size_t i = parent + 1; i < entries.size(); i++)
		if (entries[i].parent == parent && entries[i].kind == kind && entries[i].number == number)
			return i;
	return SAMPLERFS_NONE;
}

std::string trimmed(std::string_view s)
{
	while (!s.empty() && (s.back() == ' ' || s.back() == '\0'))
		s.remove_suffix(1);
	return std::string(s);
}

void clamp_points(samplerfs_sample &s)
{
	s.end = std::min(s.end, s.frames);
	s.start = std::min(s.start, s.end);
	s.loop_end = std::min(std::max(s.loop_end, s.start), s.end);
	s.loop_start = std::min(std::max(s.loop_start, s.start), s.loop_end);
	if (s.loop_start == s.loop_end)
		s.loop = SAMPLERFS_LOOP_NONE;
}

}
