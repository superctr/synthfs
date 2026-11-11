// Copyright 2026 Ian Karlsson
#include "store.hpp"

#include <algorithm>
#include <cstring>

namespace syfs {

bool HostStore::read(uint64_t offset, void *buf, size_t n) const
{
	if (!io_.read || offset > size_ || n > size_ - offset)
		return false;
	return io_.read(io_.user, offset, buf, n) == n;
}

bool HostStore::write(uint64_t offset, const void *buf, size_t n) const
{
	if (!io_.write || io_.write(io_.user, offset, buf, n) != n)
		return false;
	size_ = std::max(size_, offset + n);
	return true;
}

static const uint8_t sync[12] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };

RawSectors::RawSectors(std::shared_ptr<const Store> under, unsigned data_at)
	: under_(std::move(under)), data_at_(data_at), size_(under_->size() / 2352 * 2048)
{
}

std::shared_ptr<const Store> RawSectors::over(std::shared_ptr<const Store> under)
{
	uint8_t head[16];
	if (under->size() < 2352 || under->size() % 2352 || !under->read(0, head, 16) || memcmp(head, sync, 12)
		|| (head[15] != 1 && head[15] != 2))
		return nullptr;
	return std::shared_ptr<const Store>(new RawSectors(std::move(under), head[15] == 1 ? 16 : 24));
}

bool RawSectors::read(uint64_t offset, void *buf, size_t n) const
{
	if (offset > size_ || n > size_ - offset)
		return false;
	auto *out = static_cast<uint8_t *>(buf);
	while (n) {
		uint64_t block = offset / 2048;
		size_t at = size_t(offset % 2048), k = std::min(n, 2048 - at);
		if (!under_->read(block * 2352 + data_at_ + at, out, k))
			return false;
		out += k;
		offset += k;
		n -= k;
	}
	return true;
}

namespace {

constexpr uint64_t CARD_HEADER = 1024;

struct Device {
	uint8_t code;
	unsigned mb, page, pages_per_block;
};

// docs/formats.md, "SmartMedia cards"
constexpr Device devices[] = {
	{ 0xEA, 2, 256, 16 }, { 0x64, 2, 256, 16 },
	{ 0xE3, 4, 512, 16 }, { 0xE5, 4, 512, 16 }, { 0x6B, 4, 512, 16 },
	{ 0xE6, 8, 512, 16 },
	{ 0x73, 16, 512, 32 },
	{ 0x75, 32, 512, 32 },
	{ 0x76, 64, 512, 32 },
	{ 0x79, 128, 512, 32 },
};

uint64_t card_bytes(const Device &d)
{
	return CARD_HEADER + uint64_t(d.mb) * 1048576 / d.page * (d.page + d.page / 32);
}

}

bool Complement::read(uint64_t offset, void *buf, size_t n) const
{
	if (!under_->read(offset, buf, n))
		return false;
	auto *p = static_cast<uint8_t *>(buf);
	for (size_t i = 0; i < n; i++)
		p[i] = uint8_t(~p[i]);
	return true;
}

bool Complement::write(uint64_t offset, const void *buf, size_t n) const
{
	auto *p = static_cast<const uint8_t *>(buf);
	uint8_t flipped[4096];
	while (n) {
		size_t k = std::min(n, sizeof flipped);
		for (size_t i = 0; i < k; i++)
			flipped[i] = uint8_t(~p[i]);
		if (!under_->write(offset, flipped, k))
			return false;
		p += k;
		offset += k;
		n -= k;
	}
	return true;
}

// the device of a card's image, from its size and the header's two codes; null where it is none
static const Device *device_of(const Store &s, uint8_t flip)
{
	uint8_t head[2];
	if (s.size() < CARD_HEADER || !s.read(0, head, 2))
		return nullptr;
	head[0] ^= flip, head[1] ^= flip;
	if (head[0] == 0x00 || head[0] == 0xFF)
		return nullptr;
	for (const Device &v : devices)
		if (v.code == head[1] && card_bytes(v) == s.size())
			return &v;
	return nullptr;
}

bool SmartMedia::is_card(const Store &s)
{
	return device_of(s, 0) != nullptr;
}

