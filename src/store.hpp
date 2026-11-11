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
	//! Writes n bytes at offset: the image's bytes change, not the store, which is a view of them.
	/*!
	 * \return false where the store is not written (a view only read, a host with no writer), any byte is outside
	 *         it, or the host's write failed.
	 */
	virtual bool write(uint64_t, const void *, size_t) const { return false; }
	//! Takes or lets go the image for this store alone, where others may write it; true where it is taken.
	virtual bool hold(bool) const { return true; }
	//! The bytes it holds now, asked anew where others may have added to the image.
	virtual uint64_t length() const { return size(); }
};

//! The image as the host gives it.
class HostStore final : public Store {
public:
	explicit HostStore(const syfs_io &io) : io_(io), size_(io.size) {}
	uint64_t size() const override { return size_; }
	bool read(uint64_t offset, void *buf, size_t n) const override;
	//! Writes anywhere: a write past the end makes the image longer.
	bool write(uint64_t offset, const void *buf, size_t n) const override;
	bool hold(bool on) const override { return !io_.hold || io_.hold(io_.user, on); }
	uint64_t length() const override;

private:
	syfs_io io_;
	mutable uint64_t size_;
};

//! A virtual hard disk's sectors, out of a VHD file: fixed, or dynamic with the blocks never written left out.
/*!
 * docs/formats.md, "VHD".  A differencing disk, whose sectors are partly its parent's, is not read.
 */
class Vhd final : public Store {
public:
	//! The view over a VHD, or null where the store is none or cannot be read as one.
	static std::shared_ptr<const Store> over(std::shared_ptr<const Store> under);
	uint64_t size() const override { return size_; }
	bool read(uint64_t offset, void *buf, size_t n) const override;
	//! Writes sectors' bytes; a dynamic disk's block not yet in the file is added at its end when it gets a byte
	//! that is not zero.
	bool write(uint64_t offset, const void *buf, size_t n) const override;
	//! Makes a dynamic VHD of a store's bytes through the host's io, blocks holding only zeros left out.
	/*!
	 * block is a block's bytes, a power of two of sectors.
	 *
	 * 
eturn The bytes the VHD takes, or 0 with why where the store is not whole sectors, is larger than a VHD
	 *         holds or a read or write failed.
	 */
	static uint64_t make(const Store &from, const syfs_io &to, std::string &why, uint32_t block = 0x200000);

private:
	Vhd() = default;
	std::shared_ptr<const Store> under_;
	uint64_t size_ = 0;
	uint32_t block_ = 0;         //!< A dynamic disk's block in bytes; 0 for a fixed disk.
	uint32_t bitmap_ = 0;        //!< The bytes of the sector bitmap before a block's data.
	uint64_t table_ = 0;         //!< The block allocation table's byte in the file.
	uint8_t footer_[512] {};     //!< The footer, to follow the blocks as they are added.
	mutable uint64_t end_ = 0;   //!< Where the footer at the file's end starts: where the next block goes.
	mutable std::vector<uint32_t> bat_;  //!< Each block's first sector in the file, 0xFFFFFFFF none.
	//! A block's sector in the file, its entry read anew where it was none: another host may have added the block.
	uint32_t sector_of(size_t block) const;
};

//! A store's bytes, each complemented.
/*!
 * How a SmartMedia card's image is kept in a VHD: its erased pages, all ones, become the zeros a dynamic VHD leaves
 * out (docs/formats.md, "SmartMedia cards").
 */
class Complement final : public Store {
public:
	explicit Complement(std::shared_ptr<const Store> under) : under_(std::move(under)) {}
	uint64_t size() const override { return under_->size(); }
	bool read(uint64_t offset, void *buf, size_t n) const override;
	bool write(uint64_t offset, const void *buf, size_t n) const override;

private:
	std::shared_ptr<const Store> under_;
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
	//! Whether a store is a card's image: a card's size, and a header with the device code of that size.
	static bool is_card(const Store &s);
	//! Whether a store is a card's image complemented, as a VHD holds one.
	static bool is_complemented(const Store &s);

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
	//! The image out of its container: a VHD's sectors (a card's complemented back), else the host's bytes.
	std::shared_ptr<const Store> plain;
	bool vhd = false;                  //!< A VHD underneath.
	bool raw = false;                  //!< Raw CD-ROM sectors underneath.
	bool card = false;                 //!< A SmartMedia card's pages underneath.
};

//! The stores of the host's image: its own, a VHD's sectors where it is one, and the view their first bytes ask for.
Stores stores_of(const syfs_io &io);

}
