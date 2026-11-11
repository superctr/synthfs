// Copyright 2026 Ian Karlsson
// synthfs-wav: an image's samples written out as WAV files, 16-bit, with their key and loop in a smpl chunk
//   synthfs-wav -n IMAGE                 the samples it would write: number, name, frames, rate
//   synthfs-wav IMAGE DIR [NODE...]     every sample, or the nodes given by number, into DIR
#include <synthfs/synthfs.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

// a sample's header, of its properties
struct Sample {
	uint32_t rate, channels, bits;
	int key;
	uint64_t frames, loop_start, loop_end;
	syfs_key loop;
};

bool sample_of(syfs_image *im, syfs_node n, Sample &s)
{
	auto num = [&](syfs_key key) {
		int64_t v = 0;
		syfs_get_int(im, n, key, &v);
		return v;
	};
	int64_t rate;
	if (!syfs_get_int(im, n, SYFS_PROP_RATE, &rate))
		return false;
	s = { uint32_t(rate), uint32_t(num(SYFS_PROP_CHANNELS)), uint32_t(num(SYFS_PROP_BITS)), int(num(SYFS_PROP_KEY)),
		uint64_t(num(SYFS_PROP_FRAMES)), uint64_t(num(SYFS_PROP_LOOP_START)), uint64_t(num(SYFS_PROP_LOOP_END)),
		syfs_key(num(SYFS_PROP_LOOP)) };
	return true;
}

std::string file_name(size_t i, const char *name)
{
	std::string s = std::to_string(i);
	s.insert(0, s.size() < 5 ? 5 - s.size() : 0, '0');
	s += ' ';
	for (const char *p = name; *p; p++)
		s += *p == '/' || *p == '\\' || uint8_t(*p) < 32 ? '_' : *p;
	return s + ".wav";
}

// node n into path as the library makes a WAV of it; the frames written, or -1 where it could not be
long long write(syfs_image *im, syfs_node n, const Sample &s, const std::string &path)
{
	const size_t total = syfs_read(im, n, SYFS_AS_WAV, 0, nullptr, 0);
	FILE *f = total ? fopen(path.c_str(), "wb") : nullptr;
	if (!f)
		return -1;
	std::vector<uint8_t> buf(1 << 20);
	size_t done = 0;
	while (done < total) {
		size_t got = syfs_read(im, n, SYFS_AS_WAV, done, buf.data(), buf.size());
		if (!got || fwrite(buf.data(), 1, got, f) != got)
			break;
		done += got;
	}
	if (fclose(f) || done < total) {
		remove(path.c_str());
		return -1;
	}
	return (long long)s.frames;
}

}

int main(int argc, char **argv)
{
	bool dry = argc > 1 && !strcmp(argv[1], "-n");
	int a = dry ? 2 : 1;
	if (argc < a + (dry ? 1 : 2)) {
		fprintf(stderr, "usage: synthfs-wav -n IMAGE\n       synthfs-wav IMAGE DIR [NODE...]\n");
		return 2;
	}
	char err[256];
	syfs_image *im = syfs_open_path(argv[a], err, sizeof err);
	if (!im) {
		fprintf(stderr, "%s: %s\n", argv[a], err);
		return 1;
	}
	std::vector<syfs_node> which;
	for (int k = a + 2; k < argc; k++)
		which.push_back(syfs_node(strtoull(argv[k], nullptr, 10)));
	if (which.empty()) {
		which.resize(syfs_list(im, SYFS_ROOT, SYFS_REL_ALL, nullptr, 0));
		syfs_list(im, SYFS_ROOT, SYFS_REL_ALL, which.data(), which.size());
	}
	int status = 0;
	for (syfs_node i : which) {
		Sample s;
		syfs_info info;
		if (!syfs_node_info(im, i, &info) || !sample_of(im, i, s))
			continue;
		const syfs_info *e = &info;
		if (dry) {
			printf("%u  %-16s  %llu frames  %u Hz  %u×%u bits\n", i, e->name, (unsigned long long)s.frames, s.rate,
				s.channels, s.bits);
			continue;
		}
		std::string path = std::string(argv[a + 1]) + "/" + file_name(i, e->name);
		long long n = write(im, i, s, path);
		if (n < 0) {
			fprintf(stderr, "%s: not written (no wave read, or the file could not be)\n", path.c_str());
			status = 1;
		} else
			printf("%s  %lld of %llu frames\n", path.c_str(), n, (unsigned long long)s.frames);
	}
	syfs_close(im);
	return status;
}
