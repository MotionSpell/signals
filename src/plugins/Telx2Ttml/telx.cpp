// Parse raw teletext data, and produce 'Page' objects
#include <cstdint>
#include <cstring> // memset
#include <memory>
#include "telx.hpp"
#include "lib_utils/format.hpp"
#include "lib_utils/log_sink.hpp" // Warning

namespace {

enum Bool { //awkward type required by the spec states
	Undef = 255,
	No = 0,
	Yes = 1,
};

struct PrimaryCharset {
	uint8_t current;
	uint8_t G0_M29;
	uint8_t G0_X28;
};

struct State {
	Bool progInfoProcessed;
};

enum DataUnit {
	NonSubtitle = 0x02,
	Subtitle = 0x03,
	Inverted = 0x0c,
	DataUnitVPS = 0xc3,
	DataUnitClosedCaptions = 0xc5
};

struct Payload {
	uint8_t unused_clock_in;
	uint8_t unused_framing_code;
	uint8_t address[2];
	uint8_t data[40];
};

uint8_t unham_8_4(uint8_t a) {
	static const uint8_t UnHam_8_4[256] = {
		0x01, 0xff, 0x01, 0x01, 0xff, 0x00, 0x01, 0xff, 0xff, 0x02, 0x01, 0xff, 0x0a, 0xff, 0xff, 0x07,
		0xff, 0x00, 0x01, 0xff, 0x00, 0x00, 0xff, 0x00, 0x06, 0xff, 0xff, 0x0b, 0xff, 0x00, 0x03, 0xff,
		0xff, 0x0c, 0x01, 0xff, 0x04, 0xff, 0xff, 0x07, 0x06, 0xff, 0xff, 0x07, 0xff, 0x07, 0x07, 0x07,
		0x06, 0xff, 0xff, 0x05, 0xff, 0x00, 0x0d, 0xff, 0x06, 0x06, 0x06, 0xff, 0x06, 0xff, 0xff, 0x07,
		0xff, 0x02, 0x01, 0xff, 0x04, 0xff, 0xff, 0x09, 0x02, 0x02, 0xff, 0x02, 0xff, 0x02, 0x03, 0xff,
		0x08, 0xff, 0xff, 0x05, 0xff, 0x00, 0x03, 0xff, 0xff, 0x02, 0x03, 0xff, 0x03, 0xff, 0x03, 0x03,
		0x04, 0xff, 0xff, 0x05, 0x04, 0x04, 0x04, 0xff, 0xff, 0x02, 0x0f, 0xff, 0x04, 0xff, 0xff, 0x07,
		0xff, 0x05, 0x05, 0x05, 0x04, 0xff, 0xff, 0x05, 0x06, 0xff, 0xff, 0x05, 0xff, 0x0e, 0x03, 0xff,
		0xff, 0x0c, 0x01, 0xff, 0x0a, 0xff, 0xff, 0x09, 0x0a, 0xff, 0xff, 0x0b, 0x0a, 0x0a, 0x0a, 0xff,
		0x08, 0xff, 0xff, 0x0b, 0xff, 0x00, 0x0d, 0xff, 0xff, 0x0b, 0x0b, 0x0b, 0x0a, 0xff, 0xff, 0x0b,
		0x0c, 0x0c, 0xff, 0x0c, 0xff, 0x0c, 0x0d, 0xff, 0xff, 0x0c, 0x0f, 0xff, 0x0a, 0xff, 0xff, 0x07,
		0xff, 0x0c, 0x0d, 0xff, 0x0d, 0xff, 0x0d, 0x0d, 0x06, 0xff, 0xff, 0x0b, 0xff, 0x0e, 0x0d, 0xff,
		0x08, 0xff, 0xff, 0x09, 0xff, 0x09, 0x09, 0x09, 0xff, 0x02, 0x0f, 0xff, 0x0a, 0xff, 0xff, 0x09,
		0x08, 0x08, 0x08, 0xff, 0x08, 0xff, 0xff, 0x09, 0x08, 0xff, 0xff, 0x0b, 0xff, 0x0e, 0x03, 0xff,
		0xff, 0x0c, 0x0f, 0xff, 0x04, 0xff, 0xff, 0x09, 0x0f, 0xff, 0x0f, 0x0f, 0xff, 0x0e, 0x0f, 0xff,
		0x08, 0xff, 0xff, 0x05, 0xff, 0x0e, 0x0d, 0xff, 0xff, 0x0e, 0x0f, 0xff, 0x0e, 0x0e, 0xff, 0x0e
	};

	uint8_t val = UnHam_8_4[a];
	if (val == 0xff) {
		val = 0;
		// g_Log->log(Warning, format("Teletext: unrecoverable data error (4): %s\n", a).c_str());
	}
	return (val & 0x0f);
}

#define MAGAZINE(telx_page) ((telx_page >> 8) & 0xf)
#define PAGE(telx_page) (telx_page & 0xff)

uint32_t unham_24_18(uint32_t a) {
	// Section 8.3
	uint8_t test = 0;
	for (uint8_t i = 0; i < 23; i++) {
		test ^= ((a >> i) & 0x01) * (i + 33);
	}
	test ^= ((a >> 23) & 0x01) * 32;

	if ((test & 0x1f) != 0x1f) {
		if ((test & 0x20) == 0x20) {
			return 0xffffffff;
		}
		a ^= 1 << (30 - test);
	}

	return (a & 0x000004) >> 2 | (a & 0x000070) >> 3 | (a & 0x007f00) >> 4 | (a & 0x7f0000) >> 5;
}

void ucs2_to_utf8(char *out, uint16_t in) {
	if (in < 0x80) {
		out[0] = in & 0x7f;
		out[1] = 0;
		out[2] = 0;
		out[3] = 0;
	} else if (in < 0x800) {
		out[0] = (in >> 6) | 0xc0;
		out[1] = (in & 0x3f) | 0x80;
		out[2] = 0;
		out[3] = 0;
	} else {
		out[0] = (in >> 12) | 0xe0;
		out[1] = ((in >> 6) & 0x3f) | 0x80;
		out[2] = (in & 0x3f) | 0x80;
		out[3] = 0;
	}
}

enum TransmissionMode {
	Parallel = 0,
	Serial = 1
};

static const int COLS = 40;
static const int ROWS = 25;

struct PageBuffer {
	uint64_t showTimestamp;
	uint64_t hideTimestamp;
	uint16_t text[ROWS][COLS];
	bool tainted; // 1 = text variable contains any data
};

}
namespace {

//most of these tables are taken from the spec or copied from other telx programs (mostly forks of telxcc)
//please note that most tables are still incomplete for a worldwide distribution (but teletext might not be deployed there)

const uint8_t Parity8[256] = {
	0x00, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x01, 0x00,
	0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x01,
	0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x01,
	0x00, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x01, 0x00,
	0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x01,
	0x00, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x01, 0x00,
	0x00, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x01, 0x00,
	0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x01,
	0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x01,
	0x00, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x01, 0x00,
	0x00, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x01, 0x00,
	0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x01,
	0x00, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x01, 0x00,
	0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x01,
	0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x01,
	0x00, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x01, 0x00
};

const uint8_t Reverse8[256] = {
	0x00, 0x80, 0x40, 0xc0, 0x20, 0xa0, 0x60, 0xe0, 0x10, 0x90, 0x50, 0xd0, 0x30, 0xb0, 0x70, 0xf0,
	0x08, 0x88, 0x48, 0xc8, 0x28, 0xa8, 0x68, 0xe8, 0x18, 0x98, 0x58, 0xd8, 0x38, 0xb8, 0x78, 0xf8,
	0x04, 0x84, 0x44, 0xc4, 0x24, 0xa4, 0x64, 0xe4, 0x14, 0x94, 0x54, 0xd4, 0x34, 0xb4, 0x74, 0xf4,
	0x0c, 0x8c, 0x4c, 0xcc, 0x2c, 0xac, 0x6c, 0xec, 0x1c, 0x9c, 0x5c, 0xdc, 0x3c, 0xbc, 0x7c, 0xfc,
	0x02, 0x82, 0x42, 0xc2, 0x22, 0xa2, 0x62, 0xe2, 0x12, 0x92, 0x52, 0xd2, 0x32, 0xb2, 0x72, 0xf2,
	0x0a, 0x8a, 0x4a, 0xca, 0x2a, 0xaa, 0x6a, 0xea, 0x1a, 0x9a, 0x5a, 0xda, 0x3a, 0xba, 0x7a, 0xfa,
	0x06, 0x86, 0x46, 0xc6, 0x26, 0xa6, 0x66, 0xe6, 0x16, 0x96, 0x56, 0xd6, 0x36, 0xb6, 0x76, 0xf6,
	0x0e, 0x8e, 0x4e, 0xce, 0x2e, 0xae, 0x6e, 0xee, 0x1e, 0x9e, 0x5e, 0xde, 0x3e, 0xbe, 0x7e, 0xfe,
	0x01, 0x81, 0x41, 0xc1, 0x21, 0xa1, 0x61, 0xe1, 0x11, 0x91, 0x51, 0xd1, 0x31, 0xb1, 0x71, 0xf1,
	0x09, 0x89, 0x49, 0xc9, 0x29, 0xa9, 0x69, 0xe9, 0x19, 0x99, 0x59, 0xd9, 0x39, 0xb9, 0x79, 0xf9,
	0x05, 0x85, 0x45, 0xc5, 0x25, 0xa5, 0x65, 0xe5, 0x15, 0x95, 0x55, 0xd5, 0x35, 0xb5, 0x75, 0xf5,
	0x0d, 0x8d, 0x4d, 0xcd, 0x2d, 0xad, 0x6d, 0xed, 0x1d, 0x9d, 0x5d, 0xdd, 0x3d, 0xbd, 0x7d, 0xfd,
	0x03, 0x83, 0x43, 0xc3, 0x23, 0xa3, 0x63, 0xe3, 0x13, 0x93, 0x53, 0xd3, 0x33, 0xb3, 0x73, 0xf3,
	0x0b, 0x8b, 0x4b, 0xcb, 0x2b, 0xab, 0x6b, 0xeb, 0x1b, 0x9b, 0x5b, 0xdb, 0x3b, 0xbb, 0x7b, 0xfb,
	0x07, 0x87, 0x47, 0xc7, 0x27, 0xa7, 0x67, 0xe7, 0x17, 0x97, 0x57, 0xd7, 0x37, 0xb7, 0x77, 0xf7,
	0x0f, 0x8f, 0x4f, 0xcf, 0x2f, 0xaf, 0x6f, 0xef, 0x1f, 0x9f, 0x5f, 0xdf, 0x3f, 0xbf, 0x7f, 0xff
};

enum {
	LATIN,
	CYRILLIC1,
	CYRILLIC2,
	CYRILLIC3,
	GREEK,
	ARABIC,
	HEBREW
};

//array positions where chars from G0_LatinNationalSubsets are injected into G0[LATIN]
const uint8_t G0_LatinNationalSubsetsPositions[13] = {
	0x03, 0x04, 0x20, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40, 0x5b, 0x5c, 0x5d, 0x5e
};

//latin subsets
struct {
	uint16_t characters[13];
} const G0_LatinNationalSubsets[14] = {
	// 0: English,
	{ { 0x00a3, 0x0024, 0x0040, 0x00ab, 0x00bd, 0x00bb, 0x005e, 0x0023, 0x002d, 0x00bc, 0x00a6, 0x00be, 0x00f7 } },
	// 1: French,
	{ { 0x00e9, 0x00ef, 0x00e0, 0x00eb, 0x00ea, 0x00f9, 0x00ee, 0x0023, 0x00e8, 0x00e2, 0x00f4, 0x00fb, 0x00e7 } },
	// 2: Swedish, Finnish, Hungarian,
	{ { 0x0023, 0x00a4, 0x00c9, 0x00c4, 0x00d6, 0x00c5, 0x00dc, 0x005f, 0x00e9, 0x00e4, 0x00f6, 0x00e5, 0x00fc } },
	// 3: Czech, Slovak,
	{ { 0x0023, 0x016f, 0x010d, 0x0165, 0x017e, 0x00fd, 0x00ed, 0x0159, 0x00e9, 0x00e1, 0x011b, 0x00fa, 0x0161 } },
	// 4: German,
	{ { 0x0023, 0x0024, 0x00a7, 0x00c4, 0x00d6, 0x00dc, 0x005e, 0x005f, 0x00b0, 0x00e4, 0x00f6, 0x00fc, 0x00df } },
	// 5: Portuguese, Spanish,
	{ { 0x00e7, 0x0024, 0x00a1, 0x00e1, 0x00e9, 0x00ed, 0x00f3, 0x00fa, 0x00bf, 0x00fc, 0x00f1, 0x00e8, 0x00e0 } },
	// 6: Italian,
	{ { 0x00a3, 0x0024, 0x00e9, 0x00b0, 0x00e7, 0x00bb, 0x005e, 0x0023, 0x00f9, 0x00e0, 0x00f2, 0x00e8, 0x00ec } },
	// 7: Rumanian,
	{ { 0x0023, 0x00a4, 0x0162, 0x00c2, 0x015e, 0x0102, 0x00ce, 0x0131, 0x0163, 0x00e2, 0x015f, 0x0103, 0x00ee } },
	// 8: Polish,
	{ { 0x0023, 0x0144, 0x0105, 0x017b, 0x015a, 0x0141, 0x0107, 0x00f3, 0x0119, 0x017c, 0x015b, 0x0142, 0x017a } },
	// 9: Turkish,
	{ { 0x0054, 0x011f, 0x0130, 0x015e, 0x00d6, 0x00c7, 0x00dc, 0x011e, 0x0131, 0x015f, 0x00f6, 0x00e7, 0x00fc } },
	// a: Serbian, Croatian, Slovenian,
	{ { 0x0023, 0x00cb, 0x010c, 0x0106, 0x017d, 0x0110, 0x0160, 0x00eb, 0x010d, 0x0107, 0x017e, 0x0111, 0x0161 } },
	// b: Estonian,
	{ { 0x0023, 0x00f5, 0x0160, 0x00c4, 0x00d6, 0x017e, 0x00dc, 0x00d5, 0x0161, 0x00e4, 0x00f6, 0x017e, 0x00fc } },
	// c: Lettish, Lithuanian,
	{ { 0x0023, 0x0024, 0x0160, 0x0117, 0x0119, 0x017d, 0x010d, 0x016b, 0x0161, 0x0105, 0x0173, 0x017e, 0x012f } }
};

//references to the G0_LatinNationalSubsets array
const uint8_t G0_LatinNationalSubsetsMap[56] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x01, 0x02, 0x03, 0x04, 0xff, 0x06, 0xff,
	0x00, 0x01, 0x02, 0x09, 0x04, 0x05, 0x06, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0x0a, 0xff, 0x07,
	0xff, 0xff, 0x0b, 0x03, 0x04, 0xff, 0x0c, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0x09, 0xff, 0xff, 0xff, 0xff
};

