#include "theme.h"

#include <string.h>

// One Dark.
static const Theme ONEDARK = {
    .bg = {40, 44, 52}, .fg = {171, 178, 191}, .cursor = {171, 178, 191},
    .ansi = {
        { 40,  44,  52}, {224, 108, 117}, {152, 195, 121}, {229, 192, 123},
        { 97, 175, 239}, {198, 120, 221}, { 86, 182, 194}, {171, 178, 191},
        { 92,  99, 112}, {224, 108, 117}, {152, 195, 121}, {229, 192, 123},
        { 97, 175, 239}, {198, 120, 221}, { 86, 182, 194}, {220, 223, 228},
    },
    .is_light = false,
};

// Dark Modern (VS Code default dark terminal palette).
static const Theme DARKMODERN = {
    .bg = {31, 31, 31}, .fg = {204, 204, 204}, .cursor = {204, 204, 204},
    .ansi = {
        {  0,   0,   0}, {205,  49,  49}, { 13, 188, 121}, {229, 229,  16},
        { 36, 114, 200}, {188,  63, 188}, { 17, 168, 205}, {229, 229, 229},
        {102, 102, 102}, {241,  76,  76}, { 35, 209, 139}, {245, 245,  67},
        { 59, 142, 234}, {214, 112, 214}, { 41, 184, 219}, {229, 229, 229},
    },
    .is_light = false,
};

// GitHub Dark.
static const Theme GITHUBDARK = {
    .bg = {13, 17, 23}, .fg = {201, 209, 217}, .cursor = {201, 209, 217},
    .ansi = {
        { 72,  79,  88}, {255, 123, 114}, { 63, 185,  80}, {210, 153,  34},
        { 88, 166, 255}, {188, 140, 255}, { 57, 197, 207}, {177, 186, 196},
        {110, 118, 129}, {255, 161, 152}, { 86, 211, 100}, {227, 179,  65},
        {121, 192, 255}, {210, 168, 255}, { 86, 212, 221}, {240, 246, 252},
    },
    .is_light = false,
};

// Gruvbox (dark).
static const Theme GRUVBOX = {
    .bg = {40, 40, 40}, .fg = {235, 219, 178}, .cursor = {235, 219, 178},
    .ansi = {
        { 40,  40,  40}, {204,  36,  29}, {152, 151,  26}, {215, 153,  33},
        { 69, 133, 136}, {177,  98, 134}, {104, 157, 106}, {168, 153, 132},
        {146, 131, 116}, {251,  73,  52}, {184, 187,  38}, {250, 189,  47},
        {131, 165, 152}, {211, 134, 155}, {142, 192, 124}, {235, 219, 178},
    },
    .is_light = false,
};

// Monokai.
static const Theme MONOKAI = {
    .bg = {39, 40, 34}, .fg = {248, 248, 242}, .cursor = {248, 248, 242},
    .ansi = {
        { 39,  40,  34}, {249,  38, 114}, {166, 226,  46}, {244, 191, 117},
        {102, 217, 239}, {174, 129, 255}, {161, 239, 228}, {248, 248, 242},
        {117, 113,  94}, {249,  38, 114}, {166, 226,  46}, {244, 191, 117},
        {102, 217, 239}, {174, 129, 255}, {161, 239, 228}, {249, 248, 245},
    },
    .is_light = false,
};

// Solarized Dark.
static const Theme SOLARIZEDDARK = {
    .bg = {  0,  43,  54}, .fg = {131, 148, 150}, .cursor = {147, 161, 161},
    .ansi = {
        {  7,  54,  66}, {220,  50,  47}, {133, 153,   0}, {181, 137,   0},
        { 38, 139, 210}, {211,  54, 130}, { 42, 161, 152}, {238, 232, 213},
        {  0,  43,  54}, {203,  75,  22}, { 88, 110, 117}, {101, 123, 131},
        {131, 148, 150}, {108, 113, 196}, {147, 161, 161}, {253, 246, 227},
    },
    .is_light = false,
};

// Catppuccin Mocha.
static const Theme CATPPUCCINMOCHA = {
    .bg = { 30,  30,  46}, .fg = {205, 214, 244}, .cursor = {245, 224, 220},
    .ansi = {
        { 69,  71,  90}, {243, 139, 168}, {166, 227, 161}, {249, 226, 175},
        {137, 180, 250}, {245, 194, 231}, {148, 226, 213}, {166, 173, 200},
        { 88,  91, 112}, {243, 139, 168}, {166, 227, 161}, {249, 226, 175},
        {137, 180, 250}, {245, 194, 231}, {148, 226, 213}, {186, 194, 222},
    },
    .is_light = false,
};

