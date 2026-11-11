// Copyright 2026 Ian Karlsson
// samplerfs-wav: an image's samples written out as WAV files, 16-bit, with their key and loop in a smpl chunk
//   samplerfs-wav -n IMAGE                 the samples it would write: index, name, frames, rate
//   samplerfs-wav IMAGE DIR [INDEX...]     every sample, or the entries given by index, into DIR
#include <samplerfs/samplerfs.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

void put(std::vector<uint8_t> &b, uint32_t v, int n)
{
	for (int k = 0; k < n; k++)
		b.push_back(uint8_t(v >> 8 * k));
}

void tag(std::vector<uint8_t> &b, const char *t)
{
	b.insert(b.end(), t, t + 4);
}

// the header up to the data chunk's body, and the smpl chunk after it
std::vector<uint8_t> header(const samplerfs_sample &s, uint32_t data_bytes, std::vector<uint8_t> &smpl)
{
	smpl.clear();
	if (s.key >= 0 || s.loop != SAMPLERFS_LOOP_NONE) {
		bool loop = s.loop != SAMPLERFS_LOOP_NONE;
		tag(smpl, "smpl");
		put(smpl, 36 + (loop ? 24 : 0), 4);
		put(smpl, 0, 4);
		put(smpl, 0, 4);
		put(smpl, s.rate ? uint32_t(1000000000.0 / s.rate + 0.5) : 0, 4);
		put(smpl, s.key >= 0 ? uint32_t(s.key) : 60, 4);
		put(smpl, 0, 4);
		put(smpl, 0, 4);
		put(smpl, 0, 4);
		put(smpl, loop ? 1 : 0, 4);
		put(smpl, 0, 4);
		if (loop) {
			put(smpl, 0, 4);
			put(smpl, s.loop == SAMPLERFS_LOOP_ALTERNATE ? 1 : 0, 4);
			put(smpl, uint32_t(s.loop_start), 4);
			put(smpl, uint32_t(s.loop_end - 1), 4);
			put(smpl, 0, 4);
			put(smpl, 0, 4);
		}
	}
	std::vector<uint8_t> h;
	tag(h, "RIFF");
	put(h, 4 + 24 + 8 + data_bytes + (data_bytes & 1) + uint32_t(smpl.size()), 4);
	tag(h, "WAVE");
	tag(h, "fmt ");
	put(h, 16, 4);
	put(h, 1, 2);
	put(h, s.channels, 2);
	put(h, s.rate, 4);
	put(h, s.rate * s.channels * 2, 4);
	put(h, s.channels * 2, 2);
	put(h, 16, 2);
	tag(h, "data");
	put(h, data_bytes, 4);
	return h;
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

// sample i into path; the frames written, or -1 where it could not be
long long write(samplerfs *fs, size_t i, const samplerfs_sample &s, const std::string &path)
{
	uint64_t bytes = s.frames * s.channels * 2;
	if (bytes > 0xFFFFFF00u)
		return -1;
	FILE *f = fopen(path.c_str(), "wb");
	if (!f)
		return -1;
	std::vector<uint8_t> smpl;
	auto h = header(s, uint32_t(bytes), smpl);
	fwrite(h.data(), 1, h.size(), f);
	std::vector<int16_t> buf(65536 * size_t(s.channels));
	std::vector<uint8_t> le(buf.size() * 2);
	uint64_t done = 0;
	while (done < s.frames) {
		size_t got = samplerfs_sample_read(fs, i, done, buf.data(), 65536);
		if (!got)
			break;
		for (size_t k = 0; k < got * s.channels; k++) {
			le[2 * k] = uint8_t(buf[k]);
			le[2 * k + 1] = uint8_t(uint16_t(buf[k]) >> 8);
		}
		fwrite(le.data(), 1, got * s.channels * 2, f);
		done += got;
	}
	// a wave cut short (on another disk of a set, a broken chain) is written as far as it goes
	bytes = done * s.channels * 2;
	if (bytes & 1)
		fputc(0, f);
	fwrite(smpl.data(), 1, smpl.size(), f);
	if (done < s.frames) {
		h = header(s, uint32_t(bytes), smpl);
		fseek(f, 0, SEEK_SET);
		fwrite(h.data(), 1, h.size(), f);
	}
	bool ok = !ferror(f);
	if (fclose(f) || !ok || (!done && s.frames)) {
		remove(path.c_str());
		return -1;
	}
	return (long long)done;
}

}

int main(int argc, char **argv)
{
	bool dry = argc > 1 && !strcmp(argv[1], "-n");
	int a = dry ? 2 : 1;
	if (argc < a + (dry ? 1 : 2)) {
		fprintf(stderr, "usage: samplerfs-wav -n IMAGE\n       samplerfs-wav IMAGE DIR [INDEX...]\n");
		return 2;
	}
	char err[256];
	samplerfs *fs = samplerfs_open_file(argv[a], err, sizeof err);
	if (!fs) {
		fprintf(stderr, "%s: %s\n", argv[a], err);
		return 1;
	}
	std::vector<size_t> which;
	for (int k = a + 2; k < argc; k++)
		which.push_back(strtoull(argv[k], nullptr, 10));
	if (which.empty())
		for (size_t i = 0; i < samplerfs_count(fs); i++)
			which.push_back(i);
	int status = 0;
	for (size_t i : which) {
		samplerfs_sample s;
		const samplerfs_entry *e = samplerfs_entry_at(fs, i);
		if (!e || !samplerfs_sample_info(fs, i, &s))
			continue;
		if (dry) {
			printf("%zu  %-16s  %llu frames  %u Hz  %u×%u bits\n", i, e->name, (unsigned long long)s.frames, s.rate,
				s.channels, s.bits);
			continue;
		}
		std::string path = std::string(argv[a + 1]) + "/" + file_name(i, e->name);
		long long n = write(fs, i, s, path);
		if (n < 0) {
			fprintf(stderr, "%s: not written (no wave read, or the file could not be)\n", path.c_str());
			status = 1;
		} else
			printf("%s  %lld of %llu frames\n", path.c_str(), n, (unsigned long long)s.frames);
	}
	samplerfs_close(fs);
	return status;
}
