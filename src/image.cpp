// Copyright 2026 Ian Karlsson
#include "internal.hpp"

#include <algorithm>
#include <cstdio>

namespace syfs {

Image::Image(const syfs_io &io) : stores_(stores_of(io))
{
}

bool Image::read(uint64_t offset, void *buf, size_t n) const
{
	return stores_.top->read(offset, buf, n);
}

std::vector<uint8_t> Image::read(uint64_t offset, size_t n) const
{
	std::vector<uint8_t> v(n);
	if (!read(offset, v.data(), n))
		v.clear();
	return v;
}

size_t Medium::add(syfs_key kind, syfs_key fmt, uint32_t number, size_t parent, std::string_view name,
	uint64_t bytes, uint64_t at)
{
	syfs::Entry e {};
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

size_t Medium::child(size_t parent, syfs_key kind, uint32_t number) const
{
	for (size_t i = parent + 1; i < entries.size(); i++)
		if (entries[i].parent == parent && entries[i].kind == kind && entries[i].number == number)
			return i;
	return syfs::NONE;
}

std::string trimmed(std::string_view s)
{
	while (!s.empty() && (s.back() == ' ' || s.back() == '\0'))
		s.remove_suffix(1);
	return std::string(s);
}

size_t read_words(const Image &im, uint64_t offset, int16_t *out, size_t n)
{
	if (offset >= im.size())
		return 0;
	n = size_t(std::min<uint64_t>(n, (im.size() - offset) / 2));
	uint8_t buf[8192];
	size_t done = 0;
	while (done < n) {
		size_t k = std::min(n - done, sizeof buf / 2);
		if (!im.read(offset + 2 * done, buf, 2 * k))
			break;
		for (size_t j = 0; j < k; j++)
			out[done + j] = int16_t(le16(buf + 2 * j));
		done += k;
	}
	return done;
}

size_t Medium::sample_at(size_t i, int) const
{
	syfs_key k = entries[i].kind;
	syfs::Sample s = blank_sample();
	return (k == SYFS_TYPE_SAMPLE || k == SYFS_TYPE_TONE || k == SYFS_TYPE_FILE) && sample(i, s) ? i : syfs::NONE;
}

syfs::Sample blank_sample()
{
	syfs::Sample s {};
	s.key = -1;
	s.channels = 1;
	s.bits = 16;
	return s;
}

void clamp_points(syfs::Sample &s)
{
	s.end = std::min(s.end, s.frames);
	s.start = std::min(s.start, s.end);
	s.loop_end = std::min(std::max(s.loop_end, s.start), s.end);
	s.loop_start = std::min(std::max(s.loop_start, s.start), s.loop_end);
	if (s.loop_start == s.loop_end)
		s.loop = SYFS_NONE;
}

}
