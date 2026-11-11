// Copyright 2026 Ian Karlsson
// The C interface: keys and their names, the format told, the nodes and their properties over a format's reader
#include "akai/akai.hpp"
#include "emu/emu.hpp"
#include "fat.hpp"
#include "iso9660.hpp"
#include "roland/roland.hpp"

#include <cstdio>
#include <memory>
#include <new>

using namespace syfs;

namespace {

struct Reader {
	syfs_key (*probe)(const Image &im);
	std::unique_ptr<Medium> (*open)(const syfs_io &io, syfs_key format);
	syfs_key (*medium)(const Image &im);
};

syfs_key disk(const Image &) { return SYFS_MEDIUM_DISK; }
syfs_key floppy(const Image &) { return SYFS_MEDIUM_FLOPPY; }

// tried in this order (docs/formats.md, "Telling them apart")
template <class M>
constexpr Reader reader(syfs_key (*medium)(const Image &))
{
	return { M::probe, M::open, medium };
}

constexpr Reader readers[] = {
	reader<roland::S700Disk>(disk),
	reader<roland::S700Floppy>(floppy),
	reader<roland::S550Disk>(disk),
	reader<roland::S550Floppy>(floppy),
	reader<roland::WaveRom>(roland::WaveRom::medium_of),
	reader<akai::S1000Disk>(disk),
	reader<akai::S1000Floppy>(floppy),
	reader<akai::S900>(floppy),
	reader<emu::Disk>(disk),
	reader<Iso9660>(disk),
	reader<Fat>(Fat::medium_of),
};

const Reader *reader_of(const Image &im, syfs_key &format)
{
	for (const Reader &r : readers)
		if ((format = r.probe(im)) != SYFS_NONE)
			return &r;
	return nullptr;
}

struct KeyName {
	syfs_key key;
	const char *cls, *name, *title;
};

constexpr KeyName key_names[] = {
#define X(constant, cls, name, title) { constant, cls, name, title },
	SYFS_KEYS(X)
#undef X
};

const KeyName *known(syfs_key key)
{
	for (const KeyName &k : key_names)
		if (k.key == key)
			return &k;
	return nullptr;
}

void say(char *err, size_t size, const char *what)
{
	if (err && size)
		snprintf(err, size, "%s", what);
}

// a property's value: an integer, a string, or a key (in i)
struct Value {
	syfs_key kind = SYFS_NONE;
	int64_t i = 0;
	std::string s;
};

Value integer(int64_t v) { return { SYFS_KIND_INT, v, {} }; }
Value string(std::string_view v) { return { SYFS_KIND_STRING, 0, std::string(v) }; }
Value key(syfs_key v) { return { SYFS_KIND_KEY, v, {} }; }

constexpr syfs_key root_props[] = { SYFS_PROP_FORMAT, SYFS_PROP_MEDIUM, SYFS_PROP_LABEL };
constexpr syfs_key node_props[] = { SYFS_PROP_NAME, SYFS_PROP_TYPE, SYFS_PROP_NUMBER, SYFS_PROP_SIZE, SYFS_PROP_FORMAT,
	SYFS_PROP_TAG };
constexpr syfs_key wave_props[] = { SYFS_PROP_RATE, SYFS_PROP_CHANNELS, SYFS_PROP_BITS, SYFS_PROP_KEY, SYFS_PROP_FRAMES,
	SYFS_PROP_START, SYFS_PROP_END, SYFS_PROP_LOOP, SYFS_PROP_LOOP_START, SYFS_PROP_LOOP_END, SYFS_PROP_REVERSE,
	SYFS_PROP_MODE };

}

struct syfs_image {
	FILE *file = nullptr;
	std::unique_ptr<Medium> medium;
	bool identified = false, listed = false;
	std::string error;
	// each node's children, the root's first; the types met, in the image's order
	std::vector<std::vector<syfs_node>> kids;
	std::vector<syfs_key> types;
	// the last sample header read
	size_t sample_of = NONE;
	bool sample_ok = false;
	Sample sample {};

	~syfs_image()
	{
		medium.reset();
		if (file)
			fclose(file);
	}

	// what tells the image apart read, once
	void identify()
	{
		if (identified)
			return;
		identified = true;
		try {
			medium->identify();
		} catch (...) {
			medium->props.clear();
		}
	}

