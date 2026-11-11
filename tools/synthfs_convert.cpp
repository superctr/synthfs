// Copyright 2026 Ian Karlsson
// synthfs-convert: an image copied into another container
//   synthfs-convert IN OUT.vhd     a raw image into a dynamic VHD
//   synthfs-convert IN.vhd OUT     a VHD's sectors out into a raw image
#include <synthfs/synthfs.h>

#include <cstdio>
#include <cstring>

static size_t file_read(void *user, uint64_t offset, void *buf, size_t n)
{
	FILE *f = static_cast<FILE *>(user);
	return fseeko(f, off_t(offset), SEEK_SET) ? 0 : fread(buf, 1, n, f);
}

static size_t file_write(void *user, uint64_t offset, const void *buf, size_t n)
{
	FILE *f = static_cast<FILE *>(user);
	return fseeko(f, off_t(offset), SEEK_SET) ? 0 : fwrite(buf, 1, n, f);
}

int main(int argc, char **argv)
{
	if (argc != 3) {
		fprintf(stderr, "usage: synthfs-convert IN OUT.vhd\n       synthfs-convert IN.vhd OUT\n");
		return 2;
	}
	const char *dot = strrchr(argv[2], '.');
	const bool vhd = dot && (!strcmp(dot, ".vhd") || !strcmp(dot, ".VHD"));
	FILE *in = fopen(argv[1], "rb");
	if (!in) {
		fprintf(stderr, "%s: cannot be opened\n", argv[1]);
		return 1;
	}
	// never over a file already there
	FILE *out = fopen(argv[2], "wbx");
	if (!out) {
		fprintf(stderr, "%s: cannot be made (a file of that name is kept)\n", argv[2]);
		fclose(in);
		return 1;
	}
	fseeko(in, 0, SEEK_END);
	syfs_io from { in, file_read, nullptr, uint64_t(ftello(in)) }, to { out, nullptr, file_write, 0 };
	char err[256] = "";
	const uint64_t n = syfs_convert(&from, &to, vhd ? SYFS_CONTAINER_VHD : SYFS_CONTAINER_RAW, err, sizeof err);
	fclose(in);
	const bool ok = !fclose(out) && n;
	if (!ok) {
		fprintf(stderr, "%s: %s\n", argv[1], err[0] ? err : "not written");
		remove(argv[2]);
		return 1;
	}
	printf("%s: %llu bytes from %llu\n", argv[2], (unsigned long long)n, (unsigned long long)from.size);
	return 0;
}
