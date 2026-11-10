// Copyright 2026 Ian Karlsson
// samplerfs-ls: an image's format, its lists and what each entry uses
//   samplerfs-ls IMAGE...          one line each: the format, the counts, the label
//   samplerfs-ls -l IMAGE...       every entry, indented under what holds it
//   samplerfs-ls -r IMAGE...       also what each entry uses, and each sample's rate and key
#include <samplerfs/samplerfs.h>

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

static const char *kinds[] = { "partition", "volume", "performance", "patch", "partial", "sample", "program", "other",
	"directory", "file", "area", "tone", "multi", "bank", "rhythm set", "wave" };
static const char *plurals[] = { "partitions", "volumes", "performances", "patches", "partials", "samples", "programs", "others",
	"directories", "files", "areas", "tones", "multis", "banks", "rhythm sets", "waves" };

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
		samplerfs *fs = samplerfs_open_file(argv[a], err, sizeof err);
		if (!fs) {
			fprintf(stderr, "%s: %s\n", argv[a], err);
			status = 1;
			continue;
		}
		size_t n = samplerfs_count(fs);
		std::map<int, size_t> counts;
		for (size_t i = 0; i < n; i++)
			counts[samplerfs_entry_at(fs, i)->kind]++;
		printf("%-11s %-6s", samplerfs_format_name(samplerfs_format_of(fs)), samplerfs_medium_name(samplerfs_medium_of(fs)));
		for (auto [k, c] : counts)
			printf(" %zu %s", c, c > 1 ? plurals[k] : kinds[k]);
		printf("  %s%s%s\n", base(argv[a]), *samplerfs_label(fs) ? "  " : "", samplerfs_label(fs));
		if (list) {
			std::vector<int> depth(n, 0);
			for (size_t i = 0; i < n; i++) {
				const samplerfs_entry *e = samplerfs_entry_at(fs, i);
				depth[i] = e->parent == SAMPLERFS_NONE ? 1 : depth[e->parent] + 1;
				printf("%*s%-11s %4u  %-16s", depth[i] * 2, "", kinds[e->kind], e->number + 1, e->name);
				if (e->format != samplerfs_format_of(fs))
					printf("  (%s)", samplerfs_format_name(e->format));
				if (e->tag[0])
					printf("  [%s]", e->tag);
				if (e->bytes)
					printf("  %llu bytes", (unsigned long long)e->bytes);
				samplerfs_sample s;
				if (refs && samplerfs_sample_info(fs, i, &s))
				{
					static const char *loops[] = { "", " loop", " alt" };
					printf("  %u Hz key %d %llu frames, plays %llu-%llu%s", s.rate, s.key, (unsigned long long)s.frames,
						(unsigned long long)s.start, (unsigned long long)s.end, s.reverse ? " reversed" : "");
					if (s.loop)
						printf("%s %llu-%llu", loops[s.loop], (unsigned long long)s.loop_start, (unsigned long long)s.loop_end);
					printf(" mode %d", s.mode);
				}
				if (refs) {
					size_t used[512];
					size_t k = samplerfs_refs(fs, i, used, 512);
					if (k) {
						printf("  uses");
						for (size_t j = 0; j < k && j < 512; j++)
							printf(" %s%s", samplerfs_entry_at(fs, used[j])->name, j + 1 < k ? "," : "");
					}
				}
				printf("\n");
			}
		}
		samplerfs_close(fs);
	}
	return status;
}