	// the lists read, once
	void list()
	{
		if (listed)
			return;
		identify();
		listed = true;
		try {
			medium->list();
		} catch (...) {
			medium->entries.clear();
			error = "the image could not be read";
		}
		const auto &e = medium->entries;
		kids.assign(e.size() + 1, {});
		for (size_t i = 0; i < e.size(); i++) {
			kids[e[i].parent == NONE ? 0 : e[i].parent + 1].push_back(syfs_node(i + 1));
			if (std::find(types.begin(), types.end(), e[i].kind) == types.end())
				types.push_back(e[i].kind);
		}
	}

	// node n's entry once the lists are read, or null for the root and for no node
	const Entry *entry(syfs_node n)
	{
		list();
		return n && n <= medium->entries.size() ? &medium->entries[n - 1] : nullptr;
	}

	bool has(syfs_node n)
	{
		list();
		return n <= medium->entries.size();
	}

	// entry i's sample header, or null where it has none
	const Sample *header(size_t i)
	{
		if (sample_of != i) {
			sample_of = i;
			sample = blank_sample();
			try {
				sample_ok = medium->sample(i, sample);
			} catch (...) {
				sample_ok = false;
			}
			if (sample_ok)
				clamp_points(sample);
		}
		return sample_ok ? &sample : nullptr;
	}

	// node n's property of a key; kind SYFS_NONE where it has none
	Value value(syfs_node n, syfs_key k)
	{
		if (!n) {
			switch (k) {
			case SYFS_PROP_FORMAT: return key(medium->format);
			case SYFS_PROP_MEDIUM: return key(medium->medium);
			case SYFS_PROP_LABEL:
				identify();
				if (!medium->label_known)
					list();
				return string(medium->label);
			default:
				identify();
				for (const Medium::Prop &p : medium->props)
					if (p.key == k)
						return p.text ? string(p.string) : integer(p.number);
				return {};
			}
		}
		const Entry *e = entry(n);
		if (!e)
			return {};
		switch (k) {
		case SYFS_PROP_NAME: return string(e->name);
		case SYFS_PROP_TYPE: return key(e->kind);
		case SYFS_PROP_NUMBER: return integer(e->number);
		case SYFS_PROP_SIZE: return integer(int64_t(e->bytes));
		case SYFS_PROP_FORMAT: return key(e->format);
		case SYFS_PROP_TAG: return e->tag[0] ? string(e->tag) : Value {};
		default: break;
		}
		const Sample *s = header(n - 1);
		if (!s)
			return {};
		switch (k) {
		case SYFS_PROP_RATE: return integer(s->rate);
		case SYFS_PROP_CHANNELS: return integer(s->channels);
		case SYFS_PROP_BITS: return integer(s->bits);
		case SYFS_PROP_KEY: return integer(s->key);
		case SYFS_PROP_FRAMES: return integer(int64_t(s->frames));
		case SYFS_PROP_START: return integer(int64_t(s->start));
		case SYFS_PROP_END: return integer(int64_t(s->end));
		case SYFS_PROP_LOOP: return key(s->loop);
		case SYFS_PROP_LOOP_START: return integer(int64_t(s->loop_start));
		case SYFS_PROP_LOOP_END: return integer(int64_t(s->loop_end));
		case SYFS_PROP_REVERSE: return integer(s->reverse);
		case SYFS_PROP_MODE: return integer(s->mode);
		default: return {};
		}
	}
};

