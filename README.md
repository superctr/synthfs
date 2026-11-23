# synthfs

The images synthesizers and samplers keep their sounds in, read: sampler disk images (CD-ROMs, hard disks, floppies)
and wave expansion ROMs.  The format and the medium are told (Roland S-550, S-330, W-30, S-760, S-770 and DJ-70, AKAI
S900, S1000 and S3000, E-mu, ISO 9660 and FAT disks and floppies; Roland SR-JV80 and SRX boards and SO-PCM1 cards, a JV's DATA card),
what the image holds is listed as nodes, each with what it uses (an S-760/S-770 volume's performances down to a
partial's samples, an AKAI program's samples and a multi's programs, an S-550 patch's tones, a board's banks of
patches and rhythm sets and its waves, each with its samples by key), and a sample's (or a WAV or AIFF file's) rate,
key, length and loop are its properties, its wave decoded to 16 bits.  C++20 inside, a C interface
(`include/synthfs/synthfs.h`, everything `syfs_`), no dependencies.

The interface is small and meant to stay so as formats are added.  An image is a tree of nodes, node 0 the image
itself; what a node is and has is told by properties, read by key; and every name (a format, a node's type, a
property, a relation) is a key, the hash of its string, with a constant in `syfs_keys.h`.
[docs/properties.md](docs/properties.md) lists them.

The image is read through the host's callback (`syfs_io`: n bytes at an offset), so a file, a buffer or a stream all
work; `syfs_open_path` opens a file.  Plain images (`.iso`, `.cdr`, `.img`) and raw sector ones (`.bin`) both read,
as does an image in a VHD (fixed or dynamic) and a compact disc as a cue sheet with its files (by path only: its
audio tracks are nodes with waves, its data track's volume read as from an image of its own), and `syfs_convert` copies a raw image into a dynamic VHD, which leaves
out the blocks that hold only zeros (and, for a SmartMedia card, the pages still erased), or a VHD's sectors back
out.  A host that runs a machine from an image reads and writes it through `syfs_image_io`, the image out of its
container as an io of its own, which adds a dynamic VHD's blocks as they are first written; a
SmartMedia card's image is read through its logical sectors; a board or card is read as dumped, and one of Roland's
tells what it was sold as (`SYFS_PROP_MODEL`, `SYFS_PROP_TITLE`).  Opening tells the format only, which is cheap; the lists are read when first asked
for.  The formats, and what is not read yet, are in [docs/formats.md](docs/formats.md).

```c
syfs_image *im = syfs_open_path("disc.iso", err, sizeof err);
int64_t format;
syfs_get_int(im, SYFS_ROOT, SYFS_PROP_FORMAT, &format);       /* SYFS_FORMAT_AKAI_S1000 */
size_t count = syfs_list(im, SYFS_ROOT, SYFS_REL_ALL, NULL, 0);
syfs_node *nodes = malloc(count * sizeof *nodes);
syfs_list(im, SYFS_ROOT, SYFS_REL_ALL, nodes, count);
for (size_t i = 0; i < count; i++) {
	syfs_info info;
	if (syfs_node_info(im, nodes[i], &info) && info.type == SYFS_TYPE_SAMPLE)
		printf("%s\n", info.name);
}
syfs_close(im);
```

`syfs_list` gives a node's children (`SYFS_REL_CHILDREN`), every node under it (`SYFS_REL_ALL`) or the nodes it uses
(`SYFS_REL_USES`), which is many to many.

A sample's wave is read in pieces from any frame: `syfs_read` with `SYFS_AS_S16` gives 16-bit values, a frame's
channels side by side, whatever the format stores (12-bit, 16-bit, a WAV's or AIFF's 8 to 32-bit integers or floating
point).

```c
int64_t frames, channels;
int16_t buf[4096];
if (syfs_get_int(im, n, SYFS_PROP_FRAMES, &frames) && syfs_get_int(im, n, SYFS_PROP_CHANNELS, &channels))
	for (uint64_t at = 0; at < (uint64_t)frames * channels * 2;) {
		size_t got = syfs_read(im, n, SYFS_AS_S16, at, buf, sizeof buf - sizeof buf % (channels * 2));
		if (!got)
			break;
		play(buf, got / (channels * 2), channels);
		at += got;
	}
```

A node's `exports` property names what else it reads as: `SYFS_AS_WAV`, the same wave as a WAV file with its key and
loop in a `smpl` chunk, and `SYFS_AS_RAW`, a file's own bytes (a FAT's or CD-ROM's file, an AKAI program or sample), and `SYFS_AS_SYX`,
an S-760's or S-770's performance, patch, partial or sample, or a board's or DATA card's JV-1080 patch or rhythm set,
as Roland Data Sets by the model's parameter address map
([docs/maps.md](docs/maps.md)); a file of such messages (`.syx`) opens as an image whose nodes are what it holds whole.
`syfs_read` with no buffer gives the size.

A FAT volume, in a raw image or a VHD, is written: `syfs_import` makes a file of the host's bytes or an empty
directory, `syfs_remove` removes a node with what it holds, `syfs_move` gives it another parent, setting
`SYFS_PROP_NAME` names it anew, and `syfs_transfer` copies a node of another image in, a file as its own bytes and
a directory or volume with what it holds.  Nodes keep their numbers through every change.  An S-760's or S-770's
hard disk takes another S-700 disk's entries the same way: `syfs_transfer` of a patch on a CD-ROM brings its
partials and their samples' waves, leaving out what the disk already has.

What a node sounds at a key, `syfs_at_key`, is the sample (a node with a wave) a patch, partial, program or board's
wave plays there, so a host can preview a sound at middle C: that sample played at its rate × 2^((60 − key) / 12),
key its original key.

A blank image is made an empty volume through the host's writer (`syfs_io`'s `write`) by `syfs_create`, with the
format and the medium: FAT (a 720 KB or 1.44 MB FAT12 floppy; a disk with an MBR and one FAT16 or FAT32 partition),
with an optional label, and the S-760's and the S-770's (a hard disk or MO as the machine formats it, or its
formatted 2DD or 2HD floppy, what a sound volume is saved to).  A blank SmartMedia card is made with no format and the
medium `SYFS_MEDIUM_SMARTMEDIA`.  What each writes is in
[docs/formats.md](docs/formats.md).