// Ayu Mirage.
static const Theme AYUMIRAGE = {
    .bg = { 31,  36,  48}, .fg = {203, 204, 198}, .cursor = {255, 204, 102},
    .ansi = {
        { 25,  30,  42}, {255,  51,  51}, {186, 230, 126}, {255, 167,  89},
        {115, 208, 255}, {212, 191, 255}, {149, 230, 203}, {199, 199, 199},
        {104, 104, 104}, {242, 121, 131}, {166, 204, 112}, {255, 204, 102},
        { 92, 207, 230}, {255, 174,  87}, {149, 230, 203}, {255, 255, 255},
    },
    .is_light = false,
};

// Tokyo Night.
static const Theme TOKYONIGHT = {
    .bg = { 26,  27,  38}, .fg = {192, 202, 245}, .cursor = {192, 202, 245},
    .ansi = {
        { 21,  22,  30}, {247, 118, 142}, {158, 206, 106}, {224, 175, 104},
        {122, 162, 247}, {187, 154, 247}, {125, 207, 255}, {169, 177, 214},
        { 65,  72, 104}, {247, 118, 142}, {158, 206, 106}, {224, 175, 104},
        {122, 162, 247}, {187, 154, 247}, {125, 207, 255}, {192, 202, 245},
    },
    .is_light = false,
};

// Rose Pine.
static const Theme ROSEPINE = {
    .bg = { 25,  23,  36}, .fg = {224, 222, 244}, .cursor = {235, 188, 186},
    .ansi = {
        { 38,  35,  58}, {235, 111, 146}, { 49, 116, 143}, {246, 193, 119},
        {156, 207, 216}, {196, 167, 231}, {235, 188, 186}, {224, 222, 244},
        {110, 106, 134}, {235, 111, 146}, { 49, 116, 143}, {246, 193, 119},
        {156, 207, 216}, {196, 167, 231}, {235, 188, 186}, {224, 222, 244},
    },
    .is_light = false,
};

// One Light.
static const Theme ONELIGHT = {
    .bg = {250, 250, 250}, .fg = {56, 58, 66}, .cursor = {64, 120, 242},
    .ansi = {
        { 56,  58,  66}, {228,  86,  73}, { 80, 161,  79}, {193, 132,   1},
        { 64, 120, 242}, {166,  38, 164}, {  9, 151, 179}, {160, 161, 167},
        {160, 161, 167}, {228,  86,  73}, { 80, 161,  79}, {193, 132,   1},
        { 64, 120, 242}, {166,  38, 164}, {  9, 151, 179}, { 56,  58,  66},
    },
    .is_light = true,
};

// Light Modern (VS Code default light terminal palette).
static const Theme LIGHTMODERN = {
    .bg = {255, 255, 255}, .fg = {59, 59, 59}, .cursor = {59, 59, 59},
    .ansi = {
        {  0,   0,   0}, {205,  49,  49}, {  0, 188,   0}, {148, 152,   0},
        {  4,  81, 165}, {188,   5, 188}, {  5, 152, 188}, { 85,  85,  85},
        {102, 102, 102}, {205,  49,  49}, { 20, 206,  20}, {181, 186,   0},
        {  4,  81, 165}, {188,   5, 188}, {  5, 152, 188}, {165, 165, 165},
    },
    .is_light = true,
};

// GitHub Light.
static const Theme GITHUBLIGHT = {
    .bg = {255, 255, 255}, .fg = {36, 41, 47}, .cursor = {3, 102, 214},
    .ansi = {
        { 36,  41,  46}, {215,  58,  73}, { 40, 167,  69}, {219, 171,   9},
        {  3, 102, 214}, { 90,  50, 163}, { 27, 124, 131}, {106, 115, 125},
        {149, 157, 165}, {203,  36,  49}, { 34, 134,  58}, {176, 136,   0},
        {  0,  92, 197}, { 90,  50, 163}, { 49, 146, 170}, {209, 213, 218},
    },
    .is_light = true,
};

