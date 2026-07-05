#include "kitty_images.h"

#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include "raylib.h"

#define KITTY_IMAGE_CACHE_CAP 256
#define KITTY_IMAGE_STALE_FRAME_LIMIT 2

typedef struct {
    bool occupied;
    bool seen;
    int stale_frames;
    KittyImageCacheKey key;
    Texture2D texture;
} KittyImageCacheEntry;

struct KittyImageRenderer {
    KittyImageCacheEntry cache[KITTY_IMAGE_CACHE_CAP];
    Texture2D deferred_textures[256];
    int deferred_texture_count;
};

KittyImageDrawPoint kitty_image_layer_draw_point(GhosttyKittyPlacementLayer layer)
{
    switch (layer) {
        case GHOSTTY_KITTY_PLACEMENT_LAYER_BELOW_BG:
            return KITTY_IMAGE_DRAW_BEFORE_BACKGROUNDS;
        case GHOSTTY_KITTY_PLACEMENT_LAYER_BELOW_TEXT:
            return KITTY_IMAGE_DRAW_BEFORE_TEXT;
        case GHOSTTY_KITTY_PLACEMENT_LAYER_ABOVE_TEXT:
        case GHOSTTY_KITTY_PLACEMENT_LAYER_ALL:
        default:
            return KITTY_IMAGE_DRAW_AFTER_TEXT;
    }
}

bool kitty_image_cache_key_equal(const KittyImageCacheKey *a,
                                 const KittyImageCacheKey *b)
{
    return a && b
        && a->image_id == b->image_id
        && a->image_handle == b->image_handle
        && a->data_ptr == b->data_ptr
        && a->data_len == b->data_len
        && a->width == b->width
        && a->height == b->height
        && a->format == b->format;
}

static size_t kitty_image_format_bpp(GhosttyKittyImageFormat format)
{
    switch (format) {
        case GHOSTTY_KITTY_IMAGE_FORMAT_RGBA:
            return 4;
        case GHOSTTY_KITTY_IMAGE_FORMAT_RGB:
            return 3;
        case GHOSTTY_KITTY_IMAGE_FORMAT_GRAY_ALPHA:
            return 2;
        case GHOSTTY_KITTY_IMAGE_FORMAT_GRAY:
            return 1;
        case GHOSTTY_KITTY_IMAGE_FORMAT_PNG:
        default:
            return 0;
    }
}

bool kitty_image_decode_to_rgba(const uint8_t *src, size_t src_len,
                                GhosttyKittyImageFormat format,
                                uint32_t width, uint32_t height,
                                uint8_t *dst, size_t dst_len)
{
    size_t pixels = (size_t)width * (size_t)height;
    if (!src || !dst || pixels == 0 || dst_len < pixels * 4)
        return false;

    size_t bpp = kitty_image_format_bpp(format);
    if (bpp == 0 || src_len < pixels * bpp)
        return false;

    switch (format) {
        case GHOSTTY_KITTY_IMAGE_FORMAT_RGBA:
            memcpy(dst, src, pixels * 4);
            return true;
        case GHOSTTY_KITTY_IMAGE_FORMAT_RGB:
            for (size_t i = 0; i < pixels; i++) {
                dst[i * 4 + 0] = src[i * 3 + 0];
                dst[i * 4 + 1] = src[i * 3 + 1];
                dst[i * 4 + 2] = src[i * 3 + 2];
                dst[i * 4 + 3] = 255;
            }
            return true;
        case GHOSTTY_KITTY_IMAGE_FORMAT_GRAY_ALPHA:
            for (size_t i = 0; i < pixels; i++) {
                uint8_t gray = src[i * 2 + 0];
                dst[i * 4 + 0] = gray;
                dst[i * 4 + 1] = gray;
                dst[i * 4 + 2] = gray;
                dst[i * 4 + 3] = src[i * 2 + 1];
            }
            return true;
        case GHOSTTY_KITTY_IMAGE_FORMAT_GRAY:
            for (size_t i = 0; i < pixels; i++) {
                uint8_t gray = src[i];
                dst[i * 4 + 0] = gray;
                dst[i * 4 + 1] = gray;
                dst[i * 4 + 2] = gray;
                dst[i * 4 + 3] = 255;
            }
            return true;
        case GHOSTTY_KITTY_IMAGE_FORMAT_PNG:
        default:
            return false;
    }
}

KittyImageRect kitty_image_dest_rect(int origin_x, int origin_y,
                                     int pad,
                                     int cell_width, int cell_height,
                                     int viewport_col, int viewport_row,
                                     int grid_cols, int grid_rows,
                                     int x_offset, int y_offset)
{
    KittyImageRect rect = {
        origin_x + pad + viewport_col * cell_width + x_offset,
        origin_y + pad + viewport_row * cell_height + y_offset,
        grid_cols * cell_width,
        grid_rows * cell_height,
    };
    return rect;
}

KittyImageRenderer *kitty_image_renderer_create(void)
{
    return (KittyImageRenderer *)calloc(1, sizeof(KittyImageRenderer));
}

