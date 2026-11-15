// Made by tools/keys.py from src/keys.def: not edited by hand.

//! \file syfs_keys.h
//! Every key the library knows, by its constant.
/*!
 * A key is the low 31 bits of its name's 32-bit FNV-1a hash, so a name's key never changes and a host may hash a
 * name itself (syfs_key_of does).  Names of two classes that read the same share their key.
 */
#ifndef SYFS_KEYS_H
#define SYFS_KEYS_H

//! The keys: SYFS_<CLASS>_<NAME>, each with its string.
enum syfs_keys {
	SYFS_NONE = 0,                            //!< None or unknown.

	SYFS_FORMAT_S550 = 0x30dc8af0,            //!< "s550"
	SYFS_FORMAT_S760 = 0x6cbd8f31,            //!< "s760"
	SYFS_FORMAT_S770 = 0x66bb4728,            //!< "s770"
	SYFS_FORMAT_AKAI_S1000 = 0x7e7466be,      //!< "akai-s1000"
	SYFS_FORMAT_AKAI_S3000 = 0x38189f9c,      //!< "akai-s3000"
	SYFS_FORMAT_ISO9660 = 0x15604137,         //!< "iso9660"
	SYFS_FORMAT_EMU3 = 0x6042a98f,            //!< "emu3"
	SYFS_FORMAT_EMAX2 = 0x46abb994,           //!< "emax2"
	SYFS_FORMAT_S330 = 0x2329cc80,            //!< "s330"
	SYFS_FORMAT_W30 = 0x6918543d,             //!< "w30"
	SYFS_FORMAT_S50 = 0x09afe73f,             //!< "s50"
	SYFS_FORMAT_DJ70 = 0x1e6c7adc,            //!< "dj70"
	SYFS_FORMAT_SRJV80 = 0x06723c80,          //!< "srjv80"
	SYFS_FORMAT_SRX = 0x483cd078,             //!< "srx"
	SYFS_FORMAT_AKAI_S900 = 0x76a6c4c2,       //!< "akai-s900"
	SYFS_FORMAT_FAT = 0x3ef81d14,             //!< "fat"
	SYFS_FORMAT_JV80 = 0x254877a1,            //!< "jv80"
	SYFS_FORMAT_JV1080 = 0x3d5fb978,          //!< "jv1080"
	SYFS_FORMAT_CDDA = 0x6ef71f89,            //!< "cdda"

	SYFS_MEDIUM_DISK = 0x456040a4,            //!< "disk"
	SYFS_MEDIUM_FLOPPY = 0x5e75f89b,          //!< "floppy"
	SYFS_MEDIUM_CARD = 0x0827595f,            //!< "card"
	SYFS_MEDIUM_BOARD = 0x6f92fe2b,           //!< "board"
	SYFS_MEDIUM_SMARTMEDIA = 0x4e2c2132,      //!< "smartmedia"

	SYFS_TYPE_IMAGE = 0x335135fa,             //!< "image"
	SYFS_TYPE_PARTITION = 0x59095409,         //!< "partition"
	SYFS_TYPE_VOLUME = 0x2ee0698f,            //!< "volume"
	SYFS_TYPE_PERFORMANCE = 0x2ece8ca5,       //!< "performance"
	SYFS_TYPE_PATCH = 0x79100aa9,             //!< "patch"
	SYFS_TYPE_PARTIAL = 0x454d769a,           //!< "partial"
	SYFS_TYPE_SAMPLE = 0x16e382a7,            //!< "sample"
	SYFS_TYPE_PROGRAM = 0x3d8466cb,           //!< "program"
	SYFS_TYPE_OTHER = 0x487d8df5,             //!< "other"
	SYFS_TYPE_DIRECTORY = 0x11913588,         //!< "directory"
	SYFS_TYPE_FILE = 0x2aea5743,              //!< "file"
	SYFS_TYPE_AREA = 0x1b0f2144,              //!< "area"
	SYFS_TYPE_TONE = 0x5c714e81,              //!< "tone"
	SYFS_TYPE_MULTI = 0x43d67fb2,             //!< "multi"
	SYFS_TYPE_BANK = 0x3b952e97,              //!< "bank"
	SYFS_TYPE_RHYTHM_SET = 0x34ce407c,        //!< "rhythm-set"
	SYFS_TYPE_WAVE = 0x29f017d4,              //!< "wave"
	SYFS_TYPE_TRACK = 0x7b3d6c3c,             //!< "track"

