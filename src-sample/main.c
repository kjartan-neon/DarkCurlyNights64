#include <cbm.h>
#include <c64.h>
#include <stdint.h>

/*
 * NOTE FOR READERS:
 * This file is a legacy/experimental renderer version of the game.
 * It uses bitmap drawing + raster-time sprite multiplexing.
 *
 * If you are new to C64 code, think of it like this:
 * - The C64 screen is updated every frame.
 * - Sprites are small movable images (player/enemies).
 * - Bitmap calls draw the static background.
 * - The main loop reads keys, updates physics, then renders.
 */

#define SCREEN ((uint8_t*)0x4400)
#define COLOR  ((uint8_t*)0xD800)
#define BITMAP ((uint8_t*)0x6000)
#define RASTER (*(volatile uint8_t*)0xD012)
#define SPR_PTR ((uint8_t*)0x47F8)
#define SPR_MEM ((uint8_t*)0x7000)

#define WIDTH 40
#define SKY_ROWS 16
#define GROUND_ROW 20
#define PLAYER_X 6

#define SPR_PLAYER 0
#define SPR_ENEMY0 1
#define SPR_ENEMY1 2
#define SPR_ENEMY2 3
#define SPR_ENEMY_MASK ((1u << SPR_ENEMY0) | (1u << SPR_ENEMY1) | (1u << SPR_ENEMY2))

#define PIX_X_BASE 24
#define PIX_Y_BASE 50

#define MUX_SPLIT0 84
#define MUX_SPLIT1 146
#define MUX_SPLIT2 210

#define KEY_SPACE 32
#define KEY_Q_UP 81
#define KEY_Q_LO 113

#define MAX_ENEMIES 6
#define MAX_ENEMY_SLOTS 3

typedef struct {
    uint8_t active;
    int16_t x;
    uint8_t yrow;
    uint8_t kind;
    uint8_t speed;
} Enemy;

/* One Enemy entry = one obstacle currently moving on screen. */

static Enemy enemies[MAX_ENEMIES];
static uint16_t score;
static uint16_t best;
static uint8_t frame_div;
static uint8_t tick;
static uint8_t speed;
static uint8_t spawn_cd;
static uint8_t run_anim;
static uint8_t rng = 93;
static uint16_t score_drawn;

static uint8_t mux_count[3];
static uint8_t mux_idx[3][MAX_ENEMY_SLOTS];

static int8_t py;
static int8_t vy;

/* py = player Y position (in character rows), vy = vertical speed. */

static const uint8_t sprite_player0[64] = {
    0, 0, 0,
    0, 24, 0,
    0, 60, 0,
    0, 126, 0,
    1, 255, 128,
    3, 255, 192,
    7, 189, 224,
    7, 255, 224,
    7, 126, 224,
    3, 255, 192,
    3, 219, 192,
    3, 219, 192,
    3, 255, 192,
    3, 102, 192,
    3, 36, 192,
    6, 36, 96,
    12, 102, 48,
    24, 66, 24,
    48, 0, 12,
    0, 0, 0,
    0, 0, 0,
    0
};

static const uint8_t sprite_player1[64] = {
    0, 0, 0,
    0, 24, 0,
    0, 60, 0,
    0, 126, 0,
    1, 255, 128,
    3, 255, 192,
    7, 189, 224,
    7, 255, 224,
    7, 126, 224,
    3, 255, 192,
    3, 219, 192,
    3, 255, 192,
    3, 219, 192,
    3, 102, 192,
    6, 36, 96,
    12, 36, 48,
    24, 102, 24,
    48, 66, 12,
    0, 0, 0,
    0, 0, 0,
    0, 0, 0,
    0
};

static const uint8_t sprite_player2[64] = {
    0, 0, 0,
    0, 24, 0,
    0, 60, 0,
    0, 126, 0,
    1, 255, 128,
    3, 255, 192,
    7, 255, 224,
    7, 189, 224,
    7, 126, 224,
    3, 255, 192,
    3, 219, 192,
    3, 219, 192,
    3, 255, 192,
    6, 102, 96,
    12, 36, 48,
    24, 36, 24,
    48, 102, 12,
    0, 66, 0,
    0, 0, 0,
    0, 0, 0,
    0, 0, 0,
    0
};