void kitty_image_renderer_destroy(KittyImageRenderer *renderer)
{
    if (!renderer)
        return;
    for (int i = 0; i < KITTY_IMAGE_CACHE_CAP; i++) {
        if (renderer->cache[i].occupied)
            UnloadTexture(renderer->cache[i].texture);
    }
    for (int i = 0; i < renderer->deferred_texture_count; i++)
        UnloadTexture(renderer->deferred_textures[i]);
    free(renderer);
}

void kitty_image_renderer_begin_frame(KittyImageRenderer *renderer)
{
    if (!renderer)
        return;
    for (int i = 0; i < KITTY_IMAGE_CACHE_CAP; i++)
        renderer->cache[i].seen = false;
}

static void defer_unload_texture(KittyImageRenderer *renderer, Texture2D tex)
{
    if (!renderer) {
        UnloadTexture(tex);
        return;
    }

    int cap = (int)(sizeof(renderer->deferred_textures) / sizeof(renderer->deferred_textures[0]));
    if (renderer->deferred_texture_count < cap)
        renderer->deferred_textures[renderer->deferred_texture_count++] = tex;
    else
        UnloadTexture(tex);
}

void kitty_image_renderer_end_frame(KittyImageRenderer *renderer)
{
    if (!renderer)
        return;
    for (int i = 0; i < renderer->deferred_texture_count; i++)
        UnloadTexture(renderer->deferred_textures[i]);
    renderer->deferred_texture_count = 0;

    for (int i = 0; i < KITTY_IMAGE_CACHE_CAP; i++) {
        KittyImageCacheEntry *entry = &renderer->cache[i];
        if (!entry->occupied)
            continue;
        if (entry->seen) {
            entry->stale_frames = 0;
            continue;
        }
        entry->stale_frames++;
        if (entry->stale_frames > KITTY_IMAGE_STALE_FRAME_LIMIT) {
            UnloadTexture(entry->texture);
            memset(entry, 0, sizeof(*entry));
        }
    }
}

static Texture2D *find_cached_texture(KittyImageRenderer *renderer,
                                      const KittyImageCacheKey *key)
{
    for (int i = 0; i < KITTY_IMAGE_CACHE_CAP; i++) {
        KittyImageCacheEntry *entry = &renderer->cache[i];
        if (entry->occupied && kitty_image_cache_key_equal(&entry->key, key)) {
            entry->seen = true;
            entry->stale_frames = 0;
            return &entry->texture;
        }
    }
    return NULL;
}

static KittyImageCacheEntry *find_cache_slot(KittyImageRenderer *renderer)
{
    for (int i = 0; i < KITTY_IMAGE_CACHE_CAP; i++) {
        if (!renderer->cache[i].occupied)
            return &renderer->cache[i];
    }

    UnloadTexture(renderer->cache[0].texture);
    memset(&renderer->cache[0], 0, sizeof(renderer->cache[0]));
    return &renderer->cache[0];
}

static Texture2D *cache_texture(KittyImageRenderer *renderer,
                                const KittyImageCacheKey *key,
                                Texture2D texture)
{
    KittyImageCacheEntry *slot = find_cache_slot(renderer);
    slot->occupied = true;
    slot->seen = true;
    slot->stale_frames = 0;
    slot->key = *key;
    slot->texture = texture;
    return &slot->texture;
}

static Texture2D load_texture_for_image(const uint8_t *data_ptr,
                                        size_t data_len,
                                        uint32_t img_w,
                                        uint32_t img_h,
                                        GhosttyKittyImageFormat fmt)
{
    uint8_t *converted = NULL;
    const uint8_t *texture_data = data_ptr;

    if (fmt != GHOSTTY_KITTY_IMAGE_FORMAT_RGBA) {
        size_t rgba_len = (size_t)img_w * (size_t)img_h * 4;
        converted = (uint8_t *)malloc(rgba_len);
        if (!converted)
            return (Texture2D){0};
        if (!kitty_image_decode_to_rgba(data_ptr, data_len, fmt, img_w, img_h,
                                        converted, rgba_len)) {
            free(converted);
            return (Texture2D){0};
        }
        texture_data = converted;
    }

    Image img = {
        .data    = (void *)(uintptr_t)texture_data,
        .width   = (int)img_w,
        .height  = (int)img_h,
        .mipmaps = 1,
        .format  = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
    };
    Texture2D tex = LoadTextureFromImage(img);
    if (tex.id != 0)
        SetTextureFilter(tex, TEXTURE_FILTER_BILINEAR);

    free(converted);
    return tex;
}

