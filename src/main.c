#include <cbm.h>
#include <c64.h>
#include <stdint.h>
#include <string.h>

#include "generated_story.h"
#include "scene01_bitmap.h"
#include "scene02_bitmap.h"

/*
 * This program runs a small interactive story on the Commodore 64.
 *
 * High-level flow:
 * 1) Set up bitmap graphics mode and color memory.
 * 2) Load and show a scene image (embedded as C arrays).
 * 3) Draw story text + options at the bottom of the screen.
 * 4) Wait for key input and move to the next scene.
 */

/* Memory addresses used by C64 bitmap mode and copied font data. */
#define BITMAP_RAM        ((uint8_t*)0xE000)
#define BITMAP_SCREEN_RAM ((uint8_t*)0xC000)
#define BITMAP_COLOR_RAM  ((uint8_t*)0xD800)
#define FONT_RAM          ((uint8_t*)0xC800)

/* VIC-II and system registers (memory-mapped hardware control). */
#define BORDER_COLOR       (*(volatile uint8_t*)0xD020)
#define BG_COLOR           (*(volatile uint8_t*)0xD021)
#define VIC_CTRL1          (*(volatile uint8_t*)0xD011)
#define VIC_CTRL2          (*(volatile uint8_t*)0xD016)
#define VIC_MEMORY_CONTROL (*(volatile uint8_t*)0xD018)
#define VIC_BANK_SELECT    (*(volatile uint8_t*)0xDD00)
#define CPU_PORT           (*(volatile uint8_t*)0x0001)
#define KERNAL_LAST_DEVICE (*(volatile uint8_t*)0x00BA)

#define SCREEN_W 40
#define SCREEN_H 25
#define TOP_ROWS 16
#define BOTTOM_START 16

#define DESC_ROW_START 14
#define DESC_ROWS 7

#define OPTION_ROW_START 21
#define OPTION_ROWS 3

#define MAX_DESC_PAGES 8

#define BITMAP_CHAR_HEIGHT 8
#define BITMAP_ROW_STRIDE 320u
#define BITMAP_TOTAL_BYTES 8000u
#define FONT_BYTES 1024u

#define BITMAP_LOAD_ADDR ((void*)0xE000)
#define SCENE_PACK_FILENAME "SCENES.BIN"

/* Debug marker shown as border color + top-left character while booting. */
static volatile uint8_t debug_stage = 0;

/* Forward declaration for debug logging helper used before write_text definition. */
static void write_text(uint8_t row, uint8_t col, const char* text, uint8_t color);
static void debug_write_line(uint8_t col, uint8_t row, const char* text, uint8_t color);

/*
 * Purpose: Show a one-letter boot/debug stage on screen and border.
 * Inputs: stage = character code for stage, border = C64 border color value.
 * Returns: nothing.
 */
static void set_debug_marker(uint8_t stage, uint8_t border)
{
    uint8_t screen_code = 31;
    uint16_t i;

    debug_stage = stage;
    BORDER_COLOR = border;

    if (stage >= 'A' && stage <= 'Z') {
        screen_code = (uint8_t)(stage - 64);
    }

    ((uint8_t*)0x0400)[0] = screen_code;
    ((uint8_t*)0xD800)[0] = COLOR_WHITE;

    /* Short pause for boot-stage visibility in debug builds. */
    for (i = 0; i < 1000; ++i) {
        __asm__("nop");
    }
}

/*
 * Purpose: Slow down boot-time disk debug output for readability.
 * Inputs: none.
 * Returns: nothing.
 */
static void debug_delay_2s(void)
{
    volatile uint16_t outer;
    volatile uint16_t inner;

    for (outer = 0; outer < 8u; ++outer) {
        for (inner = 0; inner < 2000u; ++inner) {
            __asm__("nop");
        }
    }
}

/*
 * Purpose: Write one disk-debug line and pause so it can be read.
 * Inputs: col, row, text, color.
 * Returns: nothing.
 */
static void debug_write_line(uint8_t col, uint8_t row, const char* text, uint8_t color)
{
    write_text(row, col, text, color);
    debug_delay_2s();
}

/*
 * Purpose: Convert ASCII character to the C64 screen-code equivalent.
 * Inputs: c = ASCII character byte.
 * Returns: C64 screen-code byte (space if unsupported).
 */
