#include <cbm.h>
#include <c64.h>
#include <stdint.h>
#include <string.h>

#include "generated_story.h"
#include "scene01_bitmap.h"

#define BITMAP_RAM        ((uint8_t*)0xE000)
#define BITMAP_SCREEN_RAM ((uint8_t*)0xC000)
#define BITMAP_COLOR_RAM  ((uint8_t*)0xD800)
#define FONT_RAM          ((uint8_t*)0xC800)

#define BORDER_COLOR       (*(volatile uint8_t*)0xD020)
#define BG_COLOR           (*(volatile uint8_t*)0xD021)
#define VIC_CTRL1          (*(volatile uint8_t*)0xD011)
#define VIC_CTRL2          (*(volatile uint8_t*)0xD016)
#define VIC_MEMORY_CONTROL (*(volatile uint8_t*)0xD018)
#define VIC_BANK_SELECT    (*(volatile uint8_t*)0xDD00)
#define CPU_PORT           (*(volatile uint8_t*)0x0001)

#define SCREEN_W 40
#define SCREEN_H 25
#define TOP_ROWS 16
#define BOTTOM_START 16

#define DESC_ROW_START 17
#define DESC_ROWS 4

#define OPTION_ROW_START 21
#define OPTION_ROWS 3

#define MAX_DESC_PAGES 8

#define BITMAP_CHAR_HEIGHT 8
#define BITMAP_ROW_STRIDE 320u
#define BITMAP_TOTAL_BYTES 8000u
#define FONT_BYTES 1024u

#define BITMAP_ASSET_FILENAME "SCENE01.BMP"
#define BITMAP_LOAD_ADDR ((void*)0xE000)

static volatile uint8_t debug_stage = 0;

static void set_debug_marker(uint8_t stage, uint8_t border)
{
    uint8_t screen_code = 31;

    debug_stage = stage;
    BORDER_COLOR = border;

    if (stage >= 'A' && stage <= 'Z') {
        screen_code = (uint8_t)(stage - 64);
    }

    ((uint8_t*)0x0400)[0] = screen_code;
    ((uint8_t*)0xD800)[0] = COLOR_WHITE;
}

static uint8_t ascii_to_screen(uint8_t c)
{
    if (c >= 'a' && c <= 'z') {
        c = (uint8_t)(c - ('a' - 'A'));
    }

    if (c >= 64 && c <= 95) {
        return (uint8_t)(c - 64);
    }
    if (c >= 32 && c <= 63) {
        return c;
    }
    if (c >= 96 && c <= 127) {
        return (uint8_t)(c - 32);
    }

    return 32;
}

static uint16_t bitmap_cell_offset(uint8_t row, uint8_t col)
{
    return (uint16_t)row * BITMAP_ROW_STRIDE + (uint16_t)col * BITMAP_CHAR_HEIGHT;
}

static void copy_font_from_rom(void)
{
    uint8_t saved_cpu_port = CPU_PORT;
    uint16_t i;

    set_debug_marker('X', COLOR_BLUE);
    CPU_PORT = (uint8_t)(saved_cpu_port & 0xFBu);
    set_debug_marker('Y', COLOR_CYAN);
    for (i = 0; i < FONT_BYTES; ++i) {
        FONT_RAM[i] = ((uint8_t*)0xD000)[i];
        if (i == 512u) {
            set_debug_marker('Z', COLOR_LIGHTBLUE);
        }
    }
    CPU_PORT = saved_cpu_port;
    set_debug_marker('W', COLOR_PURPLE);
}

static void clear_bitmap(void)
{
    uint16_t i;
    for (i = 0; i < BITMAP_TOTAL_BYTES; ++i) {
        BITMAP_RAM[i] = 0x00;
    }
}

static void initialize_bitmap_colors(void)
{
    uint16_t i;
    for (i = 0; i < (uint16_t)(SCREEN_W * SCREEN_H); ++i) {
        BITMAP_SCREEN_RAM[i] = 0x01;
        BITMAP_COLOR_RAM[i] = COLOR_WHITE;
    }
}

