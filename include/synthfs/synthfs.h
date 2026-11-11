// Copyright 2026 Ian Karlsson

//! \file synthfs.h
//! synthfs: the images synthesizers and samplers keep their sounds in, read as nodes with properties.
/*!
 * CD-ROMs, hard disks, floppies, wave expansion boards and cards: the format told, what the image holds listed, what
 * each node uses, a sample's wave decoded.
 */
#ifndef SYNTHFS_H
#define SYNTHFS_H

#include <stddef.h>
#include <stdint.h>

#include "syfs_keys.h"

#ifdef __cplusplus
extern "C" {
#endif

//! A name of the interface's: a format, a medium, a node's type, a property, a relation, a conversion.
/*!
 * The ones the library knows are syfs_keys.h's constants; SYFS_NONE is none or unknown.
 */
typedef uint32_t syfs_key;

//! The name's key, whether the library knows the name or not.
syfs_key syfs_key_of(const char *name);

//! A known key's name ("akai-s1000").
/*!
 * \return "none" for SYFS_NONE, NULL for a key the library does not know.
 */
const char *syfs_name(syfs_key key);

//! A known key's title for a reader ("AKAI S1000").
/*!
 * \return "none" for SYFS_NONE, NULL for a key the library does not know.
 */
const char *syfs_title(syfs_key key);

//! The formats the library tells, i from 0.
/*!
 * \return SYFS_NONE past the last.
 */
syfs_key syfs_format_at(size_t i);

//! The image's bytes, through the host.
/*!
 * read and write move n bytes at offset into or from buf and return the count done (short at the end or on an
 * error).  The io is copied; user must stay valid until syfs_close.
 *
 * An image that several hosts have open at once, one or more of them writing (two machines on one disk), is given
 * length and hold: a dynamic VHD grows as it is written, so each asks the image's length where its own count may
 * be old, and holds the image while it adds to it.  hold returns 0 where the image cannot be taken; a host holds
 * with a lock on the file that every one of them honours.  Left NULL, the image is taken to be this io's alone.
 */
typedef struct syfs_io {
	void *user;                                                               //!< Passed to read and write.
	size_t (*read)(void *user, uint64_t offset, void *buf, size_t n);         //!< Reads n bytes at offset.
	size_t (*write)(void *user, uint64_t offset, const void *buf, size_t n);  //!< Writes them; NULL where only read.
	uint64_t size;                                                            //!< The image's bytes.
	uint64_t (*length)(void *user);     //!< The image's bytes now, where others may write it too; else NULL.
	int (*hold)(void *user, int on);    //!< Takes (1) or lets go (0) the image for this io alone; else NULL.
} syfs_io;

//! An opened image.
typedef struct syfs_image syfs_image;

//! Opens an image through the host's io.
/*!
 * The format and the medium are told from the first sectors, which is cheap (a scan of many images asks for no
 * more); what the image holds is read when a node, or a disk's label, is first asked for.  A VHD's sectors (fixed
 * or dynamic; SYFS_PROP_CONTAINER tells), a raw CD-ROM image's sectors and a SmartMedia card's pages are read
 * through (the medium SYFS_MEDIUM_SMARTMEDIA).  An image of no format the library tells opens with format SYFS_NONE
 * and no nodes.
 *
 * \return NULL with why in err where it cannot be read.
 */
syfs_image *syfs_open(const syfs_io *io, char *err, size_t err_size);

//! Opens an image file.
/*!
 * \return NULL with why in err where it cannot be read.
 */
syfs_image *syfs_open_path(const char *path, char *err, size_t err_size);

//! Closes an image.
void syfs_close(syfs_image *im);

//! Why the last call on the image failed.
/*!
 * \return "" where it did not.
 */
const char *syfs_error(const syfs_image *im);

//! The image out of its container, as an io of its own: for a host that reads and writes the medium itself.
/*!
 * A VHD's sectors (a card's own image, of one kept in a VHD), else the image's bytes as they lie.  A write to a
 * dynamic VHD adds the blocks it needs to the file, through the io the image was opened with; out->write is NULL
 * where that had none.  A write of whole bytes inside the image is done whole or fails: nothing is written short.
 * The io stays good until syfs_close.  What was read of the image's lists is not read anew after a write.  Where the
 * image was opened with length and hold, what another host writes to it is read here, and a dynamic VHD may be
 * written by several at once.
 *
 * \return 0 where there is no image.
 */
int syfs_image_io(syfs_image *im, syfs_io *out);

//! Writes an empty volume of a format over a blank image of io->size bytes.
/*!
 * The volume is as the machine's own format leaves it; only its structures are written.  The formats made:
 * SYFS_FORMAT_FAT, SYFS_FORMAT_S760, SYFS_FORMAT_S770 (docs/formats.md, "Formatting").
 *
 * \param format SYFS_NONE with SYFS_MEDIUM_SMARTMEDIA for a blank card, not formatted: an image of a 2 to 128 MB
 *               card's size.
 * \param medium SYFS_MEDIUM_DISK or SYFS_MEDIUM_FLOPPY, or SYFS_NONE for a floppy where the size is one's (720 KB,
 *               1.44 MB) and a disk otherwise.
 * \param label  The volume's name, NULL or "" for none: a FAT volume's label (a DOS short name's 11 characters), an
 *               S-760's or S-770's hard disk's name (12 characters of ASCII; "Formatted" without one).  Their
 *               floppies hold none.
 * \return 1 done, or 0 with why in err: a format or medium that cannot be made, a size the layout cannot have, a
 *         label it cannot hold, a write that failed.
 */
int syfs_create(const syfs_io *io, syfs_key format, syfs_key medium, const char *label, char *err, size_t err_size);

//! Copies an image into another container: a raw image into a VHD, or a VHD's sectors out into a raw image.
/*!
 * What is copied is the image out of its container, byte for byte: nothing of a format is read.  to is written from
 * its start; its size is not looked at.  A SmartMedia card's image is kept complemented in a VHD, so that its erased
 * pages are left out, and comes back out as it went in.
 *
 * \param container SYFS_CONTAINER_VHD for a dynamic VHD (the 2 MB blocks holding only zeros left out; the image must
 *                  be whole 512-byte sectors), or SYFS_CONTAINER_RAW.
 * \return The bytes written, or 0 with why in err.
 */
uint64_t syfs_convert(const syfs_io *from, const syfs_io *to, syfs_key container, char *err, size_t err_size);

//! A node: what an image holds, by a number that stays its own while the image is open.
typedef uint32_t syfs_node;

//! The image itself.
#define SYFS_ROOT ((syfs_node)0)
//! No node.
#define SYFS_NO_NODE ((syfs_node)-1)

//! What every node has.
typedef struct syfs_info {
	syfs_node parent;    //!< The node that holds it; SYFS_ROOT at the top, SYFS_NO_NODE for the root.
	syfs_key type;       //!< SYFS_TYPE_.
	const char *name;    //!< UTF-8; the root's is the image's label.  Stays until the image is closed.
	uint32_t number;     //!< Its place in its list in the image, from 0 (the slot the machine numbers from 1).
	uint64_t size;       //!< Its bytes in the image; the root's the image's; 0 where unknown.
} syfs_info;

//! A node's parent, type, name, number and size.
/*!
 * \return 0 where there is no such node.
 */
int syfs_node_info(syfs_image *im, syfs_node n, syfs_info *out);

//! The types of the nodes the image holds, i from 0.
/*!
 * \return SYFS_NONE past the last.
 */
syfs_key syfs_type_at(syfs_image *im, size_t i);

//! The nodes in a relation to node n, in the image's order.
/*!
 * - SYFS_REL_CHILDREN: the nodes n holds.  One parent each, a tree.
 * - SYFS_REL_USES: the nodes n needs to sound, each once in the order first used, read from n's record: a volume's
 *   performances, a performance's patches, a patch's partials, a partial's samples; a program's samples and a
 *   multi's programs; a patch's tones; a board's wave's samples by key.  Many to many: a sample may be used by many.
 * - SYFS_REL_ALL: every node under n, the nodes they hold among them.
 *
 * \return The count found; at most max are written to out.
 */
size_t syfs_list(syfs_image *im, syfs_node n, syfs_key relation, syfs_node *out, size_t max);

//! The sample node n sounds at a MIDI key (0 to 127).
/*!
 * The answer is a node with a wave (SYFS_AS_S16) and a sample's properties.  A sample, tone or audio file is its
 * own; a patch's, partial's, program's or board's wave's is the one its first layer plays at the key.  Velocity,
 * the other layers and each one's tuning are left to the host.
 *
 * \return SYFS_NO_NODE where nothing sounds there or n plays no samples by key.
 */
syfs_node syfs_at_key(syfs_image *im, syfs_node n, int key);

//! A property: a value of a node's by its key (SYFS_PROP_).
/*!
 * The root's are the image's: SYFS_PROP_FORMAT, SYFS_PROP_MEDIUM, SYFS_PROP_CONTAINER, SYFS_PROP_LABEL, the volume's
 * name where it is apart from the label (SYFS_PROP_VOLUME, an S-760's or S-770's disk's), and what
 * tells a board or card apart (SYFS_PROP_MODEL, _TITLE, _NUMBER, _ID, _DATE), none of which reads the image's lists.
 * Every other node has SYFS_PROP_NAME, _TYPE, _NUMBER, _SIZE and _FORMAT (the format of the volume it is in: one
 * disk may mix two), _TAG where the image gives it a code of its own, and _EXPORTS where its contents can be read; a
 * node with a wave has SYFS_PROP_RATE down to _MODE (docs/properties.md).
 */
typedef struct syfs_prop {
	syfs_key key;        //!< SYFS_PROP_.
	syfs_key kind;       //!< SYFS_KIND_INT, SYFS_KIND_STRING or SYFS_KIND_KEY.
	int writable;        //!< Whether it can be set.
} syfs_prop;

//! Node n's properties, i from 0.
/*!
 * \return 0 past the last.
 */
int syfs_prop_at(syfs_image *im, syfs_node n, size_t i, syfs_prop *out);

//! Reads a property as an integer: an integer's value, or a key's.
/*!
 * \return 0 where the node has none of that key or it is a string.
 */
int syfs_get_int(syfs_image *im, syfs_node n, syfs_key key, int64_t *value);

//! Reads a property as a string, cut to fit buf: a string's value, or a key's name.
/*!
 * \return 0 where the node has none of that key or it is an integer.
 */
int syfs_get_str(syfs_image *im, syfs_node n, syfs_key key, char *buf, size_t size);

//! Writes an integer property.
/*!
 * \return 0 with why in syfs_error where it is not writable or the value cannot be held.
 */
int syfs_set_int(syfs_image *im, syfs_node n, syfs_key key, int64_t value);

//! Writes a string property: SYFS_PROP_NAME, which names the node anew, where syfs_prop says it is writable.
/*!
 * \return 0 with why in syfs_error where it is not writable or the value cannot be held.
 */
int syfs_set_str(syfs_image *im, syfs_node n, syfs_key key, const char *value);

//! Makes a node under parent, of the host's bytes.
/*!
 * The image must have been opened with a writer, and its format be one that is written: SYFS_FORMAT_FAT, where the
 * image is the volume's own bytes or a VHD (not a SmartMedia card's pages, nor raw CD-ROM sectors).  Nodes keep
 * their numbers through every change; the new one's is returned.
 *
 * \param type SYFS_TYPE_FILE, or SYFS_TYPE_DIRECTORY, which is made empty: from is then NULL.
 * \param as   SYFS_AS_RAW: the bytes are the file's own.
 * \param from The bytes, from->size of them, read through from->read.
 * \return SYFS_NO_NODE with why in syfs_error: a parent that holds no nodes, a name it has already or cannot
 *         hold, a volume that is full.
 */
syfs_node syfs_import(syfs_image *im, syfs_node parent, syfs_key type, syfs_key as, const char *name,
	const syfs_io *from);

//! Removes node n from the image, with what it holds.
/*!
 * \return 0 with why in syfs_error.
 */
int syfs_remove(syfs_image *im, syfs_node n);

//! Gives node n another parent: SYFS_ROOT, or a node that holds nodes of its type.
/*!
 * \return 0 with why in syfs_error: a name the parent has already, a node moved into itself.
 */
int syfs_move(syfs_image *im, syfs_node n, syfs_node parent);

//! Copies node n of another image under parent in this one, with what it holds.
/*!
 * A file is copied as its own bytes (SYFS_AS_RAW), a directory, volume or other holder of files as a directory of
 * the same name with what it holds: so from a CD-ROM, a FAT volume or an AKAI volume into a FAT volume.
 *
 * \return The new node, or SYFS_NO_NODE with why in to's syfs_error; what was copied before it failed stays.
 */
syfs_node syfs_transfer(syfs_image *to, syfs_node parent, syfs_image *from, syfs_node n);

//! Reads node n's contents in a conversion: up to size bytes from byte at.
/*!
 * SYFS_PROP_EXPORTS names the conversions a node has ("raw wav s16").  With buf NULL nothing is read and the bytes
 * there are from at are returned: the whole, with at 0.
 *
 * - SYFS_AS_RAW: the bytes of a file as its volume keeps it: a FAT's or CD-ROM's file, an AKAI program's, sample's
 *   or multi's.
 * - SYFS_AS_WAV: a wave as a WAV file of 16-bit values, the original key and the loop in a smpl chunk where the
 *   sample has them; where the wave is cut short in the image, zeros.
 * - SYFS_AS_S16: a wave decoded to 16-bit signed values in the host's byte order, the channels of a frame side by
 *   side, SYFS_PROP_FRAMES frames in all; at and size are whole frames'.  12-bit and 8-bit values come scaled to 16,
 *   wider ones cut to their top 16 bits, floating point rounded and clipped, a board's samples decoded from the tone
 *   generators' code.
 *
 * \return The count read: short at the end, where the image ends or a chain breaks; 0 where n has no such contents.
 */
size_t syfs_read(syfs_image *im, syfs_node n, syfs_key as, uint64_t at, void *buf, size_t size);

#ifdef __cplusplus
}
#endif

#endif
