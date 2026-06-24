#include "theme.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define EXPECT_TRUE(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: expected true: %s\n", __FILE__, __LINE__, #expr); \
        failures++; \
    } \
} while (0)

#define EXPECT_RGB(c, R, G, B) do { \
    ThemeColor c__ = (c); \
    if (c__.r != (R) || c__.g != (G) || c__.b != (B)) { \
        fprintf(stderr, "FAIL %s:%d: expected {%d,%d,%d}, got {%d,%d,%d}\n", \
                __FILE__, __LINE__, (R), (G), (B), c__.r, c__.g, c__.b); \
        failures++; \
    } \
} while (0)

static void test_resolve(void)
{
    Theme t = theme_resolve("light");
    EXPECT_TRUE(t.is_light);
    EXPECT_TRUE(t.bg.r > t.fg.r);            // light bg brighter than its fg

    Theme d = theme_resolve("dark");
    EXPECT_TRUE(!d.is_light);
    EXPECT_TRUE(d.fg.r > d.bg.r);            // dark fg brighter than its bg

    EXPECT_TRUE(!theme_resolve("nonsense").is_light);   // unknown → dark
    EXPECT_TRUE(!theme_resolve(NULL).is_light);          // NULL-safe
}

static void test_palette256(void)
{
    Theme t = theme_resolve("dark");
    ThemeColor pal[256];
    theme_build_palette256(&t, pal);

    // 0-15 mirror the theme's ANSI colors.
    EXPECT_RGB(pal[0], t.ansi[0].r, t.ansi[0].g, t.ansi[0].b);
    EXPECT_RGB(pal[15], t.ansi[15].r, t.ansi[15].g, t.ansi[15].b);

    // 16 = cube origin (0,0,0); 231 = cube max (255,255,255).
    EXPECT_RGB(pal[16], 0, 0, 0);
    EXPECT_RGB(pal[231], 255, 255, 255);

    // Grayscale ramp: 232 = 8, 255 = 8 + 23*10 = 238.
    EXPECT_RGB(pal[232], 8, 8, 8);
    EXPECT_RGB(pal[255], 238, 238, 238);
}

static void test_selector(void)
{
    EXPECT_TRUE(theme_count() >= 5);

    int gi = theme_index_of("gruvbox");          // slug ↔ index round-trip
    EXPECT_TRUE(strcmp(theme_slug(gi), "gruvbox") == 0);
    EXPECT_TRUE(strcmp(theme_name(gi), "Gruvbox") == 0);

    EXPECT_TRUE(strcmp(theme_slug(theme_index_of("dark")), "onedark") == 0);   // legacy
    EXPECT_TRUE(strcmp(theme_slug(theme_index_of("light")), "onelight") == 0);
    EXPECT_TRUE(theme_index_of("bogus") == 0);    // unknown → One Dark

    EXPECT_TRUE(theme_index_of("darkmodern") > 0);
    EXPECT_TRUE(theme_index_of("githubdark") > 0);
    EXPECT_TRUE(theme_index_of("monokai") > 0);

    // Light variants present and flagged light.
    EXPECT_TRUE(theme_resolve("lightmodern").is_light);
    EXPECT_TRUE(theme_resolve("githublight").is_light);
    EXPECT_TRUE(theme_resolve("gruvboxlight").is_light);
}

int main(void)
{
    test_resolve();
    test_palette256();
    test_selector();

    if (failures) {
        fprintf(stderr, "%d theme test failure(s)\n", failures);
        return 1;
    }
    return 0;
}
