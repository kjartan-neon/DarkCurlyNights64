/*
 * NeonRunner64 - Neon city runner
 *
 * Background strategy:
 *   - Sky is plain black char cells (never scrolled)
 *   - 4 building sprites (spr 3-6) slide left at scroll speed and recycle
 *   - Ground is a single static char row (never scrolled either)
 *   - Only HUD rows need redraw (score changes only)
 *   - shift_screen_left is REMOVED -- nothing to shift
 *   - Fine-scroll register still runs for smooth pixel motion
 *
 * VIC bank 1:
 *   0x4400  SCREEN_GAME   screen RAM
 *   0x4800  CHARSET_RAM   custom tiles
 *   0x47F8  SPR_PTR       sprite pointers
 *   0x7000  SPR_MEM       sprite bitmaps
 */

#include <cbm.h>
#include <c64.h>
#include <stdint.h>

#define SCREEN_TEXT  ((uint8_t*)0x0400)
#define SCREEN_GAME  ((uint8_t*)0x4400)
#define CHARSET_RAM  ((uint8_t*)0x4800)
#define SPR_MEM      ((uint8_t*)0x7000)
#define SPR_PTR      ((uint8_t*)0x47F8)
#define RASTER       (*(volatile uint8_t*)0xD012)

#define VICADDR_GAME  0x12u
#define VICADDR_TEXT  0x14u

#define W           40
#define H           25
#define HUD_ROWS     3      /* rows 0-2: score bar */
#define GROUND_ROW  22      /* char row of ground top */

/* ── Sprite slots ─────────────────────────────────────────────── */
#define SPR_PLAYER   0
#define SPR_ENEMY0   1
#define SPR_ENEMY1   2
#define SPR_BLDG0    3      /* building sprites */
#define SPR_BLDG1    4
#define SPR_BLDG2    5
#define SPR_BLDG3    6
#define NUM_BLDG     4

/* ── Pixel constants ──────────────────────────────────────────── */
/* VIC display: top border ~50px, each char row = 8px */
/* Ground char row 22: top pixel = 50 + 22*8 = 226               */
/* Sprite Y sits with top-left at given coord; 21px tall player   */
/* Player foot at 226+4 = 230 -> top at 230-21 = 209             */
#define PLAYER_X          72
#define PLAYER_GROUND_Y  209

#define JUMP_SHORT_VY    -8
#define JUMP_HOLD_FRAMES 12u

/* Buildings: tall sprites (63px), foot at ground pixel 230       */
/* top at 230-63+1 = 168 for full-height; vary by height offset  */
#define BLDG_FOOT        230u   /* pixel Y of building base */
#define BLDG_SPR_H        63u   /* max sprite height */
#define BLDG_SPACING     100u   /* pixels between building centres */

/* Screen right edge pixel x = 24 + 40*8 = 344 */
#define SCREEN_RIGHT_PX  344u
#define SCREEN_LEFT_PX    24u
#define ENEMY_UNLOCK_SCROLL_PX 320u

#define PLAYER_HIT_X_OFF 4u
#define PLAYER_HIT_W     16u
#define PLAYER_HIT_Y_OFF 4u
#define PLAYER_HIT_H     17u

#define ENEMY_HIT_X_OFF  3u
#define ENEMY_HIT_W      18u
#define ENEMY_HIT_Y_OFF  6u
#define ENEMY_HIT_H      15u

#define ENEMY_KIND_GROUND 0u
#define ENEMY_KIND_AIR    1u
#define ENEMY_AIR_Y      178u

#define SID_FREQ_LO   (*(volatile uint8_t*)0xD400)
#define SID_FREQ_HI   (*(volatile uint8_t*)0xD401)
#define SID_PW_LO     (*(volatile uint8_t*)0xD402)
#define SID_PW_HI     (*(volatile uint8_t*)0xD403)
#define SID_CTRL1     (*(volatile uint8_t*)0xD404)
#define SID_AD1       (*(volatile uint8_t*)0xD405)
#define SID_SR1       (*(volatile uint8_t*)0xD406)
#define SID_MODE_VOL  (*(volatile uint8_t*)0xD418)

