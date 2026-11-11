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
	size_t wave(size_t i, const syfs::Sample &s, uint64_t first, int16_t *out, size_t frames) const override;

private:
	fat::Geometry g_;
	std::vector<uint32_t> first_;  //!< Each entry's first cluster, beside entries.
	mutable std::map<uint64_t, std::vector<uint8_t>> fat_cache_;
	std::vector<uint32_t> visited_;

	uint32_t next(uint32_t c) const;
	bool valid(uint32_t c) const { return c >= 2 && uint64_t(c) < uint64_t(g_.clusters) + 2; }
	//! A chain from first, at most max clusters, stopping at its end, a cluster out of the volume or one seen before.
	std::vector<uint32_t> chain(uint32_t first, uint64_t max) const;
	bool read_file(const std::vector<uint32_t> &clusters, uint64_t offset, void *buf, size_t n) const;
	void walk(const std::vector<uint8_t> &dir, size_t parent, int depth, bool root);
};

}
