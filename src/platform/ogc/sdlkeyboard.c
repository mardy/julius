#include "ogc_keyboard.h"

#include <SDL.h>
#include <SDL_ttf.h>

#define ANIMATION_TIME_ENTER 2000
#define ANIMATION_TIME_EXIT 500
#define NUM_ROWS 4
#define NUM_LAYOUTS 4
#define ROW_HEIGHT 40
#define ROW_SPACING 8
#define KEYBOARD_HEIGHT 320
#define FONT_NAME "ogcosk/keys.ttf"
#define FONT_SIZE 16
#define TEXTURE_CACHE_SIZE 40

struct SDL_OGC_DriverData {
    int16_t screen_width;
    int16_t screen_height;
    int16_t start_pan_y;
    int16_t target_pan_y;
    int8_t highlight_row;
    int8_t highlight_col;
    int8_t active_layout;
    int visible_height;
    int start_ticks;
    int start_visible_height;
    int target_visible_height;
    int animation_time;
    SDL_Color key_color;
    TTF_Font *key_font;
    SDL_Texture **key_textures;
};

typedef struct SpecialKey {
    int8_t width;
    const char *symbol;
} SpecialKey;

typedef struct RowLayout {
    const SpecialKey *sk_left;
    const char **symbols;
    const SpecialKey *sk_right;
} RowLayout;

typedef struct ButtonRow {
    int8_t start_x;
    int8_t spacing;
    int8_t width;
    int8_t num_sk_left;
    int8_t num_symbols;
    int8_t num_sk_right;
    RowLayout layouts[NUM_LAYOUTS];
} ButtonRow;

typedef const ButtonRow *ButtonRows[];

static const char KEYCAP_BACKSPACE[] = "\u2190";
static const char KEYCAP_SHIFT[] = "\u2191";
static const char *KEYCAP_NUMBERS = "123";
static const char *KEYCAP_SYMBOLS = "SYM";
static const char *KEYCAP_ABC = "abc";
static const char *KEYCAP_SPACE = " ";
static const char *KEYCAP_RETURN = "\n";
static const char *KEYCAP_PERIOD = ".";

static const SpecialKey sk_backspace = { 85, KEYCAP_BACKSPACE };
static const SpecialKey sk_shift = { 85, KEYCAP_SHIFT };
static const char *row0syms[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "0" };
static const ButtonRow row0 = { 14, 8, 54, 0, 10, 0, {
    { NULL, row0syms, NULL },
}};
static const char *row1syms0[] = { "q", "w", "e", "r", "t", "y", "u", "i", "o", "p" };
static const char *row1syms1[] = { "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P" };
static const ButtonRow row1 = { 14, 8, 54, 0, 10, 0, {
    { NULL, row1syms0, NULL },
    { NULL, row1syms1, NULL },
}};
static const char *row2syms0[] = { "a", "s", "d", "f", "g", "h", "j", "k", "l" };
static const char *row2syms1[] = { "A", "S", "D", "F", "G", "H", "J", "K", "L" };
static const ButtonRow row2 = { 45, 8, 54, 0, 9, 0, {
    { NULL, row2syms0, NULL },
    { NULL, row2syms1, NULL },
}};
static const char *row3syms0[] = { "z", "x", "c", "v", "b", "n", "m" };
static const char *row3syms1[] = { "Z", "X", "C", "V", "B", "N", "M" };
static const ButtonRow row3 = { 14, 8, 54, 1, 7, 1, {
    { &sk_shift, row3syms0, &sk_backspace },
    { &sk_shift, row3syms1, &sk_backspace },
}};

static const ButtonRows rows = { &row0, &row1, &row2, &row3 };

static void HideScreenKeyboard(SDL_OGC_VkContext *context);

static inline int key_id_from_pos(int row, int col)
{
    int key_id = 0;
    for (int i = 0; i < row; i++) {
        key_id += rows[i]->num_sk_left + rows[i]->num_symbols + rows[i]->num_sk_right;
    }
    key_id += col;
    return key_id;
}

static inline void key_id_to_pos(int key_id, int *row, int *col)
{
}

static void initialize_key_textures(SDL_OGC_DriverData *data)
{
    if (data->key_textures) return;

    data->key_textures = SDL_calloc(sizeof(SDL_Texture *), TEXTURE_CACHE_SIZE);
}

