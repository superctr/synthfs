// Copyright 2026 Ian Karlsson
// synthfs-edit: an image's volume changed, where its format is one written (a FAT volume, raw or in a VHD)
//   synthfs-edit IMAGE add FILE [DIR]          a file of the host's put in a directory (the top without one)
//   synthfs-edit IMAGE mkdir PATH              a directory made
//   synthfs-edit IMAGE rm PATH                 a file or a directory removed, with what it holds
//   synthfs-edit IMAGE mv PATH NEW             named anew, moved to another directory, or both
//   synthfs-edit IMAGE from OTHER PATH [DIR]   a node of another image copied in, with what it holds
//   synthfs-edit IMAGE get PATH FILE [wav]     a node written out: its own bytes, or its wave as a WAV file
// A PATH is names from the top with '/' between; "/" or "" is the top.
#include <synthfs/synthfs.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

size_t file_read(void *user, uint64_t offset, void *buf, size_t n)
{
	FILE *f = static_cast<FILE *>(user);
	return fseeko(f, off_t(offset), SEEK_SET) ? 0 : fread(buf, 1, n, f);
}

size_t file_write(void *user, uint64_t offset, const void *buf, size_t n)
{
	FILE *f = static_cast<FILE *>(user);
	return fseeko(f, off_t(offset), SEEK_SET) ? 0 : fwrite(buf, 1, n, f);
}

uint64_t size_of(FILE *f)
{
	fseeko(f, 0, SEEK_END);
	return uint64_t(ftello(f));
}

// the node at a path, or SYFS_NO_NODE
syfs_node at_path(syfs_image *im, std::string path)
{
	syfs_node n = SYFS_ROOT;
	while (!path.empty() && path.back() == '/')
		path.pop_back();
	for (size_t from = path.find_first_not_of('/'); from < path.size();) {
		size_t to = path.find('/', from);
		if (to == std::string::npos)
			to = path.size();
		const std::string name = path.substr(from, to - from);
		std::vector<syfs_node> kids(syfs_list(im, n, SYFS_REL_CHILDREN, nullptr, 0));
		syfs_list(im, n, SYFS_REL_CHILDREN, kids.data(), kids.size());
		syfs_node found = SYFS_NO_NODE;
		for (syfs_node k : kids) {
			syfs_info info;
			if (found == SYFS_NO_NODE && syfs_node_info(im, k, &info) && name == info.name)
				found = k;
		}
		if (found == SYFS_NO_NODE)
			return found;
		n = found;
		from = to + 1;
	}
	return n;
}

// a path's last name, and the path before it
std::string leaf(const std::string &path)
{
	const size_t slash = path.find_last_of('/');
	return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string stem(const std::string &path)
{
	const size_t slash = path.find_last_of('/');
	return slash == std::string::npos ? "" : path.substr(0, slash);
}

int fail(const char *what, const char *why)
{
	fprintf(stderr, "%s: %s\n", what, why && *why ? why : "not done");
	return 1;
}

int usage()
{
	fprintf(stderr, "usage: synthfs-edit IMAGE add FILE [DIR] | mkdir PATH | rm PATH | mv PATH NEW\n"
		"                           | from OTHER PATH [DIR] | get PATH FILE [wav]\n");
	return 2;
}

}