static const uint8_t sprite_enemy0[64] = {
    0, 0, 0,
    0, 126, 0,
    1, 255, 128,
    3, 255, 192,
    7, 255, 224,
    7, 231, 224,
    7, 255, 224,
    3, 255, 192,
    3, 219, 192,
    3, 255, 192,
    7, 255, 224,
    15, 255, 240,
    31, 255, 248,
    63, 255, 252,
    63, 231, 252,
    31, 195, 248,
    15, 129, 240,
    7, 0, 224,
    2, 0, 64,
    0, 0, 0,
    0, 0, 0,
    0
};

static const uint8_t sprite_enemy1[64] = {
    0, 0, 0,
    0, 24, 0,
    0, 60, 0,
    0, 126, 0,
    1, 255, 128,
    3, 255, 192,
    3, 231, 192,
    3, 255, 192,
    7, 255, 224,
    15, 255, 240,
    31, 255, 248,
    31, 231, 248,
    15, 195, 240,
    7, 129, 224,
    3, 0, 192,
    1, 0, 128,
    0, 0, 0,
    0, 0, 0,
    0, 0, 0,
    0, 0, 0,
    0, 0, 0,
    0
};

static void putcxy(uint8_t x, uint8_t y, uint8_t ch, uint8_t col) {
    uint16_t p = (uint16_t)y * WIDTH + x;
    SCREEN[p] = ch;
    COLOR[p] = col;
}

static uint8_t to_sc(char ch) {
    if (ch >= 'A' && ch <= 'Z') {
        return (uint8_t)(1 + (ch - 'A'));
    }
    if (ch >= '0' && ch <= '9') {
        return (uint8_t)ch;
    }
    if (ch == ' ') {
        return 32;
    }
    if (ch == ':') {
        return 58;
    }
    if (ch == '-') {
        return 45;
    }
    if (ch == '=') {
        return 61;
    }
    return 32;
}

static void textxy(uint8_t x, uint8_t y, const char* s, uint8_t col) {
    uint16_t p = (uint16_t)y * WIDTH + x;
    while (*s && x < WIDTH) {
        SCREEN[p] = to_sc(*s);
        COLOR[p] = col;
        ++s;
        ++x;
        ++p;
    }
}

static void numxy(uint8_t x, uint8_t y, uint16_t val, uint8_t col) {
    char buf[6];
    uint8_t i = 0;
    if (val == 0) {
        putcxy(x, y, to_sc('0'), col);
        return;
    }
    while (val > 0 && i < 5) {
        buf[i++] = (char)('0' + (val % 10));
        val = (uint16_t)(val / 10);
    }
    while (i > 0) {
        putcxy(x++, y, to_sc(buf[--i]), col);
    }
}

static void clear_row(uint8_t y, uint8_t ch, uint8_t col) {
    uint8_t x;
    uint16_t p = (uint16_t)y * WIDTH;
    for (x = 0; x < WIDTH; ++x) {
        SCREEN[p + x] = ch;
        COLOR[p + x] = col;
    }
}

static uint8_t rnd8(void) {
    rng = (uint8_t)(rng * 17 + 31);
    return rng;
}

static void copy64(uint8_t* dst, const uint8_t* src) {
    uint8_t i;
    for (i = 0; i < 64; ++i) {
        dst[i] = src[i];
    }
}

static void set_vic_bank1(void) {
    CIA2.ddra |= 0x03u;
    CIA2.pra = (uint8_t)((CIA2.pra & 0xFCu) | 0x02u);
}

static void set_sprite_xy(uint8_t sprite, uint16_t x, uint8_t y) {
    uint8_t mask = (uint8_t)(1u << sprite);
    VIC.spr_pos[sprite].x = (uint8_t)x;
    VIC.spr_pos[sprite].y = y;
    if (x > 255) {
        VIC.spr_hi_x |= mask;
    } else {
        VIC.spr_hi_x &= (uint8_t)~mask;
    }
}

/*
 * Load all sprite graphics into C64 sprite memory and set colors.
 * Called once when the program starts.
 */
static void init_sprites(void) {
    copy64(SPR_MEM + 0x000, sprite_player0);
    copy64(SPR_MEM + 0x040, sprite_player1);
    copy64(SPR_MEM + 0x080, sprite_player2);
    copy64(SPR_MEM + 0x0C0, sprite_enemy0);
    copy64(SPR_MEM + 0x100, sprite_enemy1);

    SPR_PTR[SPR_PLAYER] = (uint8_t)((0x7000u) / 64u);
    SPR_PTR[SPR_ENEMY0] = (uint8_t)((0x70C0u) / 64u);
    SPR_PTR[SPR_ENEMY1] = (uint8_t)((0x70C0u) / 64u);
    SPR_PTR[SPR_ENEMY2] = (uint8_t)((0x70C0u) / 64u);

    VIC.spr_mcolor = 0;
    VIC.spr_exp_x = 0;
    VIC.spr_exp_y = 0;
    VIC.spr_bg_prio = 0;
    VIC.spr_color[SPR_PLAYER] = COLOR_CYAN;
    VIC.spr_color[SPR_ENEMY0] = COLOR_RED;
    VIC.spr_color[SPR_ENEMY1] = COLOR_LIGHTRED;
    VIC.spr_color[SPR_ENEMY2] = COLOR_ORANGE;
}

