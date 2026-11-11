/* Copyright 2026 Ian Karlsson
 * synthfs: the images synthesizers and samplers keep their sounds in (CD-ROMs, hard disks, floppies, wave expansion
 * boards and cards) read as nodes with properties: the format told, what the image holds listed, what each node
 * uses, a sample's wave decoded. */
#ifndef SYNTHFS_H
#define SYNTHFS_H

#include <stddef.h>
#include <stdint.h>

#include "syfs_keys.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A name of the interface's: a format, a medium, a node's type, a property, a relation, a conversion.  The ones the
 * library knows are syfs_keys.h's constants; SYFS_NONE is none or unknown. */
typedef uint32_t syfs_key;

/* the name's key, whether the library knows the name or not */
syfs_key syfs_key_of(const char *name);
/* a known key's name ("akai-s1000") and its title for a reader ("AKAI S1000"); "none" for SYFS_NONE, NULL for a key
 * the library does not know */
const char *syfs_name(syfs_key key);
const char *syfs_title(syfs_key key);
/* the formats the library tells, i from 0; SYFS_NONE past the last */
syfs_key syfs_format_at(size_t i);

/* The image's bytes, through the host: n bytes at offset read into or written from buf, the count done returned
 * (short at the end or on an error).  write is NULL for an image only read.  The io is copied; user must stay valid
 * until syfs_close. */
typedef struct syfs_io {
	void *user;
	size_t (*read)(void *user, uint64_t offset, void *buf, size_t n);
	size_t (*write)(void *user, uint64_t offset, const void *buf, size_t n);
	uint64_t size;
} syfs_io;

typedef struct syfs_image syfs_image;

/* The image opened: its format and medium told from its first sectors, which is cheap (a scan of many images asks
 * for no more); what it holds is read when a node, or the label, is first asked for.  NULL with why in err where it
 * cannot be read.  An image of no format the library tells opens with format SYFS_NONE and no nodes. */
syfs_image *syfs_open(const syfs_io *io, char *err, size_t err_size);
syfs_image *syfs_open_path(const char *path, char *err, size_t err_size);
void syfs_close(syfs_image *im);
/* why the last call on the image failed; "" where it did not */
const char *syfs_error(const syfs_image *im);

/* An empty volume of a format written over a blank image of io->size bytes, as the machine's own format leaves it; 1
 * done, or 0 with why in err (a format or medium that cannot be made, a size the layout cannot have, a label it
 * cannot hold, a write that failed).  Only the volume's structures are written.  medium is SYFS_MEDIUM_DISK or
 * SYFS_MEDIUM_FLOPPY, or SYFS_NONE for a floppy where the size is one's (720 KB, 1.44 MB) and a disk otherwise.
 * label is NULL or "" for none; only SYFS_FORMAT_FAT holds one (a DOS short name's 11 characters).  The formats
 * made: SYFS_FORMAT_FAT, SYFS_FORMAT_S760, SYFS_FORMAT_S770 (docs/formats.md, "Formatting"). */
int syfs_create(const syfs_io *io, syfs_key format, syfs_key medium, const char *label, char *err, size_t err_size);

/* A node: what an image holds, by a number that stays its own while the image is open.  SYFS_ROOT is the image
 * itself. */
typedef uint32_t syfs_node;
#define SYFS_ROOT ((syfs_node)0)
#define SYFS_NO_NODE ((syfs_node)-1)

typedef struct syfs_info {
	syfs_node parent;    /* the node that holds it (a partition's volume, a volume's file, a bank's patch); SYFS_ROOT
	                        at the top, SYFS_NO_NODE for the root */
	syfs_key type;       /* SYFS_TYPE_ */
	const char *name;    /* UTF-8; the root's is the image's label.  Stays until the image is closed */
	uint32_t number;     /* its place in its list in the image, from 0 (the slot the machine numbers from 1) */
	uint64_t size;       /* its bytes in the image: a sample's wave room, a file's length; the root's the image's; 0
	                        where unknown */
} syfs_info;

