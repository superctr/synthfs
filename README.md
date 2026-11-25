# synthfs

A library for reading and writing the disk, card and ROM images of samplers and synthesizers from the late 80s to the
early 2000s.

Available as a C library (C++20 inside, no dependencies) and a command-line tool.

BSD-3.  No images are included: bring your own.

## Supported formats

Images synthfs can open and list:

- **Roland S-760, S-770** (and S-750): hard disks, MO, CD-ROMs and floppies, down to each performance's patches,
  partials and samples
- **Roland DJ-70** floppies
- **Roland S-550, S-330, W-30**: S-550 hard disks and floppies of all three, with patches and tones
- **Roland SR-JV80 and SRX** wave expansion boards and **SO-PCM1** cards: patches, rhythm sets and waves
- **Roland JV-80, JV-1080 and JD-990** DATA cards
- **Roland XV-5080, XV-3080 and Fantom** `.SVD` files
- **JV-880, JV-1080, JD-990, XV-5080, XV-3080 and Fantom** battery memory dumps
- **Roland system exclusive** files (`.syx`)
- **AKAI S900, S1000, S3000**: S1000 and S3000 disks and floppies, S900 floppies, with programs, samples and multis
- **E-mu EIII, E-IV, Emax II** discs (recognized only)
- **ISO 9660** CD-ROMs and **FAT12/16/32** disks and floppies
- **Audio CDs** as cue sheets
- **WAV** and **AIFF** files

Images can be plain (`.iso`, `.img`, `.cdr`), raw CD sectors (`.bin`), cue sheets (`.cue`), VHD (fixed or dynamic)
or SmartMedia card dumps.

## Features

- Every sample can be read as 16-bit audio, whatever the format stores, along with its rate, key and loop.
- Export:
	- samples as WAV files, with key and loop
	- files on FAT volumes, CD-ROMs and AKAI disks as their own bytes
	- S-760/S-770 and expansion board sounds, DATA cards and `.SVD` files as Roland SysEx (`.syx`)
- Writing:
	- FAT volumes: add, remove, rename and move files and directories
	- S-760/S-770 hard disks: copy sounds in from another S-700 image, bringing everything they use and skipping what
	  the disk already has
- Creating blank images:
	- FAT floppies (720 KB, 1.44 MB) and hard disks (FAT16, FAT32)
	- S-760/S-770 hard disks and floppies, formatted as the machines format them
	- SmartMedia cards
- Converting raw images to dynamic VHD and back.
- Images shared between programs: a VHD may be written by two processes at once.

## Building

```sh
just build          # the library, the synthfs tool and the tests
just test
```

Or with plain CMake:

```sh
cmake -S . -B build
cmake --build build
```

Needs CMake 3.23 or newer and a C++20 compiler.  Python 3 is needed only to regenerate the key and map tables.

## Command line

```sh
synthfs ls disc.iso                 # what the image holds
synthfs ls -l disc.iso              # every node in it
synthfs wav disc.iso out/           # every sample as a WAV file
synthfs shell disc.iso              # browse the image like a file tree
synthfs scan *.iso                  # one line per image
synthfs convert disk.img disk.vhd   # raw image to dynamic VHD
```

`synthfs help` lists every command.

For library usage, the full command line and the project layout, see [docs/advanced.md](docs/advanced.md).

## Documentation

- [docs/advanced.md](docs/advanced.md): using the library and the tool
- [docs/formats.md](docs/formats.md): the formats as read, and what is not read yet
- [docs/properties.md](docs/properties.md): node types and properties
- [docs/maps.md](docs/maps.md): the parameter address maps used for SysEx export

## Credits

Created by superctr 2026.

### Disclaimers

This project is not affiliated with Roland Corporation, AKAI or E-mu Systems.

AI disclosure: Anthropic's Claude models were used in this project.