	SYFS_CONTAINER_RAW = 0x43ff12f1,          //!< "raw"
	SYFS_CONTAINER_VHD = 0x1214ab35,          //!< "vhd"
	SYFS_CONTAINER_CUE = 0x65561300,          //!< "cue"

	SYFS_LOOP_FORWARD = 0x345fa81a,           //!< "forward"
	SYFS_LOOP_ALTERNATE = 0x43f6e25d,         //!< "alternate"

	SYFS_KIND_INT = 0x15e97e5e,               //!< "int"
	SYFS_KIND_STRING = 0x17c16538,            //!< "string"
	SYFS_KIND_KEY = 0x6815c86c,               //!< "key"

	SYFS_PROP_FORMAT = 0x399d8552,            //!< "format"
	SYFS_PROP_MEDIUM = 0x35afd776,            //!< "medium"
	SYFS_PROP_CONTAINER = 0x0daff178,         //!< "container"
	SYFS_PROP_LABEL = 0x769717fd,             //!< "label"
	SYFS_PROP_VOLUME = 0x2ee0698f,            //!< "volume"
	SYFS_PROP_MODEL = 0x308b665a,             //!< "model"
	SYFS_PROP_TITLE = 0x1865b509,             //!< "title"
	SYFS_PROP_ID = 0x37386ae0,                //!< "id"
	SYFS_PROP_DATE = 0x5472dc59,              //!< "date"
	SYFS_PROP_NAME = 0x0d39bde6,              //!< "name"
	SYFS_PROP_TYPE = 0x5127f14d,              //!< "type"
	SYFS_PROP_NUMBER = 0x1bd670a0,            //!< "number"
	SYFS_PROP_SIZE = 0x23a0d95c,              //!< "size"
	SYFS_PROP_TAG = 0x15f72993,               //!< "tag"
	SYFS_PROP_RATE = 0x2e839c67,              //!< "rate"
	SYFS_PROP_CHANNELS = 0x7be86875,          //!< "channels"
	SYFS_PROP_BITS = 0x22622f79,              //!< "bits"
	SYFS_PROP_KEY = 0x6815c86c,               //!< "key"
	SYFS_PROP_FRAMES = 0x7b71324f,            //!< "frames"
	SYFS_PROP_START = 0x652b04df,             //!< "start"
	SYFS_PROP_END = 0x6a8e75aa,               //!< "end"
	SYFS_PROP_LOOP = 0x5def486b,              //!< "loop"
	SYFS_PROP_LOOP_START = 0x6a5d16aa,        //!< "loop-start"
	SYFS_PROP_LOOP_END = 0x0a2e84ff,          //!< "loop-end"
	SYFS_PROP_REVERSE = 0x21506c05,           //!< "reverse"
	SYFS_PROP_MODE = 0x6c6ee012,              //!< "mode"
	SYFS_PROP_EXPORTS = 0x6dac1240,           //!< "exports"
	SYFS_PROP_SECTOR = 0x6691818f,            //!< "sector"
	SYFS_PROP_SECTORS = 0x730f9fb4,           //!< "sectors"
	SYFS_PROP_PREGAP = 0x6713b272,            //!< "pregap"
	SYFS_PROP_EMPHASIS = 0x5ff3a9f7,          //!< "emphasis"
	SYFS_PROP_CATEGORY = 0x4f2f4271,          //!< "category"
	SYFS_PROP_BANK_SELECT = 0x56dc32d6,       //!< "bank-select"
	SYFS_PROP_SHORT_NAME = 0x686ad60b,        //!< "short-name"

