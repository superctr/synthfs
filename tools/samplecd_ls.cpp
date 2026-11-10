// Copyright 2026 Ian Karlsson
// samplecd-ls: an image's format, the machines that load it and its lists
//   samplecd-ls IMAGE...          one line each
//   samplecd-ls -l IMAGE...       every entry, indented under what holds it
//   samplecd-ls -r IMAGE...       also what each S-7xx entry uses, and each sample's rate and key
#include <samplecd/samplecd.h>

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

static const char *kinds[] = { "partition", "volume", "performance", "patch", "partial", "sample", "program", "other",
	"directory", "file" };
static const char *plurals[] = { "partitions", "volumes", "performances", "patches", "partials", "samples", "programs", "others",
	"directories", "files" };

static std::string machines(uint32_t m)
{
	std::string s;
	if (m & SAMPLECD_S760)
		s += " S-760";
	if (m & SAMPLECD_S770)
		s += " S-770";
	if (m & SAMPLECD_XV5080)
		s += " XV-5080";
	return s.empty() ? " none" : s;
}

static const char *base(const char *path)
{
	const char *s = strrchr(path, '/');
	return s ? s + 1 : path;
}

int main(int argc, char **argv)
{
	bool list = false, refs = false;
	int status = 0;
	for (int a = 1; a < argc; a++) {
		if (!strcmp(argv[a], "-l")) {
			list = true;
			continue;
		}
		if (!strcmp(argv[a], "-r")) {
			list = refs = true;
			continue;
		}
		char err[256];
		samplecd *cd = samplecd_open_file(argv[a], err, sizeof err);
		if (!cd) {
			fprintf(stderr, "%s: %s\n", argv[a], err);
			status = 1;
			continue;
		}
		size_t n = samplecd_count(cd);
		std::map<int, size_t> counts;
		for (size_t i = 0; i < n; i++)
			counts[samplecd_entry_at(cd, i)->kind]++;
		printf("%-14s%s:", samplecd_format_name(samplecd_format_of(cd)), machines(samplecd_machines(cd)).c_str());
		for (auto [k, c] : counts)
			printf(" %zu %s", c, c > 1 ? plurals[k] : kinds[k]);
		printf("  %s%s%s\n", base(argv[a]), *samplecd_label(cd) ? "  " : "", samplecd_label(cd));
		if (list) {
			std::vector<int> depth(n, 0);
			for (size_t i = 0; i < n; i++) {
				const samplecd_entry *e = samplecd_entry_at(cd, i);
				depth[i] = e->parent == SAMPLECD_NONE ? 1 : depth[e->parent] + 1;
				printf("%*s%-11s %4u  %-16s", depth[i] * 2, "", kinds[e->kind], e->number + 1, e->name);
				if (e->category[0])
					printf("  [%s]", e->category);
				if (e->bytes)
					printf("  %llu bytes", (unsigned long long)e->bytes);
				samplecd_sample s;
				if (refs && samplecd_sample_info(cd, i, &s))
					printf("  %u Hz key %d %llu frames", s.rate, s.key, (unsigned long long)s.frames);
				if (refs) {
					size_t used[512];
					size_t k = samplecd_refs(cd, i, used, 512);
					if (k) {
						printf("  uses");
						for (size_t j = 0; j < k && j < 512; j++)
							printf(" %s%s", samplecd_entry_at(cd, used[j])->name, j + 1 < k ? "," : "");
					}
				}
				printf("\n");
			}
		}
		samplecd_close(cd);
	}
	return status;
}