static uint8_t ascii_to_screen(uint8_t c)
{
    /* Convert normal ASCII text to C64 screen-code values. */
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

/*
 * Purpose: Translate a text-cell position into bitmap memory byte offset.
 * Inputs: row = text row index, col = text column index.
 * Returns: offset inside BITMAP_RAM for that 8x8 character cell.
 */
static uint16_t bitmap_cell_offset(uint8_t row, uint8_t col)
{
    /*
     * Each text cell is 8 pixels tall in bitmap memory.
     * Convert (row, col) character position to byte offset in BITMAP_RAM.
     */
    return (uint16_t)row * BITMAP_ROW_STRIDE + (uint16_t)col * BITMAP_CHAR_HEIGHT;
}

/*
 * Purpose: Copy character glyphs from C64 ROM into writable RAM.
 * Inputs: none.
 * Returns: nothing.
 */
static void copy_font_from_rom(void)
{
    /*
     * Copy the built-in character set from ROM to RAM.
     * We later read glyph bytes from FONT_RAM when drawing text ourselves.
     */
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

/*
 * Purpose: Clear full bitmap pixel memory to black.
 * Inputs: none.
 * Returns: nothing.
 */
static void clear_bitmap(void)
{
    /* Fill entire bitmap memory with 0 bits (black pixels). */
    uint16_t i;
    for (i = 0; i < BITMAP_TOTAL_BYTES; ++i) {
        BITMAP_RAM[i] = 0x00;
    }
}

/*
 * Purpose: Initialize per-cell bitmap color memory.
 * Inputs: none.
 * Returns: nothing.
 */
static void initialize_bitmap_colors(void)
{
    /* Initialize color nybbles for every screen cell (white on black). */
    uint16_t i;
    for (i = 0; i < (uint16_t)(SCREEN_W * SCREEN_H); ++i) {
        BITMAP_SCREEN_RAM[i] = 0x01;
        BITMAP_COLOR_RAM[i] = COLOR_WHITE;
    }
}

/*
 * Purpose: Set foreground/background colors for one text cell.
 * Inputs: row/col cell location, fg_color and bg_color.
 * Returns: nothing.
 */
static void set_bitmap_cell_color(uint8_t row, uint8_t col, uint8_t fg_color, uint8_t bg_color)
{
    /* In bitmap mode, SCREEN_RAM stores per-cell fg/bg color selection. */
    uint16_t offset = (uint16_t)row * SCREEN_W + col;
    BITMAP_SCREEN_RAM[offset] = (uint8_t)((bg_color << 4) | (fg_color & 0x0Fu));
    BITMAP_COLOR_RAM[offset] = fg_color;
}

/*
 * Purpose: Clear one 8x8 bitmap cell and set its colors.
 * Inputs: row/col cell location plus fg/bg colors.
 * Returns: nothing.
 */
static void clear_bitmap_cell(uint8_t row, uint8_t col, uint8_t fg_color, uint8_t bg_color)
{
    /* Clear one 8x8 cell worth of bitmap bytes. */
    uint16_t offset = bitmap_cell_offset(row, col);
    uint8_t pixel_row;

    for (pixel_row = 0; pixel_row < BITMAP_CHAR_HEIGHT; ++pixel_row) {
        BITMAP_RAM[offset + pixel_row] = 0x00;
    }

    set_bitmap_cell_color(row, col, fg_color, bg_color);
}

/*
 * Purpose: Clear one full text row (all 40 columns).
 * Inputs: row index and text color.
 * Returns: nothing.
 */
static void clear_line(uint8_t row, uint8_t color)
{
    /* Clear a full 40-column text row in bitmap mode. */
    uint8_t col;
    for (col = 0; col < SCREEN_W; ++col) {
        clear_bitmap_cell(row, col, color, COLOR_BLACK);
    }
}

/*
 * Purpose: Draw one C64 glyph into bitmap memory.
 * Inputs: row/col cell location, screen_code glyph index, color.
 * Returns: nothing.
 */
static void draw_screen_code(uint8_t row, uint8_t col, uint8_t screen_code, uint8_t color)
{
    /* Draw one character glyph into bitmap memory at (row, col). */
    const uint8_t* glyph = &FONT_RAM[(uint16_t)screen_code * BITMAP_CHAR_HEIGHT];
    uint16_t offset = bitmap_cell_offset(row, col);
    uint8_t pixel_row;

    for (pixel_row = 0; pixel_row < BITMAP_CHAR_HEIGHT; ++pixel_row) {
        BITMAP_RAM[offset + pixel_row] = glyph[pixel_row];
    }

    set_bitmap_cell_color(row, col, color, COLOR_BLACK);
}

/*
 * Purpose: Draw a null-terminated text string at a row/column.
 * Inputs: row/col start, text pointer, color.
 * Returns: nothing.
 */
static void write_text(uint8_t row, uint8_t col, const char* text, uint8_t color)
{
    /* Print a C string by drawing characters one cell at a time. */
    while (*text && col < SCREEN_W) {
        draw_screen_code(row, col, ascii_to_screen((uint8_t)*text), color);
        ++text;
        ++col;
    }
}

/*
 * Purpose: Word-wrap and optionally draw one page of description text.
 * Inputs: text buffer, start index, draw flag (0=measure, 1=draw).
 * Returns: index of next unread character (next page start).
 */
static uint16_t render_description_page(const char* text, uint16_t start, uint8_t draw)
{
    /*
     * Word-wrap description text into the fixed description area.
     * Returns index of next unread character (for multi-page descriptions).
     */
    uint8_t current_row = DESC_ROW_START;
    uint8_t col = 0;
    uint16_t idx = start;
    uint8_t has_more = 0;

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
            has_more = 1;
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

    if (has_more && draw) {
        uint8_t last_row = (uint8_t)(DESC_ROW_START + DESC_ROWS - 1);
        write_text(last_row, 37, "(...)", COLOR_CYAN);
    }

    return idx;
}

/*
 * Purpose: Calculate all description page start positions in advance.
 * Inputs: text description and output array `pages`.
 * Returns: number of pages found.
 */
static uint8_t compute_description_pages(const char* text, uint16_t* pages)
{
    /* Precompute where each description page starts in the source string. */
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

/*
 * Purpose: Configure VIC-II registers for hires bitmap mode.
 * Inputs: none.
 * Returns: nothing.
 */
static void configure_bitmap_mode(void)
{
    /* Configure VIC-II for standard hires bitmap mode. */
    VIC_BANK_SELECT = (uint8_t)(VIC_BANK_SELECT & 0xFCu);
    VIC_MEMORY_CONTROL = 0x08u;
    VIC_CTRL1 = (uint8_t)((VIC_CTRL1 & 0x80u) | 0x3Bu);
    VIC_CTRL2 = (uint8_t)((VIC_CTRL2 & 0xE0u) | 0x08u);
}

/*
 * Purpose: Apply white foreground on black background for all cells.
 * Inputs: none.
 * Returns: nothing.
 */
static void apply_monochrome_palette(void)
{
    /* Force all cells to white-on-black for a consistent monochrome look. */
    uint8_t row;
    uint8_t col;

    for (row = 0; row < SCREEN_H; ++row) {
        for (col = 0; col < SCREEN_W; ++col) {
            set_bitmap_cell_color(row, col, COLOR_WHITE, COLOR_BLACK);
        }
    }
}

/*
 * Purpose: Draw fallback top graphic bars (debug/demo helper).
 * Inputs: none.
 * Returns: nothing.
 */
static void draw_top_fallback(void)
{
    /*
     * Optional fallback art (color bars).
     * Not used in normal flow when scene bitmaps are available.
     */
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

/*
 * Purpose: Build SCENENN.BMP filename from a numeric scene id.
 * Inputs: scene_id and output buffer (must hold at least 12 bytes).
 * Returns: nothing.
 */
static void build_scene_filename(uint8_t scene_id, char* out_name)
{
    out_name[0] = 'S';
    out_name[1] = 'C';
    out_name[2] = 'E';
    out_name[3] = 'N';
    out_name[4] = 'E';
    out_name[5] = (char)('0' + ((scene_id / 10u) % 10u));
    out_name[6] = (char)('0' + (scene_id % 10u));
    out_name[7] = '.';
    out_name[8] = 'B';
    out_name[9] = 'M';
    out_name[10] = 'P';
    out_name[11] = '\0';
}

/*
 * Purpose: Try loading one bitmap file from one specific device number.
 * Inputs: filename (SCENENN.BMP) and device number.
 * Returns: 1 on success, 0 on failure.
 */
static uint8_t try_load_bitmap_from_device(const char* filename, uint8_t device)
{
    uint8_t status;

    cbm_k_setlfs(1, device, 0);
    cbm_k_setnam(filename);
    cbm_k_load(CBM_LOAD_RAM, BITMAP_LOAD_ADDR);
    status = cbm_k_readst();
    cbm_k_clall();

    return (uint8_t)(status == 0);
}

/*
 * Purpose: Load bitmap bytes for a scene from disk into BITMAP_RAM.
 * Inputs: scene_id selecting file name SCENENN.BMP.
 * Returns: 1 on success, 0 on load failure.
 */
static uint8_t load_bitmap_from_disk(uint8_t scene_id)
{
    uint8_t i;
    char bitmap_asset_filename[12];
    uint8_t device_candidates[4] = {8, 9, 10, 11};
    uint8_t candidate_count = 4;

    build_scene_filename(scene_id, bitmap_asset_filename);

    set_debug_marker('L', COLOR_ORANGE);

    for (i = 0; i < candidate_count; ++i) {
        set_debug_marker('N', COLOR_BROWN);
        if (try_load_bitmap_from_device(bitmap_asset_filename, device_candidates[i])) {
            set_debug_marker('S', COLOR_GREEN);
            return 1;
        }
    }

    set_debug_marker('E', COLOR_LIGHTRED);
    return 0;
}

/*
 * Purpose: Open packed scene file SCENES.BIN for reading on one device.
 * Inputs: device number.
 * Returns: 1 on success, 0 on failure.
 */
static uint8_t open_scene_pack_for_device(uint8_t device)
{
    cbm_k_setlfs(2, device, 2);
    cbm_k_setnam(SCENE_PACK_FILENAME);
    cbm_k_open();
    if (cbm_k_readst() != 0) {
        set_debug_marker('1', COLOR_RED);
        cbm_k_clall();
        return 0;
    }

    cbm_k_chkin(2);
    if (cbm_k_readst() != 0) {
        set_debug_marker('2', COLOR_ORANGE);
        cbm_k_close(2);
        cbm_k_clall();
        return 0;
    }

    set_debug_marker('3', COLOR_YELLOW);
    return 1;
}

/*
 * Purpose: Close packed scene file logical channel.
 * Inputs: none.
 * Returns: nothing.
 */
static void close_scene_pack(void)
{
    cbm_k_clrch();
    cbm_k_close(2);
    cbm_k_clall();
}

/*
 * Purpose: Read one byte from active pack channel.
 * Inputs: out_byte pointer.
 * Returns: 1 on success, 0 on read error.
 */
static uint8_t read_pack_byte(uint8_t* out_byte)
{
    *out_byte = cbm_k_chrin();
    return (uint8_t)((cbm_k_readst() & 0x80u) == 0);
}

/*
 * Purpose: Read and validate SCN1 pack header, allowing optional PRG load bytes.
 * Inputs: out_scene_count pointer.
 * Returns: 1 on valid header, 0 on failure.
 */
static uint8_t read_pack_header(uint8_t* out_scene_count)
{
    uint8_t byte_value;
    uint8_t sig_index = 0;
    uint8_t scanned = 0;

    while (scanned < 32u) {
        if (!read_pack_byte(&byte_value)) {
            return 0;
        }
        ++scanned;

        if (sig_index == 0) {
            sig_index = (uint8_t)(byte_value == 'S');
        } else if (sig_index == 1) {
            if (byte_value == 'C') {
                sig_index = 2;
            } else {
                sig_index = (uint8_t)(byte_value == 'S');
            }
        } else if (sig_index == 2) {
            if (byte_value == 'N') {
                sig_index = 3;
            } else {
                sig_index = (uint8_t)(byte_value == 'S');
            }
        } else {
            if (byte_value == '1') {
                sig_index = 4;
                break;
            }
            sig_index = (uint8_t)(byte_value == 'S');
        }
    }

    if (sig_index != 4) {
        return 0;
    }

    if (!read_pack_byte(out_scene_count)) {
        return 0;
    }

    return 1;
}

/*
 * Purpose: Load one scene from SCENES.BIN on one specific device.
 * Inputs: scene_id, device.
 * Returns: 1 on success, 0 on failure.
 */
static uint8_t load_bitmap_from_pack_device(uint8_t scene_id, uint8_t device)
{
    uint8_t i;
    uint8_t scene_count;
    uint8_t byte_value;
    uint8_t found = 0;
    uint16_t target_size = 0;
    uint32_t target_offset = 0;
    uint32_t skip_count;
    uint16_t data_index;

    if (!open_scene_pack_for_device(device)) {
        return 0;
    }

    if (!read_pack_header(&scene_count)) { set_debug_marker('4', COLOR_RED); close_scene_pack(); return 0; }
    set_debug_marker('9', COLOR_CYAN);

    for (i = 0; i < scene_count; ++i) {
        uint8_t entry_scene_id;
        uint8_t off_lo;
        uint8_t off_mid;
        uint8_t off_hi;
        uint8_t size_lo;
        uint8_t size_hi;

        if (!read_pack_byte(&entry_scene_id)) { close_scene_pack(); return 0; }
        if (!read_pack_byte(&off_lo)) { close_scene_pack(); return 0; }
        if (!read_pack_byte(&off_mid)) { close_scene_pack(); return 0; }
        if (!read_pack_byte(&off_hi)) { close_scene_pack(); return 0; }
        if (!read_pack_byte(&size_lo)) { close_scene_pack(); return 0; }
        if (!read_pack_byte(&size_hi)) { close_scene_pack(); return 0; }

        if (entry_scene_id == scene_id) {
            found = 1;
            target_offset = ((uint32_t)off_hi << 16) | ((uint32_t)off_mid << 8) | (uint32_t)off_lo;
            target_size = (uint16_t)(((uint16_t)size_hi << 8) | (uint16_t)size_lo);
        }
    }

    if (!found || target_size == 0 || target_size > BITMAP_TOTAL_BYTES) {
        set_debug_marker('A', COLOR_RED);
        close_scene_pack();
        return 0;
    }
    set_debug_marker('B', COLOR_LIGHTBLUE);

    set_debug_marker('C', COLOR_PURPLE);
    for (skip_count = 0; skip_count < target_offset; ++skip_count) {
        if (!read_pack_byte(&byte_value)) {
            set_debug_marker('D', COLOR_RED);
            close_scene_pack();
            return 0;
        }
    }

    set_debug_marker('E', COLOR_GREEN);
    clear_bitmap();
    for (data_index = 0; data_index < target_size; ++data_index) {
        if (!read_pack_byte(&BITMAP_RAM[data_index])) {
            set_debug_marker('F', COLOR_RED);
            close_scene_pack();
            return 0;
        }
    }
    set_debug_marker('G', COLOR_GREEN);

    close_scene_pack();
    set_debug_marker('H', COLOR_GREEN);
    return 1;
}

/*
 * Purpose: Load scene from packed SCENES.BIN trying common disk devices.
 * Inputs: scene_id.
 * Returns: 1 on success, 0 on failure.
 */
static uint8_t load_bitmap_from_pack(uint8_t scene_id)
{
    uint8_t i;
    uint8_t device_candidates[4] = {8, 9, 10, 11};
    uint8_t candidate_count = 4;

    for (i = 0; i < candidate_count; ++i) {
        if (load_bitmap_from_pack_device(scene_id, device_candidates[i])) {
            set_debug_marker('P', COLOR_GREEN);
            return 1;
        }
    }

    return 0;
}

/*
 * Purpose: Load one of the first 2 scene bitmaps from embedded arrays.
 * Inputs: scene_id.
 * Returns: 1 on success, 0 if scene is not embedded.
 */
static uint8_t load_bitmap_from_embedded(uint8_t scene_id)
{
    switch (scene_id) {
        case 1:
            memcpy(BITMAP_RAM, SCENE01_BITMAP_DATA, SCENE01_BITMAP_SIZE);
            return 1;
        case 2:
            memcpy(BITMAP_RAM, SCENE02_BITMAP_DATA, SCENE02_BITMAP_SIZE);
            return 1;
        default:
            return 0;
    }
}

/*
 * Purpose: Boot-time validation: attempt to open SCENES.BIN to confirm disk access.
 * Inputs: none.
 * Returns: 1 if pack is accessible, 0 otherwise.
 */
static uint8_t validate_pack_accessible(void)
{
    uint8_t device_candidates[4];
    uint8_t candidate_count = 4;
    uint8_t i;
    uint8_t base_row;
    uint8_t scene_count;
    uint8_t status;

    /* Probe all common IEC device numbers used by VICE/C64 drives. */
    device_candidates[0] = 8;
    device_candidates[1] = 9;
    device_candidates[2] = 10;
    device_candidates[3] = 11;

    set_debug_marker('V', COLOR_CYAN);
    debug_write_line(1, 2, "DISK VALIDATION START", COLOR_LIGHTGREEN);

    for (i = 0; i < candidate_count; ++i) {
        base_row = (uint8_t)(2 + (i * 4));
        cbm_k_clall();

        switch (device_candidates[i]) {
            case 8:
                debug_write_line(1, (uint8_t)(base_row + 1), "TRY DEVICE 8         ", COLOR_YELLOW);
                break;
            case 9:
                debug_write_line(1, (uint8_t)(base_row + 1), "TRY DEVICE 9         ", COLOR_YELLOW);
                break;
            case 10:
                debug_write_line(1, (uint8_t)(base_row + 1), "TRY DEVICE 10        ", COLOR_YELLOW);
                break;
            default:
                debug_write_line(1, (uint8_t)(base_row + 1), "TRY DEVICE 11        ", COLOR_YELLOW);
                break;
        }

        cbm_k_setlfs(2, device_candidates[i], 2);
        set_debug_marker('1', COLOR_LIGHTBLUE);
        cbm_k_setnam(SCENE_PACK_FILENAME);
        cbm_k_open();
        status = cbm_k_readst();

        if (status != 0) {
            debug_write_line(1, (uint8_t)(base_row + 2), "OPEN FAIL            ", COLOR_LIGHTRED);
            continue;
        }
        debug_write_line(1, (uint8_t)(base_row + 2), "OPEN OK              ", COLOR_GREEN);
        set_debug_marker('2', COLOR_LIGHTBLUE);

        cbm_k_chkin(2);
        status = cbm_k_readst();
        if (status != 0) {
            debug_write_line(1, (uint8_t)(base_row + 3), "CHKIN FAIL           ", COLOR_LIGHTRED);
            cbm_k_close(2);
            cbm_k_clall();
            continue;
        }
        debug_write_line(1, (uint8_t)(base_row + 3), "CHKIN OK             ", COLOR_GREEN);
        set_debug_marker('3', COLOR_LIGHTBLUE);

        if (!read_pack_header(&scene_count)) {
            cbm_k_clrch();
            cbm_k_close(2);
            cbm_k_clall();
            debug_write_line(1, (uint8_t)(base_row + 4), "HEADER FAIL          ", COLOR_LIGHTRED);
            continue;
        }

        cbm_k_clrch();
        cbm_k_close(2);
        cbm_k_clall();
        debug_write_line(1, (uint8_t)(base_row + 4), "HEADER OK            ", COLOR_GREEN);
        debug_write_line(1, 19, "PACK FOUND           ", COLOR_GREEN);
        set_debug_marker('W', COLOR_GREEN);
        return 1;
    }

    debug_write_line(1, 19, "PACK NOT FOUND       ", COLOR_LIGHTRED);
    set_debug_marker('Z', COLOR_RED);
    return 0;
}

/*
 * Purpose: Load scene bitmap from embedded data (1-3) or disk (others).
 * Inputs: scene_id.
 * Returns: 1 on success, 0 on failure.
 */
static uint8_t load_bitmap_for_scene(uint8_t scene_id)
{
    if (load_bitmap_from_embedded(scene_id)) {
        set_debug_marker('S', COLOR_GREEN);
        return 1;
    }

    if (load_bitmap_from_pack(scene_id)) {
        return 1;
    }

    return load_bitmap_from_disk(scene_id);
}

/*
 * Purpose: Find array index in STORY_SCENES by scene ID.
 * Inputs: scene_id from option target.
 * Returns: matching index, or 0 if not found.
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

/*
 * Purpose: Resolve conditional option branching based on player flags.
 * Inputs: option pointer and current flag bitfield.
 * Returns: target scene ID to jump to.
 */
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

/*
 * Purpose: Clear only the text description rows before redrawing text.
 * Inputs: none.
 * Returns: nothing.
 */
static void clear_description_area(void)
{
    uint8_t row;
    for (row = DESC_ROW_START; row < (uint8_t)(DESC_ROW_START + DESC_ROWS); ++row) {
        clear_line(row, COLOR_WHITE);
    }
}

/*
 * Purpose: Draw complete scene frame (image + title + text + options).
 * Inputs: scene pointer, page start index, page index, total pages.
 * Returns: nothing.
 */
static void draw_scene(const StoryScene* scene, uint16_t page_start, uint8_t page_index, uint8_t page_count)
{
    /* Render one full frame for the current scene and description page. */
    uint8_t row;

    for (row = BOTTOM_START; row < SCREEN_H; ++row) {
        clear_line(row, COLOR_WHITE);
    }

    /* Load the correct bitmap for this scene. */
    if (!load_bitmap_for_scene(scene->id)) {
        write_text(0, 0, "BITMAP LOAD FAILED", COLOR_LIGHTRED);
    }

    clear_description_area();

    write_text(13, 0, "SCENE:", COLOR_YELLOW);
    write_text(13, 7, scene->title, COLOR_YELLOW);

    render_description_page(scene->description, page_start, 1);

    if (page_count > 1) {
        char page_buf[8] = {'P', 'G', ' ', '1', '/', '1', '\0', '\0'};
        page_buf[3] = (char)('1' + page_index);
        page_buf[5] = (char)('0' + page_count);
        write_text(13, 33, page_buf, COLOR_CYAN);
    }

    if (scene->option_count == 0) {
        write_text(24, 0, "END OF PART ONE", COLOR_LIGHTRED);
        write_text(25, 0, "SPACE: NEXT PAGE  R: RESTART", COLOR_CYAN);
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
            write_text(row_opt, 3, option->text, COLOR_GREEN);
        }
    }
}

/*
 * Purpose: Program entry point; initialize hardware and run the game loop.
 * Inputs: none.
 * Returns: 0 when user quits.
 */
int main(void)
{
    /* scene_index points into STORY_SCENES; flags track collected story state. */
    uint8_t scene_index = 0;
    uint8_t flags = 0;

    /* Basic machine/screen init. */
    BORDER_COLOR = COLOR_BLACK;
    BG_COLOR = COLOR_BLACK;
    cbm_k_clrch();
    set_debug_marker('A', COLOR_BLACK);

    /* Disable interrupts while copying font from ROM.
     * Then initialize bitmap memory and switch VIC-II mode.
     */
    __asm__("sei");
    copy_font_from_rom();
    __asm__("cli");
    set_debug_marker('B', COLOR_BLUE);
    configure_bitmap_mode();
    set_debug_marker('M', COLOR_PURPLE);
    clear_bitmap();
    set_debug_marker('C', COLOR_CYAN);
    initialize_bitmap_colors();
    set_debug_marker('I', COLOR_LIGHTBLUE);

    /* Load initial bitmap (scene 1). */
    if (!load_bitmap_for_scene(1)) {
        draw_top_fallback();
    }
    set_debug_marker('G', COLOR_GREEN);

    apply_monochrome_palette();
    set_debug_marker('P', COLOR_WHITE);

    /* Boot-time disk validation: confirm SCENES.BIN is accessible. */
    if (!validate_pack_accessible()) {
        write_text(0, 0, "DISK ACCESS FAILED", COLOR_LIGHTRED);
        set_debug_marker('T', COLOR_RED);
        return 1;
    }
    set_debug_marker('U', COLOR_GREEN);

    /* Main game loop: show scene, wait input, transition to next scene. */
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

            /* Draw current scene and current description page. */
            draw_scene(scene, desc_pages[desc_page], desc_page, desc_page_count);

            /* Blocking input wait: keep polling until user presses a key. */
            do {
                key = cbm_k_getin();
            } while (key == 0);

            /* Q quits immediately. */
            if (key == 'q' || key == 'Q') {
                return 0;
            }

            /* SPACE cycles long descriptions page-by-page. */
            if (key == ' ') {
                if (desc_page_count > 1) {
                    desc_page = (uint8_t)((desc_page + 1) % desc_page_count);
                }
                continue;
            }

            /* End scenes have no options; only restart is accepted. */
            if (scene->option_count == 0) {
                if (key == 'r' || key == 'R') {
                    scene_index = 0;
                    flags = 0;
                    break;
                }
                continue;
            }

            /* Number keys 1..9 pick an option and jump to target scene. */
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