static void enable_game_sprites(uint8_t on) {
    if (on) {
        VIC.spr_ena |= (1u << SPR_PLAYER) | (1u << SPR_ENEMY0) | (1u << SPR_ENEMY1) | (1u << SPR_ENEMY2);
    } else {
        VIC.spr_ena &= (uint8_t)~((1u << SPR_PLAYER) | (1u << SPR_ENEMY0) | (1u << SPR_ENEMY1) | (1u << SPR_ENEMY2));
    }
}

static uint8_t row_to_y(int8_t row) {
    return (uint8_t)(PIX_Y_BASE + (uint8_t)row * 8u - 13u);
}

static uint8_t enemy_to_band(uint8_t yrow) {
    if (yrow <= 12) {
        return 0;
    }
    if (yrow <= 16) {
        return 1;
    }
    return 2;
}

static void spawn_enemy(void) {
    uint8_t i;
    for (i = 0; i < MAX_ENEMIES; ++i) {
        if (!enemies[i].active) {
            uint8_t r;
            enemies[i].active = 1;
            enemies[i].x = (int16_t)(PIX_X_BASE + 40 * 8 + (rnd8() & 31));
            r = (uint8_t)(rnd8() & 3);
            if (r == 0) {
                enemies[i].yrow = GROUND_ROW;
            } else if (r == 1) {
                enemies[i].yrow = (uint8_t)(GROUND_ROW - 2);
            } else {
                enemies[i].yrow = (uint8_t)(GROUND_ROW - 7);
            }
            enemies[i].kind = (uint8_t)(rnd8() & 1);
            enemies[i].speed = (uint8_t)(1 + (score > 40 ? 1 : 0));
            return;
        }
    }
}

static void update_enemies(void) {
    uint8_t i;
    for (i = 0; i < MAX_ENEMIES; ++i) {
        if (!enemies[i].active) {
            continue;
        }
        enemies[i].x -= (int16_t)enemies[i].speed;
        if (enemies[i].x < (PIX_X_BASE - 24)) {
            enemies[i].active = 0;
            ++score;
            if (speed > 1 && (score % 16) == 0) {
                --speed;
            }
        }
    }
}

static void update_player_sprite(void) {
    uint16_t px = (uint16_t)(PIX_X_BASE + (uint16_t)PLAYER_X * 8u);

    if (py == GROUND_ROW) {
        SPR_PTR[SPR_PLAYER] = (uint8_t)((0x7000u + ((uint16_t)run_anim * 64u)) / 64u);
    } else {
        SPR_PTR[SPR_PLAYER] = (uint8_t)(0x7000u / 64u);
    }

    set_sprite_xy(SPR_PLAYER, px, row_to_y(py));
}

static void build_mux_lists(void) {
    uint8_t i;
    mux_count[0] = 0;
    mux_count[1] = 0;
    mux_count[2] = 0;

    for (i = 0; i < MAX_ENEMIES; ++i) {
        uint8_t band;
        uint8_t c;
        if (!enemies[i].active) {
            continue;
        }
        if (enemies[i].x > (PIX_X_BASE + 40 * 8 + 24) || enemies[i].x < (PIX_X_BASE - 24)) {
            continue;
        }
        band = enemy_to_band(enemies[i].yrow);
        c = mux_count[band];
        if (c < MAX_ENEMY_SLOTS) {
            mux_idx[band][c] = i;
            mux_count[band] = (uint8_t)(c + 1);
        }
    }
}

static void apply_mux_band(uint8_t band) {
    uint8_t s;
    uint8_t ena = (uint8_t)(1u << SPR_PLAYER);

    for (s = 0; s < MAX_ENEMY_SLOTS; ++s) {
        uint8_t spr = (uint8_t)(SPR_ENEMY0 + s);
        if (s < mux_count[band]) {
            uint8_t eidx = mux_idx[band][s];
            SPR_PTR[spr] = (uint8_t)((0x70C0u + ((uint16_t)enemies[eidx].kind * 64u)) / 64u);
            set_sprite_xy(spr, (uint16_t)enemies[eidx].x, row_to_y((int8_t)enemies[eidx].yrow));
            ena |= (uint8_t)(1u << spr);
        }
    }
    VIC.spr_ena = ena;
}

