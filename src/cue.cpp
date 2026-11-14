// Copyright 2026 Ian Karlsson
// A compact disc from a cue sheet (docs/formats.md, "Cue sheets")
#include "cue.hpp"

#include "audio.hpp"

#include <cctype>

namespace syfs {

namespace {

constexpr uint32_t RAW = 2352, USER = 2048, FRAMES = RAW / 4;

//! A file the sheet names, whole.
class FileStore final : public Store {
public:
	explicit FileStore(FILE *f) : f_(f)
	{
#ifdef _WIN32
		_fseeki64(f_, 0, SEEK_END);
		size_ = uint64_t(_ftelli64(f_));
#else
		fseeko(f_, 0, SEEK_END);
		size_ = uint64_t(ftello(f_));
#endif
	}
	~FileStore() override { fclose(f_); }
	uint64_t size() const override { return size_; }
	bool read(uint64_t offset, void *buf, size_t n) const override
	{
		if (offset > size_ || n > size_ - offset)
			return false;
#ifdef _WIN32
		if (_fseeki64(f_, int64_t(offset), SEEK_SET))
#else
		if (fseeko(f_, off_t(offset), SEEK_SET))
#endif
			return false;
		return fread(buf, 1, n, f_) == n;
	}

private:
	FILE *f_;
	uint64_t size_ = 0;
};

//! A run of a store's bytes, each pair swapped where the file keeps its values the other way round.
class Slice final : public Store {
public:
	Slice(std::shared_ptr<const Store> under, uint64_t at, uint64_t size, bool swapped = false)
		: under_(std::move(under)), at_(at), size_(size), swapped_(swapped)
	{
	}
	uint64_t size() const override { return size_; }
	bool read(uint64_t offset, void *buf, size_t n) const override
	{
		if (offset > size_ || n > size_ - offset)
			return false;
		if (!swapped_)
			return under_->read(at_ + offset, buf, n);
		// whole pairs read, swapped, and the bytes asked for taken of them
		const uint64_t from = offset & ~uint64_t(1), to = std::min((offset + n + 1) & ~uint64_t(1), size_);
		std::vector<uint8_t> pairs(size_t(to - from) + 1);
		if (!under_->read(at_ + from, pairs.data(), size_t(to - from)))
			return false;
		for (size_t k = 0; k + 1 < pairs.size(); k += 2)
			std::swap(pairs[k], pairs[k + 1]);
		memcpy(buf, pairs.data() + (offset - from), n);
		return true;
	}

private:
	std::shared_ptr<const Store> under_;
	uint64_t at_, size_;
	bool swapped_;
};

size_t store_read(void *user, uint64_t offset, void *buf, size_t n)
{
	auto *s = static_cast<const Store *>(user);
	try {
		if (offset >= s->size())
			return 0;
		n = size_t(std::min<uint64_t>(n, s->size() - offset));
		return s->read(offset, buf, n) ? n : 0;
	} catch (...) {
		return 0;
	}
}

std::string upper(std::string_view s)
{
	std::string u(s);
	for (char &c : u)
		c = char(toupper(uint8_t(c)));
	return u;
}

// a line's next word, or its next quoted run
std::string_view word(std::string_view &line)
{
	while (!line.empty() && isspace(uint8_t(line.front())))
		line.remove_prefix(1);
	if (line.empty())
		return {};
	size_t end;
	std::string_view w;
	if (line.front() == '"') {
		end = line.find('"', 1);
		w = line.substr(1, end == line.npos ? line.npos : end - 1);
		end = end == line.npos ? line.size() : end + 1;
	} else {
		for (end = 0; end < line.size() && !isspace(uint8_t(line[end])); end++) {}
		w = line.substr(0, end);
	}
	line.remove_prefix(end);
	return w;
}

// mm:ss:ff as sectors, or -1
int64_t msf(std::string_view s)
{
	unsigned m, sec, f;
	char end;
	if (sscanf(std::string(s).c_str(), "%u:%u:%u%c", &m, &sec, &f, &end) != 3 || sec > 59 || f > 74)
		return -1;
	return (int64_t(m) * 60 + sec) * 75 + f;
}

}

struct CueDisc {
	struct File {
		std::shared_ptr<const Store> store;  //!< Its sectors: a WAV's frames, a file's bytes.
		uint32_t sector = 0;                 //!< Bytes a sector, once a track tells.
		uint32_t sectors = 0;
	};
	struct Track {
		int number = 0;
		bool audio = false, emphasis = false;
		size_t file = 0;
		int64_t index0 = -1, index1 = -1;    //!< In its file.
		uint32_t pregap = 0;                 //!< Silence before it that no file holds.
		std::string title;
		uint32_t start = 0, sectors = 0;     //!< On the disc: from its index 1 to the next track's.
		uint32_t gap = 0;                    //!< The sectors before its start that count down to it.
		uint32_t in_file = 0;                //!< Its sectors from index 1 that its own file holds, before the next track.
	};
	//! Sectors of the disc in order: a file's, or silence.
	struct Run {
		uint32_t lba, sectors;
		size_t file;  //!< NONE: silence.
		uint32_t at;
	};
	std::vector<File> files;
	std::vector<Track> tracks;
	std::vector<Run> runs;
	uint32_t total = 0;
	std::string title;
	size_t data = NONE;                       //!< The first data track.
	std::shared_ptr<const Store> top;         //!< What the disc is opened over.

