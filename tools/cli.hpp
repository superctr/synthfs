// Copyright 2026 Ian Karlsson
// synthfs, the command line: an image's nodes named, listed, exported and changed, by subcommand or in a shell
#pragma once

#include <synthfs/synthfs.h>

#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace cli {

// an open image, and where the shell stands in it
struct Session {
	syfs_image *im = nullptr;
	std::string path;
	FILE *file = nullptr;       // the image's file, where it was opened for writing
	syfs_node cwd = SYFS_ROOT;
	bool shell = false;
	bool interactive = false;   // the shell's input a terminal
	bool quit = false;

	~Session() { close(); }
	bool open(const std::string &image, bool write);
	bool close();
};

struct Args {
	std::map<char, std::string> opt;
	std::vector<std::string> pos;
	bool has(char c) const { return opt.count(c) != 0; }
	const char *get(char c) const { return has(c) ? opt.at(c).c_str() : nullptr; }
};

enum Need { NO_IMAGE, READS, WRITES, SHELL };

struct Command {
	const char *name;
	const char *options;   // its option letters, ':' after one that takes a value
	Need need;
	int min, max;          // its arguments after the options (and after the image on the command line); max -1 any
	const char *usage;
	const char *help;
	int (*run)(Session &, const Args &);
};

const std::vector<Command> &commands();
const Command *find_command(const std::string &name);
void print_help(FILE *f, bool shell);
void print_command_help(FILE *f, const Command &c, bool shell);

// the name of a key, or its value in hex where the library does not know it
std::string key_name(syfs_key k);
const char *base_name(const char *path);

// a path's node, from the node `from`; SYFS_NO_NODE with why in err
syfs_node resolve(syfs_image *im, syfs_node from, const std::string &path, std::string &err);
// a node's path from the top, as resolve reads it back
std::string path_of(syfs_image *im, syfs_node n);
std::vector<syfs_node> related(syfs_image *im, syfs_node n, syfs_key relation);

int fail(const std::string &what, const std::string &why);

}