/*
 * Render one frame using raster splits.
 * Why this exists:
 * - C64 has limited hardware sprites.
 * - Multiplexing reuses sprite slots at different scanline bands.
 * Returns non-zero if a player collision happened this frame.
 */
static uint8_t render_mux_frame(void) {
    uint8_t coll = 0;

    while (RASTER != 250) {
    }
    while (RASTER == 250) {
    }

    update_player_sprite();
    apply_mux_band(0);

    while (RASTER < MUX_SPLIT0) {
    }
    coll |= (uint8_t)(VIC.spr_coll & (1u << SPR_PLAYER));
    apply_mux_band(1);

    while (RASTER < MUX_SPLIT1) {
    }
    coll |= (uint8_t)(VIC.spr_coll & (1u << SPR_PLAYER));
    apply_mux_band(2);

    while (RASTER < MUX_SPLIT2) {
    }
    coll |= (uint8_t)(VIC.spr_coll & (1u << SPR_PLAYER));
    return coll;
}

static void wait_frame(void) {
    while (RASTER != 250) {
    }
    while (RASTER == 250) {
    }
}

static void clear_screen(uint8_t ch, uint8_t col) {
    uint8_t y;
    for (y = 0; y < 25; ++y) {
        clear_row(y, ch, col);
    }
}

static void clear_bitmap(uint8_t v) {
    uint16_t i;
    for (i = 0; i < 8000u; ++i) {
        BITMAP[i] = v;
    }
}

static void set_text_mode(void) {
    set_vic_bank1();
    VIC.ctrl1 &= (uint8_t)~0x20u;
    VIC.ctrl2 &= (uint8_t)~0x10u;
    VIC.addr = 0x14u;
}

static void set_bitmap_mode(void) {
    set_vic_bank1();
    VIC.ctrl1 |= 0x20u;
    VIC.ctrl2 &= (uint8_t)~0x10u;
    VIC.addr = 0x18u;
}

static void plot_pixel(uint16_t x, uint8_t y) {
    uint16_t o;
    if (x >= 320u || y >= 200u) {
        return;
    }
    o = (uint16_t)(y & 0xF8u) * 40u + (x & 0xF8u) + (y & 7u);
    BITMAP[o] |= (uint8_t)(0x80u >> (x & 7u));
}

static void hline(uint16_t x0, uint16_t x1, uint8_t y) {
    uint16_t x;
    for (x = x0; x <= x1; ++x) {
        plot_pixel(x, y);
    }
}

static void fill_rect(uint16_t x0, uint8_t y0, uint16_t x1, uint8_t y1) {
    uint8_t y;
    for (y = y0; y <= y1; ++y) {
        hline(x0, x1, y);
    }
}

static void setup_bitmap_colors(void) {
    uint16_t i;
    for (i = 0; i < 1000u; ++i) {
        SCREEN[i] = (uint8_t)((COLOR_LIGHTBLUE << 4) | COLOR_BLACK);
        COLOR[i] = COLOR_BLACK;
    }
}

static void draw_pixel_background(void) {
    uint8_t i;
    clear_bitmap(0);
    setup_bitmap_colors();

    fill_rect(0, 188, 319, 199);
    hline(0, 319, 187);

    fill_rect(18, 122, 46, 187);
    fill_rect(60, 140, 86, 187);
    fill_rect(100, 116, 132, 187);
    fill_rect(150, 132, 184, 187);
    fill_rect(206, 108, 238, 187);
    fill_rect(258, 126, 294, 187);

    for (i = 0; i < 26; ++i) {
        uint16_t sx = (uint16_t)(8u + ((uint16_t)rnd8() % 304u));
        uint8_t sy = (uint8_t)(8u + (rnd8() % 96u));
        plot_pixel(sx, sy);
    }
}

static void draw_hud(void) {
    uint16_t w;
    uint16_t x;
    if (score == score_drawn) {
        return;
    }

    fill_rect(8, 6, 311, 12);
    fill_rect(10, 8, 309, 10);

    w = (uint16_t)(score % 300u);
    for (x = 0; x < w; ++x) {
        plot_pixel((uint16_t)(10u + x), 9);
        if ((x & 3u) == 0u) {
            plot_pixel((uint16_t)(10u + x), 8);
            plot_pixel((uint16_t)(10u + x), 10);
        }
    }

    score_drawn = score;
}