// Gruvbox Light (ansi 0 kept dark for legible "black" text on the light bg).
static const Theme GRUVBOXLIGHT = {
    .bg = {251, 241, 199}, .fg = {60, 56, 54}, .cursor = {60, 56, 54},
    .ansi = {
        { 60,  56,  54}, {204,  36,  29}, {152, 151,  26}, {215, 153,  33},
        { 69, 133, 136}, {177,  98, 134}, {104, 157, 106}, {124, 111, 100},
        {146, 131, 116}, {157,   0,   6}, {121, 116,  14}, {181, 118,  20},
        {  7, 102, 120}, {143,  63, 113}, { 66, 123,  88}, { 60,  56,  54},
    },
    .is_light = true,
};

// Solarized Light.
static const Theme SOLARIZEDLIGHT = {
    .bg = {253, 246, 227}, .fg = {101, 123, 131}, .cursor = { 88, 110, 117},
    .ansi = {
        {  7,  54,  66}, {220,  50,  47}, {133, 153,   0}, {181, 137,   0},
        { 38, 139, 210}, {211,  54, 130}, { 42, 161, 152}, {238, 232, 213},
        {  0,  43,  54}, {203,  75,  22}, { 88, 110, 117}, {101, 123, 131},
        {131, 148, 150}, {108, 113, 196}, {147, 161, 161}, {253, 246, 227},
    },
    .is_light = true,
};

// Catppuccin Latte.
static const Theme CATPPUCCINLATTE = {
    .bg = {239, 241, 245}, .fg = { 76,  79, 105}, .cursor = {220, 138, 120},
    .ansi = {
        { 92,  95, 119}, {210,  15,  57}, { 64, 160,  43}, {223, 142,  29},
        { 30, 102, 245}, {234, 118, 203}, { 23, 146, 153}, {172, 176, 190},
        {108, 111, 133}, {210,  15,  57}, { 64, 160,  43}, {223, 142,  29},
        { 30, 102, 245}, {234, 118, 203}, { 23, 146, 153}, {188, 192, 204},
    },
    .is_light = true,
};

// Ayu Light.
static const Theme AYULIGHT = {
    .bg = {250, 250, 250}, .fg = { 92, 103, 115}, .cursor = {255, 153,  64},
    .ansi = {
        {  0,   0,   0}, {240, 113, 120}, {134, 179,   0}, {242, 174,  73},
        { 85, 180, 212}, {163, 122, 204}, { 76, 191, 153}, { 92, 103, 115},
        {171, 176, 182}, {240, 113, 120}, {134, 179,   0}, {242, 174,  73},
        { 85, 180, 212}, {163, 122, 204}, { 76, 191, 153}, {255, 255, 255},
    },
    .is_light = true,
};

// Tokyo Night Day.
static const Theme TOKYONIGHTDAY = {
    .bg = {225, 226, 231}, .fg = { 55,  96, 191}, .cursor = { 55,  96, 191},
    .ansi = {
        {180, 181, 185}, {245,  42, 101}, { 88, 117,  57}, {140, 108,  62},
        { 46, 125, 233}, {152,  84, 241}, {  0, 113, 151}, { 97, 114, 176},
        {161, 166, 197}, {255,  71, 116}, { 92, 133,  36}, {162, 118,  41},
        { 53, 138, 255}, {164,  99, 255}, {  0, 126, 168}, { 55,  96, 191},
    },
    .is_light = true,
};

// Rose Pine Dawn.
static const Theme ROSEPINEDAWN = {
    .bg = {250, 244, 237}, .fg = { 87,  82, 121}, .cursor = {180,  99, 122},
    .ansi = {
        {242, 233, 225}, {180,  99, 122}, { 40, 105, 131}, {234, 157,  52},
        { 86, 148, 159}, {144, 122, 169}, {215, 130, 126}, { 87,  82, 121},
        {152, 147, 165}, {180,  99, 122}, { 40, 105, 131}, {234, 157,  52},
        { 86, 148, 159}, {144, 122, 169}, {215, 130, 126}, { 87,  82, 121},
    },
    .is_light = true,
};

// Everforest Light.
static const Theme EVERFORESTLIGHT = {
    .bg = {253, 246, 227}, .fg = { 92, 106, 114}, .cursor = { 92, 106, 114},
    .ansi = {
        { 92, 106, 114}, {248,  85,  82}, {141, 161,   1}, {223, 160,   0},
        { 58, 148, 197}, {223, 105, 186}, { 53, 167, 124}, {223, 221, 200},
        {166, 176, 160}, {248,  85,  82}, {141, 161,   1}, {223, 160,   0},
        { 58, 148, 197}, {223, 105, 186}, { 53, 167, 124}, { 92, 106, 114},
    },
    .is_light = true,
};