//G2
const uint16_t G2[1][96] = {
	{
		// Latin G2 Supplementary Set
		0x0020, 0x00a1, 0x00a2, 0x00a3, 0x0024, 0x00a5, 0x0023, 0x00a7, 0x00a4, 0x2018, 0x201c, 0x00ab, 0x2190, 0x2191, 0x2192, 0x2193,
		0x00b0, 0x00b1, 0x00b2, 0x00b3, 0x00d7, 0x00b5, 0x00b6, 0x00b7, 0x00f7, 0x2019, 0x201d, 0x00bb, 0x00bc, 0x00bd, 0x00be, 0x00bf,
		0x0020, 0x0300, 0x0301, 0x0302, 0x0303, 0x0304, 0x0306, 0x0307, 0x0308, 0x0000, 0x030a, 0x0327, 0x005f, 0x030b, 0x0328, 0x030c,
		0x2015, 0x00b9, 0x00ae, 0x00a9, 0x2122, 0x266a, 0x20ac, 0x2030, 0x03B1, 0x0000, 0x0000, 0x0000, 0x215b, 0x215c, 0x215d, 0x215e,
		0x03a9, 0x00c6, 0x0110, 0x00aa, 0x0126, 0x0000, 0x0132, 0x013f, 0x0141, 0x00d8, 0x0152, 0x00ba, 0x00de, 0x0166, 0x014a, 0x0149,
		0x0138, 0x00e6, 0x0111, 0x00f0, 0x0127, 0x0131, 0x0133, 0x0140, 0x0142, 0x00f8, 0x0153, 0x00df, 0x00fe, 0x0167, 0x014b, 0x0020
	}
};