/* ── Input ────────────────────────────────────────────────────── */
#define KEY_SPACE  32
#define KEY_Q_UP   81
#define KEY_Q_LO  113

/* ── Tile IDs ─────────────────────────────────────────────────── */
#define TILE_EMPTY   0
#define TILE_SOLID   1   /* full block (ground fill) */
#define TILE_GTOP    2   /* ground top edge */
#define TILE_HUD     3   /* score bar filled */

/* ── Game state ───────────────────────────────────────────────── */
typedef struct { uint8_t active; int16_t x; uint8_t y; uint8_t speed; uint8_t kind; } Enemy;

/* Building sprite state */
typedef struct { int16_t x; uint8_t color; uint8_t hoff; } Bldg;

static Enemy    enemies[2];
static Bldg     bldgs[NUM_BLDG];
static uint16_t score;
static uint16_t best;
static uint8_t  tick;
static uint8_t  rng = 91;

static int16_t  player_y;
static int8_t   player_vy;
static uint8_t  player_anim;

static uint8_t  fine_scroll;
static uint8_t  scroll_delay;
static uint8_t  scroll_counter;
static uint16_t world_scroll_px;

static uint8_t  spawn_counter;
static uint16_t hud_score_drawn;
static uint8_t  jump_hold_frames;
static uint8_t  space_was_down;
static uint8_t  jump_sfx_timer;

/* ── RNG ──────────────────────────────────────────────────────── */
static uint8_t rnd8(void){rng=(uint8_t)(rng*17u+31u);return rng;}

static uint8_t space_is_down(void){
    uint8_t old_ddra=CIA1.ddra;
    uint8_t old_ddrb=CIA1.ddrb;
    uint8_t old_pra=CIA1.pra;
    uint8_t cols;

    CIA1.ddra=0xFFu;
    CIA1.ddrb=0x00u;
    CIA1.pra=0x7Fu;
    cols=CIA1.prb;

    CIA1.pra=old_pra;
    CIA1.ddra=old_ddra;
    CIA1.ddrb=old_ddrb;

    return (uint8_t)((cols & 0x10u)==0u);
}

static uint8_t current_scroll_step(void){
    if(scroll_delay > 6u) return 1u;
    if(scroll_delay > 4u) return 2u;
    return 3u;
}

static void init_sound(void){
    SID_MODE_VOL = (uint8_t)((SID_MODE_VOL & 0xF0u) | 0x0Fu);
    SID_AD1 = 0x28u;
    SID_SR1 = 0xA8u;
    SID_CTRL1 = 0;
    jump_sfx_timer = 0;
}

static void play_jump_sfx(void){
    SID_FREQ_LO = 0x60u;
    SID_FREQ_HI = 0x28u;
    SID_PW_LO = 0x00u;
    SID_PW_HI = 0x08u;
    SID_CTRL1 = 0x21u;
    jump_sfx_timer = 6u;
}

static void update_sound(void){
    if(jump_sfx_timer > 0u){
        --jump_sfx_timer;
        if(jump_sfx_timer == 0u){
            SID_CTRL1 = 0x20u;
        }
    }
}

