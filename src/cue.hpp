// Copyright 2026 Ian Karlsson
//! \file cue.hpp
//! A compact disc as a cue sheet tells it: its tracks out of the files the sheet names (docs/formats.md, "Cue
//! sheets").
#pragma once

#include "internal.hpp"

#include <functional>

namespace syfs {

struct CueDisc;

//! A cue sheet's disc: the tracks, after what the data track's volume holds where a format reads it.
class Cue final : public Medium {
public:
	//! Opens a data track's volume through the format that reads it; null where none does.
	using Volume = std::function<std::unique_ptr<Medium>(const syfs_io &)>;

	//! Whether a file's bytes are a cue sheet: text with a FILE and a TRACK line.
	static bool is_sheet(std::string_view text);
	//! The disc of a sheet whose files are in dir.
	/*!
	 * \return null with why where the sheet cannot be read or a file it names cannot be opened.
	 */
	static std::unique_ptr<Medium> open(std::string_view text, const std::string &dir, const Volume &volume,
		std::string &why);

	Cue(std::shared_ptr<const CueDisc> disc, const syfs_io &io, const Volume &volume);
	void identify() override;
	void list() override;
	std::vector<size_t> refs(size_t i) const override;
	bool sample(size_t i, syfs::Sample &s) const override;
	size_t wave(size_t i, const syfs::Sample &s, uint64_t first, int16_t *out, size_t frames) const override;
	uint64_t raw_size(size_t i) const override;
	bool raw(size_t i, uint64_t offset, void *buf, size_t n) const override;
	size_t sample_at(size_t i, int key) const override;
	syfs_key container() const override { return SYFS_CONTAINER_CUE; }
	bool number(size_t i, syfs_key key, int64_t &value) const override;

private:
	std::shared_ptr<const CueDisc> disc_;
	std::unique_ptr<Medium> volume_;  //!< The data track's volume, its entries the first of this disc's.
	size_t held_ = 0;                 //!< The entries that are the volume's.
};

}