const uint16_t G2_Accents[15][52] = {
	// A B C D E F G H I J K L M N O P Q R S T U V W X Y Z a b c d e f g h i j k l m n o p q r s t u v w x y z
	// grave
	{ 0x00c0, 0x0000, 0x0000, 0x0000, 0x00c8, 0x0000, 0x0000, 0x0000, 0x00cc, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x00d2, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x00d9, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x00e0, 0x0000, 0x0000, 0x0000, 0x00e8, 0x0000, 0x0000, 0x0000, 0x00ec, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x00f2, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x00f9, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000 },
	// acute
	{ 0x00c1, 0x0000, 0x0106, 0x0000, 0x00c9, 0x0000, 0x0000, 0x0000, 0x00cd, 0x0000, 0x0000, 0x0139, 0x0000, 0x0143, 0x00d3, 0x0000, 0x0000, 0x0154, 0x015a, 0x0000, 0x00da, 0x0000, 0x0000, 0x0000, 0x00dd, 0x0179, 0x00e1, 0x0000, 0x0107, 0x0000, 0x00e9, 0x0000, 0x0123, 0x0000, 0x00ed, 0x0000, 0x0000, 0x013a, 0x0000, 0x0144, 0x00f3, 0x0000, 0x0000, 0x0155, 0x015b, 0x0000, 0x00fa, 0x0000, 0x0000, 0x0000, 0x00fd, 0x017a },
	// circumflex
	{ 0x00c2, 0x0000, 0x0108, 0x0000, 0x00ca, 0x0000, 0x011c, 0x0124, 0x00ce, 0x0134, 0x0000, 0x0000, 0x0000, 0x0000, 0x00d4, 0x0000, 0x0000, 0x0000, 0x015c, 0x0000, 0x00db, 0x0000, 0x0174, 0x0000, 0x0176, 0x0000, 0x00e2, 0x0000, 0x0109, 0x0000, 0x00ea, 0x0000, 0x011d, 0x0125, 0x00ee, 0x0135, 0x0000, 0x0000, 0x0000, 0x0000, 0x00f4, 0x0000, 0x0000, 0x0000, 0x015d, 0x0000, 0x00fb, 0x0000, 0x0175, 0x0000, 0x0177, 0x0000 },
	// tilde
	{ 0x00c3, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0128, 0x0000, 0x0000, 0x0000, 0x0000, 0x00d1, 0x00d5, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0168, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x00e3, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0129, 0x0000, 0x0000, 0x0000, 0x0000, 0x00f1, 0x00f5, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0169, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000 },
	// macron
	{ 0x0100, 0x0000, 0x0000, 0x0000, 0x0112, 0x0000, 0x0000, 0x0000, 0x012a, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x014c, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x016a, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0101, 0x0000, 0x0000, 0x0000, 0x0113, 0x0000, 0x0000, 0x0000, 0x012b, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x014d, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x016b, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000 },
	// breve
	{ 0x0102, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x011e, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x016c, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0103, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x011f, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x016d, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000 },
	// dot
	{ 0x0000, 0x0000, 0x010a, 0x0000, 0x0116, 0x0000, 0x0120, 0x0000, 0x0130, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x017b, 0x0000, 0x0000, 0x010b, 0x0000, 0x0117, 0x0000, 0x0121, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x017c },
	// umlaut
	{ 0x00c4, 0x0000, 0x0000, 0x0000, 0x00cb, 0x0000, 0x0000, 0x0000, 0x00cf, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x00d6, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x00dc, 0x0000, 0x0000, 0x0000, 0x0178, 0x0000, 0x00e4, 0x0000, 0x0000, 0x0000, 0x00eb, 0x0000, 0x0000, 0x0000, 0x00ef, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x00f6, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x00fc, 0x0000, 0x0000, 0x0000, 0x00ff, 0x0000 },
	// (unused)
	{ 0 },
	// ring
	{ 0x00c5, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x016e, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x00e5, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x016f, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000 },
	// cedilla
	{ 0x0000, 0x0000, 0x00c7, 0x0000, 0x0000, 0x0000, 0x0122, 0x0000, 0x0000, 0x0000, 0x0136, 0x013b, 0x0000, 0x0145, 0x0000, 0x0000, 0x0000, 0x0156, 0x015e, 0x0162, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x00e7, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0137, 0x013c, 0x0000, 0x0146, 0x0000, 0x0000, 0x0000, 0x0157, 0x015f, 0x0163, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000 },
	// (unused)
	{ 0 },
	// double acute
	{ 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0150, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0170, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0151, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0171, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000 },
	// ogonek
	{ 0x0104, 0x0000, 0x0000, 0x0000, 0x0118, 0x0000, 0x0000, 0x0000, 0x012e, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0172, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0105, 0x0000, 0x0000, 0x0000, 0x0119, 0x0000, 0x0000, 0x0000, 0x012f, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0173, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000 },
	// caron
	{ 0x0000, 0x0000, 0x010c, 0x010e, 0x011a, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x013d, 0x0000, 0x0147, 0x0000, 0x0000, 0x0000, 0x0158, 0x0160, 0x0164, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x017d, 0x0000, 0x0000, 0x010d, 0x010f, 0x011b, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x013e, 0x0000, 0x0148, 0x0000, 0x0000, 0x0000, 0x0159, 0x0161, 0x0165, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x017e }
};


