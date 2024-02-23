#include "ogc_keyboard.h"

#include <SDL.h>
#include <SDL_ttf.h>

#define ANIMATION_TIME_ENTER 1000
#define ANIMATION_TIME_EXIT 500
#define NUM_ROWS 5
#define NUM_LAYOUTS 4
#define ROW_HEIGHT 40
#define ROW_SPACING 12
#define KEYBOARD_HEIGHT (NUM_ROWS * (ROW_HEIGHT + ROW_SPACING))
#define FONT_NAME "ogcosk/keys.ttf"
#define FONT_SIZE 24
#define TEXTURE_CACHE_SIZE 40
#define FOCUS_BORDER 4

struct SDL_OGC_DriverData {
    int16_t screen_width;
    int16_t screen_height;
    int16_t start_pan_y;
    int16_t target_pan_y;
    int8_t focus_row;
    int8_t focus_col;
    int8_t highlight_row;
    int8_t highlight_col;
    int8_t active_layout;
    int visible_height;
    int start_ticks;
    int start_visible_height;
    int target_visible_height;
    int animation_time;
    SDL_Color key_color;
    SDL_Cursor *app_cursor;
    SDL_Cursor *default_cursor;
    TTF_Font *key_font;
    SDL_Texture **key_textures;
};

typedef struct RowLayout {
    const char **symbols;
} RowLayout;

typedef struct ButtonRow {
    int8_t start_x;
    int8_t spacing;
    int8_t num_keys;
    uint16_t special_keys_bitmask;
    uint16_t enter_key_bitmask;
    /* button widths, in units of 2 pixels */
    const uint8_t *widths;
    RowLayout layouts[NUM_LAYOUTS];
} ButtonRow;

typedef const ButtonRow *ButtonRows[];

static const char KEYCAP_BACKSPACE[] = "\u2190";
static const char KEYCAP_SHIFT[] = "\u2191";
static const char KEYCAP_SYM1[] = "1/2";
static const char KEYCAP_SYM2[] = "2/2";
static const char KEYCAP_SYMBOLS[] = "=\\<";
static const char KEYCAP_ABC[] = "abc";
static const char KEYCAP_SPACE[] = " ";
static const char KEYCAP_RETURN[] = "\u23CE";
static const char KEYCAP_PERIOD[] = ".";
static const SDL_Color ColorKeyBgLetter = { 0x5A, 0x60, 0x6A, 0xff };
static const SDL_Color ColorKeyBgLetterHigh = { 0x5A / 2, 0x60 / 2, 0x6A / 2, 0xff };
static const SDL_Color ColorKeyBgEnter = { 0x00, 0x3C, 0x00, 0xff };
static const SDL_Color ColorKeyBgEnterHigh = { 0x32, 0x3C*2, 0x3E, 0xff };
static const SDL_Color ColorKeyBgSpecial = { 0x32, 0x36, 0x3E, 0xff };
static const SDL_Color ColorKeyBgSpecialHigh = { 0x32/2, 0x36/2, 0x3E/2, 0xff };
static const SDL_Color ColorFocus = { 0xe0, 0xf0, 0x10, 0xff };

static const uint8_t s_widths_10[] = { 26, 26, 26, 26, 26, 26, 26, 26, 26, 26 };
static const char *row0syms[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "0" };
static const char *row0syms2[] = { "~", "@", "#", "$", "%", "^", "&", "*", "(", ")" };
static const ButtonRow row0 = { 6, 12, 10, 0x0, 0x0, s_widths_10, {
    { row0syms },
    { row0syms },
    { row0syms2 },
    { row0syms2 },
}};

static const char *row1syms0[] = { "q", "w", "e", "r", "t", "y", "u", "i", "o", "p" };
static const char *row1syms1[] = { "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P" };
static const char *row1syms2[] = { "\\", "/", "€", "¢", "=", "-", "_", "+", "[", "]" };
static const char *row1syms3[] = { "©", "®", "£", "µ", "¥", "№", "°", "\u2605", "\u261e", "\u261c" };
static const ButtonRow row1 = { 6, 12, 10, 0x0, 0x0, s_widths_10, {
    { row1syms0 },
    { row1syms1 },
    { row1syms2 },
    { row1syms3 },
}};

static const char *row2syms0[] = { "a", "s", "d", "f", "g", "h", "j", "k", "l" };
static const char *row2syms1[] = { "A", "S", "D", "F", "G", "H", "J", "K", "L" };
static const char *row2syms2[] = { "<", ">", "¿", "¡", "—", "´", "|", "{", "}" };
static const char *row2syms3[] = { "«", "»", "\u263A", "\u2639", "\U0001f600", "\U0001f609", "\U0001f622", "\U0001f607", "\U0001f608" };
static const ButtonRow row2 = { 38, 12, 9, 0x0, 0x0, s_widths_10, {
    { row2syms0 },
    { row2syms1 },
    { row2syms2 },
    { row2syms3 },
}};