	bool parse(std::string_view text, const std::string &dir, std::string &why);
	bool lay(std::string &why);
	bool read(uint64_t offset, void *buf, size_t n) const;
};

//! The whole disc as raw sectors, where no track holds data: the audio as it lies, zeros elsewhere.
class DiscStore final : public Store {
public:
	explicit DiscStore(const CueDisc &d) : d_(d) {}
	uint64_t size() const override { return uint64_t(d_.total) * RAW; }
	bool read(uint64_t offset, void *buf, size_t n) const override { return d_.read(offset, buf, n); }

private:
	const CueDisc &d_;
};

bool CueDisc::read(uint64_t offset, void *buf, size_t n) const
{
	if (offset > uint64_t(total) * RAW || n > uint64_t(total) * RAW - offset)
		return false;
	auto *out = static_cast<uint8_t *>(buf);
	auto run = std::upper_bound(runs.begin(), runs.end(), offset / RAW,
		[](uint64_t lba, const Run &r) { return lba < r.lba; });
	if (run != runs.begin())
		run--;
	for (; n && run != runs.end(); run++) {
		const uint64_t from = uint64_t(run->lba) * RAW, to = from + uint64_t(run->sectors) * RAW;
		if (offset >= to)
			continue;
		const size_t k = size_t(std::min<uint64_t>(n, to - offset));
		if (run->file == NONE || files[run->file].sector != RAW)
			memset(out, 0, k);
		else if (!files[run->file].store->read(uint64_t(run->at) * RAW + (offset - from), out, k))
			return false;
		out += k;
		offset += k;
		n -= k;
	}
	return !n;
}

bool CueDisc::parse(std::string_view text, const std::string &dir, std::string &why)
{
	if (text.substr(0, 3) == "\xEF\xBB\xBF")
		text.remove_prefix(3);
	while (!text.empty()) {
		size_t eol = text.find_first_of("\r\n");
		std::string_view line = text.substr(0, eol);
		text.remove_prefix(eol == text.npos ? text.size() : eol + 1);
		const std::string key = upper(word(line));
		Track *t = tracks.empty() ? nullptr : &tracks.back();
		if (key == "FILE") {
			const std::string name(word(line)), kind = upper(word(line));
			if (kind != "BINARY" && kind != "MOTOROLA" && kind != "WAVE" && kind != "AIFF")
				return why = "the sheet names a file of a kind that is not read: " + kind, false;
			// as the sheet names it, else by its last part: sheets carry the paths of where they were written
			const size_t cut = name.find_last_of("/\\");
			FILE *f = fopen((dir + "/" + name).c_str(), "rb");
			if (!f && cut != name.npos)
				f = fopen((dir + "/" + name.substr(cut + 1)).c_str(), "rb");
			if (!f)
				return why = "a file the sheet names cannot be opened: " + name, false;
			std::shared_ptr<const Store> whole = std::make_shared<FileStore>(f);
			File file;
			if (kind == "WAVE" || kind == "AIFF") {
				syfs::Sample s = blank_sample();
				AudioLayout lay;
				auto read = [&](uint64_t offset, void *buf, size_t n) { return whole->read(offset, buf, n); };
				if (!audio_sample(read, whole->size(), s, &lay) || lay.code != AudioLayout::SIGNED || lay.width != 2
					|| lay.block != 4 || s.rate != 44100)
					return why = "an audio file the sheet names is not 16-bit stereo at 44.1 kHz: " + name, false;
				file.store = std::make_shared<Slice>(whole, lay.data, s.frames * 4, lay.big);
			} else
				file.store = std::make_shared<Slice>(whole, 0, whole->size(), kind == "MOTOROLA");
			files.push_back(std::move(file));
		} else if (key == "TRACK") {
			if (files.empty())
				return why = "the sheet has a track before any file", false;
			Track n;
			n.number = atoi(std::string(word(line)).c_str());
			const std::string mode = upper(word(line));
			n.audio = mode == "AUDIO";
			const uint32_t sector = n.audio || mode == "MODE1/2352" || mode == "MODE2/2352" ? RAW
			                      : mode == "MODE1/2048" ? USER : 0;
			if (!sector || n.number < 1 || n.number > 99)
				return why = "the sheet has a track of a mode that is not read: " + mode, false;
			File &f = files.back();
			if (f.sector && f.sector != sector)
				return why = "a file's tracks are of two sector sizes", false;
			f.sector = sector;
			f.sectors = uint32_t(std::min<uint64_t>(f.store->size() / sector, UINT32_MAX));
			n.file = files.size() - 1;
			if (!n.audio && data == NONE)
				data = tracks.size();
			tracks.push_back(std::move(n));
		} else if (key == "INDEX" && t) {
			const int which = atoi(std::string(word(line)).c_str());
			const int64_t at = msf(word(line));
			if (at < 0)
				return why = "the sheet has an index that cannot be read", false;
			if (which == 0)
				t->index0 = at;
			else if (which == 1)
				t->index1 = at;
		} else if (key == "PREGAP" && t) {
			const int64_t gap = msf(word(line));
			if (gap < 0)
				return why = "the sheet has a pregap that cannot be read", false;
			t->pregap = uint32_t(gap);
		} else if (key == "FLAGS" && t) {
			for (std::string_view w = word(line); !w.empty(); w = word(line))
				t->emphasis |= upper(w) == "PRE";
		} else if (key == "TITLE")
			(t ? t->title : title) = std::string(word(line));
	}
	if (tracks.empty())
		return why = "the sheet has no track", false;
	return lay(why);
}

bool CueDisc::lay(std::string &why)
{
	uint64_t lba = 0;
	size_t file = NONE;
	uint32_t cursor = 0;
	auto flush = [&](uint32_t upto) {
		if (file != NONE && upto > cursor) {
			runs.push_back({ uint32_t(lba), upto - cursor, file, cursor });
			lba += upto - cursor;
			cursor = upto;
		}
	};
	for (Track &t : tracks) {
		if (t.file != file) {
			if (file != NONE)
				flush(files[file].sectors);
			file = t.file;
			cursor = 0;
		}
		const int64_t first = t.index0 >= 0 ? t.index0 : t.index1;
		if (t.index1 < first || first < cursor || t.index1 > files[file].sectors)
			return why = "a track's indexes are not within its file, in order", false;
		flush(uint32_t(first));
		if (t.pregap) {
			runs.push_back({ uint32_t(lba), t.pregap, NONE, 0 });
			lba += t.pregap;
		}
		t.gap = t.pregap + uint32_t(t.index1 - first);
		t.start = uint32_t(lba + uint64_t(t.index1 - first));
		t.in_file = files[file].sectors - uint32_t(t.index1);
	}
	flush(files[file].sectors);
	if (lba > 100 * 60 * 75)
		return why = "the sheet's disc is longer than a compact disc", false;
	total = uint32_t(lba);
	for (size_t i = 0; i < tracks.size(); i++) {
		Track &t = tracks[i];
		t.sectors = (i + 1 < tracks.size() ? tracks[i + 1].start : total) - t.start;
		if (i + 1 < tracks.size() && tracks[i + 1].file == t.file) {
			const Track &n = tracks[i + 1];
			t.in_file = uint32_t((n.index0 >= 0 ? n.index0 : n.index1) - t.index1);
		}
	}
	return true;
}

bool Cue::is_sheet(std::string_view text)
{
	if (text.find('\0') != text.npos)
		return false;
	bool file = false, track = false;
	while (!text.empty()) {
		size_t eol = text.find_first_of("\r\n");
		std::string_view line = text.substr(0, eol);
		text.remove_prefix(eol == text.npos ? text.size() : eol + 1);
		const std::string key = upper(word(line));
		file |= key == "FILE";
		track |= file && key == "TRACK";
	}
	return track;
}

std::unique_ptr<Medium> Cue::open(std::string_view text, const std::string &dir, const Volume &volume, std::string &why)
{
	auto disc = std::make_shared<CueDisc>();
	if (!disc->parse(text, dir, why))
		return nullptr;
	// opened over the data track as its file holds it, or over the disc's audio
	if (disc->data != NONE) {
		const CueDisc::Track &t = disc->tracks[disc->data];
		const CueDisc::File &f = disc->files[t.file];
		disc->top = std::make_shared<Slice>(f.store, uint64_t(t.index1) * f.sector, uint64_t(t.in_file) * f.sector);
	} else
		disc->top = std::make_shared<DiscStore>(*disc);
	const syfs_io io { const_cast<Store *>(disc->top.get()), store_read, nullptr, disc->top->size() };
	return std::make_unique<Cue>(std::move(disc), io, volume);
}

Cue::Cue(std::shared_ptr<const CueDisc> disc, const syfs_io &io, const Volume &volume)
	: Medium(io, SYFS_FORMAT_CDDA), disc_(std::move(disc))
{
	if (disc_->data != NONE && volume)
		volume_ = volume(io);
	medium = volume_ ? volume_->medium : syfs_key(SYFS_MEDIUM_DISK);
	const bool audio = std::any_of(disc_->tracks.begin(), disc_->tracks.end(), [](const auto &t) { return t.audio; });
	format = volume_ ? volume_->format : audio ? syfs_key(SYFS_FORMAT_CDDA) : syfs_key(SYFS_NONE);
}

void Cue::identify()
{
	if (volume_) {
		volume_->identify();
		props = volume_->props;
		label = volume_->label;
		label_known = volume_->label_known;
	} else {
		label = disc_->title;
		label_known = true;
	}
	set(SYFS_PROP_SECTORS, disc_->total);
}

void Cue::list()
{
	if (volume_) {
		volume_->list();
		entries = volume_->entries;
		label = volume_->label;
	}
	held_ = entries.size();
	where.assign(held_, 0);
	for (size_t k = 0; k < disc_->tracks.size(); k++) {
		const CueDisc::Track &t = disc_->tracks[k];
		char name[16];
		snprintf(name, sizeof name, "Track %02d", t.number);
		// the first data track is read as its user data, the image this is opened over
		const uint64_t bytes = t.audio ? uint64_t(t.sectors) * RAW : k == disc_->data ? image.size() : 0;
		add(SYFS_TYPE_TRACK, t.audio ? syfs_key(SYFS_FORMAT_CDDA) : format == SYFS_FORMAT_CDDA ? syfs_key(SYFS_NONE) : format,
			uint32_t(t.number - 1), NONE, t.title.empty() ? name : t.title, bytes, 0);
	}
}

std::vector<size_t> Cue::refs(size_t i) const
{
	return i < held_ ? volume_->refs(i) : std::vector<size_t> {};
}

size_t Cue::sample_at(size_t i, int key) const
{
	return i < held_ ? volume_->sample_at(i, key) : Medium::sample_at(i, key);
}

bool Cue::sample(size_t i, syfs::Sample &s) const
{
	if (i < held_)
		return volume_->sample(i, s);
	const CueDisc::Track &t = disc_->tracks[i - held_];
	if (!t.audio)
		return false;
	s.rate = 44100;
	s.channels = 2;
	s.frames = s.end = uint64_t(t.sectors) * FRAMES;
	return true;
}

size_t Cue::wave(size_t i, const syfs::Sample &s, uint64_t first, int16_t *out, size_t frames) const
{
	if (i < held_)
		return volume_->wave(i, s, first, out, frames);
	const CueDisc::Track &t = disc_->tracks[i - held_];
	uint8_t buf[RAW];
	size_t done = 0;
	while (done < frames) {
		const size_t k = std::min(frames - done, sizeof buf / 4);
		if (!disc_->read(uint64_t(t.start) * RAW + (first + done) * 4, buf, k * 4))
			break;
		for (size_t j = 0; j < 2 * k; j++)
			out[2 * done + j] = int16_t(le16(buf + 2 * j));
		done += k;
	}
	return done;
}

uint64_t Cue::raw_size(size_t i) const
{
	return i < held_ ? volume_->raw_size(i) : entries[i].bytes;
}

bool Cue::raw(size_t i, uint64_t offset, void *buf, size_t n) const
{
	if (i < held_)
		return volume_->raw(i, offset, buf, n);
	const CueDisc::Track &t = disc_->tracks[i - held_];
	if (offset > entries[i].bytes || n > entries[i].bytes - offset)
		return false;
	return t.audio ? disc_->read(uint64_t(t.start) * RAW + offset, buf, n) : image.read(offset, buf, n);
}

bool Cue::number(size_t i, syfs_key key, int64_t &value) const
{
	if (i < held_)
		return volume_->number(i, key, value);
	const CueDisc::Track &t = disc_->tracks[i - held_];
	switch (key) {
	case SYFS_PROP_SECTOR: value = t.start; return true;
	case SYFS_PROP_SECTORS: value = t.sectors; return true;
	case SYFS_PROP_PREGAP: value = t.gap; return true;
	case SYFS_PROP_EMPHASIS: value = t.emphasis; return t.audio;
	default: return false;
	}
}

}