static void set_bitmap_cell_color(uint8_t row, uint8_t col, uint8_t fg_color, uint8_t bg_color)
{
    uint16_t offset = (uint16_t)row * SCREEN_W + col;
    BITMAP_SCREEN_RAM[offset] = (uint8_t)((bg_color << 4) | (fg_color & 0x0Fu));
    BITMAP_COLOR_RAM[offset] = fg_color;
}

static void clear_bitmap_cell(uint8_t row, uint8_t col, uint8_t fg_color, uint8_t bg_color)
{
    uint16_t offset = bitmap_cell_offset(row, col);
    uint8_t pixel_row;

    for (pixel_row = 0; pixel_row < BITMAP_CHAR_HEIGHT; ++pixel_row) {
        BITMAP_RAM[offset + pixel_row] = 0x00;
    }

    set_bitmap_cell_color(row, col, fg_color, bg_color);
}

static void clear_line(uint8_t row, uint8_t color)
{
    uint8_t col;
    for (col = 0; col < SCREEN_W; ++col) {
        clear_bitmap_cell(row, col, color, COLOR_BLACK);
    }
}

static void draw_screen_code(uint8_t row, uint8_t col, uint8_t screen_code, uint8_t color)
{
    const uint8_t* glyph = &FONT_RAM[(uint16_t)screen_code * BITMAP_CHAR_HEIGHT];
    uint16_t offset = bitmap_cell_offset(row, col);
    uint8_t pixel_row;

    for (pixel_row = 0; pixel_row < BITMAP_CHAR_HEIGHT; ++pixel_row) {
        BITMAP_RAM[offset + pixel_row] = glyph[pixel_row];
    }

    set_bitmap_cell_color(row, col, color, COLOR_BLACK);
}

static void write_text(uint8_t row, uint8_t col, const char* text, uint8_t color)
{
    while (*text && col < SCREEN_W) {
        draw_screen_code(row, col, ascii_to_screen((uint8_t)*text), color);
        ++text;
        ++col;
    }
}

static uint16_t render_description_page(const char* text, uint16_t start, uint8_t draw)
{
    uint8_t current_row = DESC_ROW_START;
    uint8_t col = 0;
    uint16_t idx = start;

    while (text[idx] == ' ') {
        ++idx;
    }

    while (text[idx] != '\0' && current_row < (uint8_t)(DESC_ROW_START + DESC_ROWS)) {
        uint16_t word_start = idx;
        uint8_t word_len = 0;

        while (text[idx] != '\0' && text[idx] != ' ') {
            ++idx;
            ++word_len;
        }

        if (word_len == 0) {
            break;
        }

        if (word_len > SCREEN_W) {
            word_len = SCREEN_W;
        }

        if (col != 0 && (uint8_t)(col + 1u + word_len) > SCREEN_W) {
            ++current_row;
            col = 0;
            idx = word_start;
            continue;
        }

        if (current_row >= (uint8_t)(DESC_ROW_START + DESC_ROWS)) {
            idx = word_start;
            break;
        }

        if (col != 0) {
            if (draw) {
                draw_screen_code(current_row, col, ascii_to_screen((uint8_t)' '), COLOR_WHITE);
            }
            ++col;
        }

        {
            uint8_t i;
            for (i = 0; i < word_len && col < SCREEN_W; ++i) {
                if (draw) {
                    draw_screen_code(current_row, col, ascii_to_screen((uint8_t)text[word_start + i]), COLOR_WHITE);
                }
                ++col;
            }
        }

        while (text[idx] == ' ') {
            ++idx;
        }
    }

    while (text[idx] == ' ') {
        ++idx;
    }

    return idx;
}

static uint8_t compute_description_pages(const char* text, uint16_t* pages)
{
    uint8_t count = 1;
    pages[0] = 0;

    while (count < MAX_DESC_PAGES) {
        uint16_t next = render_description_page(text, pages[count - 1], 0);
        if (text[next] == '\0' || next == pages[count - 1]) {
            break;
        }
        pages[count] = next;
        ++count;
    }

    return count;
}