void kitty_image_renderer_draw_layer(KittyImageRenderer *renderer,
                                     GhosttyTerminal terminal,
                                     GhosttyKittyGraphics graphics,
                                     GhosttyKittyGraphicsPlacementIterator placement_iter,
                                     int origin_x, int origin_y,
                                     int cell_width, int cell_height,
                                     int pad,
                                     GhosttyKittyPlacementLayer layer)
{
    if (!renderer || !terminal || !graphics || !placement_iter)
        return;

    ghostty_kitty_graphics_placement_iterator_set(placement_iter,
        GHOSTTY_KITTY_GRAPHICS_PLACEMENT_ITERATOR_OPTION_LAYER, &layer);

    if (ghostty_kitty_graphics_get(graphics,
            GHOSTTY_KITTY_GRAPHICS_DATA_PLACEMENT_ITERATOR,
            &placement_iter) != GHOSTTY_SUCCESS)
        return;

    while (ghostty_kitty_graphics_placement_next(placement_iter)) {
        uint32_t image_id = 0;
        ghostty_kitty_graphics_placement_get(placement_iter,
            GHOSTTY_KITTY_GRAPHICS_PLACEMENT_DATA_IMAGE_ID, &image_id);

        GhosttyKittyGraphicsImage image_handle =
            ghostty_kitty_graphics_image(graphics, image_id);
        if (!image_handle)
            continue;

        int32_t vp_col = 0, vp_row = 0;
        if (ghostty_kitty_graphics_placement_viewport_pos(
                placement_iter, image_handle, terminal,
                &vp_col, &vp_row) != GHOSTTY_SUCCESS)
            continue;

        uint32_t img_w = 0, img_h = 0;
        ghostty_kitty_graphics_image_get(image_handle,
            GHOSTTY_KITTY_IMAGE_DATA_WIDTH, &img_w);
        ghostty_kitty_graphics_image_get(image_handle,
            GHOSTTY_KITTY_IMAGE_DATA_HEIGHT, &img_h);
        if (img_w == 0 || img_h == 0)
            continue;

        GhosttyKittyImageFormat fmt = GHOSTTY_KITTY_IMAGE_FORMAT_RGBA;
        ghostty_kitty_graphics_image_get(image_handle,
            GHOSTTY_KITTY_IMAGE_DATA_FORMAT, &fmt);

        GhosttyKittyImageCompression compression = GHOSTTY_KITTY_IMAGE_COMPRESSION_NONE;
        ghostty_kitty_graphics_image_get(image_handle,
            GHOSTTY_KITTY_IMAGE_DATA_COMPRESSION, &compression);
        if (compression != GHOSTTY_KITTY_IMAGE_COMPRESSION_NONE)
            continue;

        const uint8_t *data_ptr = NULL;
        size_t data_len = 0;
        ghostty_kitty_graphics_image_get(image_handle,
            GHOSTTY_KITTY_IMAGE_DATA_DATA_PTR, &data_ptr);
        ghostty_kitty_graphics_image_get(image_handle,
            GHOSTTY_KITTY_IMAGE_DATA_DATA_LEN, &data_len);
        size_t bpp = kitty_image_format_bpp(fmt);
        if (!data_ptr || bpp == 0 || data_len < (size_t)img_w * img_h * bpp)
            continue;

        uint32_t grid_cols = 0, grid_rows = 0;
        if (ghostty_kitty_graphics_placement_grid_size(
                placement_iter, image_handle, terminal,
                &grid_cols, &grid_rows) != GHOSTTY_SUCCESS)
            continue;
        if (grid_cols == 0 || grid_rows == 0)
            continue;

        uint32_t src_x = 0, src_y = 0, src_w = 0, src_h = 0;
        if (ghostty_kitty_graphics_placement_source_rect(
                placement_iter, image_handle,
                &src_x, &src_y, &src_w, &src_h) != GHOSTTY_SUCCESS)
            continue;

        uint32_t x_offset = 0, y_offset = 0;
        ghostty_kitty_graphics_placement_get(placement_iter,
            GHOSTTY_KITTY_GRAPHICS_PLACEMENT_DATA_X_OFFSET, &x_offset);
        ghostty_kitty_graphics_placement_get(placement_iter,
            GHOSTTY_KITTY_GRAPHICS_PLACEMENT_DATA_Y_OFFSET, &y_offset);

        KittyImageCacheKey key = {
            .image_id = image_id,
            .image_handle = (const void *)image_handle,
            .data_ptr = (const void *)data_ptr,
            .data_len = data_len,
            .width = img_w,
            .height = img_h,
            .format = fmt,
        };

        Texture2D *tex = find_cached_texture(renderer, &key);
        if (!tex) {
            Texture2D loaded = load_texture_for_image(data_ptr, data_len, img_w, img_h, fmt);
            if (loaded.id == 0)
                continue;
            tex = cache_texture(renderer, &key, loaded);
        }

        KittyImageRect dest = kitty_image_dest_rect(
            origin_x, origin_y, pad,
            cell_width, cell_height,
            (int)vp_col, (int)vp_row,
            (int)grid_cols, (int)grid_rows,
            (int)x_offset, (int)y_offset);

        Rectangle src_rect = {
            (float)src_x, (float)src_y,
            (float)src_w, (float)src_h
        };
        Rectangle dst_rect = {
            (float)dest.x, (float)dest.y,
            (float)dest.w, (float)dest.h
        };
        DrawTexturePro(*tex, src_rect, dst_rect,
                       (Vector2){0, 0}, 0.0f, WHITE);
    }
}