// Fangs Dark — brand flagship. Charcoal bg, muted teal/amber accents.
static const Theme FANGSDARK = {
    .bg = { 28,  30,  34}, .fg = {198, 202, 208}, .cursor = { 86, 182, 194},
    .ansi = {
        { 28,  30,  34}, {239,  83,  80}, { 92, 191, 154}, {242, 174,  89},
        { 86, 182, 194}, {184, 124, 204}, {106, 189, 172}, {198, 202, 208},
        { 80,  84,  92}, {255,  92,  87}, {115, 213, 174}, {255, 196, 106},
        {109, 205, 218}, {209, 153, 228}, {130, 211, 191}, {243, 245, 248},
    },
    .is_light = false,
};

// Fangs Light — brand flagship. Warm off-white bg, deep slate text.
static const Theme FANGSLIGHT = {
    .bg = {250, 248, 243}, .fg = { 55,  60,  67}, .cursor = { 31, 137, 149},
    .ansi = {
        { 55,  60,  67}, {204,  52,  43}, { 62, 153,  88}, {179, 122,  10},
        { 31, 137, 149}, {149,  74, 165}, { 58, 150, 116}, {150, 154, 160},
        {113, 120, 128}, {230,  67,  55}, { 80, 179,  98}, {211, 151,  34},
        { 54, 164, 178}, {176, 104, 193}, { 82, 174, 139}, { 38,  42,  47},
    },
    .is_light = true,
};

// Dracula.
static const Theme DRACULA = {
    .bg = {40, 42, 54}, .fg = {248, 248, 242}, .cursor = {248, 248, 242},
    .ansi = {
        { 40,  42,  54}, {255,  85,  85}, { 80, 250, 123}, {241, 250, 140},
        {189, 147, 249}, {255, 121, 198}, {139, 233, 253}, {248, 248, 242},
        { 68,  71,  90}, {255, 110, 103}, { 80, 250, 123}, {241, 250, 140},
        {189, 147, 249}, {255, 121, 198}, {139, 233, 253}, {255, 255, 255},
    },
    .is_light = false,
};

// Nord.
static const Theme NORD = {
    .bg = {46, 52, 64}, .fg = {216, 222, 233}, .cursor = {216, 222, 233},
    .ansi = {
        { 59,  66,  82}, {191,  97, 106}, {163, 190, 140}, {235, 203, 139},
        {129, 161, 193}, {180, 142, 173}, {136, 192, 208}, {229, 233, 240},
        { 76,  86, 106}, {191,  97, 106}, {163, 190, 140}, {235, 203, 139},
        {129, 161, 193}, {180, 142, 173}, {143, 188, 187}, {236, 239, 244},
    },
    .is_light = false,
};

// Kanagawa.
static const Theme KANAGAWA = {
    .bg = { 31,  31,  40}, .fg = {220, 215, 186}, .cursor = {255, 160,  38},
    .ansi = {
        { 31,  31,  40}, {255,  91,  93}, {118, 148, 106}, {220, 165,  76},
        {145, 184, 208}, {154, 120, 158}, { 95, 160, 160}, {220, 215, 186},
        { 84,  84, 100}, {255,  91,  93}, {118, 148, 106}, {220, 165,  76},
        {145, 184, 208}, {154, 120, 158}, { 95, 160, 160}, {255, 255, 240},
    },
    .is_light = false,
};

// Everforest Dark.
static const Theme EVERFORESTDARK = {
    .bg = { 50,  57,  53}, .fg = {211, 198, 170}, .cursor = {167, 192, 128},
    .ansi = {
        { 50,  57,  53}, {230, 126, 128}, {167, 192, 128}, {219, 188, 127},
        {127, 187, 179}, {223, 105, 186}, {123, 189, 152}, {211, 198, 170},
        {123, 136, 128}, {230, 126, 128}, {167, 192, 128}, {219, 188, 127},
        {127, 187, 179}, {223, 105, 186}, {123, 189, 152}, {243, 239, 224},
    },
    .is_light = false,
};