static void configure_bitmap_mode(void)
{
    VIC_BANK_SELECT = (uint8_t)(VIC_BANK_SELECT & 0xFCu);
    VIC_MEMORY_CONTROL = 0x08u;
    VIC_CTRL1 = (uint8_t)((VIC_CTRL1 & 0x80u) | 0x3Bu);
    VIC_CTRL2 = (uint8_t)((VIC_CTRL2 & 0xE0u) | 0x08u);
}

static void apply_monochrome_palette(void)
{
    uint8_t row;
    uint8_t col;

    for (row = 0; row < SCREEN_H; ++row) {
        for (col = 0; col < SCREEN_W; ++col) {
            set_bitmap_cell_color(row, col, COLOR_WHITE, COLOR_BLACK);
        }
    }
}

static void draw_top_fallback(void)
{
    static const uint8_t palette[] = {
        COLOR_BLACK,
        COLOR_BLUE,
        COLOR_CYAN,
        COLOR_LIGHTBLUE,
        COLOR_PURPLE,
        COLOR_RED
    };
    uint8_t row;

    for (row = 0; row < TOP_ROWS; ++row) {
        uint8_t col;
        uint8_t shade = palette[(row / 3u) % (sizeof(palette) / sizeof(palette[0]))];

        for (col = 0; col < SCREEN_W; ++col) {
            uint16_t offset = bitmap_cell_offset(row, col);
            uint8_t pixel_row;
            for (pixel_row = 0; pixel_row < BITMAP_CHAR_HEIGHT; ++pixel_row) {
                BITMAP_RAM[offset + pixel_row] = 0xFF;
            }
            set_bitmap_cell_color(row, col, shade, COLOR_BLACK);
        }
    }
}

static uint8_t load_bitmap_from_embedded(void)
{
    set_debug_marker('L', COLOR_ORANGE);
    set_debug_marker('T', COLOR_BROWN);  /* Test: before memcpy */
    memcpy(BITMAP_RAM, SCENE01_BITMAP_DATA, SCENE01_BITMAP_SIZE);
    set_debug_marker('U', COLOR_RED);    /* Test: after memcpy */
    set_debug_marker('R', COLOR_LIGHTGREEN);
    set_debug_marker('S', COLOR_GREEN);
    return 1;
}

/* Disk loading disabled; using embedded bitmap instead.
static uint8_t load_bitmap_from_disk(void)
{
    uint8_t status;

    set_debug_marker('L', COLOR_ORANGE);
    cbm_k_setlfs(1, 8, 0);
    set_debug_marker('N', COLOR_BROWN);
    cbm_k_setnam(BITMAP_ASSET_FILENAME);
    set_debug_marker('D', COLOR_RED);
    cbm_k_load(CBM_LOAD_RAM, BITMAP_LOAD_ADDR);
    set_debug_marker('R', COLOR_LIGHTGREEN);
    status = cbm_k_readst();
    cbm_k_clall();

    if (status == 0) {
        set_debug_marker('S', COLOR_GREEN);
    } else {
        set_debug_marker('E', COLOR_LIGHTRED);
    }

    return (uint8_t)(status == 0);
}
*/

static uint8_t find_scene_index(uint8_t scene_id)
{
    uint8_t i;
    for (i = 0; i < STORY_SCENE_COUNT; ++i) {
        if (STORY_SCENES[i].id == scene_id) {
            return i;
        }
    }
    return 0;
}

static uint8_t resolve_target(const StoryOption* option, uint8_t flags)
{
    if (option->condition == STORY_COND_HAS_MULTITOOL && !(flags & STORY_FLAG_MULTITOOL) && option->alt_target_scene != 255) {
        return option->alt_target_scene;
    }
    if (option->condition == STORY_COND_HAS_COFFEE && !(flags & STORY_FLAG_COFFEE) && option->alt_target_scene != 255) {
        return option->alt_target_scene;
    }
    return option->target_scene;
}

