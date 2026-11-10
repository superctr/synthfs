/* Copyright 2026 Ian Karlsson
 * samplecd: sampler CD-ROM images read: the format told, the sounds listed, what each uses. */
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
	SAMPLECD_S550,        /* Roland S-550 / W-30 SCSI format: areas of patches and tones */
	SAMPLECD_S760,        /* Roland S-700 series volumes, the disc made on an S-760 */
	SAMPLECD_S770,        /* Roland S-700 series volumes, the disc made on an S-770 or S-750 (SYS-772) */
	SAMPLECD_AKAI_S1000,  /* AKAI partitions, S1000 volumes */
	SAMPLECD_AKAI_S3000,  /* AKAI partitions, S3000 volumes (a disc with any is S3000) */
	SAMPLECD_ISO9660,     /* a plain CD-ROM; its WAV and AIFF files listed */
	SAMPLECD_EMU3,        /* E-mu EIII / E-IV */
	SAMPLECD_EMAX2,       /* E-mu Emax II */
} samplecd_format;

typedef enum samplecd_kind {
	SAMPLECD_PARTITION,    /* AKAI: a partition, named A to Z */
	SAMPLECD_VOLUME,       /* S-760/S-770 and AKAI */
	SAMPLECD_PERFORMANCE,  /* S-760/S-770 */
	SAMPLECD_PATCH,        /* S-760/S-770 and S-550 */
	SAMPLECD_PARTIAL,      /* S-760/S-770 */
	SAMPLECD_SAMPLE,       /* S-760/S-770 and AKAI */
	SAMPLECD_PROGRAM,      /* AKAI */
	SAMPLECD_OTHER,        /* AKAI: a file of another type; its type letter in tag */
	SAMPLECD_DIRECTORY,    /* ISO 9660: a directory holding audio files */
	SAMPLECD_FILE,         /* ISO 9660: a WAV or AIFF file */
	SAMPLECD_AREA,         /* S-550: an area, a disk's worth of patches and tones */
	SAMPLECD_TONE,         /* S-550: a tone, a wave and its settings */
} samplecd_kind;

#define SAMPLECD_NONE ((size_t)-1)

typedef struct samplecd_entry {
	samplecd_kind kind;
	samplecd_format format;  /* the format of the volume or area it is in (an AKAI disc mixes S1000 and S3000) */
	uint32_t number;         /* its place in its list on the disc, from 0 (the slot the machine numbers from 1) */
	size_t parent;           /* the entry that holds it (a partition's volume, a volume's file, an area's tone), or SAMPLECD_NONE */
	char name[64];           /* UTF-8, trailing blanks dropped; an ISO 9660 file's without its version */
	char tag[8];             /* the disc's own code: an S-760/S-770 name's category (STR, PAD...), an AKAI file's type
	                            letter (p, s, x, d, q, t, m, c), an S-550 area's category number; else empty */
	uint64_t bytes;          /* its size on the disc: a sample's or tone's wave room, a file's length; 0 where unknown */
} samplecd_entry;

typedef struct samplecd_sample {
	uint32_t rate;       /* Hz */
	int32_t key;         /* the original key, MIDI number; -1 unknown */
	uint64_t frames;     /* the wave's length in sample frames (S-760/S-770: at most) */
} samplecd_sample;

typedef struct samplecd samplecd;

/* The format alone, from the first sectors (cheap: for a scan of many images) */
samplecd_format samplecd_probe(const samplecd_io *io);

/* The image opened and its lists read; NULL with why in err where it cannot be read.  A format with no lists
 * (E-mu) opens with none. */
samplecd *samplecd_open(const samplecd_io *io, char *err, size_t err_size);
samplecd *samplecd_open_file(const char *path, char *err, size_t err_size);
void samplecd_close(samplecd *cd);

samplecd_format samplecd_format_of(const samplecd *cd);
const char *samplecd_format_name(samplecd_format format);
/* the disc's own label where it has one (an S-760/S-770 disc's system line, an S-550 disc's title, an ISO 9660
 * volume's ID), else "" */
const char *samplecd_label(const samplecd *cd);

size_t samplecd_count(const samplecd *cd);
const samplecd_entry *samplecd_entry_at(const samplecd *cd, size_t i);

/* The entries entry i uses, by index, read from its record, each once in the order first used: an S-760/S-770
 * volume's performances, a performance's patches, a patch's partials, a partial's samples; an AKAI program's
 * samples (by name, in its volume); an S-550 patch's tones.  The count found is returned; at most max are written
 * to out.  0 for kinds that use nothing. */
size_t samplecd_refs(samplecd *cd, size_t i, size_t *out, size_t max);

/* A sample's or tone's rate, key and length, read from its header; 0 where entry i has none or it cannot be read */
int samplecd_sample_info(samplecd *cd, size_t i, samplecd_sample *out);

#ifdef __cplusplus
}
#endif

#endif