```c
syfs_io w = { f, NULL, file_write, 1474560 };
if (!syfs_create(&w, SYFS_FORMAT_FAT, SYFS_MEDIUM_FLOPPY, "SAMPLES", err, sizeof err))
	fprintf(stderr, "%s\n", err);
```

## Building

```sh
just build          # the library, the tools (synthfs-ls, -wav, -convert, -edit) and the tests (CMake 3.23+, C++20)
just test
just keys           # syfs_keys.h made again after a change to src/keys.def (Python 3)
just scan DIR       # every image under DIR, one line each
```

In another project: `add_subdirectory(synthfs)` and link `synthfs`; the tools and the tests are built only
where synthfs is the top level (`SYNTHFS_TOOLS`, `SYNTHFS_TESTS`).

`synthfs-ls IMAGE...` prints each image's format, counts and label; `-l` lists every node under what holds
it, `-r` adds what each uses and each sample's header.  `synthfs-wav IMAGE DIR [NODE...]` writes every sample (or
the nodes given by number) into DIR as 16-bit WAV files, named by number and name, the original key and loop in a
`smpl` chunk; `synthfs-wav -n IMAGE` lists what it would write.  `synthfs-convert IN OUT.vhd` makes a dynamic VHD of a raw image and
`synthfs-convert IN.vhd OUT` a raw image of a VHD, never over a file already there.  `synthfs-edit IMAGE` changes a FAT
volume: `add FILE [DIR]`, `mkdir PATH`, `rm PATH`, `mv PATH NEW`, `from OTHER PATH [DIR]` (a node of another image
copied in) and `get PATH FILE [wav]` (a node written out).

## Layout

`src/` holds the interface (`synthfs.cpp`, with the order formats are tried in), the names (`keys.def`), the image
reader and ISO 9660; each maker has a directory and a namespace (`src/roland`, `src/akai`, `src/emu`) declaring its formats in one header, each
a class with a static `probe` and `open`.

## Later

Writing the samplers' own volumes, and formatting a DJ-70's floppy and an S-760's
Volume Dump FD disk.
