# samplerfs

Sampler disk images (CD-ROMs, hard disks, floppies) and wave expansion ROMs read: the format and the medium told
(Roland S-550, S-330, W-30, S-760, S-770 and DJ-70, AKAI S900, S1000 and S3000, E-mu, ISO 9660 and FAT disks and
floppies; Roland SR-JV80 and SRX boards and SO-PCM1 cards), the sounds listed, what each uses (an S-760/S-770
volume's performances down to a partial's samples, an AKAI program's samples and a multi's programs, an S-550
patch's tones), a sample's (or a WAV or AIFF file's) rate, key, length and loop and its wave decoded to 16 bits, and a
board's banks of patches and rhythm sets and its waves, each with its samples by key, decoded likewise.  C++20 inside,
a C interface (`include/samplerfs/samplerfs.h`), no dependencies.

The image is read through the host's callback (`samplerfs_io`: n bytes at an offset), so a file, a buffer or a
stream all work; `samplerfs_open_file` opens a file.  Plain images (`.iso`, `.cdr`, `.img`) and raw sector ones (`.bin`)
both read; a board or card is read as dumped.  The formats, and what is not read yet, are in [docs/formats.md](docs/formats.md).

```c
samplerfs *fs = samplerfs_open_file("disc.iso", err, sizeof err);
for (size_t i = 0; i < samplerfs_count(fs); i++) {
	const samplerfs_entry *e = samplerfs_entry_at(fs, i);
	if (e->kind == SAMPLERFS_SAMPLE && e->format == SAMPLERFS_AKAI_S1000)
		printf("%s\n", e->name);
}
samplerfs_close(fs);
```

A sample's wave is read in pieces from any frame: `samplerfs_sample_read` gives 16-bit values, a frame's channels side
by side, whatever the format stores (12-bit, 16-bit, a WAV's or AIFF's 8 to 32-bit integers or floating point).

```c
samplerfs_sample s;
int16_t buf[4096];
if (samplerfs_sample_info(fs, i, &s))
	for (uint64_t at = 0; at < s.frames;) {
		size_t n = samplerfs_sample_read(fs, i, at, buf, 4096 / s.channels);
		if (!n)
			break;
		play(buf, n, s.channels);
		at += n;
	}
```

A blank image is made an empty volume through the host's writer (`samplerfs_write_io`: n bytes written at an
offset): `samplerfs_format_dos_floppy` (720 KB and 1.44 MB FAT12) and `samplerfs_format_fat_disk` (an MBR and one
FAT16 or FAT32 partition), each with an optional label; `samplerfs_format_s760_disk` and `samplerfs_format_s770_disk`
(a hard disk or MO as an S-760, or an S-770 or S-750, formats it) and `samplerfs_format_s760_floppy` and
`samplerfs_format_s770_floppy` (their formatted 2DD or 2HD floppy, what a sound volume is saved to).  What each
writes is in [docs/formats.md](docs/formats.md).

```c
samplerfs_write_io w = { f, file_write, 1474560 };
if (!samplerfs_format_dos_floppy(&w, "SAMPLES", err, sizeof err))
	fprintf(stderr, "%s\n", err);
```

## Building

```sh
just build          # the library, samplerfs-ls, samplerfs-wav and the tests (CMake 3.23+, a C++20 compiler)
just test
just scan DIR       # every image under DIR, one line each
```

In another project: `add_subdirectory(samplerfs)` and link `samplerfs`; the tools and the tests are built only
where samplerfs is the top level (`SAMPLERFS_TOOLS`, `SAMPLERFS_TESTS`).

`samplerfs-ls IMAGE...` prints each image's format, counts and label; `-l` lists every entry under what holds
it, `-r` adds what each uses and each sample's header.  `samplerfs-wav IMAGE DIR [INDEX...]` writes every sample (or
the entries given by index) into DIR as 16-bit WAV files, named by index and name, the original key and loop in a
`smpl` chunk; `samplerfs-wav -n IMAGE` lists what it would write.

## Layout

`src/` holds the interface (`samplerfs.cpp`, with the order formats are tried in), the image reader and ISO 9660; each
maker has a directory and a namespace (`src/roland`, `src/akai`, `src/emu`) declaring its formats in one header, each
a class with a static `probe` and `open`.

## Later

Writing sounds into an image, and formatting a DJ-70's floppy and an S-760's
Volume Dump FD disk.
