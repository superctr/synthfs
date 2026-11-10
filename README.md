# samplerfs

Sampler disk images (CD-ROMs, hard disks, floppies) and wave expansion ROMs read: the format and the medium told
(Roland S-550, S-330, W-30, S-760, S-770 and DJ-70, AKAI S900, S1000 and S3000, E-mu, ISO 9660 and FAT disks and
floppies; Roland SR-JV80 and SRX boards and SO-PCM1 cards), the sounds listed, what each uses (an S-760/S-770
volume's performances down to a partial's samples, an AKAI program's samples and a multi's programs, an S-550
patch's tones), a sample's (or a WAV or AIFF file's) rate, key, length and loop, and a board's banks of patches and
rhythm sets and its waves by name.  C++20 inside, a C interface (`include/samplerfs/samplerfs.h`), no dependencies.

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

A blank image is made an empty volume through the host's writer (`samplerfs_write_io`: n bytes written at an
offset): `samplerfs_format_dos_floppy` (720 KB and 1.44 MB FAT12) and `samplerfs_format_fat_disk` (an MBR and one
FAT16 or FAT32 partition), each with an optional label; what each writes is in [docs/formats.md](docs/formats.md).

```c
samplerfs_write_io w = { f, file_write, 1474560 };
if (!samplerfs_format_dos_floppy(&w, "SAMPLES", err, sizeof err))
	fprintf(stderr, "%s\n", err);
```

## Building

```sh
just build          # the library, samplerfs-ls and the tests (CMake 3.23+, a C++20 compiler)
just test
just scan DIR       # every image under DIR, one line each
```

In another project: `add_subdirectory(samplerfs)` and link `samplerfs`; the tool and the tests are built only
where samplerfs is the top level (`SAMPLERFS_TOOLS`, `SAMPLERFS_TESTS`).

`samplerfs-ls IMAGE...` prints each image's format, counts and label; `-l` lists every entry under what holds
it, `-r` adds what each uses and each sample's header.

## Layout

`src/` holds the interface (`samplerfs.cpp`, with the order formats are tried in), the image reader and ISO 9660; each
maker has a directory and a namespace (`src/roland`, `src/akai`, `src/emu`) declaring its formats in one header, each
a class with a static `probe` and `open`.

## Later

Reading a sample's wave out (to WAV), writing sounds into an image, and formatting S-760 and S-770 disks and floppies
(what their blank volumes hold is not known well enough yet).