static void free_key_textures(SDL_OGC_DriverData *data)
{
    if (!data->key_textures) return;

    for (SDL_Texture **t = data->key_textures; *t != NULL; t++) {
        SDL_DestroyTexture(*t);
    }
    SDL_free(data->key_textures);
    data->key_textures = NULL;

}

static inline const char *text_by_pos(SDL_OGC_DriverData *data, int row, int col)
{
    const ButtonRow *br = rows[row];
    int l = data->active_layout;

    if (col < br->num_sk_left) {
        return br->layouts[l].sk_left[col].symbol;
    }

    int key = col - br->num_sk_left;
    if (key < br->num_symbols) {
        return br->layouts[l].symbols[key];
    }

    return br->layouts[l].sk_right[key - br->num_symbols].symbol;
}

static inline SDL_Texture *load_key_texture(SDL_OGC_DriverData *data, SDL_Renderer *renderer,
                                            int row, int col)
{
    const char *text;
    SDL_Surface *surface;
    SDL_Texture *texture;

    printf("Have to load texture %d %d\n", row, col);
    text = text_by_pos(data, row, col);
    surface = TTF_RenderUTF8_Blended(data->key_font, text, data->key_color);
    texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);
    return texture;
}

static inline SDL_Texture *lookup_key_texture(SDL_OGC_DriverData *data, SDL_Renderer *renderer,
                                              int row, int col)
{
    int key_id;

    key_id = key_id_from_pos(row, col);
    if (data->key_textures[key_id] == NULL) {
        data->key_textures[key_id] = load_key_texture(data, renderer, row, col);
    }

    return data->key_textures[key_id];
}

static inline void draw_key(SDL_OGC_VkContext *context, SDL_Renderer *renderer,
                            int row, int col, const SDL_Rect *rect)
{
    SDL_OGC_DriverData *data = context->driverdata;
    SDL_Texture *texture;
    SDL_Rect dstRect;

    texture = lookup_key_texture(data, renderer, row, col);
    SDL_QueryTexture(texture, NULL, NULL, &dstRect.w, &dstRect.h);
    dstRect.x = rect->x + (rect->w - dstRect.w) / 2;
    dstRect.y = rect->y + (rect->h - dstRect.h) / 2;
    SDL_RenderCopy(renderer, texture, NULL, &dstRect);
}

static inline void draw_key_background(SDL_OGC_VkContext *context, SDL_Renderer *renderer,
                                       SDL_Rect *rect, int row, int col)
{
    SDL_OGC_DriverData *data = context->driverdata;
    int highlighted;

    highlighted = row == data->highlight_row && col == data->highlight_col;
    if (highlighted) {
        SDL_SetRenderDrawColor(renderer, 0x5A/2, 0x60/2, 0x6A/2, 255);
    }
    SDL_RenderFillRect(renderer, rect);
    if (highlighted) {
        // restore previous color
        SDL_SetRenderDrawColor(renderer, 0x5A, 0x60, 0x6A, 255);
    }
}

static void draw_keyboard(SDL_OGC_VkContext *context, SDL_Renderer *renderer)
{
    SDL_OGC_DriverData *data = context->driverdata;
    int start_y = data->screen_height - data->visible_height + 5;
    int layout_idx = data->active_layout;

    SDL_SetRenderDrawColor(renderer, 0x5A, 0x60, 0x6A, 255);

    for (int row = 0; row < NUM_ROWS; row++) {
        const ButtonRow *br = rows[row];
        const RowLayout *layout = &br->layouts[layout_idx];
        int y = start_y + (ROW_HEIGHT + ROW_SPACING) * row;
        int x = br->start_x;
        int col = 0;

        for (int key = 0; key < br->num_sk_left; key++, col++) {
            const SpecialKey *sk = &layout->sk_left[key];
            SDL_Rect rect;
            rect.x = x;
            rect.y = y;
            rect.w = sk->width;
            rect.h = ROW_HEIGHT;
            draw_key_background(context, renderer, &rect, row, col);
            draw_key(context, renderer, row, col, &rect);

            x += sk->width + br->spacing;
        }

        for (int key = 0; key < br->num_symbols; key++, col++) {
            SDL_Rect rect;
            rect.x = x;
            rect.y = y;
            rect.w = br->width;
            rect.h = ROW_HEIGHT;
            draw_key_background(context, renderer, &rect, row, col);
            draw_key(context, renderer, row, col, &rect);

            x += br->width + br->spacing;
        }

        for (int key = 0; key < br->num_sk_right; key++, col++) {
            const SpecialKey *sk = &layout->sk_right[key];
            SDL_Rect rect;
            rect.x = x;
            rect.y = y;
            rect.w = sk->width;
            rect.h = ROW_HEIGHT;
            draw_key_background(context, renderer, &rect, row, col);
            draw_key(context, renderer, row, col, &rect);

            x += sk->width + br->spacing;
        }
    }
}