int main(int argc, char **argv)
{
	if (argc < 4)
		return usage();
	const std::string cmd = argv[2];
	const bool reads = cmd == "get";
	FILE *f = fopen(argv[1], reads ? "rb" : "r+b");
	if (!f)
		return fail(argv[1], "cannot be opened");
	char err[256] = "";
	const syfs_io io { f, file_read, reads ? nullptr : file_write, size_of(f), nullptr, nullptr };
	syfs_image *im = syfs_open(&io, err, sizeof err);
	if (!im) {
		fclose(f);
		return fail(argv[1], err);
	}
	int status = 0;
	if (cmd == "add" && argc <= 5) {
		FILE *in = fopen(argv[3], "rb");
		const syfs_node dir = at_path(im, argc > 4 ? argv[4] : "");
		if (!in || dir == SYFS_NO_NODE)
			status = fail(in ? argv[4] : argv[3], in ? "no such directory" : "cannot be opened");
		else {
			const syfs_io from { in, file_read, nullptr, size_of(in), nullptr, nullptr };
			if (syfs_import(im, dir, SYFS_TYPE_FILE, SYFS_AS_RAW, leaf(argv[3]).c_str(), &from) == SYFS_NO_NODE)
				status = fail(argv[3], syfs_error(im));
		}
		if (in)
			fclose(in);
	} else if (cmd == "mkdir" && argc == 4) {
		const syfs_node dir = at_path(im, stem(argv[3]));
		if (dir == SYFS_NO_NODE
			|| syfs_import(im, dir, SYFS_TYPE_DIRECTORY, SYFS_AS_RAW, leaf(argv[3]).c_str(), nullptr) == SYFS_NO_NODE)
			status = fail(argv[3], dir == SYFS_NO_NODE ? "no such directory to make it in" : syfs_error(im));
	} else if (cmd == "rm" && argc == 4) {
		const syfs_node n = at_path(im, argv[3]);
		if (n == SYFS_NO_NODE || n == SYFS_ROOT || !syfs_remove(im, n))
			status = fail(argv[3], n == SYFS_NO_NODE || n == SYFS_ROOT ? "no such file or directory" : syfs_error(im));
	} else if (cmd == "mv" && argc == 5) {
		const syfs_node n = at_path(im, argv[3]), dir = at_path(im, stem(argv[4]));
		if (n == SYFS_NO_NODE || n == SYFS_ROOT || dir == SYFS_NO_NODE)
			status = fail(n == SYFS_NO_NODE || n == SYFS_ROOT ? argv[3] : argv[4], "no such file or directory");
		else if (!syfs_move(im, n, dir) || !syfs_set_str(im, n, SYFS_PROP_NAME, leaf(argv[4]).c_str()))
			status = fail(argv[3], syfs_error(im));
	} else if (cmd == "from" && (argc == 5 || argc == 6)) {
		syfs_image *other = syfs_open_path(argv[3], err, sizeof err);
		const syfs_node n = other ? at_path(other, argv[4]) : SYFS_NO_NODE, dir = at_path(im, argc > 5 ? argv[5] : "");
		if (!other || n == SYFS_NO_NODE || n == SYFS_ROOT || dir == SYFS_NO_NODE)
			status = fail(!other ? argv[3] : dir == SYFS_NO_NODE ? argv[5] : argv[4], !other ? err : "no such file or directory");
		else if (syfs_transfer(im, dir, other, n) == SYFS_NO_NODE)
			status = fail(argv[4], syfs_error(im));
		if (other)
			syfs_close(other);
	} else if (cmd == "get" && (argc == 5 || argc == 6)) {
		const syfs_key as = argc == 6 ? syfs_key_of(argv[5]) : syfs_key(SYFS_AS_RAW);
		const syfs_node n = at_path(im, argv[3]);
		const size_t total = n == SYFS_NO_NODE ? 0 : syfs_read(im, n, as, 0, nullptr, 0);
		// never over a file already there
		FILE *out = total ? fopen(argv[4], "wbx") : nullptr;
		if (!out)
			status = fail(total ? argv[4] : argv[3], total ? "cannot be made (a file of that name is kept)"
				: n == SYFS_NO_NODE ? "no such file or directory" : "nothing there to write out as that");
		else {
			std::vector<uint8_t> buf(1 << 20);
			size_t done = 0;
			for (size_t got; done < total && (got = syfs_read(im, n, as, done, buf.data(), buf.size())); done += got)
				if (fwrite(buf.data(), 1, got, out) != got)
					break;
			if (fclose(out) || done < total) {
				remove(argv[4]);
				status = fail(argv[4], "not written whole");
			}
		}
	} else
		status = usage();
	syfs_close(im);
	if (fclose(f) && !status)
		status = fail(argv[1], "could not be written");
	return status;
}