/* ── Text helpers ─────────────────────────────────────────────── */
static uint8_t to_sc(char ch){
    if(ch>='A'&&ch<='Z')return(uint8_t)(1+(ch-'A'));
    if(ch>='0'&&ch<='9')return(uint8_t)ch;
    return 32;
}
static void clear_text_screen(uint8_t col){
    uint16_t i;for(i=0;i<1000u;++i){SCREEN_TEXT[i]=32;COLOR_RAM[i]=col;}
}
static void textxy(uint8_t x,uint8_t y,const char*s,uint8_t col){
    uint16_t p=(uint16_t)y*W+x;
    while(*s&&x<W){SCREEN_TEXT[p]=to_sc(*s);COLOR_RAM[p]=col;++s;++x;++p;}
}
static void numxy(uint8_t x,uint8_t y,uint16_t v,uint8_t col){
    char buf[6];uint8_t i=0;uint16_t p=(uint16_t)y*W+x;
    if(v==0){SCREEN_TEXT[p]=to_sc('0');COLOR_RAM[p]=col;return;}
    while(v>0&&i<5){buf[i++]=(char)('0'+(v%10));v=(uint16_t)(v/10);}
    while(i>0&&x<W){SCREEN_TEXT[p]=to_sc(buf[--i]);COLOR_RAM[p]=col;++x;++p;}
}

/* ── VIC sync ─────────────────────────────────────────────────── */
static void wait_frame(void){while(RASTER!=250){}while(RASTER==250){}}

/* ── VIC bank ─────────────────────────────────────────────────── */
static void set_vic_bank0(void){CIA2.ddra|=0x03u;CIA2.pra=(uint8_t)((CIA2.pra&0xFCu)|0x03u);}
static void set_vic_bank1(void){CIA2.ddra|=0x03u;CIA2.pra=(uint8_t)((CIA2.pra&0xFCu)|0x02u);}

static void set_text_mode_default(void){
    VIC.ctrl2=(uint8_t)((VIC.ctrl2&0xF8u)|7u);
    set_vic_bank0();
    VIC.ctrl1&=(uint8_t)~0x20u;
    VIC.ctrl2&=(uint8_t)~0x10u;
    VIC.addr=VICADDR_TEXT;
}
static void set_game_tile_mode(void){
    set_vic_bank1();
    VIC.ctrl1&=(uint8_t)~0x20u;
    VIC.ctrl2&=(uint8_t)~0x10u;
    VIC.addr=VICADDR_GAME;
    fine_scroll=7;
    VIC.ctrl2=(uint8_t)((VIC.ctrl2&0xF8u)|fine_scroll);
}

/* ── Sprite helpers ───────────────────────────────────────────── */
static void set_sprite_xy(uint8_t sp,uint16_t x,uint8_t y){
    uint8_t mask=(uint8_t)(1u<<sp);
    VIC.spr_pos[sp].x=(uint8_t)x;VIC.spr_pos[sp].y=y;
    if(x>255u)VIC.spr_hi_x|=mask;else VIC.spr_hi_x&=(uint8_t)~mask;
}
static void copy64(uint8_t*dst,const uint8_t*src){uint8_t i;for(i=0;i<64u;++i)dst[i]=src[i];}

/* ── Tile definitions ─────────────────────────────────────────── */
static void set_tile(uint8_t idx,uint8_t b0,uint8_t b1,uint8_t b2,uint8_t b3,
                     uint8_t b4,uint8_t b5,uint8_t b6,uint8_t b7){
    uint16_t o=(uint16_t)idx*8u;
    CHARSET_RAM[o+0]=b0;CHARSET_RAM[o+1]=b1;CHARSET_RAM[o+2]=b2;CHARSET_RAM[o+3]=b3;
    CHARSET_RAM[o+4]=b4;CHARSET_RAM[o+5]=b5;CHARSET_RAM[o+6]=b6;CHARSET_RAM[o+7]=b7;
}
static void init_tiles(void){
    uint16_t i;for(i=0;i<256u*8u;++i)CHARSET_RAM[i]=0;
    set_tile(TILE_EMPTY, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00);
    set_tile(TILE_SOLID, 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF);
    set_tile(TILE_GTOP,  0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00);
    set_tile(TILE_HUD,   0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF);
}

