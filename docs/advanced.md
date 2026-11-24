# synthfs advanced usage

## Using the library

`include/synthfs/synthfs.h` is the whole interface; every function and type starts with `syfs_`.

The interface is small and stays small as formats are added:

- An image is a tree of **nodes**.  Node 0 (`SYFS_ROOT`) is the image itself.
- What a node is and has is told by **properties**, read by key.
- Every name (a format, a node type, a property, a relation) is a **key**: the hash of its string, with a constant in
  `syfs_keys.h`.  [properties.md](properties.md) lists them.

The image is read through the host's callback (`syfs_io`: n bytes at an offset), so a file, a buffer or a stream all
work; `syfs_open_path` opens a file.  Opening only identifies the format, which is cheap; the lists are read when
first asked for.

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
(`SYFS_REL_USES`, many to many: an S-760 performance uses patches, a patch uses partials, a partial uses samples).

### Containers

- Plain images (`.iso`, `.cdr`, `.img`) and raw sector ones (`.bin`) both read.
- A VHD (fixed or dynamic) is read through its sectors.
- A cue sheet is opened by path only.  Its audio tracks are nodes with waves; its data track is read as an image of
  its own.
- A SmartMedia card's image is read through its logical sectors.
- `syfs_convert` copies a raw image into a dynamic VHD, leaving out blocks of zeros (and a SmartMedia card's erased
  pages), or a VHD back into a raw image.
- A host that runs a machine from an image reads and writes it through `syfs_image_io`: the image out of its
  container, as an io of its own.  A dynamic VHD grows as its blocks are first written.
- A board or card made by Roland tells what it was sold as (`SYFS_PROP_MODEL`, `SYFS_PROP_TITLE`).

### Samples

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

`syfs_at_key` gives the sample a patch, partial, program or board's wave plays at a key, so a host can preview a
sound at middle C: that sample played at its rate × 2^((60 − key) / 12), where key is its original key.

### Exports

A node's `exports` property names what else it can be read as.  `syfs_read` with no buffer gives the size.

- `SYFS_AS_WAV`: the wave as a WAV file, with its key and loop in a `smpl` chunk.
- `SYFS_AS_RAW`: a file's own bytes (a FAT or CD-ROM file, an AKAI program or sample).
- `SYFS_AS_SYX`: Roland Data Sets, by the model's parameter address map ([maps.md](maps.md)).  Available for an
  S-760's or S-770's performances, patches, partials and samples; a board's or DATA card's JV-1080 patches and rhythm
  sets; an SRX board's or `.SVD` file's XV-5080 or XV-3080 ones.

A file of such messages (`.syx`) opens as an image whose nodes are what it holds whole.  A battery memory dump says
nothing about itself, so it is opened with `syfs_open_as` and its model.

### Writing

A FAT volume, in a raw image or a VHD, can be written:

- `syfs_import` makes a file from the host's bytes, or an empty directory;
- `syfs_remove` removes a node with everything under it;
- `syfs_move` gives a node another parent;
- setting `SYFS_PROP_NAME` renames it;
- `syfs_transfer` copies a node from another image: a file as its own bytes, a directory or volume with what it holds.

An S-760's or S-770's hard disk takes another S-700 image's entries the same way: `syfs_transfer` of a patch on a
CD-ROM brings its partials and their samples, leaving out what the disk already has.

Nodes keep their numbers through every change.

### Creating images

`syfs_create` writes an empty volume through the host's writer (`syfs_io`'s `write`), given a format and a medium:

- FAT: a 720 KB or 1.44 MB FAT12 floppy, or a hard disk with an MBR and one FAT16 or FAT32 partition, with an optional
  label;
- S-760 and S-770: a hard disk or MO as the machine formats it, or a formatted 2DD or 2HD floppy (what a sound volume
  is saved to);
- a blank SmartMedia card: no format, medium `SYFS_MEDIUM_SMARTMEDIA`.

```c
syfs_io w = { f, NULL, file_write, 1474560 };
if (!syfs_create(&w, SYFS_FORMAT_FAT, SYFS_MEDIUM_FLOPPY, "SAMPLES", err, sizeof err))
	fprintf(stderr, "%s\n", err);
```

What each one writes is in [formats.md](formats.md).

### In another project

```cmake
add_subdirectory(synthfs)
target_link_libraries(my_host PRIVATE synthfs)
```

The tool and the tests are built only where synthfs is the top-level project (`SYNTHFS_TOOLS`, `SYNTHFS_TESTS`).

## The command line

`synthfs COMMAND IMAGE [ARGS...]` runs one command on an image.  `synthfs shell [-w] IMAGE` reads commands from
stdin and lets you walk the image's nodes like a file tree (`cd`, `pwd`); `-w` opens it for writing.

Reading:

- `ls` lists a node and what it holds: `-l` every node under it, `-r` with what each uses and each sample's header.
- `info` shows a node's properties; `uses` what it uses.
- `export` writes nodes in a format they export: `-t syx -o all.syx /` gives the whole image's Data Sets.
- `wav DIR` writes every sample as a WAV file.

Changing a FAT volume (and `transfer` into an S-760/S-770 disk): `add`, `mkdir`, `rm`, `mv`, `transfer`, `set`.

Images: `scan IMAGE...` prints one line per image, `convert IN OUT` makes a VHD of a raw image or a raw image of a VHD,
`create` makes an empty volume.

A node is named by a path of names (`/SUB/B.TXT`), or `TYPE:NUMBER` where names are shared (`patch:3`).  A battery
memory dump is opened as `FORMAT:memory=FILE` (`jv1080:memory=backup.bin`).  `synthfs help` and
`synthfs COMMAND --help` give the rest.

## Other build targets

```sh
just keys           # syfs_keys.h regenerated after a change to src/keys.def (Python 3)
just maps           # src/maps.cpp regenerated from maps/*.json (Python 3)
just scan DIR       # every image under DIR, one line each
```

## Layout

`src/` holds the interface (`synthfs.cpp`, with the order formats are tried in), the names (`keys.def`), the image
reader and ISO 9660.  Each maker has a directory and a namespace (`src/roland`, `src/akai`, `src/emu`) declaring its
formats in one header, each a class with a static `probe` and `open`.

## Later

Writing the samplers' own volumes, and formatting a DJ-70's floppy and an S-760's Volume Dump FD disk.