// Material Oceanic.
static const Theme MATERIALOCEANIC = {
    .bg = { 38,  50,  56}, .fg = {176, 190, 197}, .cursor = {128, 203, 196},
    .ansi = {
        { 38,  50,  56}, {255,  83, 112}, {195, 232, 141}, {255, 202,  40},
        {130, 170, 255}, {199, 146, 234}, {128, 203, 196}, {176, 190, 197},
        { 84, 110, 122}, {255,  83, 112}, {195, 232, 141}, {255, 202,  40},
        {130, 170, 255}, {199, 146, 234}, {128, 203, 196}, {255, 255, 255},
    },
    .is_light = false,
};

// Kanagawa Lotus (light variant).
static const Theme KANAGAWALOTUS = {
    .bg = {245, 240, 232}, .fg = { 84,  78,  78}, .cursor = {183, 110,  38},
    .ansi = {
        { 84,  78,  78}, {195,  64,  67}, { 92, 128,  79}, {169, 128,  58},
        {117, 154, 184}, {149, 119, 147}, { 84, 146, 146}, { 84,  78,  78},
        {140, 135, 130}, {195,  64,  67}, { 92, 128,  79}, {169, 128,  58},
        {117, 154, 184}, {149, 119, 147}, { 84, 146, 146}, { 46,  42,  42},
    },
    .is_light = true,
};

// One Light Pro (brighter, higher-contrast One Light).
static const Theme ONELIGHTPRO = {
    .bg = {255, 255, 255}, .fg = { 31,  35,  40}, .cursor = { 36, 114, 200},
    .ansi = {
        { 31,  35,  40}, {228,  86,  73}, { 58, 140,  71}, {193, 132,   1},
        { 36, 114, 200}, {166,  38, 164}, {  9, 151, 179}, {140, 145, 153},
        {140, 145, 153}, {228,  86,  73}, { 58, 140,  71}, {193, 132,   1},
        { 36, 114, 200}, {166,  38, 164}, {  9, 151, 179}, { 31,  35,  40},
    },
    .is_light = true,
};

// Catppuccin Frappe.
static const Theme CATPPUCCINFRAPPE = {
    .bg = { 48,  52,  70}, .fg = {198, 208, 245}, .cursor = {242, 170, 132},
    .ansi = {
        { 81,  87, 109}, {231, 130, 132}, {166, 209, 137}, {229, 200, 144},
        {140, 170, 238}, {244, 184, 228}, {129, 200, 190}, {165, 173, 203},
        {115, 121, 148}, {231, 130, 132}, {166, 209, 137}, {229, 200, 144},
        {140, 170, 238}, {244, 184, 228}, {153, 209, 199}, {198, 208, 245},
    },
    .is_light = false,
};

// Dracula Soft (light background variant).
static const Theme DRACULASOFT = {
    .bg = {248, 248, 248}, .fg = { 40,  42,  54}, .cursor = {189, 147, 249},
    .ansi = {
        { 68,  71,  90}, {255,  85,  85}, { 80, 250, 123}, {241, 250, 140},
        {189, 147, 249}, {255, 121, 198}, {139, 233, 253}, { 40,  42,  54},
        { 98, 102, 124}, {255, 110, 103}, { 80, 250, 123}, {241, 250, 140},
        {189, 147, 249}, {255, 121, 198}, {139, 233, 253}, { 25,  26,  34},
    },
    .is_light = true,
};

// Nord Light (polar day variant).
static const Theme NORDLIGHT = {
    .bg = {236, 239, 244}, .fg = { 59,  66,  82}, .cursor = { 94, 129, 172},
    .ansi = {
        { 59,  66,  82}, {191,  97, 106}, {163, 190, 140}, {235, 203, 139},
        { 94, 129, 172}, {180, 142, 173}, {136, 192, 208}, {216, 222, 233},
        {129, 142, 164}, {191,  97, 106}, {163, 190, 140}, {235, 203, 139},
        { 94, 129, 172}, {180, 142, 173}, {143, 188, 187}, { 46,  52,  64},
    },
    .is_light = true,
};

// Everforest Light Hard (higher contrast variant).
static const Theme EVERFORESTLIGHTHARD = {
    .bg = {253, 246, 227}, .fg = { 60,  67,  63}, .cursor = { 92, 120,  67},
    .ansi = {
        { 60,  67,  63}, {230,  85,  87}, {132, 160,  86}, {214, 158,  44},
        { 76, 144, 160}, {210,  87, 174}, { 86, 165, 120}, {160, 159, 145},
        {135, 145, 138}, {230,  85,  87}, {132, 160,  86}, {214, 158,  44},
        { 76, 144, 160}, {210,  87, 174}, { 86, 165, 120}, { 40,  45,  42},
    },
    .is_light = true,
};

