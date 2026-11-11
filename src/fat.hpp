// Copyright 2026 Ian Karlsson
//! \file fat.hpp
//! A FAT volume (FAT12, FAT16, FAT32), bare or behind an MBR: every directory and file.
#pragma once

#include "internal.hpp"

#include <map>
#include <optional>

namespace syfs {

namespace fat {

//! A volume's layout, from its boot sector's BPB.
struct Geometry {
	uint64_t base = 0;         //!< The boot sector's byte in the image.
	uint32_t sector = 0;       //!< Bytes a sector.
	uint32_t cluster = 0;      //!< Bytes a cluster.
	uint64_t total = 0;        //!< Sectors.
	uint64_t fat_at = 0;       //!< The first FAT, bytes from base.
	uint64_t fat_bytes = 0;    //!< One FAT's.
	uint64_t root_at = 0;      //!< FAT12 and FAT16: the root directory, bytes from base.
	uint32_t root_entries = 0;
	uint32_t root_cluster = 0; //!< FAT32.
	uint64_t data_at = 0;      //!< Cluster 2, bytes from base.
	uint32_t clusters = 0;
	int bits = 0;              //!< 12, 16 or 32.
	uint8_t label[11] {};      //!< The BPB's.
};

//! The volume whose boot sector is at base, where its BPB is a FAT's.
std::optional<Geometry> geometry(const uint8_t *bs, uint64_t base, uint64_t image_size);

//! The volume an image holds: bare, or the first FAT partition of its MBR.
std::optional<Geometry> find(const Image &im);

//! An empty volume written over a blank image: a DOS floppy, or a hard disk behind an MBR.
/*!
 * False with why where the image's size or the label cannot be had.
 */
bool format_floppy(const syfs_io &io, const char *label, std::string &why);
bool format_disk(const syfs_io &io, const char *label, std::string &why);

}

class Fat final : public Medium {
public:
	using Medium::Medium;
	static syfs_key probe(const Image &im);
	static std::unique_ptr<Medium> open(const syfs_io &io, syfs_key format);
	//! A floppy where the volume is bare and of a floppy's size, else a disk.
	static syfs_key medium_of(const Image &im);
	void list() override;
	bool sample(size_t i, syfs::Sample &out) const override;
	uint64_t raw_size(size_t i) const override { return entries[i].kind == SYFS_TYPE_FILE ? entries[i].bytes : 0; }
	bool raw(size_t i, uint64_t offset, void *buf, size_t n) const override;
	size_t wave(size_t i, const syfs::Sample &s, uint64_t first, int16_t *out, size_t frames) const override;
	//! Written where the image is: files and directories made, removed, named and moved (docs/formats.md, "FAT").
	bool writes() const override { return true; }
	size_t create(size_t parent, syfs_key type, std::string_view name, uint64_t bytes, const Source &from,
		std::string &why) override;
	bool remove(size_t i, std::string &why) override;
	bool rename(size_t i, std::string_view name, std::string &why) override;
	bool move(size_t i, size_t parent, std::string &why) override;

private:
	fat::Geometry g_;
	std::vector<uint32_t> first_;  //!< Each entry's first cluster, beside entries.
	//! Each entry's directory slots, bytes from the volume's start: its long name's, then its own.
	std::vector<std::vector<uint64_t>> slots_;
	uint32_t hint_ = 2;            //!< Where the search for a free cluster starts.
	bool fsinfo_stale_ = false;    //!< FAT32's free count has been marked unknown.
	mutable std::map<uint64_t, std::vector<uint8_t>> fat_cache_;
	std::vector<uint32_t> visited_;

	uint32_t next(uint32_t c) const;
	bool valid(uint32_t c) const { return c >= 2 && uint64_t(c) < uint64_t(g_.clusters) + 2; }
	//! A chain from first, at most max clusters, stopping at its end, a cluster out of the volume or one seen before.
	std::vector<uint32_t> chain(uint32_t first, uint64_t max) const;
	bool read_file(const std::vector<uint32_t> &clusters, uint64_t offset, void *buf, size_t n) const;
	//! A directory's entries; clusters is its chain, null for the fixed root of FAT12 and FAT16.
	void walk(const std::vector<uint8_t> &dir, const std::vector<uint32_t> *clusters, size_t parent, int depth);

	bool put(uint64_t at, const void *buf, size_t n) const { return image.write(g_.base + at, buf, n); }
	uint64_t cluster_at(uint32_t c) const { return g_.data_at + uint64_t(c - 2) * g_.cluster; }
	//! Sets a cluster's entry in every copy of the FAT.
	bool link(uint32_t c, uint32_t value);
	//! A chain of n free clusters, linked and ended; empty where the volume has not that many.
	std::vector<uint32_t> take(uint64_t n);
	bool release(uint32_t first);
	//! The slots of a directory in order, bytes from the volume's start; parent syfs::NONE is the root.
	std::vector<uint64_t> slots_of(size_t parent) const;
	//! A run of n free slots in a directory, its chain made longer where it is full; empty where it cannot be.
	std::vector<uint64_t> room(size_t parent, size_t n, std::string &why);
	//! Writes the slots of a name and its entry into a directory; the slots, empty with why where it cannot.
	std::vector<uint64_t> enter(size_t parent, std::string_view name, const uint8_t *entry, std::string &why);
	bool vacate(const std::vector<uint64_t> &slots);
	bool taken(size_t parent, std::string_view name, size_t but) const;
	bool under(size_t i, size_t maybe_ancestor) const;
};

}
