// Copyright 2026 Ian Karlsson
//! \file maps.hpp
//! The parameter address maps (maps/<model>.json) as tables: tools/maps.py makes src/maps.cpp of them.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace syfs::map {

enum : uint8_t {
	SIGNED = 1,       //!< Two's complement in its bits.
	LOW_FIRST = 2,    //!< Its bytes low first; else high first.
	FIELD = 4,        //!< Some bits of its value only: `bit` and `width`.
	RECEIVE_ONLY = 8, //!< Taken by the machine and never sent.
};

//! One parameter of a block.
struct Param {
	uint32_t offset;  //!< Its first unit's place in the block.
	uint8_t size;     //!< Units, the high one first.
	uint8_t bits;     //!< Bits a unit: 4 where values go as nibbles, 7.
	uint8_t flags;
	uint8_t bit;      //!< A field's lowest bit.
	uint8_t width;    //!< A field's bits.
	uint8_t frac;     //!< The low bits that are a fraction.
	int64_t min, max;
};

//! A block of parameters: one record's worth, wherever it is addressed.
struct Block {
	const char *id;   //!< The map's own name for it ("patch").
	uint32_t size;    //!< Units.
	const Param *params;
	uint32_t count;
	uint32_t name_at; //!< Where its name's first character is.
	uint8_t name_size; //!< Characters of the name, 0 where it has none.
	uint8_t name_units; //!< Units a character.
	uint32_t packed;  //!< Its bytes in a packed record, 0 where it is in none.
};

//! A block at an address.
struct Entry {
	uint32_t addr;    //!< The address's bytes as base-128 digits.
	uint16_t block;   //!< Which of the map's blocks.
	uint16_t area;    //!< Which of the map's areas it is of.
};

//! One thing the map holds at a place: a block, or the blocks of a group (a patch's common and tones).
struct Area {
	const char *group; //!< The map's name for the block or group of blocks it is ("1-3").
	const char *bank;  //!< What the places like it are called ("User Patch").
	uint32_t number;   //!< Its place among its bank's, from 0.
	uint32_t addr;
	uint32_t first;    //!< Its first entry.
	uint32_t count;    //!< Its entries.
};

//! A model's map.
struct Map {
	const char *id;   //!< The file's name ("s760").
	const char *model;
	uint8_t model_id[4];
	uint8_t model_id_size;
	uint8_t addr_size; //!< Bytes an address.
	const Block *blocks;
	uint32_t block_count;
	const Entry *entries;
	uint32_t entry_count;
	const Area *areas;
	uint32_t area_count;

	const Block *block(std::string_view id) const;
	//! The nth place a block is at, else null.
	const Entry *at(const Block *b, uint32_t n = 0) const;
	//! How many places a block is at.
	uint32_t places(const Block *b) const;
	//! The entry an address is in, else null.
	const Entry *entry_of(uint32_t addr) const;
	//! A bank's nth area, else null.
	const Area *area(std::string_view bank, uint32_t n) const;
	//! How many areas a bank has.
	uint32_t areas_of(std::string_view bank) const;
	//! A group's area of a number, whatever its bank; else null.
	const Area *area_in(std::string_view group, uint32_t n) const;
};

//! A map by its file's name, else null.
const Map *find(std::string_view id);
//! Every map, `count` of them.
const Map *all(size_t &count);

//! An area's packed record as its blocks' units, a vector for each entry of the area.
/*!
 * A packed record is each block's parameters in the order of their offsets, each in its bits from the record's
 * lowest bit up, a parameter of several units its last unit first; each block starts on a byte and has
 * Block::packed of them.  False where a block of the area is in no packed record or the record is short.
 */
bool unpack(const Map &m, const Area &a, const uint8_t *rec, size_t size, std::vector<std::vector<uint8_t>> &units);
//! The units put back into a record of the area's bytes; what the map has no parameter for is zero.
bool pack(const Map &m, const Area &a, const std::vector<std::vector<uint8_t>> &units, std::vector<uint8_t> &rec);

}