/* ── Building sprite bitmaps ──────────────────────────────────── */
/*
 * Building sprites: 24px wide × 21 rows used (fits in 63 bytes).
 * Pattern: solid block with two 4px windows per row pair.
 * The sprite is Y-expanded (VIC spr_exp_y) to be 42px tall.
 *
 * Bit pattern per row (24 bits):
 *   solid edge + window gap + solid + window gap + solid
 *   0xFF 0x99 0xFF  -> 11111111 10011001 11111111
 *
 * Non-window rows:
 *   0xFF 0xFF 0xFF  -> full solid
 */
static const uint8_t spr_building[64]={
    /* rows 0-20 (21 bytes per 3-byte row = 21 rows) */
    0xFF,0xFF,0xFF,  /* solid top */
    0xFF,0x99,0xFF,  /* windows */
    0xFF,0xFF,0xFF,
    0xFF,0x99,0xFF,  /* windows */
    0xFF,0xFF,0xFF,
    0xFF,0x99,0xFF,  /* windows */
    0xFF,0xFF,0xFF,
    0xFF,0x99,0xFF,  /* windows */
    0xFF,0xFF,0xFF,
    0xFF,0x99,0xFF,  /* windows */
    0xFF,0xFF,0xFF,
    0xFF,0x99,0xFF,  /* windows */
    0xFF,0xFF,0xFF,
    0xFF,0x99,0xFF,  /* windows */
    0xFF,0xFF,0xFF,
    0xFF,0x99,0xFF,  /* windows */
    0xFF,0xFF,0xFF,
    0xFF,0x99,0xFF,  /* windows */
    0xFF,0xFF,0xFF,
    0xFF,0x99,0xFF,  /* windows */
    0xFF,0xFF,0xFF,  /* solid bottom */
    0
};

/* ── Player / enemy sprites ───────────────────────────────────── */
static const uint8_t spr_player0[64]={
    0,0,0, 0,24,0, 0,60,0, 0,126,0, 1,255,128, 3,255,192, 7,189,224,
    7,255,224, 7,126,224, 3,255,192, 3,219,192, 3,219,192, 3,255,192,
    3,102,192, 3,36,192, 6,36,96, 12,102,48, 24,66,24, 48,0,12, 0,0,0, 0,0,0, 0};
static const uint8_t spr_player1[64]={
    0,0,0, 0,24,0, 0,60,0, 0,126,0, 1,255,128, 3,255,192, 7,189,224,
    7,255,224, 7,126,224, 3,255,192, 3,219,192, 3,255,192, 3,219,192,
    3,102,192, 6,36,96, 12,36,48, 24,102,24, 48,66,12, 0,0,0, 0,0,0, 0,0,0, 0};
static const uint8_t spr_enemy[64]={
    0,0,0, 0,126,0, 1,255,128, 3,255,192, 7,255,224, 7,231,224, 7,255,224,
    3,255,192, 3,219,192, 3,255,192, 7,255,224, 15,255,240, 31,255,248,
    63,255,252, 63,231,252, 31,195,248, 15,129,240, 7,0,224, 2,0,64, 0,0,0, 0,0,0, 0};

