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

typedef struct { const char *slug; const char *name; const Theme *theme; } ThemeEntry;

static const ThemeEntry ENTRIES[] = {
    {"onedark",    "One Dark",    &ONEDARK},
    {"darkmodern", "Dark Modern", &DARKMODERN},
    {"githubdark", "GitHub Dark", &GITHUBDARK},
    {"gruvbox",    "Gruvbox",     &GRUVBOX},
    {"monokai",     "Monokai",      &MONOKAI},
    {"onelight",    "One Light",    &ONELIGHT},
    {"lightmodern", "Light Modern", &LIGHTMODERN},
    {"githublight", "GitHub Light", &GITHUBLIGHT},
    {"gruvboxlight","Gruvbox Light",&GRUVBOXLIGHT},
};
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
    if (strcmp(slug, "dark") == 0)        // legacy aliases
        slug = "onedark";
    else if (strcmp(slug, "light") == 0)
        slug = "onelight";
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
