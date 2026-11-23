// Copyright 2026 Ian Karlsson
// synthfs, the command line: the image opened, and its nodes named by path
#include "cli.hpp"

#include <cstring>

namespace cli {

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

bool known(const std::string &name)
{
	return syfs_name(syfs_key_of(name.c_str())) != nullptr;
}

bool digits(const std::string &s)
{
	return !s.empty() && s.size() < 10 && s.find_first_not_of("0123456789") == std::string::npos;
}

// a path's names, each with whether a backslash in it took a character as it is
std::vector<std::pair<std::string, bool>> split(const std::string &path)
{
	std::vector<std::pair<std::string, bool>> out(1);
	for (size_t k = 0; k < path.size(); k++) {
		if (path[k] == '\\' && k + 1 < path.size()) {
			out.back().first += path[++k];
			out.back().second = true;
		} else if (path[k] == '/')
			out.emplace_back();
		else
			out.back().first += path[k];
	}
	return out;
}

// a name that reads back as itself, not as one of the other forms
bool plain(const std::string &name)
{
	if (name.empty() || name == "." || name == ".." || name[0] == '@' || name.find_first_of("/\\") != std::string::npos)
		return false;
	const size_t colon = name.find(':'), eq = name.find('=');
	if (colon != std::string::npos && known(name.substr(0, colon)) && digits(name.substr(colon + 1)))
		return false;
	return eq == std::string::npos || !known(name.substr(0, eq));
}

std::string token(const syfs_info &info)
{
	return std::string(syfs_name(info.type) ? syfs_name(info.type) : "node") + ":" + std::to_string(info.number + 1);
}

}

bool Session::open(const std::string &image, bool write)
{
	close();
	char err[256] = "";
	// FORMAT:MEDIUM=FILE: an image that does not tell what it is, opened as the kind named
	const size_t eq = image.find('='), colon = image.find(':');
	const syfs_key format = colon < eq && eq != std::string::npos ? syfs_key_of(image.substr(0, colon).c_str()) : syfs_key(SYFS_NONE);
	const syfs_key medium = format ? syfs_key_of(image.substr(colon + 1, eq - colon - 1).c_str()) : syfs_key(SYFS_NONE);
	if (format && syfs_name(format) && syfs_name(medium)) {
		file = fopen(image.c_str() + eq + 1, "rb");
		if (!file)
			return fail(image.substr(eq + 1), "cannot be opened"), false;
		fseeko(file, 0, SEEK_END);
		const syfs_io io { file, file_read, nullptr, uint64_t(ftello(file)), nullptr, nullptr };
		im = syfs_open_as(&io, format, medium, err, sizeof err);
	} else if (write) {
		file = fopen(image.c_str(), "r+b");
		if (!file)
			return fail(image, "cannot be opened for writing"), false;
		fseeko(file, 0, SEEK_END);
		const syfs_io io { file, file_read, file_write, uint64_t(ftello(file)), nullptr, nullptr };
		im = syfs_open(&io, err, sizeof err);
	} else
		im = syfs_open_path(image.c_str(), err, sizeof err);
	if (!im) {
		if (file)
			fclose(file);
		file = nullptr;
		return fail(image, err), false;
	}
	path = image;
	cwd = SYFS_ROOT;
	return true;
}

bool Session::close()
{
	if (im)
		syfs_close(im);
	im = nullptr;
	bool ok = true;
	if (file && fclose(file))
		ok = !fail(path, "could not be written");
	file = nullptr;
	return ok;
}

std::string key_name(syfs_key k)
{
	if (const char *s = syfs_name(k))
		return s;
	char buf[16];
	snprintf(buf, sizeof buf, "0x%08x", unsigned(k));
	return buf;
}

const char *base_name(const char *path)
{
	const char *s = strrchr(path, '/');
	return s ? s + 1 : path;
}

int fail(const std::string &what, const std::string &why)
{
	fprintf(stderr, "%s: %s\n", what.c_str(), why.empty() ? "not done" : why.c_str());
	return 1;
}

std::vector<syfs_node> related(syfs_image *im, syfs_node n, syfs_key relation)
{
	std::vector<syfs_node> v(syfs_list(im, n, relation, nullptr, 0));
	v.resize(syfs_list(im, n, relation, v.data(), v.size()));
	return v;
}

syfs_node resolve(syfs_image *im, syfs_node from, const std::string &path, std::string &err)
{
	syfs_node n = !path.empty() && path[0] == '/' ? SYFS_ROOT : from;
	syfs_info info;
	for (const auto &[name, escaped] : split(path)) {
		if (!escaped && (name.empty() || name == "."))
			continue;
		if (!escaped && name == "..") {
			if (n != SYFS_ROOT && syfs_node_info(im, n, &info))
				n = info.parent;
			continue;
		}
		if (!escaped && name[0] == '@' && digits(name.substr(1))) {
			n = syfs_node(std::stoul(name.substr(1)));
			if (!syfs_node_info(im, n, &info)) {
				err = name + ": no such node";
				return SYFS_NO_NODE;
			}
			continue;
		}
		// TYPE:NUMBER, TYPE=NAME, or a name
		syfs_key type = SYFS_NONE;
		uint32_t number = 0;
		std::string want = name;
		const size_t colon = name.find(':'), eq = name.find('=');
		if (!escaped && colon != std::string::npos && known(name.substr(0, colon)) && digits(name.substr(colon + 1))) {
			type = syfs_key_of(name.substr(0, colon).c_str());
			number = uint32_t(std::stoul(name.substr(colon + 1)));
			want.clear();
		} else if (!escaped && eq != std::string::npos && known(name.substr(0, eq))) {
			type = syfs_key_of(name.substr(0, eq).c_str());
			want = name.substr(eq + 1);
		}
		std::vector<syfs_node> found;
		for (syfs_node k : related(im, n, SYFS_REL_CHILDREN))
			if (syfs_node_info(im, k, &info) && (!type || type == info.type)
				&& (want.empty() ? info.number + 1 == number : want == info.name))
				found.push_back(k);
		if (found.size() != 1) {
			err = name + (found.empty() ? ": no such node" : ": more than one node of that name:");
			for (syfs_node k : found)
				if (syfs_node_info(im, k, &info))
					err += " " + token(info);
			return SYFS_NO_NODE;
		}
		n = found[0];
	}
	return n;
}

std::string path_of(syfs_image *im, syfs_node n)
{
	std::string path;
	syfs_info info;
	while (n != SYFS_ROOT && syfs_node_info(im, n, &info)) {
		int names = 0, tokens = 0;
		syfs_info other;
		for (syfs_node k : related(im, info.parent, SYFS_REL_CHILDREN))
			if (syfs_node_info(im, k, &other)) {
				names += !strcmp(other.name, info.name);
				tokens += other.type == info.type && other.number == info.number;
			}
		const std::string part = names == 1 && plain(info.name) ? std::string(info.name)
			: tokens == 1 ? token(info) : "@" + std::to_string(n);
		path = "/" + part + path;
		if (part[0] == '@')
			return part + path.substr(part.size() + 1);
		n = info.parent;
	}
	return path.empty() ? "/" : path;
}

}