static void draw_scene(const StoryScene* scene, uint16_t page_start, uint8_t page_index, uint8_t page_count)
{
    uint8_t row;

    for (row = BOTTOM_START; row < SCREEN_H; ++row) {
        clear_line(row, COLOR_WHITE);
    }

    write_text(16, 0, "SCENE:", COLOR_YELLOW);
    write_text(16, 7, scene->title, COLOR_YELLOW);

    render_description_page(scene->description, page_start, 1);

    if (page_count > 1) {
        char page_buf[8] = {'P', 'G', ' ', '1', '/', '1', '\0', '\0'};
        page_buf[3] = (char)('1' + page_index);
        page_buf[5] = (char)('0' + page_count);
        write_text(16, 33, page_buf, COLOR_CYAN);
    }

    if (scene->option_count == 0) {
        write_text(21, 0, "END OF PART ONE", COLOR_LIGHTRED);
        write_text(22, 0, "SPACE: NEXT TEXT PAGE", COLOR_CYAN);
        write_text(23, 0, "R: RESTART  Q: QUIT", COLOR_YELLOW);
        return;
    }

    {
        uint8_t i;
        for (i = 0; i < scene->option_count && i < OPTION_ROWS; ++i) {
            const StoryOption* option = &STORY_OPTIONS[scene->first_option + i];
            uint8_t row_opt = (uint8_t)(OPTION_ROW_START + i);
            char option_number[4] = {'1', ')', ' ', '\0'};

            option_number[0] = (char)('1' + i);
            write_text(row_opt, 0, option_number, COLOR_GREEN);
            write_text(row_opt, 3, option->text, COLOR_WHITE);
        }
    }
}

int main(void)
{
    uint8_t scene_index = 0;
    uint8_t flags = 0;

    BORDER_COLOR = COLOR_BLACK;
    BG_COLOR = COLOR_BLACK;
    cbm_k_clrch();
    set_debug_marker('A', COLOR_BLACK);

    __asm__("sei");
    copy_font_from_rom();
    __asm__("cli");
    set_debug_marker('B', COLOR_BLUE);
    clear_bitmap();
    set_debug_marker('C', COLOR_CYAN);
    initialize_bitmap_colors();
    set_debug_marker('I', COLOR_LIGHTBLUE);
    configure_bitmap_mode();
    set_debug_marker('M', COLOR_PURPLE);

    /* Load embedded bitmap instead of from disk */
    if (!load_bitmap_from_embedded()) {
        draw_top_fallback();
        set_debug_marker('F', COLOR_YELLOW);
    } else {
        set_debug_marker('G', COLOR_GREEN);
    }

    apply_monochrome_palette();
    set_debug_marker('P', COLOR_WHITE);

    for (;;) {
        set_debug_marker('O', COLOR_LIGHTGREEN);
        const StoryScene* scene = &STORY_SCENES[scene_index];
        uint16_t desc_pages[MAX_DESC_PAGES];
        uint8_t desc_page_count;
        uint8_t desc_page = 0;

        flags |= scene->grants_flags;
        desc_page_count = compute_description_pages(scene->description, desc_pages);

        for (;;) {
            uint8_t key;

            draw_scene(scene, desc_pages[desc_page], desc_page, desc_page_count);

            do {
                key = cbm_k_getin();
            } while (key == 0);

            if (key == 'q' || key == 'Q') {
                return 0;
            }

            if (key == ' ') {
                if (desc_page_count > 1) {
                    desc_page = (uint8_t)((desc_page + 1) % desc_page_count);
                }
                continue;
            }

            if (scene->option_count == 0) {
                if (key == 'r' || key == 'R') {
                    scene_index = 0;
                    flags = 0;
                    break;
                }
                continue;
            }

            if (key >= '1' && key <= '9') {
                uint8_t choice = (uint8_t)(key - '1');
                if (choice < scene->option_count && choice < OPTION_ROWS) {
                    const StoryOption* option = &STORY_OPTIONS[scene->first_option + choice];
                    uint8_t next_scene_id = resolve_target(option, flags);
                    scene_index = find_scene_index(next_scene_id);
                    break;
                }
            }
        }
    }
}
