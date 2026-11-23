// Copyright 2026 Ian Karlsson
// synthfs, the command line: the commands, each one function for the command line and the shell alike
#include "cli.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <set>
#include <unistd.h>

namespace cli {

namespace {

int64_t num(syfs_image *im, syfs_node n, syfs_key key)
{
	int64_t v = 0;
	syfs_get_int(im, n, key, &v);
	return v;
}

std::string str(syfs_image *im, syfs_node n, syfs_key key)
{
	char buf[1024] = "";
	syfs_get_str(im, n, key, buf, sizeof buf);
	return buf;
}

std::string plural(syfs_key type)
{
	std::string s = syfs_title(type);
	if (type == SYFS_TYPE_PATCH)
		return s + "es";
	if (type == SYFS_TYPE_DIRECTORY)
		return "directories";
	return s + "s";
}

const char *title(syfs_key k)
{
	const char *t = syfs_title(k);
	return t ? t : "?";
}

// the nodes the arguments name, the session's own where there are none
bool nodes_of(Session &s, const std::vector<std::string> &paths, size_t from, std::vector<syfs_node> &out)
{
	bool ok = true;
	for (size_t k = from; k < paths.size(); k++) {
		std::string err;
		const syfs_node n = resolve(s.im, s.cwd, paths[k], err);
		if (n == SYFS_NO_NODE)
			ok = !fail(s.path, err);
		else
			out.push_back(n);
	}
	if (paths.size() <= from)
		out.push_back(s.cwd);
	return ok;
}

// ---- listing

// an image's line: the format, the medium, the counts of each type, the file's name and the label
void summary(syfs_image *im, const char *file)
{
	std::map<syfs_key, size_t> counts;
	syfs_info info;
	for (syfs_node n : related(im, SYFS_ROOT, SYFS_REL_ALL))
		if (syfs_node_info(im, n, &info))
			counts[info.type]++;
	const syfs_key format = syfs_key(num(im, SYFS_ROOT, SYFS_PROP_FORMAT));
	printf("%-14s %-6s", format ? title(format) : "unknown", title(syfs_key(num(im, SYFS_ROOT, SYFS_PROP_MEDIUM))));
	for (size_t i = 0; syfs_key t = syfs_type_at(im, i); i++)
		printf(" %zu %s", counts[t], counts[t] > 1 ? plural(t).c_str() : title(t));
	const std::string label = str(im, SYFS_ROOT, SYFS_PROP_LABEL);
	printf("  %s%s%s", base_name(file), label.empty() ? "" : "  ", label.c_str());
	// what a board or card was sold as
	char model[64], name[64];
	if (syfs_get_str(im, SYFS_ROOT, SYFS_PROP_MODEL, model, sizeof model)) {
		const bool titled = syfs_get_str(im, SYFS_ROOT, SYFS_PROP_TITLE, name, sizeof name);
		printf("  (%s%s%s)", model, titled ? " " : "", titled ? name : "");
	}
	printf("\n");
}

// a node's line: its type, number and name, with its format where not the image's, its tag and its size; with refs,
// a sample's header and what it uses
void row(syfs_image *im, syfs_node n, int depth, bool refs, syfs_key format)
{
	syfs_info info;
	if (!syfs_node_info(im, n, &info))
		return;
	printf("%*s%-11s %4u  %-16s", depth * 2, "", title(info.type), info.number + 1, info.name);
	const syfs_key f = syfs_key(num(im, n, SYFS_PROP_FORMAT));
	if (f != format)
		printf("  (%s)", title(f));
	char tag[16];
	if (syfs_get_str(im, n, SYFS_PROP_TAG, tag, sizeof tag))
		printf("  [%s]", tag);
	if (info.size)
		printf("  %llu bytes", (unsigned long long)info.size);
	int64_t rate;
	if (refs && syfs_get_int(im, n, SYFS_PROP_RATE, &rate)) {
		printf("  %lld Hz key %lld %lld frames, plays %lld-%lld%s", (long long)rate, (long long)num(im, n, SYFS_PROP_KEY),
			(long long)num(im, n, SYFS_PROP_FRAMES), (long long)num(im, n, SYFS_PROP_START),
			(long long)num(im, n, SYFS_PROP_END), num(im, n, SYFS_PROP_REVERSE) ? " reversed" : "");
		const syfs_key loop = syfs_key(num(im, n, SYFS_PROP_LOOP));
		if (loop)
			printf(" %s %lld-%lld", loop == SYFS_LOOP_FORWARD ? "loop" : "alt", (long long)num(im, n, SYFS_PROP_LOOP_START),
				(long long)num(im, n, SYFS_PROP_LOOP_END));
		printf(" mode %lld", (long long)num(im, n, SYFS_PROP_MODE));
	}
	if (refs) {
		const auto used = related(im, n, SYFS_REL_USES);
		if (!used.empty())
			printf("  uses");
		for (size_t j = 0; j < used.size(); j++)
			if (syfs_node_info(im, used[j], &info))
				printf(" %s%s", info.name, j + 1 < used.size() ? "," : "");
	}
	printf("\n");
}

enum Listing { SELF, CHILDREN, ALL, REFS };

void list(syfs_image *im, const char *file, syfs_node n, Listing how)
{
	const syfs_key format = syfs_key(num(im, SYFS_ROOT, SYFS_PROP_FORMAT));
	if (n == SYFS_ROOT)
		summary(im, file);
	else
		row(im, n, 0, how == REFS, format);
	if (how == SELF)
		return;
	std::map<syfs_node, int> depth { { n, 0 } };
	syfs_info info;
	for (syfs_node k : related(im, n, how == CHILDREN ? SYFS_REL_CHILDREN : SYFS_REL_ALL))
		if (syfs_node_info(im, k, &info)) {
			depth[k] = depth[info.parent] + 1;
			row(im, k, depth[k], how == REFS, format);
		}
}

Listing listing_of(const Args &a, Listing plain)
{
	return a.has('r') ? REFS : a.has('l') ? ALL : a.has('d') ? SELF : plain;
}

// the line for a node named elsewhere: its type, number, name and path
void ref(syfs_image *im, syfs_node n)
{
	syfs_info info;
	if (syfs_node_info(im, n, &info))
		printf("%-11s %4u  %-16s  %s\n", title(info.type), info.number + 1, info.name, path_of(im, n).c_str());
}

int ls(Session &s, const Args &a)
{
	std::vector<syfs_node> nodes;
	int status = !nodes_of(s, a.pos, 0, nodes);
	for (syfs_node n : nodes)
		list(s.im, s.path.c_str(), n, listing_of(a, CHILDREN));
	if (*syfs_error(s.im))
		status = fail(s.path, syfs_error(s.im));
	return status;
}

int scan(Session &, const Args &a)
{
	int status = 0;
	for (const std::string &image : a.pos) {
		Session one;
		if (!one.open(image, false)) {
			status = 1;
			continue;
		}
		list(one.im, image.c_str(), SYFS_ROOT, listing_of(a, SELF));
		if (*syfs_error(one.im))
			status = fail(image, syfs_error(one.im));
	}
	return status;
}

int info(Session &s, const Args &a)
{
	std::vector<syfs_node> nodes;
	int status = !nodes_of(s, a.pos, 0, nodes);
	for (size_t k = 0; k < nodes.size(); k++) {
		const syfs_node n = nodes[k];
		if (k)
			printf("\n");
		printf("%-12s @%u\n%-12s %s\n", "node", n, "path", path_of(s.im, n).c_str());
		syfs_prop p;
		for (size_t i = 0; syfs_prop_at(s.im, n, i, &p); i++) {
			std::string v;
			int64_t x;
			if (p.kind == SYFS_KIND_STRING)
				v = str(s.im, n, p.key);
			else if (syfs_get_int(s.im, n, p.key, &x))
				v = p.kind == SYFS_KIND_KEY ? key_name(syfs_key(x)) : std::to_string(x);
			printf("%-12s %s%s\n", key_name(p.key).c_str(), v.c_str(), p.writable ? "  (writable)" : "");
		}
	}
	return status;
}

// every node n uses, and what they use, in the order first reached
std::vector<syfs_node> needs(syfs_image *im, syfs_node n)
{
	std::vector<syfs_node> out;
	std::set<syfs_node> seen { n };
	for (size_t k = 0, more = 1; more; k++) {
		const auto used = related(im, k ? out[k - 1] : n, SYFS_REL_USES);
		for (syfs_node u : used)
			if (seen.insert(u).second)
				out.push_back(u);
		more = k < out.size();
	}
	return out;
}

int uses(Session &s, const Args &a)
{
	std::vector<syfs_node> nodes;
	int status = !nodes_of(s, a.pos, 0, nodes);
	for (syfs_node n : nodes) {
		if (nodes.size() > 1)
			printf("%s:\n", path_of(s.im, n).c_str());
		if (a.has('b')) {
			for (syfs_node k : related(s.im, SYFS_ROOT, SYFS_REL_ALL)) {
				const auto used = related(s.im, k, SYFS_REL_USES);
				if (std::find(used.begin(), used.end(), n) != used.end())
					ref(s.im, k);
			}
		} else
			for (syfs_node u : a.has('a') ? needs(s.im, n) : related(s.im, n, SYFS_REL_USES))
				ref(s.im, u);
	}
	return status;
}

int at(Session &s, const Args &a)
{
	std::string err;
	const syfs_node n = resolve(s.im, s.cwd, a.pos[0], err);
	if (n == SYFS_NO_NODE)
		return fail(s.path, err);
	char *end;
	const long key = strtol(a.pos[1].c_str(), &end, 10);
	if (*end || key < 0 || key > 127)
		return fail(a.pos[1], "not a key (0 to 127)");
	const syfs_node k = syfs_at_key(s.im, n, int(key));
	if (k == SYFS_NO_NODE)
		return fail(a.pos[0], "nothing sounds at key " + a.pos[1]);
	ref(s.im, k);
	return 0;
}

// ---- export

std::vector<std::string> exports_of(syfs_image *im, syfs_node n)
{
	std::vector<std::string> v;
	const std::string e = str(im, n, SYFS_PROP_EXPORTS);
	for (size_t from = e.find_first_not_of(' '); from < e.size();) {
		const size_t to = std::min(e.find(' ', from), e.size());
		v.push_back(e.substr(from, to - from));
		from = e.find_first_not_of(' ', to);
	}
	return v;
}

bool exports(syfs_image *im, syfs_node n, syfs_key as)
{
	for (const std::string &e : exports_of(im, n))
		if (syfs_key_of(e.c_str()) == as)
			return true;
	return false;
}

// a file made for writing; never over one already there unless forced
FILE *create(const std::string &path, bool force)
{
	FILE *f = fopen(path.c_str(), force ? "wb" : "wbx");
	if (!f)
		fail(path, errno == EEXIST ? "cannot be made (a file of that name is kept; -f writes over it)" : strerror(errno));
	return f;
}

// node n's contents in a conversion written to f; the bytes, or -1 where not all were read or written
long long put(syfs_image *im, syfs_node n, syfs_key as, FILE *f)
{
	const size_t total = syfs_read(im, n, as, 0, nullptr, 0);
	std::vector<uint8_t> buf(1 << 20);
	size_t done = 0;
	for (size_t got; done < total && (got = syfs_read(im, n, as, done, buf.data(), buf.size())); done += got)
		if (fwrite(buf.data(), 1, got, f) != got)
			return -1;
	return total && done == total ? (long long)total : -1;
}

int export_(Session &s, const Args &a)
{
	const syfs_key as = a.has('t') ? syfs_key_of(a.get('t')) : syfs_key(SYFS_NONE);
	if (a.has('t') && as != SYFS_AS_RAW && as != SYFS_AS_WAV && as != SYFS_AS_S16 && as != SYFS_AS_SYX)
		return fail(a.get('t'), "no such conversion (raw, wav, s16, syx)");
	std::vector<syfs_node> given;
	if (!nodes_of(s, a.pos, 0, given))
		return 1;
	// each node in its conversion: itself where it exports it, else every node under it that does; with -u, what
	// it uses too
	std::vector<std::pair<syfs_node, syfs_key>> out;
	std::set<syfs_node> seen;
	auto take = [&](syfs_node n, syfs_key k) {
		if (exports(s.im, n, k) && seen.insert(n).second)
			out.emplace_back(n, k);
	};
	for (syfs_node n : given) {
		syfs_key k = as;
		if (!k) {
			const auto e = exports_of(s.im, n);
			if (e.empty())
				return fail(path_of(s.im, n), "exports nothing itself (-t names a conversion of the nodes under it)");
			k = syfs_key_of(e[0].c_str());
		}
		const size_t before = out.size();
		if (exports(s.im, n, k))
			take(n, k);
		else
			for (syfs_node c : related(s.im, n, SYFS_REL_ALL))
				take(c, k);
		if (a.has('u'))
			for (size_t i = before, end = out.size(); i < end; i++)
				for (syfs_node u : needs(s.im, out[i].first))
					take(u, k);
		if (out.size() == before)
			return fail(path_of(s.im, n), "nothing there exports " + key_name(k));
	}
	size_t waves = 0;
	for (const auto &[n, k] : out)
		waves += k == SYFS_AS_WAV;
	if (waves > 1)
		return fail(s.path, "one WAV file at a time (wav writes each to a directory)");
	const char *to = a.get('o');
	const bool stdout_ = !to || !strcmp(to, "-");
	if (stdout_ && isatty(fileno(stdout)))
		return fail("export", "not written to a terminal (-o FILE)");
	FILE *f = stdout_ ? stdout : create(to, a.has('f'));
	if (!f)
		return 1;
	long long bytes = 0;
	int status = 0;
	for (const auto &[n, k] : out) {
		const long long got = put(s.im, n, k, f);
		if (got < 0) {
			status = fail(path_of(s.im, n), "not written whole");
			break;
		}
		bytes += got;
	}
	if (stdout_)
		return fflush(stdout) ? fail("export", "not written") : status;
	if (fclose(f) && !status)
		status = fail(to, "not written whole");
	if (status)
		remove(to);
	else
		printf("%s: %lld bytes of %zu node%s\n", to, bytes, out.size(), out.size() > 1 ? "s" : "");
	return status;
}

// a sample's file name: its node, five digits, and its name
std::string wav_name(syfs_node n, const char *name)
{
	std::string s = std::to_string(n);
	s.insert(0, s.size() < 5 ? 5 - s.size() : 0, '0');
	s += ' ';
	for (const char *p = name; *p; p++)
		s += *p == '/' || *p == '\\' || uint8_t(*p) < 32 ? '_' : *p;
	return s + ".wav";
}

int wav(Session &s, const Args &a)
{
	const bool dry = a.has('n');
	if (!dry && a.pos.empty())
		return fail("wav", "a directory to write into, or -n");
	std::vector<syfs_node> given, which;
	int status = !nodes_of(s, a.pos, dry ? 0 : 1, given);
	// a node with a wave, or every node under it with one
	for (syfs_node n : given) {
		int64_t rate;
		if (n != SYFS_ROOT && syfs_get_int(s.im, n, SYFS_PROP_RATE, &rate))
			which.push_back(n);
		else
			for (syfs_node k : related(s.im, n, SYFS_REL_ALL))
				which.push_back(k);
	}
	for (syfs_node n : which) {
		syfs_info info;
		int64_t rate;
		if (!syfs_node_info(s.im, n, &info) || !syfs_get_int(s.im, n, SYFS_PROP_RATE, &rate))
			continue;
		const unsigned long long frames = (unsigned long long)num(s.im, n, SYFS_PROP_FRAMES);
		if (dry) {
			printf("%u  %-16s  %llu frames  %u Hz  %u×%u bits\n", n, info.name, frames, unsigned(rate),
				unsigned(num(s.im, n, SYFS_PROP_CHANNELS)), unsigned(num(s.im, n, SYFS_PROP_BITS)));
			continue;
		}
		const std::string path = a.pos[0] + "/" + wav_name(n, info.name);
		FILE *f = syfs_read(s.im, n, SYFS_AS_WAV, 0, nullptr, 0) ? fopen(path.c_str(), "wb") : nullptr;
		const bool ok = f && put(s.im, n, SYFS_AS_WAV, f) >= 0;
		if (f && (fclose(f) || !ok))
			remove(path.c_str());
		if (!f || !ok)
			status = fail(path, "not written (no wave read, or the file could not be)");
		else
			printf("%s  %llu of %llu frames\n", path.c_str(), frames, frames);
	}
	return status;
}

// ---- changes

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

// a path's last name, unescaped, and the path before it
std::string leaf(const std::string &path)
{
	size_t slash = std::string::npos;
	for (size_t k = 0; k < path.size(); k++)
		if (path[k] == '\\')
			k++;
		else if (path[k] == '/')
			slash = k;
	std::string out;
	for (size_t k = slash == std::string::npos ? 0 : slash + 1; k < path.size(); k++)
		out += path[k] == '\\' && k + 1 < path.size() ? path[++k] : path[k];
	return out;
}

std::string stem(const std::string &path)
{
	size_t slash = std::string::npos;
	for (size_t k = 0; k < path.size(); k++)
		if (path[k] == '\\')
			k++;
		else if (path[k] == '/')
			slash = k;
	return slash == std::string::npos ? "." : slash == 0 ? "/" : path.substr(0, slash);
}

syfs_node node_at(Session &s, const std::string &path)
{
	std::string err;
	const syfs_node n = resolve(s.im, s.cwd, path, err);
	if (n == SYFS_NO_NODE)
		fail(s.path, err);
	return n;
}

int add(Session &s, const Args &a)
{
	const syfs_node dir = node_at(s, a.pos.size() > 1 ? a.pos[1] : ".");
	if (dir == SYFS_NO_NODE)
		return 1;
	FILE *in = fopen(a.pos[0].c_str(), "rb");
	if (!in)
		return fail(a.pos[0], "cannot be opened");
	const syfs_io from { in, file_read, nullptr, size_of(in), nullptr, nullptr };
	const std::string name = a.has('n') ? a.get('n') : base_name(a.pos[0].c_str());
	const bool ok = syfs_import(s.im, dir, SYFS_TYPE_FILE, SYFS_AS_RAW, name.c_str(), &from) != SYFS_NO_NODE;
	fclose(in);
	return ok ? 0 : fail(a.pos[0], syfs_error(s.im));
}

int mkdir(Session &s, const Args &a)
{
	const syfs_node dir = node_at(s, stem(a.pos[0]));
	if (dir == SYFS_NO_NODE)
		return 1;
	if (syfs_import(s.im, dir, SYFS_TYPE_DIRECTORY, SYFS_AS_RAW, leaf(a.pos[0]).c_str(), nullptr) == SYFS_NO_NODE)
		return fail(a.pos[0], syfs_error(s.im));
	return 0;
}

int rm(Session &s, const Args &a)
{
	int status = 0;
	for (const std::string &p : a.pos) {
		const syfs_node n = node_at(s, p);
		if (n == SYFS_ROOT)
			status = fail(p, "the image itself is not removed");
		else if (n == SYFS_NO_NODE)
			status = 1;
		else if (!syfs_remove(s.im, n))
			status = fail(p, syfs_error(s.im));
	}
	return status;
}

int mv(Session &s, const Args &a)
{
	const syfs_node n = node_at(s, a.pos[0]);
	if (n == SYFS_NO_NODE)
		return 1;
	if (n == SYFS_ROOT)
		return fail(a.pos[0], "the image itself is not moved");
	// into a directory that is there, keeping the name; else to the path's directory under its last name
	std::string err;
	syfs_node dir = resolve(s.im, s.cwd, a.pos[1], err);
	syfs_info info, to;
	std::string name;
	if (dir == SYFS_NO_NODE || (dir != SYFS_ROOT && (!syfs_node_info(s.im, dir, &to) || to.type != SYFS_TYPE_DIRECTORY))) {
		dir = node_at(s, stem(a.pos[1]));
		name = leaf(a.pos[1]);
	}
	if (dir == SYFS_NO_NODE || !syfs_node_info(s.im, n, &info))
		return 1;
	if (dir != info.parent && !syfs_move(s.im, n, dir))
		return fail(a.pos[0], syfs_error(s.im));
	if (!name.empty() && name != info.name && !syfs_set_str(s.im, n, SYFS_PROP_NAME, name.c_str()))
		return fail(a.pos[0], syfs_error(s.im));
	return 0;
}

int transfer(Session &s, const Args &a)
{
	Session other;
	if (!other.open(a.pos[0], false))
		return 1;
	std::string err;
	const syfs_node n = resolve(other.im, SYFS_ROOT, a.pos[1], err);
	if (n == SYFS_NO_NODE)
		return fail(a.pos[0], err);
	if (n == SYFS_ROOT)
		return fail(a.pos[1], "a node of the image, not the image itself");
	const syfs_node dir = node_at(s, a.pos.size() > 2 ? a.pos[2] : ".");
	if (dir == SYFS_NO_NODE)
		return 1;
	if (syfs_transfer(s.im, dir, other.im, n) == SYFS_NO_NODE)
		return fail(a.pos[1], syfs_error(s.im));
	return 0;
}

int set(Session &s, const Args &a)
{
	const syfs_node n = node_at(s, a.pos[0]);
	if (n == SYFS_NO_NODE)
		return 1;
	const syfs_key key = syfs_key_of(a.pos[1].c_str());
	syfs_prop p {};
	bool has = false;
	for (size_t i = 0; !has && syfs_prop_at(s.im, n, i, &p); i++)
		has = p.key == key;
	if (!has)
		return fail(a.pos[1], "no such property of " + path_of(s.im, n));
	const char *v = a.pos[2].c_str();
	bool ok;
	if (p.kind == SYFS_KIND_STRING)
		ok = syfs_set_str(s.im, n, key, v);
	else if (p.kind == SYFS_KIND_KEY)
		ok = syfs_set_int(s.im, n, key, syfs_key_of(v));
	else {
		char *end;
		errno = 0;
		const long long x = strtoll(v, &end, 0);
		if (*end || end == v || errno)
			return fail(v, "not a number");
		ok = syfs_set_int(s.im, n, key, x);
	}
	return ok ? 0 : fail(a.pos[0], syfs_error(s.im));
}

// ---- images

int convert(Session &, const Args &a)
{
	const char *in_path = a.pos[0].c_str(), *out_path = a.pos[1].c_str();
	const char *dot = strrchr(out_path, '.');
	const bool vhd = dot && (!strcmp(dot, ".vhd") || !strcmp(dot, ".VHD"));
	FILE *in = fopen(in_path, "rb");
	if (!in)
		return fail(in_path, "cannot be opened");
	FILE *out = create(out_path, false);
	if (!out) {
		fclose(in);
		return 1;
	}
	const syfs_io from { in, file_read, nullptr, size_of(in), nullptr, nullptr }, to { out, nullptr, file_write, 0, nullptr, nullptr };
	char err[256] = "";
	const uint64_t n = syfs_convert(&from, &to, vhd ? SYFS_CONTAINER_VHD : SYFS_CONTAINER_RAW, err, sizeof err);
	fclose(in);
	if (fclose(out) || !n) {
		remove(out_path);
		return fail(in_path, err[0] ? err : "not written");
	}
	printf("%s: %llu bytes from %llu\n", out_path, (unsigned long long)n, (unsigned long long)from.size);
	return 0;
}

int make(Session &, const Args &a)
{
	const syfs_key format = syfs_key_of(a.pos[0].c_str());
	const syfs_key medium = a.has('m') ? syfs_key_of(a.get('m')) : syfs_key(SYFS_NONE);
	char *end;
	errno = 0;
	uint64_t size = strtoull(a.pos[1].c_str(), &end, 10);
	if (*end == 'K' || *end == 'k')
		size <<= 10, end++;
	else if (*end == 'M' || *end == 'm')
		size <<= 20, end++;
	if (*end || !size || errno)
		return fail(a.pos[1], "not a size (bytes, or K or M of them)");
	FILE *f = create(a.pos[2], a.has('f'));
	if (!f)
		return 1;
	const syfs_io io { f, file_read, file_write, size, nullptr, nullptr };
	char err[256] = "";
	bool ok = !ftruncate(fileno(f), off_t(size));
	ok = ok && syfs_create(&io, format, medium, a.get('l'), err, sizeof err);
	if (fclose(f))
		ok = false;
	if (!ok) {
		remove(a.pos[2].c_str());
		return fail(a.pos[2], err[0] ? err : "not written");
	}
	return 0;
}

// ---- the shell's own

int open(Session &s, const Args &a)
{
	if (!s.open(a.pos[0], a.has('w')))
		return 1;
	if (s.interactive)
		list(s.im, s.path.c_str(), SYFS_ROOT, SELF);
	return 0;
}

int cd(Session &s, const Args &a)
{
	const syfs_node n = node_at(s, a.pos.empty() ? "/" : a.pos[0]);
	if (n == SYFS_NO_NODE)
		return 1;
	s.cwd = n;
	return 0;
}

int pwd(Session &s, const Args &)
{
	printf("%s\n", path_of(s.im, s.cwd).c_str());
	return 0;
}

int help(Session &s, const Args &a)
{
	if (a.pos.empty()) {
		print_help(stdout, s.shell);
		return 0;
	}
	const Command *c = find_command(a.pos[0]);
	if (!c)
		return fail(a.pos[0], "no such command");
	print_command_help(stdout, *c, s.shell);
	return 0;
}

int quit(Session &s, const Args &)
{
	s.quit = true;
	return 0;
}

const std::vector<Command> table = {
	{ "ls", "dlr", READS, 0, -1, "[-d|-l|-r] [PATH...]",
		"A node's line and its children's: the image's is its format, counts and label.\n"
		"  -d  the node's line alone\n  -l  every node under it, indented under what holds it\n"
		"  -r  as -l, with each sample's header and what each node uses", ls },
	{ "info", "", READS, 0, -1, "[PATH...]", "A node's number, path and every property.", info },
	{ "uses", "ab", READS, 0, -1, "[-a|-b] [PATH...]",
		"The nodes a node uses to sound.\n  -a  and what they use, all the way down\n  -b  the nodes that use it instead",
		uses },
	{ "at", "", READS, 2, 2, "PATH KEY", "The sample a node sounds at a MIDI key (0 to 127).", at },
	{ "export", "t:o:fu", READS, 1, -1, "[-t AS] [-u] [-o FILE] [-f] PATH...",
		"Nodes' contents in a conversion, one after another, to FILE or stdout.  A node that does not export it\n"
		"itself gives every node under it that does (\"/\": the whole image).\n"
		"  -t AS    raw, wav, s16 or syx (each node's first export without it)\n"
		"  -u       also every node they use, all the way down\n"
		"  -o FILE  where to write (\"-\" stdout, the default; never a terminal)\n"
		"  -f       write over a file already there", export_ },
	{ "wav", "n", READS, 0, -1, "DIR [PATH...] | -n [PATH...]",
		"Each sample (a node with a wave, or every one under a node; the whole image without PATH) as a 16-bit WAV\n"
		"file in DIR, named by node number and name, its key and loop in a smpl chunk.\n"
		"  -n  list what would be written: node number, name, frames, rate", wav },
	{ "add", "n:", WRITES, 1, 2, "[-n NAME] FILE [DIR]", "A file of the host's put in a directory (here without one).",
		add },
	{ "mkdir", "", WRITES, 1, 1, "PATH", "A directory made.", mkdir },
	{ "rm", "", WRITES, 1, -1, "PATH...", "Nodes removed, with what they hold.", rm },
	{ "mv", "", WRITES, 2, 2, "PATH NEW",
		"A node moved into the directory NEW, or named anew, moved to NEW's directory, or both.", mv },
	{ "transfer", "", WRITES, 2, 3, "OTHER PATH [DIR]",
		"A node of the image OTHER copied into a directory (here without one), with what it holds.", transfer },
	{ "set", "", WRITES, 3, 3, "PATH PROPERTY VALUE", "A writable property set (info marks them).", set },
	{ "scan", "lr", NO_IMAGE, 1, -1, "[-l|-r] IMAGE...", "Each image's line, and with -l or -r its nodes as ls gives them.",
		scan },
	{ "convert", "", NO_IMAGE, 2, 2, "IN OUT",
		"An image into another container: a raw image into a dynamic VHD where OUT ends .vhd, else a VHD's\n"
		"sectors out into a raw image.  Never over a file already there.", convert },
	{ "create", "m:l:f", NO_IMAGE, 3, 3, "[-m MEDIUM] [-l LABEL] [-f] FORMAT SIZE FILE",
		"An empty volume of SIZE bytes (or K, M) made: FORMAT fat, s760 or s770, or none with -m smartmedia for a\n"
		"blank card.\n  -m MEDIUM  floppy, disk or smartmedia (by the size without it)\n"
		"  -l LABEL   the volume's label\n  -f         write over a file already there", make },
	{ "open", "w", SHELL, 1, 1, "[-w] IMAGE", "Another image opened in place of this one; -w for writing.", open },
	{ "cd", "", SHELL, 0, 1, "[PATH]", "Stand at a node (the image without PATH).", cd },
	{ "pwd", "", SHELL, 0, 0, "", "The path of where the shell stands.", pwd },
	{ "help", "", SHELL, 0, 1, "[COMMAND]", "The commands, or one's options.", help },
	{ "quit", "", SHELL, 0, 0, "", "Leave the shell (so does exit, or the end of input).", quit },
};

}

const std::vector<Command> &commands()
{
	return table;
}

const Command *find_command(const std::string &name)
{
	for (const Command &c : table)
		if (name == c.name || (name == "exit" && !strcmp(c.name, "quit")))
			return &c;
	return nullptr;
}

void print_command_help(FILE *f, const Command &c, bool shell)
{
	const bool image = !shell && (c.need == READS || c.need == WRITES);
	fprintf(f, "usage: %s%s%s%s%s\n%s\n", shell ? "" : "synthfs ", c.name, image ? " IMAGE" : "", *c.usage ? " " : "",
		c.usage, c.help);
	if (c.need == WRITES && shell)
		fprintf(f, "The image opened with -w.\n");
}

void print_help(FILE *f, bool shell)
{
	if (!shell)
		fprintf(f, "usage: synthfs COMMAND IMAGE [ARGS...]   a command on an image\n"
			"       synthfs COMMAND [ARGS...]         scan, convert, create, help\n"
			"       synthfs shell [-w] [IMAGE]        commands read from stdin; -w opens for writing\n"
			"       synthfs COMMAND --help\n"
			"An IMAGE of the form FORMAT:MEDIUM=FILE is opened as that kind where the file does not tell it\n"
			"(jv1080:memory=FILE, xv5080:memory=FILE, xv3080:memory=FILE: a machine's battery memory).\n");
	auto group = [&](const char *head, Need need) {
		fprintf(f, "\n%s\n", head);
		for (const Command &c : table)
			if (c.need == need && (shell || need != SHELL || !strcmp(c.name, "help")))
				fprintf(f, *c.usage ? "  %-9s %s\n" : "  %s\n", c.name, c.usage);
	};
	group("Reading:", READS);
	group("Changing (a FAT volume, raw or in a VHD; an S-760/S-770 disk takes transfer):", WRITES);
	group("Images:", NO_IMAGE);
	group(shell ? "The shell:" : "Also:", SHELL);
	fprintf(f, "\nA PATH names a node by names from the top (\"/\") or from where the shell stands, '/' between, \".\"\n"
		"and \"..\" as in a file tree.  Where names are shared, TYPE:NUMBER picks a node by its type and the number\n"
		"ls shows (\"patch:3\"), TYPE=NAME by its type and name (\"patch=STR:Strings\"), and @NODE any node by the\n"
		"number info gives, which holds while the image is open.  A backslash takes the next character as it is\n"
		"(\"\\/\" a slash in a name, \"\\patch:3\" a name).  pwd and the commands' paths print names where unique.\n");
	if (!shell)
		fprintf(f, "\nThe shell prompts where stdin is a terminal; otherwise it stops at the first command that fails,\n"
			"with its status.  '#' at a word's start begins a comment.\n");
}

}
