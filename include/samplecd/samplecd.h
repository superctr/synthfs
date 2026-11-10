/* Copyright 2026 Ian Karlsson
 * samplecd: sampler CD-ROM images read: the format told, the sounds listed, which machines can load them. */
#ifndef SAMPLECD_H
#define SAMPLECD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The image's bytes, read through the host: n bytes at offset into buf, the count read returned (short at the
 * end or on an error).  The io is copied; user must stay valid until samplecd_close. */
typedef struct samplecd_io {
	void *user;
	size_t (*read)(void *user, uint64_t offset, void *buf, size_t n);
	uint64_t size;
} samplecd_io;

typedef enum samplecd_format {
	SAMPLECD_UNKNOWN = 0,
	SAMPLECD_S7XX,     /* Roland S-700 series volumes (S-760, S-770, S-750, SP-700) */
	SAMPLECD_S550,     /* Roland S-550 / W-30 SCSI format */
	SAMPLECD_AKAI,     /* AKAI S1000 / S3000 partitions */
	SAMPLECD_ISO9660,  /* a plain CD-ROM; its WAV and AIFF files listed */
	SAMPLECD_EMU3,     /* E-mu EIII / E-IV */
	SAMPLECD_EMAX2,    /* E-mu Emax II */
} samplecd_format;

/* The machines that load an entry: bits, so a disc's are the union of its entries' */
enum {
	SAMPLECD_S760 = 1u << 0,    /* S-760 (AKAI S1000 volumes and S-550 discs by Convert Load) */
	SAMPLECD_S770 = 1u << 1,    /* S-770, S-750, SP-700 (S-550 discs by Convert Load) */
	SAMPLECD_XV5080 = 1u << 2,  /* XV-5080 */
};

typedef enum samplecd_kind {
	SAMPLECD_PARTITION,    /* AKAI: a partition, named A to Z */
	SAMPLECD_VOLUME,       /* S-7xx and AKAI */
	SAMPLECD_PERFORMANCE,  /* S-7xx */
	SAMPLECD_PATCH,        /* S-7xx */
	SAMPLECD_PARTIAL,      /* S-7xx */
	SAMPLECD_SAMPLE,       /* S-7xx and AKAI */
	SAMPLECD_PROGRAM,      /* AKAI */
	SAMPLECD_OTHER,        /* AKAI: a file of another type (effects, multi, drum, cue list) */
	SAMPLECD_DIRECTORY,    /* ISO 9660: a directory holding audio files */
	SAMPLECD_FILE,         /* ISO 9660: a WAV or AIFF file */
} samplecd_kind;

#define SAMPLECD_NONE ((size_t)-1)

typedef struct samplecd_entry {
	samplecd_kind kind;
	uint32_t number;     /* its place in its list on the disc, from 0 (the slot the machine numbers from 1) */
	size_t parent;       /* the entry that holds it (an AKAI volume's partition, a file's volume or directory), or SAMPLECD_NONE */
	char name[64];       /* UTF-8, trailing blanks dropped; an ISO 9660 file's without its version */
	char category[4];    /* an S-7xx name's category code (STR, PAD...), else empty */
	uint64_t bytes;      /* its size on the disc: a sample's wave, a file's length; 0 where the list does not say */
	uint32_t machines;   /* SAMPLECD_S760 | ... : the machines that load it */
} samplecd_entry;

typedef struct samplecd_sample {
	uint32_t rate;       /* Hz */
	int32_t key;         /* the original key, MIDI number; -1 unknown */
	uint64_t frames;     /* the wave's length in sample frames, at most */
} samplecd_sample;

typedef struct samplecd samplecd;

/* The format alone, from the first sectors (cheap: for a scan of many images) */
samplecd_format samplecd_probe(const samplecd_io *io);

/* The image opened and its lists read; NULL with why in err where it cannot be read.  A format with no lists
 * (S-550, E-mu) opens with none. */
samplecd *samplecd_open(const samplecd_io *io, char *err, size_t err_size);
samplecd *samplecd_open_file(const char *path, char *err, size_t err_size);
void samplecd_close(samplecd *cd);

samplecd_format samplecd_format_of(const samplecd *cd);
const char *samplecd_format_name(samplecd_format format);
/* the disc's own label where it has one (an S-7xx disc's system line, an ISO 9660 volume's ID), else "" */
const char *samplecd_label(const samplecd *cd);
/* the machines that load anything on it */
uint32_t samplecd_machines(const samplecd *cd);

size_t samplecd_count(const samplecd *cd);
const samplecd_entry *samplecd_entry_at(const samplecd *cd, size_t i);

/* The entries entry i uses, by index, read from its record: an S-7xx volume's performances, a performance's
 * patches, a patch's partials, a partial's samples, each once in the order first used.  The count found is
 * returned; at most max are written to out.  0 for kinds that use nothing or are not read yet. */
size_t samplecd_refs(samplecd *cd, size_t i, size_t *out, size_t max);

/* A sample's rate, key and length, read from its header; 0 where entry i is not a sample or cannot be read */
int samplecd_sample_info(samplecd *cd, size_t i, samplecd_sample *out);

#ifdef __cplusplus
}
#endif

#endif