	SYFS_REL_CHILDREN = 0x67a9c9d2,           //!< "children"
	SYFS_REL_USES = 0x61786085,               //!< "uses"
	SYFS_REL_ALL = 0x13254bc4,                //!< "all"

	SYFS_AS_S16 = 0x17a6396d,                 //!< "s16"
	SYFS_AS_WAV = 0x13e2bd39,                 //!< "wav"
	SYFS_AS_RAW = 0x43ff12f1,                 //!< "raw"
};

//! Every key once: X(constant, class, name, title).
#define SYFS_KEYS(X) \
	X(SYFS_FORMAT_S550, "format", "s550", "S-550") \
	X(SYFS_FORMAT_S760, "format", "s760", "S-760") \
	X(SYFS_FORMAT_S770, "format", "s770", "S-770") \
	X(SYFS_FORMAT_AKAI_S1000, "format", "akai-s1000", "AKAI S1000") \
	X(SYFS_FORMAT_AKAI_S3000, "format", "akai-s3000", "AKAI S3000") \
	X(SYFS_FORMAT_ISO9660, "format", "iso9660", "ISO 9660") \
	X(SYFS_FORMAT_EMU3, "format", "emu3", "E-mu EIII/E-IV") \
	X(SYFS_FORMAT_EMAX2, "format", "emax2", "E-mu Emax II") \
	X(SYFS_FORMAT_S330, "format", "s330", "S-330") \
	X(SYFS_FORMAT_W30, "format", "w30", "W-30") \
	X(SYFS_FORMAT_S50, "format", "s50", "S-50") \
	X(SYFS_FORMAT_DJ70, "format", "dj70", "DJ-70") \
	X(SYFS_FORMAT_SRJV80, "format", "srjv80", "SR-JV80") \
	X(SYFS_FORMAT_SRX, "format", "srx", "SRX") \
	X(SYFS_FORMAT_AKAI_S900, "format", "akai-s900", "AKAI S900") \
	X(SYFS_FORMAT_FAT, "format", "fat", "FAT") \
	X(SYFS_FORMAT_JV80, "format", "jv80", "JV-80") \
	X(SYFS_FORMAT_JV1080, "format", "jv1080", "JV-1080") \
	X(SYFS_FORMAT_CDDA, "format", "cdda", "Audio CD") \
	X(SYFS_MEDIUM_DISK, "medium", "disk", "disk") \
	X(SYFS_MEDIUM_FLOPPY, "medium", "floppy", "floppy") \
	X(SYFS_MEDIUM_CARD, "medium", "card", "card") \
	X(SYFS_MEDIUM_BOARD, "medium", "board", "board") \
	X(SYFS_MEDIUM_SMARTMEDIA, "medium", "smartmedia", "SmartMedia") \
	X(SYFS_TYPE_IMAGE, "type", "image", "image") \
	X(SYFS_TYPE_PARTITION, "type", "partition", "partition") \
	X(SYFS_TYPE_VOLUME, "type", "volume", "volume") \
	X(SYFS_TYPE_PERFORMANCE, "type", "performance", "performance") \
	X(SYFS_TYPE_PATCH, "type", "patch", "patch") \
	X(SYFS_TYPE_PARTIAL, "type", "partial", "partial") \
	X(SYFS_TYPE_SAMPLE, "type", "sample", "sample") \
	X(SYFS_TYPE_PROGRAM, "type", "program", "program") \
	X(SYFS_TYPE_OTHER, "type", "other", "other") \
	X(SYFS_TYPE_DIRECTORY, "type", "directory", "directory") \
	X(SYFS_TYPE_FILE, "type", "file", "file") \
	X(SYFS_TYPE_AREA, "type", "area", "area") \
	X(SYFS_TYPE_TONE, "type", "tone", "tone") \
	X(SYFS_TYPE_MULTI, "type", "multi", "multi") \
	X(SYFS_TYPE_BANK, "type", "bank", "bank") \
	X(SYFS_TYPE_RHYTHM_SET, "type", "rhythm-set", "rhythm set") \
	X(SYFS_TYPE_WAVE, "type", "wave", "wave") \
	X(SYFS_TYPE_TRACK, "type", "track", "track") \
	X(SYFS_CONTAINER_RAW, "container", "raw", "raw") \
	X(SYFS_CONTAINER_VHD, "container", "vhd", "VHD") \
	X(SYFS_CONTAINER_CUE, "container", "cue", "cue sheet") \
	X(SYFS_LOOP_FORWARD, "loop", "forward", "forward") \
	X(SYFS_LOOP_ALTERNATE, "loop", "alternate", "alternate") \
	X(SYFS_KIND_INT, "kind", "int", "int") \
	X(SYFS_KIND_STRING, "kind", "string", "string") \
	X(SYFS_KIND_KEY, "kind", "key", "key") \
	X(SYFS_PROP_FORMAT, "prop", "format", "format") \
	X(SYFS_PROP_MEDIUM, "prop", "medium", "medium") \
	X(SYFS_PROP_CONTAINER, "prop", "container", "container") \
	X(SYFS_PROP_LABEL, "prop", "label", "label") \
	X(SYFS_PROP_VOLUME, "prop", "volume", "volume") \
	X(SYFS_PROP_MODEL, "prop", "model", "model") \
	X(SYFS_PROP_TITLE, "prop", "title", "title") \
	X(SYFS_PROP_ID, "prop", "id", "id") \
	X(SYFS_PROP_DATE, "prop", "date", "date") \
	X(SYFS_PROP_NAME, "prop", "name", "name") \
	X(SYFS_PROP_TYPE, "prop", "type", "type") \
	X(SYFS_PROP_NUMBER, "prop", "number", "number") \
	X(SYFS_PROP_SIZE, "prop", "size", "size") \
	X(SYFS_PROP_TAG, "prop", "tag", "tag") \
	X(SYFS_PROP_RATE, "prop", "rate", "rate") \
	X(SYFS_PROP_CHANNELS, "prop", "channels", "channels") \
	X(SYFS_PROP_BITS, "prop", "bits", "bits") \
	X(SYFS_PROP_KEY, "prop", "key", "key") \
	X(SYFS_PROP_FRAMES, "prop", "frames", "frames") \
	X(SYFS_PROP_START, "prop", "start", "start") \
	X(SYFS_PROP_END, "prop", "end", "end") \
	X(SYFS_PROP_LOOP, "prop", "loop", "loop") \
	X(SYFS_PROP_LOOP_START, "prop", "loop-start", "loop-start") \
	X(SYFS_PROP_LOOP_END, "prop", "loop-end", "loop-end") \
	X(SYFS_PROP_REVERSE, "prop", "reverse", "reverse") \
	X(SYFS_PROP_MODE, "prop", "mode", "mode") \
	X(SYFS_PROP_EXPORTS, "prop", "exports", "exports") \
	X(SYFS_PROP_SECTOR, "prop", "sector", "sector") \
	X(SYFS_PROP_SECTORS, "prop", "sectors", "sectors") \
	X(SYFS_PROP_PREGAP, "prop", "pregap", "pregap") \
	X(SYFS_PROP_EMPHASIS, "prop", "emphasis", "emphasis") \
	X(SYFS_PROP_CATEGORY, "prop", "category", "category") \
	X(SYFS_PROP_BANK_SELECT, "prop", "bank-select", "bank select") \
	X(SYFS_PROP_SHORT_NAME, "prop", "short-name", "short name") \
	X(SYFS_REL_CHILDREN, "rel", "children", "children") \
	X(SYFS_REL_USES, "rel", "uses", "uses") \
	X(SYFS_REL_ALL, "rel", "all", "all") \
	X(SYFS_AS_S16, "as", "s16", "s16") \
	X(SYFS_AS_WAV, "as", "wav", "WAV") \
	X(SYFS_AS_RAW, "as", "raw", "raw")

#endif