static const uint8_t s_widths_7_2[] = { 42, 26, 26, 26, 26, 26, 26, 26, 42 };
static const char *row3syms0[] = { KEYCAP_SHIFT, "z", "x", "c", "v", "b", "n", "m", KEYCAP_BACKSPACE };
static const char *row3syms1[] = { KEYCAP_SHIFT, "Z", "X", "C", "V", "B", "N", "M", KEYCAP_BACKSPACE };
static const char *row3syms2[] = { KEYCAP_SYM1, "`", "\"", "'", ":", ";", "!", "?", KEYCAP_BACKSPACE };
static const char *row3syms3[] = { KEYCAP_SYM2, "\u26a0", "§", "±", "\u2642", "\u2640", "\u2600", "\u263e", KEYCAP_BACKSPACE };
static const ButtonRow row3 = { 6, 12, 9, 0x101, 0x0, s_widths_7_2, {
    { row3syms0 },
    { row3syms1 },
    { row3syms2 },
    { row3syms3 },
}};

static const uint8_t s_widths_bar[] = { 42, 26, 122, 26, 74 };
static const char *row4syms0[] = { KEYCAP_SYMBOLS, ",", KEYCAP_SPACE, KEYCAP_PERIOD, KEYCAP_RETURN };
static const char *row4syms2[] = { KEYCAP_ABC, ",", KEYCAP_SPACE, KEYCAP_PERIOD, KEYCAP_RETURN };
static const ButtonRow row4 = { 6, 12, 5, 0x1, 0x10, s_widths_bar, {
    { row4syms0 },
    { row4syms0 },
    { row4syms2 },
    { row4syms2 },
}};

static const ButtonRows rows = { &row0, &row1, &row2, &row3, &row4 };

static void HideScreenKeyboard(SDL_OGC_VkContext *context);