/* SPR_MEM layout:
   0x7000 (+0x000) player frame 0
   0x7040 (+0x040) player frame 1
   0x7080 (+0x080) enemy
   0x70C0 (+0x0C0) building
*/
static void init_sprites(void){
    copy64(SPR_MEM+0x000,spr_player0);
    copy64(SPR_MEM+0x040,spr_player1);
    copy64(SPR_MEM+0x080,spr_enemy);
    copy64(SPR_MEM+0x0C0,spr_building);

    SPR_PTR[SPR_PLAYER]=(uint8_t)(0x7000u/64u);
    SPR_PTR[SPR_ENEMY0]=(uint8_t)(0x7080u/64u);
    SPR_PTR[SPR_ENEMY1]=(uint8_t)(0x7080u/64u);
    SPR_PTR[SPR_BLDG0] =(uint8_t)(0x70C0u/64u);
    SPR_PTR[SPR_BLDG1] =(uint8_t)(0x70C0u/64u);
    SPR_PTR[SPR_BLDG2] =(uint8_t)(0x70C0u/64u);
    SPR_PTR[SPR_BLDG3] =(uint8_t)(0x70C0u/64u);

    VIC.spr_mcolor=0;
    VIC.spr_exp_x =0;
    /* Y-expand buildings so 21-row sprite becomes 42px tall */
    VIC.spr_exp_y =(uint8_t)((1u<<SPR_BLDG0)|(1u<<SPR_BLDG1)|(1u<<SPR_BLDG2)|(1u<<SPR_BLDG3));
    VIC.spr_bg_prio=(uint8_t)((1u<<SPR_BLDG0)|(1u<<SPR_BLDG1)|(1u<<SPR_BLDG2)|(1u<<SPR_BLDG3));
    VIC.spr_color[SPR_PLAYER]=COLOR_CYAN;
    VIC.spr_color[SPR_ENEMY0]=COLOR_RED;
    VIC.spr_color[SPR_ENEMY1]=COLOR_ORANGE;
    /* neon building colors */
    VIC.spr_color[SPR_BLDG0]=COLOR_BLUE;
    VIC.spr_color[SPR_BLDG1]=COLOR_PURPLE;
    VIC.spr_color[SPR_BLDG2]=COLOR_BLUE;
    VIC.spr_color[SPR_BLDG3]=COLOR_PURPLE;
    VIC.spr_ena=0;
}

/* ── Building sprite logic ────────────────────────────────────── */
static uint8_t bldg_y(uint8_t hoff){
    /* hoff 0-3: offset from tallest. Each step = 10px shorter (higher Y start) */
    return(uint8_t)(BLDG_FOOT - 42u + hoff*10u);
}

static void recycle_bldg(uint8_t i,int16_t x){
    bldgs[i].x=x;
    bldgs[i].hoff=(uint8_t)(rnd8()&3u);
    /* alternate window color */
    bldgs[i].color=(uint8_t)((i&1u)?COLOR_LIGHTBLUE:COLOR_YELLOW);
    /* update multicolor: window pixel color via spr_mcolor1 not available simply;
       just keep the pre-set spr_color -- the window pixels show bgcolor0 (black) */
}

static void init_bldgs(void){
    uint8_t i;
    for(i=0;i<NUM_BLDG;++i){
        /* space buildings evenly across screen + one offscreen right */
        recycle_bldg(i,(int16_t)(SCREEN_LEFT_PX+30u+(uint16_t)i*BLDG_SPACING));
    }
}

static void update_bldgs(uint8_t pixels_scrolled){
    uint8_t i;
    uint8_t ena=VIC.spr_ena;
    for(i=0;i<NUM_BLDG;++i){
        uint8_t si=(uint8_t)(SPR_BLDG0+i);
        bldgs[i].x-=(int16_t)pixels_scrolled;
        /* recycle off left edge */
        if(bldgs[i].x<(int16_t)(SCREEN_LEFT_PX-24)){
            recycle_bldg(i,(int16_t)(bldgs[i].x+(int16_t)(NUM_BLDG*BLDG_SPACING)));
        }
        set_sprite_xy(si,(uint16_t)bldgs[i].x,bldg_y(bldgs[i].hoff));
        ena|=(uint8_t)(1u<<si);
    }
    VIC.spr_ena=ena;
}

/* ── Static screen setup ──────────────────────────────────────── */
/*
 * The char screen never scrolls. We set:
 *   rows 0-2   : HUD (redrawn on score change)
 *   rows 3-21  : all TILE_EMPTY (black sky, never touched again)
 *   row 22     : TILE_GTOP (ground edge, cyan)
 *   rows 23-24 : TILE_SOLID (ground fill, dark green)
 */