// the header's rest is zeros in a card's image, so ones in its complement
bool SmartMedia::is_complemented(const Store &s)
{
	uint8_t head[CARD_HEADER];
	return device_of(s, 0xFF) && s.read(0, head, sizeof head)
		&& std::all_of(head + 3, head + sizeof head, [](uint8_t v) { return v == 0xFF; });
}

std::shared_ptr<const Store> SmartMedia::over(std::shared_ptr<const Store> under)
{
	const Device *d = device_of(*under, 0);
	if (!d)
		return nullptr;
	std::shared_ptr<SmartMedia> sm(new SmartMedia);
	sm->page_ = d->page;
	sm->total_ = d->page + d->page / 32;
	sm->sectors_ = d->pages_per_block * d->page / 512;
	uint32_t physical = uint32_t((under->size() - CARD_HEADER) / sm->total_ / d->pages_per_block);
	uint32_t blocks = physical >= 1024 ? physical / 1024 * 1000 : physical * 1000 / 1024;
	if (!blocks)
		return nullptr;
	sm->map_.assign(blocks, UINT32_MAX);
	// a block's logical number is in its first page's spare area, within its zone of 1024 blocks (1000 logical);
	// the first block to claim a number has it
	for (uint32_t b = 0; b < physical; b++) {
		uint8_t spare[8];
		uint64_t row = uint64_t(b) * d->pages_per_block;
		if (!under->read(CARD_HEADER + row * sm->total_ + d->page, spare, 8))
			break;
		unsigned field = unsigned(spare[6]) << 8 | spare[7];
		if ((field & 0xF800) != 0x1000)
			continue;
		uint32_t logical = b / 1024 * 1000 + ((field >> 1) & 0x3FF);
		if (logical < blocks && sm->map_[logical] == UINT32_MAX)
			sm->map_[logical] = b;
	}
	sm->under_ = std::move(under);
	return sm;
}

bool SmartMedia::read(uint64_t offset, void *buf, size_t n) const
{
	if (offset > size() || n > size() - offset)
		return false;
	auto *out = static_cast<uint8_t *>(buf);
	const uint32_t pages = 512 / page_;
	while (n) {
		uint64_t sector = offset / 512;
		uint32_t block = uint32_t(sector / sectors_), in = uint32_t(sector % sectors_);
		size_t from = size_t(offset % 512), k = std::min(n, 512 - from);
		uint8_t data[512];
		memset(data, 0xFF, sizeof data);
		if (map_[block] != UINT32_MAX)
			for (uint32_t p = 0; p < pages; p++) {
				uint64_t row = (uint64_t(map_[block]) * sectors_ + in) * pages + p;
				if (!under_->read(CARD_HEADER + row * total_, data + p * page_, page_))
					return false;
			}
		memcpy(out, data + from, k);
		out += k;
		offset += k;
		n -= k;
	}
	return true;
}

bool SmartMedia::blank(const syfs_io &io, std::string &why)
{
	// the device code a size is made with: the first of its size
	const Device *d = nullptr;
	for (const Device &v : devices)
		if (!d && card_bytes(v) == io.size)
			d = &v;
	if (!d) {
		why = "the image is not the size of a 2 to 128 MB card's";
		return false;
	}
	std::vector<uint8_t> buf(65536, 0xFF);
	std::fill(buf.begin(), buf.begin() + CARD_HEADER, uint8_t(0));
	buf[0] = 0xEC;
	buf[1] = d->code;
	buf[2] = 0xA5;
	for (uint64_t at = 0; at < io.size;) {
		size_t k = size_t(std::min<uint64_t>(buf.size(), io.size - at));
		if (io.write(io.user, at, buf.data(), k) != k) {
			why = "the image could not be written";
			return false;
		}
		at += k;
		std::fill(buf.begin(), buf.begin() + CARD_HEADER, uint8_t(0xFF));
	}
	return true;
}

Stores stores_of(const syfs_io &io)
{
	Stores s;
	std::shared_ptr<const Store> host = std::make_shared<const HostStore>(io);
	if (auto vhd = Vhd::over(host)) {
		host = vhd;
		s.vhd = true;
		if (SmartMedia::is_complemented(*host))
			host = std::make_shared<const Complement>(host);
	}
	s.top = s.plain = host;
	if (auto raw = RawSectors::over(host)) {
		s.top = raw;
		s.raw = true;
	} else if (auto card = SmartMedia::over(host)) {
		s.top = card;
		s.card = true;
	}
	return s;
}

}