struct TeletextState : ITeletextParser {

	std::vector<Page> parse(SpanC data, int64_t time) override;

	Modules::KHost* host;
	int pageNum = 0;
	PrimaryCharset primaryCharset = { 0x00, Undef, Undef };
	State states = { No };
	uint32_t framesProduced = 0;
	uint8_t cc_map[256] = { 0 };
	TransmissionMode transmissionMode = Serial;
	uint8_t receivingData = No; // flag indicating if incoming data should be processed or ignored
	PageBuffer pageBuffer;
	uint16_t G0[5][96] = { //G0 charsets in UCS-2
		{
			// Latin G0 Primary Set
			0x0020, 0x0021, 0x0022, 0x00a3, 0x0024, 0x0025, 0x0026, 0x0027, 0x0028, 0x0029, 0x002a, 0x002b, 0x002c, 0x002d, 0x002e, 0x002f,
			0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037, 0x0038, 0x0039, 0x003a, 0x003b, 0x003c, 0x003d, 0x003e, 0x003f,
			0x0040, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047, 0x0048, 0x0049, 0x004a, 0x004b, 0x004c, 0x004d, 0x004e, 0x004f,
			0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057, 0x0058, 0x0059, 0x005a, 0x00ab, 0x00bd, 0x00bb, 0x005e, 0x0023,
			0x002d, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067, 0x0068, 0x0069, 0x006a, 0x006b, 0x006c, 0x006d, 0x006e, 0x006f,
			0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077, 0x0078, 0x0079, 0x007a, 0x00bc, 0x00a6, 0x00be, 0x00f7, 0x007f
		},
		{
			// Cyrillic G0 Primary Set - Option 1 - Serbian/Croatian
			0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x044b, 0x0027, 0x0028, 0x0029, 0x002a, 0x002b, 0x002c, 0x002d, 0x002e, 0x002f,
			0x0030, 0x0031, 0x3200, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037, 0x0038, 0x0039, 0x003a, 0x003b, 0x003c, 0x003d, 0x003e, 0x003f,
			0x0427, 0x0410, 0x0411, 0x0426, 0x0414, 0x0415, 0x0424, 0x0413, 0x0425, 0x0418, 0x0408, 0x041a, 0x041b, 0x041c, 0x041d, 0x041e,
			0x041f, 0x040c, 0x0420, 0x0421, 0x0422, 0x0423, 0x0412, 0x0403, 0x0409, 0x040a, 0x0417, 0x040b, 0x0416, 0x0402, 0x0428, 0x040f,
			0x0447, 0x0430, 0x0431, 0x0446, 0x0434, 0x0435, 0x0444, 0x0433, 0x0445, 0x0438, 0x0428, 0x043a, 0x043b, 0x043c, 0x043d, 0x043e,
			0x043f, 0x042c, 0x0440, 0x0441, 0x0442, 0x0443, 0x0432, 0x0423, 0x0429, 0x042a, 0x0437, 0x042b, 0x0436, 0x0422, 0x0448, 0x042f
		},
		{
			// Cyrillic G0 Primary Set - Option 2 - Russian/Bulgarian
			0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x044b, 0x0027, 0x0028, 0x0029, 0x002a, 0x002b, 0x002c, 0x002d, 0x002e, 0x002f,
			0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037, 0x0038, 0x0039, 0x003a, 0x003b, 0x003c, 0x003d, 0x003e, 0x003f,
			0x042e, 0x0410, 0x0411, 0x0426, 0x0414, 0x0415, 0x0424, 0x0413, 0x0425, 0x0418, 0x0419, 0x041a, 0x041b, 0x041c, 0x041d, 0x041e,
			0x041f, 0x042f, 0x0420, 0x0421, 0x0422, 0x0423, 0x0416, 0x0412, 0x042c, 0x042a, 0x0417, 0x0428, 0x042d, 0x0429, 0x0427, 0x042b,
			0x044e, 0x0430, 0x0431, 0x0446, 0x0434, 0x0435, 0x0444, 0x0433, 0x0445, 0x0438, 0x0439, 0x043a, 0x043b, 0x043c, 0x043d, 0x043e,
			0x043f, 0x044f, 0x0440, 0x0441, 0x0442, 0x0443, 0x0436, 0x0432, 0x044c, 0x044a, 0x0437, 0x0448, 0x044d, 0x0449, 0x0447, 0x044b
		},
		{
			// Cyrillic G0 Primary Set - Option 3 - Ukrainian
			0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x00ef, 0x0027, 0x0028, 0x0029, 0x002a, 0x002b, 0x002c, 0x002d, 0x002e, 0x002f,
			0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037, 0x0038, 0x0039, 0x003a, 0x003b, 0x003c, 0x003d, 0x003e, 0x003f,
			0x042e, 0x0410, 0x0411, 0x0426, 0x0414, 0x0415, 0x0424, 0x0413, 0x0425, 0x0418, 0x0419, 0x041a, 0x041b, 0x041c, 0x041d, 0x041e,
			0x041f, 0x042f, 0x0420, 0x0421, 0x0422, 0x0423, 0x0416, 0x0412, 0x042c, 0x0049, 0x0417, 0x0428, 0x042d, 0x0429, 0x0427, 0x00cf,
			0x044e, 0x0430, 0x0431, 0x0446, 0x0434, 0x0435, 0x0444, 0x0433, 0x0445, 0x0438, 0x0439, 0x043a, 0x043b, 0x043c, 0x043d, 0x043e,
			0x043f, 0x044f, 0x0440, 0x0441, 0x0442, 0x0443, 0x0436, 0x0432, 0x044c, 0x0069, 0x0437, 0x0448, 0x044d, 0x0449, 0x0447, 0x00ff
		},
		{
			// Greek G0 Primary Set
			0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027, 0x0028, 0x0029, 0x002a, 0x002b, 0x002c, 0x002d, 0x002e, 0x002f,
			0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037, 0x0038, 0x0039, 0x003a, 0x003b, 0x003c, 0x003d, 0x003e, 0x003f,
			0x0390, 0x0391, 0x0392, 0x0393, 0x0394, 0x0395, 0x0396, 0x0397, 0x0398, 0x0399, 0x039a, 0x039b, 0x039c, 0x039d, 0x039e, 0x039f,
			0x03a0, 0x03a1, 0x03a2, 0x03a3, 0x03a4, 0x03a5, 0x03a6, 0x03a7, 0x03a8, 0x03a9, 0x03aa, 0x03ab, 0x03ac, 0x03ad, 0x03ae, 0x03af,
			0x03b0, 0x03b1, 0x03b2, 0x03b3, 0x03b4, 0x03b5, 0x03b6, 0x03b7, 0x03b8, 0x03b9, 0x03ba, 0x03bb, 0x03bc, 0x03bd, 0x03be, 0x03bf,
			0x03c0, 0x03c1, 0x03c2, 0x03c3, 0x03c4, 0x03c5, 0x03c6, 0x03c7, 0x03c8, 0x03c9, 0x03ca, 0x03cb, 0x03cc, 0x03cd, 0x03ce, 0x03cf
		}
		//{ // Arabic G0 Primary Set
		//},
		//{ // Hebrew G0 Primary Set
		//}
	};
};