static void init_static_screen(void){
    uint8_t x,y;
    /* sky rows: all black empty */
    for(y=HUD_ROWS;y<GROUND_ROW;++y){
        uint16_t row=(uint16_t)y*W;
        for(x=0;x<W;++x){
            SCREEN_GAME[row+x]=TILE_EMPTY;
            COLOR_RAM[row+x]=COLOR_BLACK;
        }
    }
    /* ground top row */
    {uint16_t row=(uint16_t)GROUND_ROW*W;
     for(x=0;x<W;++x){SCREEN_GAME[row+x]=TILE_GTOP;COLOR_RAM[row+x]=COLOR_CYAN;}}
    /* ground fill rows */
    for(y=GROUND_ROW+1u;y<H;++y){
        uint16_t row=(uint16_t)y*W;
        for(x=0;x<W;++x){SCREEN_GAME[row+x]=TILE_SOLID;COLOR_RAM[row+x]=COLOR_BLUE;}
    }
    /* HUD rows cleared */
    for(y=0;y<HUD_ROWS;++y){
        uint16_t row=(uint16_t)y*W;
        for(x=0;x<W;++x){SCREEN_GAME[row+x]=TILE_EMPTY;COLOR_RAM[row+x]=COLOR_BLACK;}
    }
}

/* ── HUD ──────────────────────────────────────────────────────── */
static void draw_hud(void){
    uint8_t i,bar;
    if(score==hud_score_drawn)return;
    hud_score_drawn=score;

    /* row 0: "SC:XXXX" */
    {uint16_t p=1;
     SCREEN_GAME[p]=to_sc('S');COLOR_RAM[p]=COLOR_CYAN;++p;
     SCREEN_GAME[p]=to_sc('C');COLOR_RAM[p]=COLOR_CYAN;++p;
     SCREEN_GAME[p]=to_sc(':');COLOR_RAM[p]=COLOR_CYAN;++p;
     {char buf[6];uint8_t ni=0;uint16_t v=score;
      if(v==0){buf[ni++]='0';}
      else{while(v>0&&ni<5){buf[ni++]=(char)('0'+(v%10));v=(uint16_t)(v/10);}}
      {uint8_t bi=ni;while(bi>0){SCREEN_GAME[p]=to_sc(buf[--bi]);COLOR_RAM[p]=COLOR_WHITE;++p;}}}}

    /* row 1: progress bar */
    bar=(uint8_t)((score>36u)?36u:score);
    for(i=0;i<36u;++i){
        uint16_t p=(uint16_t)W+2u+i;
        SCREEN_GAME[p]=(i<bar)?TILE_HUD:TILE_EMPTY;
        COLOR_RAM[p]=(i<bar)?COLOR_CYAN:COLOR_BLACK;
    }
    /* row 2: separator line */
    for(i=0;i<W;++i){
        uint16_t p=(uint16_t)(2*W)+i;
        SCREEN_GAME[p]=TILE_GTOP;
        COLOR_RAM[p]=COLOR_GRAY2;
    }
}

/* ── Enemy logic ──────────────────────────────────────────────── */
static void spawn_enemy(void){
    uint8_t i;
    for(i=0;i<2u;++i){
        if(!enemies[i].active){
            enemies[i].active=1;
            enemies[i].x=336;
            enemies[i].kind=(uint8_t)((rnd8() & 3u)==0u ? ENEMY_KIND_AIR : ENEMY_KIND_GROUND);
            enemies[i].y=(enemies[i].kind==ENEMY_KIND_AIR)?ENEMY_AIR_Y:(uint8_t)PLAYER_GROUND_Y;
            enemies[i].speed=(uint8_t)((score>60u)?3u:2u);
            return;
        }
    }
}
static void update_enemies(void){
    uint8_t i;
    for(i=0;i<2u;++i){
        if(!enemies[i].active)continue;
        enemies[i].x-=enemies[i].speed;
        enemies[i].y=(enemies[i].kind==ENEMY_KIND_AIR)?ENEMY_AIR_Y:(uint8_t)PLAYER_GROUND_Y;
        if(enemies[i].x<-24){enemies[i].active=0;++score;}
    }
}

