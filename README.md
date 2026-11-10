# samplecd

Sampler CD-ROM images read: the format told (Roland S-550, S-760, S-770, AKAI S1000 and S3000, E-mu, ISO 9660),
the sounds listed, what each uses (an S-760/S-770 volume's performances down to a partial's samples, an AKAI
program's samples, an S-550 patch's tones) and a sample's rate, key and length.  C++20 inside, a C interface
(`include/samplecd/samplecd.h`), no dependencies.

The image is read through the host's callback (`samplecd_io`: n bytes at an offset), so a file, a buffer or a
stream all work; `samplecd_open_file` opens a file.  Plain images (`.iso`, `.cdr`) and raw sector ones (`.bin`) both
read.  The formats, and what is not read yet, are in [docs/formats.md](docs/formats.md).

```c
samplecd *cd = samplecd_open_file("disc.iso", err, sizeof err);
for (size_t i = 0; i < samplecd_count(cd); i++) {
	const samplecd_entry *e = samplecd_entry_at(cd, i);
	if (e->kind == SAMPLECD_SAMPLE && e->format == SAMPLECD_AKAI_S1000)
		printf("%s\n", e->name);
}
samplecd_close(cd);
```

## Building

```sh
just build          # the library, samplecd-ls and the tests (CMake 3.23+, a C++20 compiler)
just test
just scan DIR       # every image under DIR, one line each
```

In another project: `add_subdirectory(samplecd)` and link `samplecd`; the tool and the tests are built only
where samplecd is the top level (`SAMPLECD_TOOLS`, `SAMPLECD_TESTS`).

`samplecd-ls IMAGE...` prints each image's format, counts and label; `-l` lists every entry under what holds
it, `-r` adds what each uses and each sample's header.

## Later

Reading a sample's wave out (to WAV), and writing sounds into an image.
