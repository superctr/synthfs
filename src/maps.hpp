// Copyright 2026 Ian Karlsson
//! \file maps.hpp
//! The parameter address maps (maps/<model>.json) as tables: tools/maps.py makes src/maps.cpp of them.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

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
};

//! A block at an address.
struct Entry {
	uint32_t addr;    //!< The address's bytes as base-128 digits.
	uint16_t block;   //!< Which of the map's blocks.
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

	const Block *block(std::string_view id) const;
	//! The nth place a block is at, else null.
	const Entry *at(const Block *b, uint32_t n = 0) const;
	//! How many places a block is at.
	uint32_t places(const Block *b) const;
	//! The entry an address is in, else null.
	const Entry *entry_of(uint32_t addr) const;
};

//! A map by its file's name, else null.
const Map *find(std::string_view id);
//! Every map, `count` of them.
const Map *all(size_t &count);

}
