# samplerfs

Sampler disk images (CD-ROMs, hard disks) read: the format told (Roland S-550, S-760, S-770, AKAI S1000 and S3000,
E-mu, ISO 9660), the sounds listed, what each uses (an S-760/S-770 volume's performances down to a partial's
samples, an AKAI program's samples and a multi's programs, an S-550 patch's tones) and a sample's rate, key, length
and loop.  C++20 inside, a C interface (`include/samplerfs/samplerfs.h`), no dependencies.

The image is read through the host's callback (`samplerfs_io`: n bytes at an offset), so a file, a buffer or a
stream all work; `samplerfs_open_file` opens a file.  Plain images (`.iso`, `.cdr`) and raw sector ones (`.bin`) both
read.  The formats, and what is not read yet, are in [docs/formats.md](docs/formats.md).

```c
samplerfs *fs = samplerfs_open_file("disc.iso", err, sizeof err);
for (size_t i = 0; i < samplerfs_count(fs); i++) {
	const samplerfs_entry *e = samplerfs_entry_at(fs, i);
	if (e->kind == SAMPLERFS_SAMPLE && e->format == SAMPLERFS_AKAI_S1000)
		printf("%s\n", e->name);
}
samplerfs_close(fs);
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

## Later

Reading a sample's wave out (to WAV), and writing sounds into an image.
