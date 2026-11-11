/* Copyright 2026 Ian Karlsson
 * samplerfs: sampler disk images (CD-ROMs, hard disks, floppies) and wave expansion boards and cards read: the format
 * told, the sounds listed, what each uses. */
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
	SAMPLERFS_AKAI_S1000,  /* AKAI partitions, S1000 volumes; an S1000 floppy */
	SAMPLERFS_AKAI_S3000,  /* AKAI partitions, S3000 volumes (a disk with any is S3000); an S3000 floppy */
	SAMPLERFS_ISO9660,     /* a plain CD-ROM; its WAV and AIFF files listed */
	SAMPLERFS_EMU3,        /* E-mu EIII / E-IV */
	SAMPLERFS_EMAX2,       /* E-mu Emax II */
	SAMPLERFS_S330,        /* Roland S-330 / S-300 floppy: one area, the S-550's layout */
	SAMPLERFS_W30,         /* Roland W-30 floppy: one area, the S-550's layout */
	SAMPLERFS_S50,         /* Roland S-50 floppy: told apart, not listed */
	SAMPLERFS_DJ70,        /* Roland S-700 series volume on a floppy made on a DJ-70 or DJ-70mkII */
	SAMPLERFS_SRJV80,      /* Roland SR-JV80 wave expansion board or SO-PCM1 card, as dumped */
	SAMPLERFS_SRX,         /* Roland SRX wave expansion board, as dumped */
	SAMPLERFS_AKAI_S900,   /* AKAI S900 / S950 floppy */
	SAMPLERFS_FAT,         /* a FAT12, FAT16 or FAT32 volume, bare or the first of an MBR's; every file listed */
} samplerfs_format;

/* What the image is of.  A hard disk, an MO and a CD-ROM share their layout and are all DISK; the host knows which
 * from where the image came. */
typedef enum samplerfs_medium {
	SAMPLERFS_MEDIUM_UNKNOWN = 0,
	SAMPLERFS_MEDIUM_DISK,     /* a hard disk's, MO's or CD-ROM's image */
	SAMPLERFS_MEDIUM_FLOPPY,   /* a floppy's sectors, end to end */
	SAMPLERFS_MEDIUM_CARD,     /* a PCM card's ROM */
	SAMPLERFS_MEDIUM_BOARD,    /* a wave expansion board's ROM */
} samplerfs_medium;

typedef enum samplerfs_kind {
	SAMPLERFS_PARTITION,    /* AKAI: a partition, named A to Z */
	SAMPLERFS_VOLUME,       /* S-760/S-770 and AKAI (an S1000/S3000 floppy is one) */
	SAMPLERFS_PERFORMANCE,  /* S-760/S-770 */
	SAMPLERFS_PATCH,        /* S-760/S-770 and S-550 */
	SAMPLERFS_PARTIAL,      /* S-760/S-770 */
	SAMPLERFS_SAMPLE,       /* S-760/S-770 and AKAI; a board's or card's wave's split, under the wave */
	SAMPLERFS_PROGRAM,      /* AKAI */
	SAMPLERFS_OTHER,        /* AKAI: a file of another type (drum inputs, effects, cue or take list, an operating
	                           system); its letter, or its type in hex, in tag */
	SAMPLERFS_DIRECTORY,    /* ISO 9660: a directory holding audio files; FAT: any directory */
	SAMPLERFS_FILE,         /* ISO 9660: a WAV or AIFF file; FAT: any file */
	SAMPLERFS_AREA,         /* S-550: an area, a disk's worth of patches and tones */
	SAMPLERFS_TONE,         /* S-550: a tone, a wave and its settings */
	SAMPLERFS_MULTI,        /* AKAI S3000: a multi, its parts' programs */
	SAMPLERFS_BANK,         /* a board or card: its patches or rhythm sets in one record shape, the shape in tag */
	SAMPLERFS_RHYTHM_SET,   /* a board or card: a rhythm set (named only in the later shapes) */
	SAMPLERFS_WAVE,         /* a board or card: a wave, the sound a patch's tone names; its samples, one a split of
	                           its keys, under it, and its sample header and frames those of the split it plays at
	                           key 60 */
} samplerfs_kind;

#define SAMPLERFS_NONE ((size_t)-1)

typedef struct samplerfs_entry {
	samplerfs_kind kind;
	samplerfs_format format;  /* the format of the volume or area it is in (an AKAI disk mixes S1000 and S3000) */
	uint32_t number;         /* its place in its list on the disk, from 0 (the slot the machine numbers from 1) */
	size_t parent;           /* the entry that holds it (a partition's volume, a volume's file, an area's tone, a bank's
	                            patch), or SAMPLERFS_NONE */
	char name[64];           /* UTF-8, trailing blanks dropped; an ISO 9660 file's without its version; a FAT file's
	                            long name where it has one */
	char tag[8];             /* the image's own code: an S-760/S-770 name's category (STR, PAD...), an AKAI file's type
	                            letter (p, s, x, d, q, t, m, c), an S-550 area's category number, a bank's record shape
	                            (an SR-JV80's type, 2 to 5; an SRX's class and type, "4:6"), a wave's split's keys
	                            ("60-64", the lowest and the highest it plays); else empty */
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
	uint32_t channels;       /* values a frame: 1 for the samplers' own formats, a WAV's or AIFF's own count */
	uint32_t bits;           /* each value's bits as stored (12 or 16 on the samplers; a WAV's or AIFF's own; 16 for a
	                            board's sample, which is coded); read as 16 whatever they are */
	int32_t key;             /* the original key, MIDI number; -1 unknown (a board's sample's fine tune is in the rate) */
	uint64_t frames;         /* the frames stored (S-760/S-770, S-550: the segments', the end padded) */
	uint64_t start, end;     /* the frames played */
	uint64_t loop_start, loop_end;  /* equal where loop is NONE */
	samplerfs_loop loop;
	int reverse;             /* played from the end down to the start (and the loop, where one, run there) */
	int mode;                /* the format's own loop mode code */
} samplerfs_sample;