static uint8_t player_hits_enemy(void){
    uint8_t i;
    uint16_t px0=(uint16_t)PLAYER_X+PLAYER_HIT_X_OFF;
    uint16_t px1=(uint16_t)(px0+PLAYER_HIT_W);
    uint16_t py0=(uint16_t)player_y+PLAYER_HIT_Y_OFF;
    uint16_t py1=(uint16_t)(py0+PLAYER_HIT_H);

    for(i=0;i<2u;++i){
        uint16_t ex0,ex1,ey0,ey1;
        if(!enemies[i].active)continue;
        ex0=(uint16_t)enemies[i].x+ENEMY_HIT_X_OFF;
        ex1=(uint16_t)(ex0+ENEMY_HIT_W);
        ey0=(uint16_t)enemies[i].y+(enemies[i].kind==ENEMY_KIND_AIR?3u:ENEMY_HIT_Y_OFF);
        ey1=(uint16_t)(ey0+ENEMY_HIT_H);

        if(px0<ex1 && px1>ex0 && py0<ey1 && py1>ey0)return 1u;
    }
    return 0u;
}

/* ── Sprite update ────────────────────────────────────────────── */
static void update_sprites(void){
    uint8_t ena=(uint8_t)(1u<<SPR_PLAYER);
    if(player_y>=(int16_t)PLAYER_GROUND_Y&&(tick&7u)==0u)player_anim^=1u;
    SPR_PTR[SPR_PLAYER]=(uint8_t)((0x7000u+(uint16_t)(player_anim*64u))/64u);
    set_sprite_xy(SPR_PLAYER,PLAYER_X,(uint8_t)player_y);
    if(enemies[0].active){
        set_sprite_xy(SPR_ENEMY0,(uint16_t)enemies[0].x,enemies[0].y);
        ena|=(uint8_t)(1u<<SPR_ENEMY0);
    }
    if(enemies[1].active){
        set_sprite_xy(SPR_ENEMY1,(uint16_t)enemies[1].x,enemies[1].y);
        ena|=(uint8_t)(1u<<SPR_ENEMY1);
    }
    /* preserve building sprite enable bits */
    ena|=(uint8_t)((1u<<SPR_BLDG0)|(1u<<SPR_BLDG1)|(1u<<SPR_BLDG2)|(1u<<SPR_BLDG3));
    VIC.spr_ena=ena;
}

/* ── Reset round ──────────────────────────────────────────────── */
static void reset_round(void){
    uint8_t i;
    set_game_tile_mode();
    VIC.bordercolor=COLOR_BLACK;
    VIC.bgcolor0   =COLOR_BLACK;
    init_tiles();
    init_sprites();
    init_static_screen();
    init_bldgs();

    score=0;tick=0;fine_scroll=7;scroll_delay=8;scroll_counter=0;spawn_counter=20;
    world_scroll_px=0;
    jump_hold_frames=0;
    space_was_down=0;
    jump_sfx_timer=0;
    SID_CTRL1=0;
    player_y=(int16_t)PLAYER_GROUND_Y;player_vy=0;player_anim=0;
    for(i=0;i<2u;++i)enemies[i].active=0;
    hud_score_drawn=0xFFFFu;
    draw_hud();
    update_sprites();
    VIC.ctrl2=(uint8_t)((VIC.ctrl2&0xF8u)|fine_scroll);
}

/* ── Title / game over ────────────────────────────────────────── */
static void title(void){
    set_text_mode_default();VIC.spr_ena=0;
    clear_text_screen(COLOR_BLACK);
    textxy(9, 6,"NEON CYBORG RUNNER",COLOR_CYAN);
    textxy(9, 8,"- NEON CITY EDITION -",COLOR_LIGHTBLUE);
    textxy(10,13,"SPACE = JUMP",COLOR_LIGHTGREEN);
    textxy(14,15,"Q = QUIT",COLOR_LIGHTGREEN);
    textxy(7, 19,"PRESS SPACE TO START",COLOR_WHITE);
    while(1){uint8_t k=cbm_k_getin();if(k==KEY_SPACE)return;wait_frame();}
}

