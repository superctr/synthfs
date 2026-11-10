// Copyright 2026 Ian Karlsson
// The C interface: the format told, the lists read, the readers' details asked for on demand
#include "internal.hpp"

#include <cstdio>
#include <memory>
#include <new>

struct samplecd {
	scd::Disc disc;
	FILE *file = nullptr;
	explicit samplecd(const samplecd_io &io) : disc(io) {}
	~samplecd()
	{
		if (file)
			fclose(file);
	}
};

using namespace scd;

static samplecd_format format_of(const Image &im)
{
	uint8_t s0[16];
	if (!im.read(0, s0, sizeof s0))
		return SAMPLECD_UNKNOWN;
	if (s7::probe(im))
		return SAMPLECD_S7XX;
	if (!memcmp(s0, "* ROLAND S-550 *", 16))
		return SAMPLECD_S550;
	if (akai::probe(im))
		return SAMPLECD_AKAI;
	if (!memcmp(s0, "EMU3", 4))
		return SAMPLECD_EMU3;
	if (!memcmp(s0, "EMX2", 4))
		return SAMPLECD_EMAX2;
	if (iso9660::probe(im))
		return SAMPLECD_ISO9660;
	return SAMPLECD_UNKNOWN;
}

static void say(char *err, size_t size, const char *what)
{
	if (err && size)
		snprintf(err, size, "%s", what);
}

extern "C" {

samplecd_format samplecd_probe(const samplecd_io *io)
{
	try {
		return io ? format_of(Image(*io)) : SAMPLECD_UNKNOWN;
	} catch (...) {
		return SAMPLECD_UNKNOWN;
	}
}

samplecd *samplecd_open(const samplecd_io *io, char *err, size_t err_size)
{
	if (!io || !io->read)
		return say(err, err_size, "no reader given"), nullptr;
	try {
		auto cd = std::make_unique<::samplecd>(*io);
		Disc &d = cd->disc;
		uint8_t first[512];
		if (!d.image.read(0, first, sizeof first))
			return say(err, err_size, "the image is shorter than a sector"), nullptr;
		d.format = format_of(d.image);
		switch (d.format) {
		case SAMPLECD_S7XX: s7::list(d); break;
		case SAMPLECD_AKAI: akai::list(d); break;
		case SAMPLECD_ISO9660: iso9660::list(d); break;
		default: break;
		}
		return cd.release();
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

samplecd *samplecd_open_file(const char *path, char *err, size_t err_size)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return say(err, err_size, "the file cannot be opened"), nullptr;
	samplecd_io io { f, file_read, 0 };
#ifdef _WIN32
	_fseeki64(f, 0, SEEK_END);
	io.size = uint64_t(_ftelli64(f));
#else
	fseeko(f, 0, SEEK_END);
	io.size = uint64_t(ftello(f));
#endif
	samplecd *cd = samplecd_open(&io, err, err_size);
	if (!cd) {
		fclose(f);
		return nullptr;
	}
	cd->file = f;
	return cd;
}

void samplecd_close(samplecd *cd)
{
	delete cd;
}

samplecd_format samplecd_format_of(const samplecd *cd)
{
	return cd->disc.format;
}

const char *samplecd_format_name(samplecd_format format)
{
	switch (format) {
	case SAMPLECD_S7XX: return "S-7xx";
	case SAMPLECD_S550: return "S-550";
	case SAMPLECD_AKAI: return "AKAI";
	case SAMPLECD_ISO9660: return "ISO 9660";
	case SAMPLECD_EMU3: return "E-mu EIII/E-IV";
	case SAMPLECD_EMAX2: return "E-mu Emax II";
	default: return "unknown";
	}
}

const char *samplecd_label(const samplecd *cd)
{
	return cd->disc.label.c_str();
}

uint32_t samplecd_machines(const samplecd *cd)
{
	if (cd->disc.format == SAMPLECD_S550)
		return SAMPLECD_S760 | SAMPLECD_S770;
	uint32_t m = 0;
	for (const auto &e : cd->disc.entries)
		m |= e.machines;
	return m;
}

size_t samplecd_count(const samplecd *cd)
{
	return cd->disc.entries.size();
}

const samplecd_entry *samplecd_entry_at(const samplecd *cd, size_t i)
{
	return i < cd->disc.entries.size() ? &cd->disc.entries[i] : nullptr;
}

size_t samplecd_refs(samplecd *cd, size_t i, size_t *out, size_t max)
{
	if (i >= cd->disc.entries.size() || cd->disc.format != SAMPLECD_S7XX)
		return 0;
	try {
		auto r = s7::refs(cd->disc, i);
		for (size_t k = 0; k < r.size() && k < max; k++)
			out[k] = r[k];
		return r.size();
	} catch (...) {
		return 0;
	}
}

int samplecd_sample_info(samplecd *cd, size_t i, samplecd_sample *out)
{
	if (i >= cd->disc.entries.size() || !out)
		return 0;
	try {
		samplecd_sample s { 0, -1, 0 };
		bool ok = cd->disc.format == SAMPLECD_S7XX ? s7::sample(cd->disc, i, s)
			: cd->disc.format == SAMPLECD_AKAI ? akai::sample(cd->disc, i, s) : false;
		if (ok)
			*out = s;
		return ok;
	} catch (...) {
		return 0;
	}
}

}