uint16_t telx_to_ucs2(uint8_t c, TeletextState const& config) {
	if (Parity8[c] == 0) {
		config.host->log(Warning, format("Teletext: unrecoverable data error (5): %s", c).c_str());
		return 0x20;
	}

	uint16_t val = c & 0x7f;
	if (val >= 0x20) {
		val = config.G0[LATIN][val - 0x20];
	}
	return val;
}

void remap_g0_charset(uint8_t c, TeletextState &config) {
	if (c != config.primaryCharset.current) {
		uint8_t m = G0_LatinNationalSubsetsMap[c];
		if (m == 0xff) {
			config.host->log(Warning, format("Teletext: G0 subset %s.%s is not implemented", (c >> 3), (c & 0x7)).c_str());
		} else {
			for (uint8_t j = 0; j < 13; j++) {
				config.G0[LATIN][G0_LatinNationalSubsetsPositions[j]] = G0_LatinNationalSubsets[m].characters[j];
			}
			config.primaryCharset.current = c;
		}
	}
}

static bool isEmpty(PageBuffer const& pageIn) {
	for (int col = 0; col < COLS; col++) {
		for (int row = 1; row < ROWS; row++) {
			if (pageIn.text[row][col] == 0x0b)
				return false;
		}
	}
	return true;
}