static void dispose_keyboard(SDL_OGC_VkContext *context)
{
    SDL_OGC_DriverData *data = context->driverdata;

    context->is_open = SDL_FALSE;
    free_key_textures(data);
}

static void update_animation(SDL_OGC_VkContext *context)
{
    SDL_OGC_DriverData *data = context->driverdata;
    uint32_t ticks, elapsed;
    int height_diff;

    ticks = SDL_GetTicks();
    elapsed = ticks - data->start_ticks;

    if (elapsed >= data->animation_time) {
        data->visible_height = data->target_visible_height;
        context->screen_pan_y = data->target_pan_y;
        printf("Desired state reached\n");
        if (data->target_visible_height == 0) {
            dispose_keyboard(context);
        }
    } else {
        height_diff = data->target_visible_height - data->start_visible_height;
        double pos = sin(M_PI_2 * elapsed / data->animation_time);
        data->visible_height = data->start_visible_height +
            height_diff * pos;
        height_diff = data->target_pan_y - data->start_pan_y;
        context->screen_pan_y = data->start_pan_y + height_diff * pos;
    }
}

static int key_at(SDL_OGC_VkContext *context, int px, int py,
                  int *out_row, int *out_col)
{
    SDL_OGC_DriverData *data = context->driverdata;
    int start_y = data->screen_height - data->visible_height + 5;
    int l = data->active_layout;

    for (int row = 0; row < NUM_ROWS; row++) {
        const ButtonRow *br = rows[row];
        int y = start_y + (ROW_HEIGHT + ROW_SPACING) * row;
        int x, col = 0;

        if (py < y) break;
        if (py >= y + ROW_HEIGHT) continue;

        x = br->start_x;

        for (int key = 0; key < br->num_sk_left; key++, col++) {
            const SpecialKey *sk = &br->layouts[l].sk_left[key];
            if (px > x && px < x + sk->width) {
                *out_row = row;
                *out_col = col;
                return 1;
            }
            x += sk->width + br->spacing;
        }

        for (int key = 0; key < br->num_symbols; key++, col++) {
            if (px > x && px < x + br->width) {
                *out_row = row;
                *out_col = col;
                return 1;
            }
            x += br->width + br->spacing;
        }

        for (int key = 0; key < br->num_sk_right; key++, col++) {
            const SpecialKey *sk = &br->layouts[l].sk_right[key];
            if (px > x && px < x + sk->width) {
                *out_row = row;
                *out_col = col;
                return 1;
            }
            x += sk->width + br->spacing;
        }
    }
    return 0;
}

static void handle_click(SDL_OGC_VkContext *context, int px, int py)
{
    SDL_OGC_DriverData *data = context->driverdata;
    int row, col;

    if (py < data->screen_height - KEYBOARD_HEIGHT) {
        HideScreenKeyboard(context);
        return;
    }

    if (key_at(context, px, py, &row, &col)) {
        const char *text = text_by_pos(data, row, col);

        /* We can use pointer comparisons here */
        if (text == KEYCAP_BACKSPACE) {
            SDL_OGC_SendVirtualKeyboardKey(SDL_PRESSED, SDL_SCANCODE_BACKSPACE);
        } else if (text == KEYCAP_SHIFT) {
            data->active_layout = !data->active_layout;
            free_key_textures(data);
            initialize_key_textures(data);
        } else {
            SDL_OGC_SendKeyboardText(text);
        }
    }
}

static void handle_motion(SDL_OGC_VkContext *context, int px, int py)
{
    SDL_OGC_DriverData *data = context->driverdata;
    int row, col;

    if (key_at(context, px, py, &row, &col)) {
        data->highlight_row = row;
        data->highlight_col = col;
    } else {
        data->highlight_row = -1;
    }
}

static void Init(SDL_OGC_VkContext *context)
{
    SDL_OGC_DriverData *data;

    printf("%s called\n", __func__);
    TTF_Init();

    data = SDL_calloc(sizeof(SDL_OGC_DriverData), 1);
    data->highlight_row = -1;
    data->key_color.r = 255;
    data->key_color.g = 255;
    data->key_color.b = 255;
    data->key_color.a = 255;
    data->key_font = TTF_OpenFont(FONT_NAME, FONT_SIZE);
    context->driverdata = data;
}

