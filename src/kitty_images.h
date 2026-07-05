#ifndef FANGS_KITTY_IMAGES_H
#define FANGS_KITTY_IMAGES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <ghostty/vt.h>

typedef struct KittyImageRenderer KittyImageRenderer;

typedef struct {
    int x;
    int y;
    int w;
    int h;
} KittyImageRect;

typedef struct {
    uint32_t image_id;
    const void *image_handle;
    const void *data_ptr;
    size_t data_len;
    uint32_t width;
    uint32_t height;
    GhosttyKittyImageFormat format;
} KittyImageCacheKey;

typedef enum {
    KITTY_IMAGE_DRAW_BEFORE_BACKGROUNDS = 0,
    KITTY_IMAGE_DRAW_BEFORE_TEXT = 1,
    KITTY_IMAGE_DRAW_AFTER_TEXT = 2,
} KittyImageDrawPoint;

KittyImageDrawPoint kitty_image_layer_draw_point(GhosttyKittyPlacementLayer layer);
bool kitty_image_cache_key_equal(const KittyImageCacheKey *a,
                                 const KittyImageCacheKey *b);
bool kitty_image_decode_to_rgba(const uint8_t *src, size_t src_len,
                                GhosttyKittyImageFormat format,
                                uint32_t width, uint32_t height,
                                uint8_t *dst, size_t dst_len);

KittyImageRect kitty_image_dest_rect(int origin_x, int origin_y,
                                     int pad,
                                     int cell_width, int cell_height,
                                     int viewport_col, int viewport_row,
                                     int grid_cols, int grid_rows,
                                     int x_offset, int y_offset);

KittyImageRenderer *kitty_image_renderer_create(void);
void kitty_image_renderer_destroy(KittyImageRenderer *renderer);
void kitty_image_renderer_begin_frame(KittyImageRenderer *renderer);
void kitty_image_renderer_end_frame(KittyImageRenderer *renderer);

void kitty_image_renderer_draw_layer(KittyImageRenderer *renderer,
                                     GhosttyTerminal terminal,
                                     GhosttyKittyGraphics graphics,
                                     GhosttyKittyGraphicsPlacementIterator placement_iter,
                                     int origin_x, int origin_y,
                                     int cell_width, int cell_height,
                                     int pad,
                                     GhosttyKittyPlacementLayer layer);

#endif // FANGS_KITTY_IMAGES_H