static
void process_row(const uint16_t* srcRow, Page& page) {
	int colStart = COLS;
	int colStop = COLS;

	for (int col = COLS-1; col >= 0; col--) {
		if (srcRow[col] == 0xb) {
			colStart = col;
			break;
		}
	}
	if (colStart >= COLS)
		return; //empty line

	for (int col = colStart + 1; col < COLS; col++) {
		if (srcRow[col] > 0x20) {
			if (colStop >= COLS) colStart = col;
			colStop = col;
		}
		if (srcRow[col] == 0xa)
			break;
	}
	if (colStop >= COLS)
		return; //empty line

	// section 12.2: Alpha White ("Set-After") - Start-of-row default condition.
	for (int col = 0; col <= colStop; col++) {
		uint16_t val = srcRow[col];

		if (col >= colStart) {
			if (val <= 0x7) {
				val = 0x20;
			}

			if (val >= 0x20) {
				char u[4] {};
				ucs2_to_utf8(u, val);
				page.lines.back() += u;
			}
		}
	}

	page.lines.push_back({});
}

std::unique_ptr<Page> process_page(TeletextState& state) {
	PageBuffer* pageIn = &state.pageBuffer;
	auto pageOut = std::make_unique<Page>();
	pageOut->lines.push_back({});

	if (isEmpty(*pageIn))
		return pageOut;

	pageIn->hideTimestamp = std::max(pageIn->hideTimestamp, pageIn->showTimestamp);

	pageOut->showTimestamp = pageIn->showTimestamp;
	pageOut->hideTimestamp = pageIn->hideTimestamp;

	for (int row = 1; row < ROWS; row++)
		process_row(pageIn->text[row], *pageOut);

	return pageOut;
}