static void RenderKeyboard(SDL_OGC_VkContext *context)
{
    SDL_OGC_DriverData *data = context->driverdata;
    SDL_Renderer *renderer;
    SDL_Rect osk_rect;

    //printf("%s called\n", __func__);
    if (data->visible_height != data->target_visible_height) {
        update_animation(context);
    }

    renderer = SDL_GetRenderer(context->window);
    SDL_SetRenderDrawColor(renderer, 0x0e, 0x0e, 0x12, 255);
    osk_rect.x = 0;
    osk_rect.y = data->screen_height - data->visible_height;
    osk_rect.w = data->screen_width;
    osk_rect.h = KEYBOARD_HEIGHT;
    SDL_RenderFillRect(renderer, &osk_rect);

    draw_keyboard(context, renderer);

    SDL_RenderFlush(renderer);
}

static SDL_bool ProcessEvent(SDL_OGC_VkContext *context, SDL_Event *event)
{
    printf("%s called\n", __func__);
    switch (event->type) {
    case SDL_MOUSEBUTTONDOWN:
        handle_click(context, event->button.x, event->button.y);
        return SDL_TRUE;
    case SDL_MOUSEMOTION:
        handle_motion(context, event->motion.x, event->motion.y);
        return SDL_TRUE;
    }
    return SDL_FALSE;
}

static void StartTextInput(SDL_OGC_VkContext *context)
{
    printf("%s called\n", __func__);
}

static void StopTextInput(SDL_OGC_VkContext *context)
{
    printf("%s called\n", __func__);
}

static void SetTextInputRect(SDL_OGC_VkContext *context, const SDL_Rect *rect)
{
    SDL_OGC_DriverData *data = context->driverdata;


    if (rect) {
        memcpy(&context->input_rect, rect, sizeof(SDL_Rect));
    } else {
        memset(&context->input_rect, 0, sizeof(SDL_Rect));
    }

    if (context->input_rect.h != 0) {
        /* Pan the input rect so that it remains visible even when the OSK is
         * open */
        int desired_input_rect_y = (data->screen_height - KEYBOARD_HEIGHT - context->input_rect.h) / 2;
        data->target_pan_y = desired_input_rect_y - context->input_rect.y;
    } else {
        data->target_pan_y = 0;
    }
    data->start_pan_y = context->screen_pan_y;
}

static void ShowScreenKeyboard(SDL_OGC_VkContext *context)
{
    SDL_OGC_DriverData *data = context->driverdata;

    initialize_key_textures(data);

    printf("%s called\n", __func__);
    if (data->screen_width == 0) {
        SDL_Rect screen;
        SDL_GetDisplayBounds(0, &screen);
        data->screen_width = screen.w;
        data->screen_height = screen.h;
        printf("Screen: %d,%d\n", screen.w, screen.h);
    }
    context->is_open = SDL_TRUE;
    data->start_ticks = SDL_GetTicks();
    data->start_visible_height = data->visible_height;
    data->target_visible_height = KEYBOARD_HEIGHT;
    data->animation_time = ANIMATION_TIME_ENTER;
}

static void HideScreenKeyboard(SDL_OGC_VkContext *context)
{
    SDL_OGC_DriverData *data = context->driverdata;

    printf("%s called\n", __func__);
    data->start_ticks = SDL_GetTicks();
    data->start_visible_height = data->visible_height;
    data->target_visible_height = 0;
    data->start_pan_y = context->screen_pan_y;
    data->target_pan_y = 0;
    data->animation_time = ANIMATION_TIME_EXIT;
}

static const SDL_OGC_VkPlugin plugin = {
    .struct_size = sizeof(SDL_OGC_VkPlugin),
    .Init = Init,
    .RenderKeyboard = RenderKeyboard,
    .ProcessEvent = ProcessEvent,
    .StartTextInput = StartTextInput,
    .StopTextInput = StopTextInput,
    .SetTextInputRect = SetTextInputRect,
    .ShowScreenKeyboard = ShowScreenKeyboard,
    .HideScreenKeyboard = HideScreenKeyboard,
};

const SDL_OGC_VkPlugin *ogc_keyboard_get_plugin()
{
    printf("%s called\n", __func__);
    return &plugin;
}