typedef struct { const char *slug; const char *name; const Theme *theme; } ThemeEntry;

static const ThemeEntry ENTRIES[] = {
    // Dark themes
    {"fangs-dark", "Fangs Dark", &FANGSDARK},
    {"onedark", "One Dark", &ONEDARK},
    {"darkmodern", "Dark Modern", &DARKMODERN},
    {"githubdark", "GitHub Dark", &GITHUBDARK},
    {"gruvbox", "Gruvbox", &GRUVBOX},
    {"monokai", "Monokai", &MONOKAI},
    {"solarizeddark", "Solarized Dark", &SOLARIZEDDARK},
    {"catppuccinmocha", "Catppuccin Mocha", &CATPPUCCINMOCHA},
    {"catppuccinfrappe", "Catppuccin Frappe", &CATPPUCCINFRAPPE},
    {"tokyonight", "Tokyo Night", &TOKYONIGHT},
    {"dracula", "Dracula", &DRACULA},
    {"nord", "Nord", &NORD},
    {"kanagawa", "Kanagawa", &KANAGAWA},
    {"everforestdark", "Everforest Dark", &EVERFORESTDARK},
    {"materialoceanic", "Material Oceanic", &MATERIALOCEANIC},

    // Light themes
    {"fangs-light", "Fangs Light", &FANGSLIGHT},
    {"onelight", "One Light", &ONELIGHT},
    {"githublight", "GitHub Light", &GITHUBLIGHT},
    {"gruvboxlight", "Gruvbox Light", &GRUVBOXLIGHT},
    {"solarizedlight", "Solarized Light", &SOLARIZEDLIGHT},
    {"catppuccinlatte", "Catppuccin Latte", &CATPPUCCINLATTE},
    {"ayulight", "Ayu Light", &AYULIGHT},
    {"rosepinedawn", "Rose Pine Dawn", &ROSEPINEDAWN},
    {"everforestlight", "Everforest Light", &EVERFORESTLIGHT},
    {"kanagawalotus", "Kanagawa Lotus", &KANAGAWALOTUS},
    {"onelightpro", "One Light Pro", &ONELIGHTPRO},
    {"draculasoft", "Dracula Soft", &DRACULASOFT},
    {"nordlight", "Nord Light", &NORDLIGHT},
    {"everforestlighthard", "Everforest Light Hard", &EVERFORESTLIGHTHARD},
};;
static const int ENTRY_COUNT = (int)(sizeof(ENTRIES) / sizeof(ENTRIES[0]));

int theme_count(void) { return ENTRY_COUNT; }

const char *theme_name(int index)
{
    return (index >= 0 && index < ENTRY_COUNT) ? ENTRIES[index].name : "";
}

const char *theme_slug(int index)
{
    return (index >= 0 && index < ENTRY_COUNT) ? ENTRIES[index].slug : "";
}

int theme_index_of(const char *slug)
{
    if (!slug || !slug[0])
        return 0;
    if (strcmp(slug, "dark") == 0)        // legacy aliases -> brand themes
        slug = "fangs-dark";
    else if (strcmp(slug, "light") == 0)
        slug = "fangs-light";
    for (int i = 0; i < ENTRY_COUNT; i++)
        if (strcmp(slug, ENTRIES[i].slug) == 0)
            return i;
    return 0;
}

Theme theme_resolve(const char *slug)
{
    return *ENTRIES[theme_index_of(slug)].theme;
}

void theme_build_palette256(const Theme *theme, ThemeColor out[256])
{
    if (!theme || !out)
        return;

    for (int i = 0; i < 16; i++)         // 0-15: themed ANSI colors
        out[i] = theme->ansi[i];

    static const unsigned char steps[6] = {0, 95, 135, 175, 215, 255};
    int idx = 16;                        // 16-231: 6x6x6 color cube
    for (int r = 0; r < 6; r++)
        for (int g = 0; g < 6; g++)
            for (int b = 0; b < 6; b++)
                out[idx++] = (ThemeColor){ steps[r], steps[g], steps[b] };

    for (int i = 0; i < 24; i++) {       // 232-255: grayscale ramp
        unsigned char v = (unsigned char)(8 + i * 10);
        out[232 + i] = (ThemeColor){ v, v, v };
    }
}