std::unique_ptr<Page> process_telx_packet(TeletextState &config, DataUnit dataUnitId, void* data, uint64_t timestamp) {
	auto packet = (const Payload*)data;
	// section 7.1.2
	uint8_t address = (unham_8_4(packet->address[1]) << 4) | unham_8_4(packet->address[0]);
	uint8_t m = address & 0x7;
	if (m == 0)
		m = 8;
	uint8_t y = (address >> 3) & 0x1f;
	uint8_t designationCode = (y > 25) ? unham_8_4(packet->data[0]) : 0x00;
	std::unique_ptr<Page> pageOut;

	if (y == 0) {
		uint8_t i = (unham_8_4(packet->data[1]) << 4) | unham_8_4(packet->data[0]);
		uint8_t subtitleFlag = (unham_8_4(packet->data[5]) & 0x08) >> 3;
		config.cc_map[i] |= subtitleFlag << (m - 1);

		const uint16_t pageNum = (m << 8) | (unham_8_4(packet->data[1]) << 4) | unham_8_4(packet->data[0]);
		const uint8_t charset = ((unham_8_4(packet->data[7]) & 0x08) | (unham_8_4(packet->data[7]) & 0x04) | (unham_8_4(packet->data[7]) & 0x02)) >> 1;

		if ((config.pageNum == 0) && (subtitleFlag == Yes) && (i < 0xff)) {
			config.pageNum = pageNum;
		}

		// Section 9.3.1.3
		config.transmissionMode = (TransmissionMode)(unham_8_4(packet->data[7]) & 0x01);
		if ((config.transmissionMode == Parallel) && (dataUnitId != Subtitle))
			return nullptr;

		if ((config.receivingData == Yes) && (
		        ((config.transmissionMode == Serial) && (PAGE(pageNum) != PAGE(config.pageNum))) ||
		        ((config.transmissionMode == Parallel) && (PAGE(pageNum) != PAGE(config.pageNum)) && (m == MAGAZINE(config.pageNum)))
		    )) {
			config.receivingData = No;
			return nullptr;
		}

		if (pageNum != config.pageNum)
			return nullptr; //page transmission is terminated, however now we are waiting for our new page

		if (config.pageBuffer.tainted) { //begining of page transmission
			config.pageBuffer.hideTimestamp = timestamp - 40;
			pageOut = process_page(config);
		}

		config.pageBuffer.showTimestamp = timestamp;
		config.pageBuffer.hideTimestamp = 0;
		memset(config.pageBuffer.text, 0x00, sizeof(config.pageBuffer.text));
		config.pageBuffer.tainted = false;
		config.receivingData = Yes;
		config.primaryCharset.G0_X28 = Undef;

		uint8_t c = (config.primaryCharset.G0_M29 != Undef) ? config.primaryCharset.G0_M29 : charset;
		remap_g0_charset(c, config);
	} else if ((m == MAGAZINE(config.pageNum)) && (y >= 1) && (y <= 23) && (config.receivingData == Yes)) {
		// Section 9.4.1
		for (uint8_t i = 0; i < 40; i++) {
			if (config.pageBuffer.text[y][i] == 0x00)
				config.pageBuffer.text[y][i] = telx_to_ucs2(packet->data[i], config);
		}
		config.pageBuffer.tainted = true;
	} else if ((m == MAGAZINE(config.pageNum)) && (y == 26) && (config.receivingData == Yes)) {
		// Section 12.3.2
		uint8_t X26Row = 0, X26Col = 0;
		uint32_t triplets[13] = { 0 };
		for (uint8_t i = 1, j = 0; i < 40; i += 3, j++) {
			triplets[j] = unham_24_18((packet->data[i + 2] << 16) | (packet->data[i + 1] << 8) | packet->data[i]);
		}

		for (auto triplet : triplets) {
			if (triplet == 0xffffffff) {
				config.host->log(Warning, format("Teletext: unrecoverable data error (1): %s", triplet).c_str());
				continue;
			}

			uint8_t data = (triplet & 0x3f800) >> 11;
			uint8_t mode = (triplet & 0x7c0) >> 6;
			uint8_t address = triplet & 0x3f;
			uint8_t row_address_group = (address >= 40) && (address <= 63);
			if ((mode == 0x04) && (row_address_group == Yes)) {
				X26Row = address - 40;
				if (X26Row == 0) X26Row = 24;
				X26Col = 0;
			}
			if ((mode >= 0x11) && (mode <= 0x1f) && (row_address_group == Yes))
				break; //termination marker

			if ((mode == 0x0f) && (row_address_group == No)) {
				X26Col = address;
				if (data > 31) config.pageBuffer.text[X26Row][X26Col] = G2[0][data - 0x20];
			}

			if ((mode >= 0x11) && (mode <= 0x1f) && (row_address_group == No)) {
				X26Col = address;
				if ((data >= 'A') && (data <= 'Z')) {
					config.pageBuffer.text[X26Row][X26Col] = G2_Accents[mode - 0x11][data - 'A'];
				} else if ((data >= 'a') && (data <= 'z')) {
					config.pageBuffer.text[X26Row][X26Col] = G2_Accents[mode - 0x11][data - 'a' + 26];
				} else {
					config.pageBuffer.text[X26Row][X26Col] = telx_to_ucs2(data, config);
				}
			}
		}
	} else if ((m == MAGAZINE(config.pageNum)) && (y == 28) && (config.receivingData == Yes)) {
		// Section 9.4.7
		if ((designationCode == 0) || (designationCode == 4)) {
			uint32_t triplet0 = unham_24_18((packet->data[3] << 16) | (packet->data[2] << 8) | packet->data[1]);
			if (triplet0 == 0xffffffff) {
				config.host->log(Warning, format("Teletext: unrecoverable data error (2): %s", triplet0).c_str());
			} else {
				if ((triplet0 & 0x0f) == 0x00) {
					config.primaryCharset.G0_X28 = (triplet0 & 0x3f80) >> 7;
					remap_g0_charset(config.primaryCharset.G0_X28, config);
				}
			}
		}
	} else if ((m == MAGAZINE(config.pageNum)) && (y == 29)) {
		// Section 9.5.1
		if ((designationCode == 0) || (designationCode == 4)) {
			uint32_t triplet0 = unham_24_18((packet->data[3] << 16) | (packet->data[2] << 8) | packet->data[1]);
			if (triplet0 == 0xffffffff) {
				config.host->log(Warning, format("Teletext: unrecoverable data error (3): %s", triplet0).c_str());
			} else {
				if ((triplet0 & 0xff) == 0x00) {
					config.primaryCharset.G0_M29 = (triplet0 & 0x3f80) >> 7;
					if (config.primaryCharset.G0_X28 == Undef) {
						remap_g0_charset(config.primaryCharset.G0_M29, config);
					}
				}
			}
		}
	} else if ((m == 8) && (y == 30)) {
		// Section 9.8
		if (config.states.progInfoProcessed == No) {
			if (unham_8_4(packet->data[0]) < 2) {
				config.states.progInfoProcessed = Yes;
			}
		}
	}

	return pageOut;
}