typedef struct samplerfs samplerfs;

/* The format alone, from the first sectors (cheap: for a scan of many images), and what the image is of in *medium
 * where medium is not NULL */
samplerfs_format samplerfs_probe(const samplerfs_io *io, samplerfs_medium *medium);

/* The image opened and its lists read; NULL with why in err where it cannot be read.  A format with no lists
 * (E-mu) opens with none. */
samplerfs *samplerfs_open(const samplerfs_io *io, char *err, size_t err_size);
samplerfs *samplerfs_open_file(const char *path, char *err, size_t err_size);
void samplerfs_close(samplerfs *fs);

samplerfs_format samplerfs_format_of(const samplerfs *fs);
samplerfs_medium samplerfs_medium_of(const samplerfs *fs);
const char *samplerfs_format_name(samplerfs_format format);
const char *samplerfs_medium_name(samplerfs_medium medium);
/* the image's own label where it has one (an S-760/S-770 disk's system line, a floppy's kind line, an S-550 disk's
 * title, an ISO 9660 volume's ID, a FAT volume's label, a board's name), else "" */
const char *samplerfs_label(const samplerfs *fs);

size_t samplerfs_count(const samplerfs *fs);
const samplerfs_entry *samplerfs_entry_at(const samplerfs *fs, size_t i);

/* The entries entry i uses, by index, read from its record, each once in the order first used: an S-760/S-770
 * volume's performances, a performance's patches, a patch's partials, a partial's samples; an AKAI program's
 * samples and a multi's programs (by name, in its volume); an S-550 patch's tones; a board's wave's samples, by key.  The count found is returned;
 * at most max are written to out.  0 for kinds that use nothing. */
size_t samplerfs_refs(samplerfs *fs, size_t i, size_t *out, size_t max);

/* A sample's or tone's (or a WAV or AIFF file's, or a board's wave's) rate, key, length and loop, read from its
 * header; 0 where entry i has none or it cannot be read */
int samplerfs_sample_info(samplerfs *fs, size_t i, samplerfs_sample *out);

/* Entry i's wave decoded to 16-bit signed values in the host's byte order, the channels of a frame side by side: up to
 * frames frames from frame first (counted as samplerfs_sample's points are) into out, which holds frames × channels
 * values.  The count read is returned: short at the wave's end, where the image ends or a chain breaks, or where the
 * wave goes on onto another disk of a floppy set; 0 where entry i has no wave samplerfs can read (a WAV or AIFF file
 * in a compressed encoding).  12-bit values come scaled to 16 (the low four bits zero), 8-bit ones likewise, wider
 * ones cut to their top 16 bits, floating point rounded and clipped; a board's samples decoded from the tone
 * generators' code, the top 16 bits of their 18. */
size_t samplerfs_sample_read(samplerfs *fs, size_t i, uint64_t first, int16_t *out, size_t frames);

/* A blank image to format, written through the host: n bytes from buf at offset, the count written returned (short
 * on an error); size is the whole image's.  A struct of its own beside samplerfs_io, which keeps its shape. */
typedef struct samplerfs_write_io {
	void *user;
	size_t (*write)(void *user, uint64_t offset, const void *buf, size_t n);
	uint64_t size;
} samplerfs_write_io;

/* An empty volume written over a blank image of io->size bytes; 1 done, or 0 with why in err (a size the layout
 * cannot have, a label it cannot hold, a write that failed).  Only the volume's structures are written; the rest of
 * the image is left as it was.  label is NULL or "" for none, else up to 11 characters of a DOS short name (letters
 * upper-cased). */

/* A DOS floppy: FAT12 on 720 KB (2DD) or 1.44 MB (2HD), their standard geometry and media bytes */
int samplerfs_format_dos_floppy(const samplerfs_write_io *io, const char *label, char *err, size_t err_size);
/* A FAT hard disk: an MBR with one partition from sector 63 to the end, FAT16 below 512 MB and FAT32 from there
 * (docs/formats.md, "Formatting") */
int samplerfs_format_fat_disk(const samplerfs_write_io *io, const char *label, char *err, size_t err_size);

/* The S-700 series' media, as the machines' own format leaves them; they hold no label.  A hard disk (or MO) of
 * whole sectors, its segments counted from its size (up to about 579 MB; a larger disk's rest is unused): */
/* as an S-760 (system 2.24) formats it, with the S-760's reserved segments; more than 3.9 MB */
int samplerfs_format_s760_disk(const samplerfs_write_io *io, char *err, size_t err_size);
/* as an S-770 or S-750 (SYS-772 2.25) formats it; more than 2.9 MB */
int samplerfs_format_s770_disk(const samplerfs_write_io *io, char *err, size_t err_size);
/* A formatted floppy, what a sound volume is saved to: 720 KB (2DD) or 1.44 MB (2HD), every sector written, as an
 * S-760 or as an S-770 formats it */
int samplerfs_format_s760_floppy(const samplerfs_write_io *io, char *err, size_t err_size);
int samplerfs_format_s770_floppy(const samplerfs_write_io *io, char *err, size_t err_size);

#ifdef __cplusplus
}
#endif

#endif
