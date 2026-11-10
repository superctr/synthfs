// Copyright 2026 Ian Karlsson
// The C interface: the format told, the lists read, the readers' details asked for on demand
#include "internal.hpp"

#include <cstdio>
#include <memory>
#include <new>

struct samplerfs {
	FILE *file = nullptr;
	std::unique_ptr<sfs::Medium> medium;
	~samplerfs()
	{
		medium.reset();
		if (file)
			fclose(file);
	}
};

using namespace sfs;

namespace {

struct Reader {
	samplerfs_format (*probe)(const Image &im);
	std::unique_ptr<Medium> (*open)(const samplerfs_io &io, samplerfs_format format);
	samplerfs_medium (*medium)(const Image &im);
};

samplerfs_medium disk(const Image &) { return SAMPLERFS_MEDIUM_DISK; }
samplerfs_medium floppy(const Image &) { return SAMPLERFS_MEDIUM_FLOPPY; }

// tried in this order (docs/formats.md, "Telling them apart")
constexpr Reader readers[] = {
	{ s700::probe, s700::open, disk },
	{ s700::probe_floppy, s700::open_floppy, floppy },
	{ s550::probe, s550::open, disk },
	{ s550::probe_floppy, s550::open_floppy, floppy },
	{ roland_wave::probe, roland_wave::open, roland_wave::medium },
	{ akai::probe, akai::open, disk },
	{ akai::probe_floppy, akai::open_floppy, floppy },
	{ akai::probe_s900, akai::open_s900, floppy },
	{ emu::probe, emu::open, disk },
	{ iso9660::probe, iso9660::open, disk },
};

const Reader *reader_of(const Image &im, samplerfs_format &format)
{
	for (const Reader &r : readers)
		if ((format = r.probe(im)) != SAMPLERFS_UNKNOWN)
			return &r;
	return nullptr;
}

}

static void say(char *err, size_t size, const char *what)
{
	if (err && size)
		snprintf(err, size, "%s", what);
}

extern "C" {

samplerfs_format samplerfs_probe(const samplerfs_io *io, samplerfs_medium *medium)
{
	if (medium)
		*medium = SAMPLERFS_MEDIUM_UNKNOWN;
	try {
		if (!io)
			return SAMPLERFS_UNKNOWN;
		Image im(*io);
		samplerfs_format format = SAMPLERFS_UNKNOWN;
		const Reader *r = reader_of(im, format);
		if (r && medium)
			*medium = r->medium(im);
		return r ? format : SAMPLERFS_UNKNOWN;
	} catch (...) {
		return SAMPLERFS_UNKNOWN;
	}
}

samplerfs *samplerfs_open(const samplerfs_io *io, char *err, size_t err_size)
{
	if (!io || !io->read)
		return say(err, err_size, "no reader given"), nullptr;
	try {
		Image im(*io);
		uint8_t first[512];
		if (!im.read(0, first, sizeof first))
			return say(err, err_size, "the image is shorter than a sector"), nullptr;
		samplerfs_format format = SAMPLERFS_UNKNOWN;
		const Reader *r = reader_of(im, format);
		auto fs = std::make_unique<::samplerfs>();
		fs->medium = r ? r->open(*io, format) : std::make_unique<Medium>(*io, SAMPLERFS_UNKNOWN);
		if (r)
			fs->medium->medium = r->medium(im);
		fs->medium->list();
		return fs.release();
	} catch (const std::bad_alloc &) {
		return say(err, err_size, "out of memory"), nullptr;
	} catch (...) {
		return say(err, err_size, "the image could not be read"), nullptr;
	}
}

static size_t file_read(void *user, uint64_t offset, void *buf, size_t n)
{
	FILE *f = static_cast<FILE *>(user);
#ifdef _WIN32
	if (_fseeki64(f, int64_t(offset), SEEK_SET))
#else
	if (fseeko(f, off_t(offset), SEEK_SET))
#endif
		return 0;
	return fread(buf, 1, n, f);
}

samplerfs *samplerfs_open_file(const char *path, char *err, size_t err_size)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return say(err, err_size, "the file cannot be opened"), nullptr;
	samplerfs_io io { f, file_read, 0 };
#ifdef _WIN32
	_fseeki64(f, 0, SEEK_END);
	io.size = uint64_t(_ftelli64(f));
#else
	fseeko(f, 0, SEEK_END);
	io.size = uint64_t(ftello(f));
#endif
	samplerfs *fs = samplerfs_open(&io, err, err_size);
	if (!fs) {
		fclose(f);
		return nullptr;
	}
	fs->file = f;
	return fs;
}

void samplerfs_close(samplerfs *fs)
{
	delete fs;
}

samplerfs_format samplerfs_format_of(const samplerfs *fs)
{
	return fs->medium->format;
}

samplerfs_medium samplerfs_medium_of(const samplerfs *fs)
{
	return fs->medium->medium;
}

const char *samplerfs_medium_name(samplerfs_medium medium)
{
	switch (medium) {
	case SAMPLERFS_MEDIUM_DISK: return "disk";
	case SAMPLERFS_MEDIUM_FLOPPY: return "floppy";
	case SAMPLERFS_MEDIUM_CARD: return "card";
	case SAMPLERFS_MEDIUM_BOARD: return "board";
	default: return "unknown";
	}
}

const char *samplerfs_format_name(samplerfs_format format)
{
	switch (format) {
	case SAMPLERFS_S550: return "S-550";
	case SAMPLERFS_S760: return "S-760";
	case SAMPLERFS_S770: return "S-770";
	case SAMPLERFS_AKAI_S1000: return "AKAI S1000";
	case SAMPLERFS_AKAI_S3000: return "AKAI S3000";
	case SAMPLERFS_ISO9660: return "ISO 9660";
	case SAMPLERFS_EMU3: return "E-mu EIII/E-IV";
	case SAMPLERFS_EMAX2: return "E-mu Emax II";
	case SAMPLERFS_S330: return "S-330";
	case SAMPLERFS_W30: return "W-30";
	case SAMPLERFS_S50: return "S-50";
	case SAMPLERFS_DJ70: return "DJ-70";
	case SAMPLERFS_SRJV80: return "SR-JV80";
	case SAMPLERFS_SRX: return "SRX";
	case SAMPLERFS_AKAI_S900: return "AKAI S900";
	default: return "unknown";
	}
}

const char *samplerfs_label(const samplerfs *fs)
{
	return fs->medium->label.c_str();
}

size_t samplerfs_count(const samplerfs *fs)
{
	return fs->medium->entries.size();
}

const samplerfs_entry *samplerfs_entry_at(const samplerfs *fs, size_t i)
{
	return i < fs->medium->entries.size() ? &fs->medium->entries[i] : nullptr;
}

size_t samplerfs_refs(samplerfs *fs, size_t i, size_t *out, size_t max)
{
	if (i >= fs->medium->entries.size())
		return 0;
	try {
		auto r = fs->medium->refs(i);
		for (size_t k = 0; k < r.size() && k < max; k++)
			out[k] = r[k];
		return r.size();
	} catch (...) {
		return 0;
	}
}

int samplerfs_sample_info(samplerfs *fs, size_t i, samplerfs_sample *out)
{
	if (i >= fs->medium->entries.size() || !out)
		return 0;
	try {
		samplerfs_sample s {};
		s.key = -1;
		bool ok = fs->medium->sample(i, s);
		if (ok) {
			clamp_points(s);
			*out = s;
		}
		return ok;
	} catch (...) {
		return 0;
	}
}

}