extern "C" {

syfs_key syfs_key_of(const char *name)
{
	uint32_t h = 0x811C9DC5u;
	for (const char *p = name ? name : ""; *p; p++)
		h = (h ^ uint8_t(*p)) * 0x01000193u;
	return h & 0x7FFFFFFFu;
}

const char *syfs_name(syfs_key key)
{
	const KeyName *k = known(key);
	return k ? k->name : key == SYFS_NONE ? "none" : nullptr;
}

const char *syfs_title(syfs_key key)
{
	const KeyName *k = known(key);
	return k ? k->title : key == SYFS_NONE ? "none" : nullptr;
}

syfs_key syfs_format_at(size_t i)
{
	for (const KeyName &k : key_names)
		if (!strcmp(k.cls, "format") && !i--)
			return k.key;
	return SYFS_NONE;
}

syfs_image *syfs_open(const syfs_io *io, char *err, size_t err_size)
{
	if (!io || !io->read)
		return say(err, err_size, "no reader given"), nullptr;
	try {
		Image image(*io);
		uint8_t first[512];
		if (!image.read(0, first, sizeof first))
			return say(err, err_size, "the image is shorter than a sector"), nullptr;
		syfs_key format = SYFS_NONE;
		const Reader *r = reader_of(image, format);
		auto im = std::make_unique<syfs_image>();
		im->medium = r ? r->open(*io, format) : std::make_unique<Medium>(*io, SYFS_NONE);
		if (r)
			im->medium->medium = r->medium(image);
		if (image.card())
			im->medium->medium = SYFS_MEDIUM_SMARTMEDIA;
		return im.release();
	} catch (const std::bad_alloc &) {
		return say(err, err_size, "out of memory"), nullptr;
	} catch (...) {
		return say(err, err_size, "the image could not be read"), nullptr;
	}
}

static size_t file_read(void *user, uint64_t offset, void *buf, size_t n)
{
	FILE *f = static_cast<FILE *>(user);
#ifdef _WIN32
	if (_fseeki64(f, int64_t(offset), SEEK_SET))
#else
	if (fseeko(f, off_t(offset), SEEK_SET))
#endif
		return 0;
	return fread(buf, 1, n, f);
}

syfs_image *syfs_open_path(const char *path, char *err, size_t err_size)
{
	FILE *f = path ? fopen(path, "rb") : nullptr;
	if (!f)
		return say(err, err_size, "the file cannot be opened"), nullptr;
	syfs_io io { f, file_read, nullptr, 0 };
#ifdef _WIN32
	_fseeki64(f, 0, SEEK_END);
	io.size = uint64_t(_ftelli64(f));
#else
	fseeko(f, 0, SEEK_END);
	io.size = uint64_t(ftello(f));
#endif
	syfs_image *im = syfs_open(&io, err, err_size);
	if (!im) {
		fclose(f);
		return nullptr;
	}
	im->file = f;
	return im;
}

void syfs_close(syfs_image *im)
{
	delete im;
}

const char *syfs_error(const syfs_image *im)
{
	return im ? im->error.c_str() : "";
}

int syfs_create(const syfs_io *io, syfs_key format, syfs_key medium, const char *label, char *err, size_t err_size)
{
	if (!io || !io->write)
		return say(err, err_size, "no writer given"), 0;
	if (medium == SYFS_NONE)
		medium = io->size == 720 * 1024 || io->size == 1440 * 1024 ? SYFS_MEDIUM_FLOPPY : SYFS_MEDIUM_DISK;
	if (medium == SYFS_MEDIUM_SMARTMEDIA && format == SYFS_NONE) {
		std::string why;
		if (SmartMedia::blank(*io, why))
			return 1;
		return say(err, err_size, why.c_str()), 0;
	}
	bool is_floppy = medium == SYFS_MEDIUM_FLOPPY;
	if (!is_floppy && medium != SYFS_MEDIUM_DISK)
		return say(err, err_size, "a medium that cannot be made"), 0;
	bool labelled = label && *label;
	if (labelled && format != SYFS_FORMAT_FAT)
		return say(err, err_size, "the format holds no label"), 0;
	try {
		std::string why;
		bool ok;
		switch (format) {
		case SYFS_FORMAT_FAT:
			ok = (is_floppy ? fat::format_floppy : fat::format_disk)(*io, label, why);
			break;
		case SYFS_FORMAT_S760:
			ok = (is_floppy ? roland::s700::format_s760_floppy : roland::s700::format_s760_disk)(*io, why);
			break;
		case SYFS_FORMAT_S770:
			ok = (is_floppy ? roland::s700::format_s770_floppy : roland::s700::format_s770_disk)(*io, why);
			break;
		default:
			return say(err, err_size, "a format that cannot be made"), 0;
		}
		if (ok)
			return 1;
		say(err, err_size, why.c_str());
	} catch (...) {
		say(err, err_size, "the image could not be written");
	}
	return 0;
}

int syfs_node_info(syfs_image *im, syfs_node n, syfs_info *out)
{
	if (!im || !out)
		return 0;
	im->error.clear();
	if (!n) {
		im->list();
		*out = { SYFS_NO_NODE, SYFS_TYPE_IMAGE, im->medium->label.c_str(), 0, im->medium->image.size() };
		return 1;
	}
	const Entry *e = im->entry(n);
	if (!e)
		return 0;
	*out = { e->parent == NONE ? SYFS_ROOT : syfs_node(e->parent + 1), e->kind, e->name, e->number, e->bytes };
	return 1;
}

syfs_key syfs_type_at(syfs_image *im, size_t i)
{
	if (!im)
		return SYFS_NONE;
	im->list();
	return i < im->types.size() ? im->types[i] : syfs_key(SYFS_NONE);
}

size_t syfs_list(syfs_image *im, syfs_node n, syfs_key relation, syfs_node *out, size_t max)
{
	if (!im || !im->has(n))
		return 0;
	im->error.clear();
	size_t count = 0;
	auto put = [&](syfs_node v) {
		if (out && count < max)
			out[count] = v;
		count++;
	};
	const auto &entries = im->medium->entries;
	switch (relation) {
	case SYFS_REL_CHILDREN:
		for (syfs_node c : im->kids[n])
			put(c);
		break;
	case SYFS_REL_ALL:
		if (!n) {
			for (size_t i = 0; i < entries.size(); i++)
				put(syfs_node(i + 1));
		} else {
			// an entry comes after the one that holds it
			std::vector<bool> under(entries.size(), false);
			under[n - 1] = true;
			for (size_t i = n; i < entries.size(); i++)
				if (entries[i].parent != NONE && under[entries[i].parent]) {
					under[i] = true;
					put(syfs_node(i + 1));
				}
		}
		break;
	case SYFS_REL_USES:
		if (n)
			try {
				for (size_t r : im->medium->refs(n - 1))
					put(syfs_node(r + 1));
			} catch (...) {
				count = 0;
			}
		break;
	default:
		break;
	}
	return count;
}

syfs_node syfs_at_key(syfs_image *im, syfs_node n, int key)
{
	if (!im || !im->entry(n) || key < 0 || key > 127)
		return SYFS_NO_NODE;
	try {
		size_t i = im->medium->sample_at(n - 1, key);
		return i == NONE ? SYFS_NO_NODE : syfs_node(i + 1);
	} catch (...) {
		return SYFS_NO_NODE;
	}
}

int syfs_prop_at(syfs_image *im, syfs_node n, size_t i, syfs_prop *out)
{
	if (!im || !out || !im->has(n))
		return 0;
	auto take = [&](const syfs_key *keys, size_t count) {
		for (size_t k = 0; k < count; k++) {
			Value v = im->value(n, keys[k]);
			if (v.kind != SYFS_NONE && !i--) {
				*out = { keys[k], v.kind, 0 };
				return true;
			}
		}
		return false;
	};
	if (!n) {
		if (take(root_props, std::size(root_props)))
			return 1;
		im->identify();
		if (i >= im->medium->props.size())
			return 0;
		const Medium::Prop &p = im->medium->props[i];
		*out = { p.key, syfs_key(p.text ? SYFS_KIND_STRING : SYFS_KIND_INT), 0 };
		return 1;
	}
	return take(node_props, std::size(node_props)) || take(wave_props, std::size(wave_props));
}

int syfs_get_int(syfs_image *im, syfs_node n, syfs_key key, int64_t *value)
{
	if (!im || !value)
		return 0;
	Value v = im->value(n, key);
	if (v.kind != SYFS_KIND_INT && v.kind != SYFS_KIND_KEY)
		return 0;
	*value = v.i;
	return 1;
}

int syfs_get_str(syfs_image *im, syfs_node n, syfs_key key, char *buf, size_t size)
{
	if (!im || !buf || !size)
		return 0;
	Value v = im->value(n, key);
	if (v.kind == SYFS_KIND_KEY) {
		const char *name = syfs_name(syfs_key(v.i));
		v.s = name ? name : "";
	} else if (v.kind != SYFS_KIND_STRING)
		return 0;
	snprintf(buf, size, "%s", v.s.c_str());
	return 1;
}

int syfs_set_int(syfs_image *im, syfs_node, syfs_key, int64_t)
{
	if (im)
		im->error = "the property cannot be written";
	return 0;
}

int syfs_set_str(syfs_image *im, syfs_node, syfs_key, const char *)
{
	if (im)
		im->error = "the property cannot be written";
	return 0;
}

size_t syfs_read(syfs_image *im, syfs_node n, syfs_key as, uint64_t at, void *buf, size_t size)
{
	if (!im || !buf || as != SYFS_AS_S16 || !im->entry(n))
		return 0;
	const Sample *s = im->header(n - 1);
	if (!s || !s->channels)
		return 0;
	size_t frame = s->channels * sizeof(int16_t);
	uint64_t first = at / frame;
	size_t frames = size / frame;
	if (at % frame || first >= s->frames || !frames)
		return 0;
	try {
		Sample header = *s;
		return frame * im->medium->wave(n - 1, header, first, static_cast<int16_t *>(buf),
			size_t(std::min<uint64_t>(frames, header.frames - first)));
	} catch (...) {
		return 0;
	}
}

}
