// Copyright 2026 Ian Karlsson
// synthfs: images listed, exported and changed, a command at a time or in a shell (synthfs --help)
#include "cli.hpp"

#include <cstring>
#include <iostream>
#include <unistd.h>

using namespace cli;

namespace {

// a line's words: blanks between, '...' as it is, "..." with \" and \\, a backslash taking the next character,
// '#' at a word's start the rest a comment
bool words_of(const std::string &line, std::vector<std::string> &out, std::string &err)
{
	out.clear();
	std::string w;
	bool in = false;
	for (size_t k = 0; k < line.size(); k++) {
		const char c = line[k];
		if (c == ' ' || c == '\t' || c == '\r') {
			if (in)
				out.push_back(w);
			w.clear();
			in = false;
		} else if (c == '#' && !in)
			break;
		else if (c == '\'' || c == '"') {
			const size_t end = line.find(c, k + 1);
			if (end == std::string::npos)
				return err = "a quote not closed", false;
			for (k++; k < end; k++)
				w += c == '"' && line[k] == '\\' && k + 1 < end && (line[k + 1] == '"' || line[k + 1] == '\\') ? line[++k] : line[k];
			in = true;
		} else if (c == '\\' && k + 1 < line.size()) {
			w += line[++k];
			in = true;
		} else {
			w += c;
			in = true;
		}
	}
	if (in)
		out.push_back(w);
	return true;
}

bool negative(const std::string &w)
{
	return w.size() > 1 && w[0] == '-' && w.find_first_not_of("0123456789", 1) == std::string::npos;
}

bool parse(const Command &c, const std::vector<std::string> &words, size_t from, Args &a, std::string &err)
{
	bool options = true;
	for (size_t k = from; k < words.size(); k++) {
		const std::string &w = words[k];
		if (options && w == "--") {
			options = false;
			continue;
		}
		if (!options || w.size() < 2 || w[0] != '-' || negative(w)) {
			a.pos.push_back(w);
			continue;
		}
		for (size_t j = 1; j < w.size(); j++) {
			const char *o = strchr(c.options, w[j]);
			if (!o || w[j] == ':')
				return err = std::string("no option -") + w[j], false;
			if (o[1] != ':') {
				a.opt[w[j]];
				continue;
			}
			if (j + 1 < w.size())
				a.opt[w[j]] = w.substr(j + 1);
			else if (k + 1 < words.size())
				a.opt[w[j]] = words[++k];
			else
				return err = std::string("-") + w[j] + " takes a value", false;
			break;
		}
	}
	return true;
}

bool wants_help(const std::vector<std::string> &words, size_t from)
{
	for (size_t k = from; k < words.size() && words[k] != "--"; k++)
		if (words[k] == "--help" || words[k] == "-h")
			return true;
	return false;
}

int usage(const Command &c, bool shell, const std::string &why)
{
	fprintf(stderr, "%s: %s\n", c.name, why.c_str());
	fprintf(stderr, "usage: %s%s%s %s\n", shell ? "" : "synthfs ", c.name,
		!shell && (c.need == READS || c.need == WRITES) ? " IMAGE" : "", c.usage);
	return 2;
}

// a command's words run: the name, then its options and arguments
int run(Session &s, const std::vector<std::string> &words)
{
	const Command *c = find_command(words[0]);
	if (!c || (!s.shell && c->need == SHELL && strcmp(c->name, "help")))
		return fail(words[0], "no such command (help lists them)"), 2;
	if (wants_help(words, 1)) {
		print_command_help(stdout, *c, s.shell);
		return 0;
	}
	Args a;
	std::string err;
	if (!parse(*c, words, 1, a, err))
		return usage(*c, s.shell, err);
	if (!s.shell && (c->need == READS || c->need == WRITES)) {
		if (a.pos.empty())
			return usage(*c, s.shell, "an image to work on");
		const std::string image = a.pos[0];
		a.pos.erase(a.pos.begin());
		if (int(a.pos.size()) < c->min || (c->max >= 0 && int(a.pos.size()) > c->max))
			return usage(*c, s.shell, "the arguments are not right");
		if (!s.open(image, c->need == WRITES))
			return 1;
	} else if (int(a.pos.size()) < c->min || (c->max >= 0 && int(a.pos.size()) > c->max))
		return usage(*c, s.shell, "the arguments are not right");
	if ((c->need == READS || c->need == WRITES || !strcmp(c->name, "cd") || !strcmp(c->name, "pwd")) && !s.im)
		return fail(c->name, "no image open (open IMAGE)");
	if (c->need == WRITES && !s.file)
		return fail(s.path, "opened for reading only (open -w IMAGE)");
	int status = c->run(s, a);
	fflush(stdout);
	if (!s.shell && !s.close() && !status)
		status = 1;
	return status;
}

int shell(int argc, char **argv)
{
	Session s;
	s.shell = true;
	s.interactive = isatty(fileno(stdin));
	bool write = false;
	int a = 2;
	for (; a < argc && argv[a][0] == '-'; a++) {
		if (!strcmp(argv[a], "-w"))
			write = true;
		else if (!strcmp(argv[a], "--help") || !strcmp(argv[a], "-h")) {
			printf("usage: synthfs shell [-w] [IMAGE]\nCommands read from stdin, one a line, on IMAGE (open IMAGE "
				"another); -w opens it for writing.\nWith a terminal it prompts; otherwise it stops at the first command "
				"that fails, with its status.\n");
			return 0;
		} else
			return fail(argv[a], "no such option (synthfs shell --help)"), 2;
	}
	if (a + 1 < argc)
		return fail("shell", "one image at a time"), 2;
	if (a < argc && !s.open(argv[a], write))
		return 1;
	std::string line;
	std::vector<std::string> words;
	int status = 0;
	while (!s.quit) {
		if (s.interactive) {
			printf("synthfs %s%s%s> ", s.im ? base_name(s.path.c_str()) : "", s.im ? ":" : "",
				s.im ? path_of(s.im, s.cwd).c_str() : "");
			fflush(stdout);
		}
		if (!std::getline(std::cin, line)) {
			if (s.interactive)
				printf("\n");
			break;
		}
		std::string err;
		if (!words_of(line, words, err))
			status = fail("synthfs", err);
		else if (words.empty())
			continue;
		else
			status = run(s, words);
		if (status && !s.interactive)
			break;
	}
	if (!s.close() && !status)
		status = 1;
	return s.interactive ? 0 : status;
}

}

int main(int argc, char **argv)
{
	if (argc < 2) {
		print_help(stderr, false);
		return 2;
	}
	const std::string cmd = argv[1];
	if (cmd == "--help" || cmd == "-h" || (cmd == "help" && argc == 2)) {
		print_help(stdout, false);
		return 0;
	}
	if (cmd == "shell")
		return shell(argc, argv);
	Session s;
	return run(s, std::vector<std::string>(argv + 1, argv + argc));
}