/*
 * Start a fresh run:
 * - reset score/state
 * - rebuild background
 * - reset player and enemies
 */
static void reset_round(void) {
    uint8_t i;
    set_bitmap_mode();
    clear_screen(32, COLOR_BLACK);
    VIC.bordercolor = COLOR_BLACK;
    VIC.bgcolor0 = COLOR_BLACK;
    py = GROUND_ROW;
    vy = 0;
    for (i = 0; i < MAX_ENEMIES; ++i) {
        enemies[i].active = 0;
    }
    score = 0;
    frame_div = 0;
    tick = 0;
    speed = 3;
    spawn_cd = 20;
    run_anim = 0;
    score_drawn = 0xFFFFu;

    draw_pixel_background();
    draw_hud();
    build_mux_lists();
    update_player_sprite();
    enable_game_sprites(1);
}

/* Show title screen and wait for SPACE. */
static void title(void) {
    set_text_mode();
    enable_game_sprites(0);
    clear_screen(32, COLOR_BLACK);
    VIC.bordercolor = COLOR_BLACK;
    VIC.bgcolor0 = COLOR_BLACK;
    textxy(9, 6, "NEON CYBORG RUNNER", COLOR_CYAN);
    textxy(12, 10, "SPACE = JUMP", COLOR_LIGHTGREEN);
    textxy(15, 12, "Q = QUIT", COLOR_LIGHTGREEN);
    textxy(8, 16, "PRESS SPACE TO START", COLOR_WHITE);

    while (1) {
        uint8_t k = cbm_k_getin();
        if (k == KEY_SPACE) {
            return;
        }
        wait_frame();
    }
}

/*
 * Show game-over screen.
 * Returns 1 to retry, 0 to quit.
 */
static uint8_t game_over(void) {
    set_text_mode();
    enable_game_sprites(0);
    if (score > best) {
        best = score;
    }

    textxy(13, 8, "CYBORG DOWN", COLOR_RED);
    textxy(10, 10, "SCORE:", COLOR_WHITE);
    numxy(17, 10, score, COLOR_WHITE);
    textxy(10, 11, "BEST:", COLOR_WHITE);
    numxy(16, 11, best, COLOR_WHITE);
    textxy(5, 14, "SPACE = RETRY  Q = QUIT", COLOR_LIGHTBLUE);

    while (1) {
        uint8_t k = cbm_k_getin();
        if (k == KEY_SPACE) {
            return 1;
        }
        if (k == KEY_Q_LO || k == KEY_Q_UP) {
            return 0;
        }
        wait_frame();
    }
}

/*
 * Main game flow (high-level):
 * 1) Show title
 * 2) Reset round
 * 3) Per frame: input -> physics -> enemy updates -> render
 * 4) On collision, show game-over and optionally restart
 */
int main(void) {
    init_sprites();
    while (1) {
        title();
        reset_round();

        while (1) {
            uint8_t i;
            uint8_t hit = 0;
            uint8_t k = cbm_k_getin();
            if (k == KEY_Q_LO || k == KEY_Q_UP) {
                return 0;
            }
            /* Jump starts only when player is on the ground. */
            if (k == KEY_SPACE && py == GROUND_ROW) {
                vy = -4;
            }

            /* Simple gravity physics: position += velocity, then pull down. */
            py = (int8_t)(py + vy);
            if (py < GROUND_ROW) {
                vy = (int8_t)(vy + 1);
            } else {
                py = GROUND_ROW;
                vy = 0;
            }

            /* Enemy movement/spawn every second frame to reduce load. */
            if (frame_div == 0) {
                update_enemies();
                if (spawn_cd > 0) {
                    --spawn_cd;
                } else {
                    spawn_enemy();
                    spawn_cd = (uint8_t)(12 + (rnd8() & 15));
                }
            }

            frame_div = (uint8_t)((frame_div + 1) & 1);
            tick++;

            if (py == GROUND_ROW && (tick & 3u) == 0u) {
                run_anim = (uint8_t)((run_anim + 1) % 3u);
            }
            draw_hud();

            /* Prepare enemies per raster band, then render multiplexed frame(s). */
            build_mux_lists();
            for (i = 0; i < speed; ++i) {
                if (render_mux_frame() != 0u) {
                    hit = 1;
                }
            }

            if (hit) {
                break;
            }
        }

        if (!game_over()) {
            return 0;
        }
    }
}