static uint8_t game_over(void){
    if(score>best)best=score;
    set_text_mode_default();VIC.spr_ena=0;
    clear_text_screen(COLOR_BLACK);
    textxy(14, 8,"CYBORG DOWN",COLOR_RED);
    textxy(10,11,"SCORE:",COLOR_WHITE);numxy(18,11,score,COLOR_WHITE);
    textxy(10,13,"BEST:", COLOR_WHITE);numxy(17,13,best, COLOR_WHITE);
    textxy(5, 18,"SPACE = RETRY   Q = QUIT",COLOR_LIGHTBLUE);
    while(1){
        uint8_t k=cbm_k_getin();
        if(k==KEY_SPACE)return 1;
        if(k==KEY_Q_LO||k==KEY_Q_UP)return 0;
        wait_frame();
    }
}

/* ── Main loop ────────────────────────────────────────────────── */
int main(void){
    init_sound();
    while(1){
        uint8_t hit;
        title();
        reset_round();
        hit=0;

        while(!hit){
            uint8_t k;
            uint8_t space_down;
            uint8_t scroll_step;
            wait_frame();
            ++tick;

            k=cbm_k_getin();
            space_down=(uint8_t)(k==KEY_SPACE);
            if(!space_down && jump_hold_frames>0u && player_vy<0){
                space_down=space_is_down();
            }
            if(k==KEY_Q_LO||k==KEY_Q_UP)return 0;
            scroll_step=current_scroll_step();

            update_bldgs(scroll_step);
            if(world_scroll_px<(uint16_t)(65535u-scroll_step)) world_scroll_px=(uint16_t)(world_scroll_px+scroll_step);
            else world_scroll_px=65535u;

            if(space_down && !space_was_down && player_y>=(int16_t)PLAYER_GROUND_Y){
                player_vy=JUMP_SHORT_VY;
                jump_hold_frames=JUMP_HOLD_FRAMES;
                play_jump_sfx();
            }
            if(!space_down) jump_hold_frames=0;
            space_was_down=space_down;

            /* gravity */
            player_y+=player_vy;
            if(player_y<(int16_t)PLAYER_GROUND_Y){
                if(player_vy<0 && jump_hold_frames>0u && space_down){
                    --jump_hold_frames;
                    if((tick&3u)==0u)player_vy+=1;
                }else{
                    if((tick&1u)==0u)player_vy+=1;
                }
            }else{player_y=(int16_t)PLAYER_GROUND_Y;player_vy=0;jump_hold_frames=0;}

            /* fine scroll counter */
            ++scroll_counter;
            if(scroll_counter>=scroll_delay){
                scroll_counter=0;
                fine_scroll=7u;
            }else{
                fine_scroll=(uint8_t)(7u-(scroll_counter*8u/scroll_delay));
            }
            VIC.ctrl2=(uint8_t)((VIC.ctrl2&0xF8u)|fine_scroll);

            /* speed up every 20 points */
            if(score>0u&&(score%20u)==0u&&scroll_delay>4u){
                scroll_delay=(uint8_t)(scroll_delay-1u);
            }

            /* enemies unlock after one full side scroll */
            if(world_scroll_px>=ENEMY_UNLOCK_SCROLL_PX){
                if(spawn_counter>0u)--spawn_counter;
                else{spawn_enemy();spawn_counter=(uint8_t)(20u+(rnd8()&31u));}
            }
            update_enemies();
            update_sprites();
            update_sound();
            draw_hud();

            if(player_hits_enemy())hit=1;
        }

        if(!game_over())return 0;
    }
}