/* 0 where there is no such node */
int syfs_node_info(syfs_image *im, syfs_node n, syfs_info *out);

/* the types of the nodes the image holds, i from 0; SYFS_NONE past the last */
syfs_key syfs_type_at(syfs_image *im, size_t i);

/* The nodes in a relation to node n, in the image's order.  The count found is returned; at most max are written
 * to out.
 *   SYFS_REL_CHILDREN  the nodes n holds: one parent each, a tree
 *   SYFS_REL_USES      the nodes n needs to sound, each once in the order first used, read from n's record: a
 *                      volume's performances, a performance's patches, a patch's partials, a partial's samples; a
 *                      program's samples and a multi's programs; a patch's tones; a board's wave's samples by key.
 *                      Many to many: a sample may be used by many
 *   SYFS_REL_ALL       every node under n, the nodes they hold among them */
size_t syfs_list(syfs_image *im, syfs_node n, syfs_key relation, syfs_node *out, size_t max);

/* The sample node n sounds at a MIDI key (0 to 127): a node with a wave (SYFS_AS_S16) and a sample's properties;
 * SYFS_NO_NODE where nothing sounds there or n plays no samples by key.  A sample, tone or audio file is its own; a
 * patch's, partial's, program's or board's wave's is the one its first layer plays at the key.  Velocity, the other
 * layers and each one's tuning are left to the host. */
syfs_node syfs_at_key(syfs_image *im, syfs_node n, int key);

/* A property: a value of a node's by its key (SYFS_PROP_), an integer, a string or a key.  The root's are the
 * image's: SYFS_PROP_FORMAT, SYFS_PROP_MEDIUM, SYFS_PROP_LABEL.  Every other node has SYFS_PROP_NAME, _TYPE, _NUMBER,
 * _SIZE and _FORMAT (the format of the volume it is in: one disk may mix two), and _TAG where the image gives it a
 * code of its own; a node with a wave has SYFS_PROP_RATE down to _MODE (docs/properties.md). */
typedef struct syfs_prop {
	syfs_key key;
	syfs_key kind;       /* SYFS_KIND_INT, SYFS_KIND_STRING or SYFS_KIND_KEY */
	int writable;
} syfs_prop;

/* node n's properties, i from 0; 0 past the last */
int syfs_prop_at(syfs_image *im, syfs_node n, size_t i, syfs_prop *out);
/* A property read; 0 where the node has none of that key or it is of another kind.  An integer's value or a key's
 * comes as an integer; a string's, or a key's name, as a string, cut to fit buf. */
int syfs_get_int(syfs_image *im, syfs_node n, syfs_key key, int64_t *value);
int syfs_get_str(syfs_image *im, syfs_node n, syfs_key key, char *buf, size_t size);
/* A property written; 0 with why in syfs_error where it is not writable or the value cannot be held */
int syfs_set_int(syfs_image *im, syfs_node n, syfs_key key, int64_t value);
int syfs_set_str(syfs_image *im, syfs_node n, syfs_key key, const char *value);

/* Node n's contents in a conversion: up to size bytes from byte at into buf.  The count read is returned: short at
 * the end, where the image ends or a chain breaks; 0 where n has no such contents.
 *   SYFS_AS_S16  a wave decoded to 16-bit signed values in the host's byte order, the channels of a frame side by
 *                side, SYFS_PROP_FRAMES frames in all; at and size are whole frames'.  12-bit and 8-bit values come
 *                scaled to 16, wider ones cut to their top 16 bits, floating point rounded and clipped, a board's
 *                samples decoded from the tone generators' code */
size_t syfs_read(syfs_image *im, syfs_node n, syfs_key as, uint64_t at, void *buf, size_t size);

#ifdef __cplusplus
}
#endif

#endif
