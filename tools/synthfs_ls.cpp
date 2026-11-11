// Copyright 2026 Ian Karlsson
// synthfs-ls: an image's format, what it holds and what each node uses
//   synthfs-ls IMAGE...          one line each: the format, the counts, the label
//   synthfs-ls -l IMAGE...       every node, indented under what holds it
//   synthfs-ls -r IMAGE...       also what each node uses, and each sample's rate and key
#include <synthfs/synthfs.h>

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

static const char *base(const char *path)
{
	const char *s = strrchr(path, '/');
	return s ? s + 1 : path;
}

static std::string plural(syfs_key type)
{
	std::string s = syfs_title(type);
	if (type == SYFS_TYPE_PATCH)
		return s + "es";
	if (type == SYFS_TYPE_DIRECTORY)
		return "directories";
	return s + "s";
}

static long long num(syfs_image *im, syfs_node n, syfs_key key)
{
	int64_t v = 0;
	syfs_get_int(im, n, key, &v);
	return v;
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
		syfs_image *im = syfs_open_path(argv[a], err, sizeof err);
		if (!im) {
			fprintf(stderr, "%s: %s\n", argv[a], err);
			status = 1;
			continue;
		}
		std::vector<syfs_node> nodes(syfs_list(im, SYFS_ROOT, SYFS_REL_ALL, nullptr, 0));
		syfs_list(im, SYFS_ROOT, SYFS_REL_ALL, nodes.data(), nodes.size());
		std::map<syfs_key, size_t> counts;
		syfs_info info;
		for (syfs_node n : nodes)
			if (syfs_node_info(im, n, &info))
				counts[info.type]++;
		syfs_key format = syfs_key(num(im, SYFS_ROOT, SYFS_PROP_FORMAT));
		char label[256] = "";
		syfs_get_str(im, SYFS_ROOT, SYFS_PROP_LABEL, label, sizeof label);
		printf("%-14s %-6s", format ? syfs_title(format) : "unknown",
			syfs_title(syfs_key(num(im, SYFS_ROOT, SYFS_PROP_MEDIUM))));
		for (size_t i = 0; syfs_key t = syfs_type_at(im, i); i++)
			printf(" %zu %s", counts[t], counts[t] > 1 ? plural(t).c_str() : syfs_title(t));
		printf("  %s%s%s\n", base(argv[a]), *label ? "  " : "", label);
		if (list) {
			std::map<syfs_node, int> depth;
			for (syfs_node n : nodes) {
				if (!syfs_node_info(im, n, &info))
					continue;
				depth[n] = depth[info.parent] + 1;
				printf("%*s%-11s %4u  %-16s", depth[n] * 2, "", syfs_title(info.type), info.number + 1, info.name);
				syfs_key f = syfs_key(num(im, n, SYFS_PROP_FORMAT));
				if (f != format)
					printf("  (%s)", syfs_title(f));
				char tag[16];
				if (syfs_get_str(im, n, SYFS_PROP_TAG, tag, sizeof tag))
					printf("  [%s]", tag);
				if (info.size)
					printf("  %llu bytes", (unsigned long long)info.size);
				int64_t rate;
				if (refs && syfs_get_int(im, n, SYFS_PROP_RATE, &rate)) {
					printf("  %lld Hz key %lld %lld frames, plays %lld-%lld%s", (long long)rate, num(im, n, SYFS_PROP_KEY),
						num(im, n, SYFS_PROP_FRAMES), num(im, n, SYFS_PROP_START), num(im, n, SYFS_PROP_END),
						num(im, n, SYFS_PROP_REVERSE) ? " reversed" : "");
					syfs_key loop = syfs_key(num(im, n, SYFS_PROP_LOOP));
					if (loop)
						printf(" %s %lld-%lld", loop == SYFS_LOOP_FORWARD ? "loop" : "alt", num(im, n, SYFS_PROP_LOOP_START),
							num(im, n, SYFS_PROP_LOOP_END));
					printf(" mode %lld", num(im, n, SYFS_PROP_MODE));
				}
				if (refs) {
					syfs_node used[512];
					size_t k = syfs_list(im, n, SYFS_REL_USES, used, 512);
					if (k) {
						printf("  uses");
						for (size_t j = 0; j < k && j < 512; j++)
							if (syfs_node_info(im, used[j], &info))
								printf(" %s%s", info.name, j + 1 < k ? "," : "");
					}
				}
				printf("\n");
			}
		}
		if (*syfs_error(im)) {
			fprintf(stderr, "%s: %s\n", argv[a], syfs_error(im));
			status = 1;
		}
		syfs_close(im);
	}
	return status;
}
