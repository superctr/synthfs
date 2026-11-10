/* Copyright 2026 Ian Karlsson
 * samplerfs: sampler disk images (CD-ROMs, hard disks) read: the format told, the sounds listed, what each uses. */
#ifndef SAMPLERFS_H
#define SAMPLERFS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The image's bytes, read through the host: n bytes at offset into buf, the count read returned (short at the
 * end or on an error).  The io is copied; user must stay valid until samplerfs_close. */
typedef struct samplerfs_io {
	void *user;
	size_t (*read)(void *user, uint64_t offset, void *buf, size_t n);
	uint64_t size;
} samplerfs_io;

typedef enum samplerfs_format {
	SAMPLERFS_UNKNOWN = 0,
	SAMPLERFS_S550,        /* Roland S-550 / W-30 SCSI format: areas of patches and tones */
	SAMPLERFS_S760,        /* Roland S-700 series volumes, the disk made on an S-760 */
	SAMPLERFS_S770,        /* Roland S-700 series volumes, the disk made on an S-770 or S-750 (SYS-772) */
	SAMPLERFS_AKAI_S1000,  /* AKAI partitions, S1000 volumes */
	SAMPLERFS_AKAI_S3000,  /* AKAI partitions, S3000 volumes (a disk with any is S3000) */
	SAMPLERFS_ISO9660,     /* a plain CD-ROM; its WAV and AIFF files listed */
	SAMPLERFS_EMU3,        /* E-mu EIII / E-IV */
	SAMPLERFS_EMAX2,       /* E-mu Emax II */
} samplerfs_format;

typedef enum samplerfs_kind {
	SAMPLERFS_PARTITION,    /* AKAI: a partition, named A to Z */
	SAMPLERFS_VOLUME,       /* S-760/S-770 and AKAI */
	SAMPLERFS_PERFORMANCE,  /* S-760/S-770 */
	SAMPLERFS_PATCH,        /* S-760/S-770 and S-550 */
	SAMPLERFS_PARTIAL,      /* S-760/S-770 */
	SAMPLERFS_SAMPLE,       /* S-760/S-770 and AKAI */
	SAMPLERFS_PROGRAM,      /* AKAI */
	SAMPLERFS_OTHER,        /* AKAI: a file of another type (drum inputs, effects, cue or take list); its letter in tag */
	SAMPLERFS_DIRECTORY,    /* ISO 9660: a directory holding audio files */
	SAMPLERFS_FILE,         /* ISO 9660: a WAV or AIFF file */
	SAMPLERFS_AREA,         /* S-550: an area, a disk's worth of patches and tones */
	SAMPLERFS_TONE,         /* S-550: a tone, a wave and its settings */
	SAMPLERFS_MULTI,        /* AKAI S3000: a multi, its parts' programs */
} samplerfs_kind;

#define SAMPLERFS_NONE ((size_t)-1)

typedef struct samplerfs_entry {
	samplerfs_kind kind;
	samplerfs_format format;  /* the format of the volume or area it is in (an AKAI disk mixes S1000 and S3000) */
	uint32_t number;         /* its place in its list on the disk, from 0 (the slot the machine numbers from 1) */
	size_t parent;           /* the entry that holds it (a partition's volume, a volume's file, an area's tone), or SAMPLERFS_NONE */
	char name[64];           /* UTF-8, trailing blanks dropped; an ISO 9660 file's without its version */
	char tag[8];             /* the disk's own code: an S-760/S-770 name's category (STR, PAD...), an AKAI file's type
	                            letter (p, s, x, d, q, t, m, c), an S-550 area's category number; else empty */
	uint64_t bytes;          /* its size on the disk: a sample's or tone's wave room, a file's length; 0 where unknown */
} samplerfs_entry;

typedef enum samplerfs_loop {
	SAMPLERFS_LOOP_NONE,       /* played once, start to end */
	SAMPLERFS_LOOP_FORWARD,    /* loop_start to loop_end over and over */
	SAMPLERFS_LOOP_ALTERNATE,  /* back and forth between them */
} samplerfs_loop;

/* A sample's wave and how it plays.  Points are frames from the first frame stored, the ends exclusive.  A
 * format's finer behaviour (an S-760/S-770 release loop, an AKAI sample's other seven loops) is left to mode. */
typedef struct samplerfs_sample {
	uint32_t rate;           /* Hz */
	int32_t key;             /* the original key, MIDI number; -1 unknown */
	uint64_t frames;         /* the frames stored (S-760/S-770, S-550: the segments', the end padded) */
	uint64_t start, end;     /* the frames played */
	uint64_t loop_start, loop_end;  /* equal where loop is NONE */
	samplerfs_loop loop;
	int reverse;             /* played from the end down to the start (and the loop, where one, run there) */
	int mode;                /* the format's own loop mode code */
} samplerfs_sample;

typedef struct samplerfs samplerfs;

/* The format alone, from the first sectors (cheap: for a scan of many images) */
samplerfs_format samplerfs_probe(const samplerfs_io *io);

/* The image opened and its lists read; NULL with why in err where it cannot be read.  A format with no lists
 * (E-mu) opens with none. */
samplerfs *samplerfs_open(const samplerfs_io *io, char *err, size_t err_size);
samplerfs *samplerfs_open_file(const char *path, char *err, size_t err_size);
void samplerfs_close(samplerfs *fs);

samplerfs_format samplerfs_format_of(const samplerfs *fs);
const char *samplerfs_format_name(samplerfs_format format);
/* the disk's own label where it has one (an S-760/S-770 disk's system line, an S-550 disk's title, an ISO 9660
 * volume's ID), else "" */
const char *samplerfs_label(const samplerfs *fs);

size_t samplerfs_count(const samplerfs *fs);
const samplerfs_entry *samplerfs_entry_at(const samplerfs *fs, size_t i);

/* The entries entry i uses, by index, read from its record, each once in the order first used: an S-760/S-770
 * volume's performances, a performance's patches, a patch's partials, a partial's samples; an AKAI program's
 * samples and a multi's programs (by name, in its volume); an S-550 patch's tones.  The count found is returned;
 * at most max are written to out.  0 for kinds that use nothing. */
size_t samplerfs_refs(samplerfs *fs, size_t i, size_t *out, size_t max);

/* A sample's or tone's rate, key, length and loop, read from its header; 0 where entry i has none or it cannot be
 * read */
int samplerfs_sample_info(samplerfs *fs, size_t i, samplerfs_sample *out);

#ifdef __cplusplus
}
#endif

#endif