std::vector<Page> TeletextState::parse(SpanC data, int64_t time) {
	int i = 1;

	std::vector<Page> pages;

	while(i <= int(data.len) - 6) {
		auto const dataUnitId = (DataUnit)data[i++];
		auto const dataUnitSize = data[i++];

		const uint8_t TELX_PAYLOAD_SIZE = 44;

		if(((dataUnitId == NonSubtitle) || (dataUnitId == Subtitle))
		    && (dataUnitSize == TELX_PAYLOAD_SIZE)) {

			if(i + TELX_PAYLOAD_SIZE > (int)data.len) {
				host->log(Warning, "truncated data unit");
				break;
			}

			uint8_t entitiesData[TELX_PAYLOAD_SIZE];
			for(int j = 0; j < TELX_PAYLOAD_SIZE; j++) {
				auto byte = data[i + j];
				entitiesData[j] = Reverse8[byte]; // reverse endianess
			}

			auto page = process_telx_packet(*this, dataUnitId, entitiesData, time);

			if(page)
				pages.push_back(*page);
		}

		i += dataUnitSize;
	}

	return pages;
}

}

ITeletextParser* createTeletextParser(Modules::KHost* host, int pageNum) {
	auto r = new TeletextState;
	r->host = host;
	r->pageNum = pageNum;
	return r;
}