static inline int key_id_from_pos(int row, int col)
{
    int key_id = 0;
    for (int i = 0; i < row; i++) {
        key_id += rows[i]->num_keys;
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

    for (int i = 0; i < TEXTURE_CACHE_SIZE; i++) {
        SDL_Texture *texture = data->key_textures[i];
        if (texture) {
            SDL_DestroyTexture(texture);
        }
    }
    SDL_free(data->key_textures);
    data->key_textures = NULL;

}

static inline const char *text_by_pos(SDL_OGC_DriverData *data, int row, int col)
{
    const ButtonRow *br = rows[row];
    const RowLayout *layout = &br->layouts[data->active_layout];

    return layout->symbols ? layout->symbols[col] : NULL;
}

static inline SDL_Texture *load_key_texture(SDL_OGC_DriverData *data, SDL_Renderer *renderer,
                                            int row, int col)
{
    const char *text;
    SDL_Surface *surface;
    SDL_Texture *texture;

    text = text_by_pos(data, row, col);
    if (!text) return NULL;

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
    if (!texture) return;

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
    const SDL_Color *color;
    const ButtonRow *br = rows[row];
    uint16_t col_mask = 1 << col;

    if (row == data->focus_row && col == data->focus_col) {
        SDL_Rect r;
        SDL_SetRenderDrawColor(renderer, ColorFocus.r, ColorFocus.g,
                               ColorFocus.b, ColorFocus.a);
        r.x = rect->x - FOCUS_BORDER;
        r.y = rect->y - FOCUS_BORDER;
        r.w = rect->w + FOCUS_BORDER * 2;
        r.h = rect->h + FOCUS_BORDER * 2;
        SDL_RenderFillRect(renderer, &r);
    }

    highlighted = row == data->highlight_row && col == data->highlight_col;
    if (col_mask & br->enter_key_bitmask) {
        color = highlighted ? &ColorKeyBgEnterHigh : &ColorKeyBgEnter;
    } else if (col_mask & br->special_keys_bitmask) {
        color = highlighted ? &ColorKeyBgSpecialHigh : &ColorKeyBgSpecial;
    } else {
        color = highlighted ? &ColorKeyBgLetterHigh : &ColorKeyBgLetter;
    }
    SDL_SetRenderDrawColor(renderer, color->r, color->g, color->b, color->a);
    SDL_RenderFillRect(renderer, rect);
}

static void draw_keyboard(SDL_OGC_VkContext *context, SDL_Renderer *renderer)
{
    SDL_OGC_DriverData *data = context->driverdata;
    int start_y = data->screen_height - data->visible_height + 5;

    for (int row = 0; row < NUM_ROWS; row++) {
        const ButtonRow *br = rows[row];
        int y = start_y + (ROW_HEIGHT + ROW_SPACING) * row;
        int x = br->start_x;

        for (int col = 0; col < br->num_keys; col++) {
            SDL_Rect rect;
            rect.x = x;
            rect.y = y;
            rect.w = br->widths[col] * 2;
            rect.h = ROW_HEIGHT;
            draw_key_background(context, renderer, &rect, row, col);
            draw_key(context, renderer, row, col, &rect);

            x += br->widths[col] * 2 + br->spacing;
        }
    }
}

static void dispose_keyboard(SDL_OGC_VkContext *context)
{
    SDL_OGC_DriverData *data = context->driverdata;

    context->is_open = SDL_FALSE;
    free_key_textures(data);

    if (data->app_cursor) {
        SDL_SetCursor(data->app_cursor);
        data->app_cursor = NULL;
    }
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
        data->animation_time = 0;
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

    for (int row = 0; row < NUM_ROWS; row++) {
        const ButtonRow *br = rows[row];
        int y = start_y + (ROW_HEIGHT + ROW_SPACING) * row;
        int x;

        if (py < y) break;
        if (py >= y + ROW_HEIGHT) continue;

        x = br->start_x;

        for (int col = 0; col < br->num_keys; col++) {
            if (px > x && px < x + br->widths[col] * 2) {
                *out_row = row;
                *out_col = col;
                return 1;
            }
            x += br->widths[col] * 2 + br->spacing;
        }
    }
    return 0;
}

static void switch_layout(SDL_OGC_VkContext *context, int level)
{
    SDL_OGC_DriverData *data = context->driverdata;

    data->active_layout = level;
    free_key_textures(data);
    initialize_key_textures(data);
}

static void activate_mouse(SDL_OGC_DriverData *data)
{
    data->focus_row = -1;
}

static void activate_joypad(SDL_OGC_DriverData *data)
{
    if (data->focus_row < 0) {
        data->focus_row = 2;
        data->focus_col = rows[data->focus_row]->num_keys / 2;
    }
    data->highlight_row = -1;
}

static void activate_key(SDL_OGC_VkContext *context, int row, int col)
{
    SDL_OGC_DriverData *data = context->driverdata;
    const char *text = text_by_pos(data, row, col);

    /* We can use pointer comparisons here */
    if (text == KEYCAP_BACKSPACE) {
        SDL_OGC_SendVirtualKeyboardKey(SDL_PRESSED, SDL_SCANCODE_BACKSPACE);
    } else if (text == KEYCAP_RETURN) {
        SDL_OGC_SendVirtualKeyboardKey(SDL_PRESSED, SDL_SCANCODE_RETURN);
    } else if (text == KEYCAP_ABC) {
        switch_layout(context, 0);
    } else if (text == KEYCAP_SHIFT) {
        switch_layout(context, !data->active_layout);
    } else if (text == KEYCAP_SYMBOLS || text == KEYCAP_SYM2) {
        switch_layout(context, 2);
    } else if (text == KEYCAP_SYM1) {
        switch_layout(context, 3);
    } else {
        SDL_OGC_SendKeyboardText(text);
    }
}

static void handle_click(SDL_OGC_VkContext *context, int px, int py)
{
    SDL_OGC_DriverData *data = context->driverdata;
    int row, col;

    if (data->focus_row >= 0) return;

    if (py < data->screen_height - KEYBOARD_HEIGHT) {
        HideScreenKeyboard(context);
        return;
    }

    if (key_at(context, px, py, &row, &col)) {
        activate_key(context, row, col);
    }
}

static void handle_motion(SDL_OGC_VkContext *context, int px, int py)
{
    SDL_OGC_DriverData *data = context->driverdata;
    int row, col;

    activate_mouse(data);

    if (key_at(context, px, py, &row, &col)) {
        data->highlight_row = row;
        data->highlight_col = col;
    } else {
        data->highlight_row = -1;
    }
}

static void move_right(SDL_OGC_DriverData *data)
{
    data->focus_col++;
    if (data->focus_col >= rows[data->focus_row]->num_keys) {
        data->focus_col = 0;
    }
}

static void move_left(SDL_OGC_DriverData *data)
{
    data->focus_col--;
    if (data->focus_col < 0) {
        data->focus_col = rows[data->focus_row]->num_keys - 1;
    }
}

static int adjust_column(int row, int oldrow, int oldcol) {
    const ButtonRow *br = rows[oldrow];
    int x, oldx, col;

    x = br->start_x;
    for (col = 0; col < oldcol; col++) {
        x += br->widths[col] * 2 + br->spacing;
    }
    /* Take the center of the button */
    oldx = x + br->widths[oldcol];

    /* Now find a button at about the same x in the new row */
    br = rows[row];
    x = br->start_x;
    for (col = 0; col < br->num_keys; col++) {
        if (x > oldx) {
            return col > 0 ? (col - 1) : col;
        }
        x += br->widths[col] * 2 + br->spacing;
    }
    return col - 1;
}

static void move_up(SDL_OGC_DriverData *data)
{
    int oldrow = data->focus_row;

    data->focus_row--;
    if (data->focus_row < 0) {
        data->focus_row = NUM_ROWS - 1;
    }

    if (oldrow >= 0) {
        data->focus_col = adjust_column(data->focus_row, oldrow, data->focus_col);
    }
}

static void move_down(SDL_OGC_DriverData *data)
{
    int oldrow = data->focus_row;

    data->focus_row++;
    if (data->focus_row >= NUM_ROWS) {
        data->focus_row = 0;
    }

    if (oldrow >= 0) {
        data->focus_col = adjust_column(data->focus_row, oldrow, data->focus_col);
    }
}

static void handle_joy_axis(SDL_OGC_VkContext *context,
                            const SDL_JoyAxisEvent *event)
{
    SDL_OGC_DriverData *data = context->driverdata;

    activate_joypad(data);

    if (event->axis == 0) {
        if (event->value > 256) move_right(data);
        else if (event->value < -256) move_left(data);
    } else if (event->axis == 1) {
        if (event->value > 256) move_down(data);
        else if (event->value < -256) move_up(data);
    }
}

static void handle_joy_hat(SDL_OGC_VkContext *context, Uint8 pos)
{
    SDL_OGC_DriverData *data = context->driverdata;

    activate_joypad(data);

    switch (pos) {
    case SDL_HAT_RIGHT: move_right(data); break;
    case SDL_HAT_LEFT: move_left(data); break;
    case SDL_HAT_DOWN: move_down(data); break;
    case SDL_HAT_UP: move_up(data); break;
    }
}

static void handle_joy_button(SDL_OGC_VkContext *context,
                              Uint8 button, Uint8 state)
{
    SDL_OGC_DriverData *data = context->driverdata;

    if (data->focus_row < 0) return;

    printf("Button %d, state %d\n", button, state);
    /* For now, only handle button press */
    if (state != SDL_PRESSED) return;

    switch (button) {
    case 0:
        activate_key(context, data->focus_row, data->focus_col);
        break;
    case 1:
        SDL_OGC_SendVirtualKeyboardKey(SDL_PRESSED, SDL_SCANCODE_BACKSPACE);
        break;
    }
}

static void Init(SDL_OGC_VkContext *context)
{
    SDL_OGC_DriverData *data;

    printf("%s called\n", __func__);
    TTF_Init();

    data = SDL_calloc(sizeof(SDL_OGC_DriverData), 1);
    data->highlight_row = -1;
    data->focus_row = -1;
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
    if (data->animation_time > 0) {
        update_animation(context);
        if (!context->is_open) return;
    }

    renderer = SDL_GetRenderer(context->window);
    SDL_SetRenderDrawColor(renderer, 0x0e, 0x0e, 0x12, 255);
    osk_rect.x = 0;
    osk_rect.y = data->screen_height - data->visible_height;
    osk_rect.w = data->screen_width;
    osk_rect.h = KEYBOARD_HEIGHT;
    SDL_RenderFillRect(renderer, &osk_rect);

    draw_keyboard(context, renderer);

    if (data->app_cursor) {
        SDL_SetCursor(data->default_cursor);
    }
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
    case SDL_JOYAXISMOTION:
        handle_joy_axis(context, &event->jaxis);
        return SDL_TRUE;
    case SDL_JOYHATMOTION:
        handle_joy_hat(context, event->jhat.value);
        return SDL_TRUE;
    case SDL_JOYBUTTONDOWN:
    case SDL_JOYBUTTONUP:
        handle_joy_button(context, event->jbutton.button,
                          event->jbutton.state);
        return SDL_TRUE;
    }

    if (event->type >= SDL_MOUSEMOTION &&
        event->type <= SDL_CONTROLLERSENSORUPDATE) {
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
    SDL_Cursor *cursor, *default_cursor;

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

    cursor = SDL_GetCursor();
    default_cursor = SDL_GetDefaultCursor();
    if (cursor != default_cursor) {
        data->app_cursor = cursor;
        data->default_cursor = default_cursor;
    }
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

