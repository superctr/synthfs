// Copyright 2026 Ian Karlsson

//! \file store.hpp
//! An image's bytes, level by level: the host's, and the views laid over them.
#pragma once

#include <synthfs/synthfs.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace syfs {

//! An image's bytes at one level.
class Store {
public:
	virtual ~Store() = default;
	//! The bytes it holds.
	virtual uint64_t size() const = 0;
	//! Reads n bytes at offset.
	/*!
	 * \return false where any is missing.
	 */
	virtual bool read(uint64_t offset, void *buf, size_t n) const = 0;
};

//! The image as the host gives it.
class HostStore final : public Store {
public:
	explicit HostStore(const syfs_io &io) : io_(io) {}
	uint64_t size() const override { return io_.size; }
	bool read(uint64_t offset, void *buf, size_t n) const override;

private:
	syfs_io io_;
};

//! A CD-ROM's 2048-byte user data, out of an image of raw 2352-byte sectors (a .bin: mode 1 or mode 2 form 1).
class RawSectors final : public Store {
public:
	//! The view over a store of raw sectors, or null where it holds none.
	static std::shared_ptr<const Store> over(std::shared_ptr<const Store> under);
	uint64_t size() const override { return size_; }
	bool read(uint64_t offset, void *buf, size_t n) const override;

private:
	RawSectors(std::shared_ptr<const Store> under, unsigned data_at);
	std::shared_ptr<const Store> under_;
	unsigned data_at_;
	uint64_t size_;
};

//! A SmartMedia card's logical sectors, out of an image of its pages and their spare areas.
/*!
 * The image is a 1024-byte header with the maker and device codes, then every page followed by its spare area
 * (docs/formats.md, "SmartMedia cards").  The logical blocks are found through the map the spare areas hold; a block
 * no physical block claims reads as erased.
 */
class SmartMedia final : public Store {
public:
	//! The view over a card's image, or null where it is none.
	static std::shared_ptr<const Store> over(std::shared_ptr<const Store> under);
	uint64_t size() const override { return uint64_t(map_.size()) * sectors_ * 512; }
	bool read(uint64_t offset, void *buf, size_t n) const override;
	//! Writes a blank card over an image of a card's size: the header, and every page erased.
	/*!
	 * \return false with why where the size is no card's or a write failed.
	 */
	static bool blank(const syfs_io &io, std::string &why);

private:
	SmartMedia() = default;
	std::shared_ptr<const Store> under_;
	unsigned page_ = 0;     //!< A page's data bytes.
	unsigned total_ = 0;    //!< A page with its spare area.
	unsigned sectors_ = 0;  //!< The 512-byte sectors a block.
	std::vector<uint32_t> map_;  //!< Each logical block's physical block, UINT32_MAX none.
};

//! An image's stores, the host's at the bottom.
struct Stores {
	std::shared_ptr<const Store> top;  //!< What a format reads.
	bool raw = false;                  //!< Raw CD-ROM sectors underneath.
	bool card = false;                 //!< A SmartMedia card's pages underneath.
};

//! The stores of the host's image: its own, and the view its first bytes ask for.
Stores stores_of(const syfs_io &io);

}
