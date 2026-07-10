/* ==========================================================================
 * DarkCurlyNights64 — Game Boy Port
 * maze_gb.h
 *
 * Reusable block-pushing (Sokoban-style) maze puzzle.
 *
 * The player enters a one-tile-wide corridor on the left and must reach the
 * exit 'E' on the right, pushing debris blocks out of the way in the correct
 * order. Pressing A restarts the current layout.
 *
 * This module lives in its own ROM bank (auto-banked) so it does not consume
 * scarce space in the non-banked HOME segment.
 * ========================================================================== */
#ifndef MAZE_GB_H
#define MAZE_GB_H

#include <gb/gb.h>
#include <stdint.h>

/* A puzzle layout. `map` is a row-major grid of length w*h using:
 *   '#' wall   '.' floor   '$' block   '@' player start   'E' exit          */
typedef struct {
    uint8_t     w;
    uint8_t     h;
    const char* map;
} MazeLevel;

/* Reset the per-playthrough puzzle progress (call when a new game starts). */
void maze_reset_progress(void) BANKED;

/* Report whether a puzzle must be played before entering `scene_id`.
 * On success, fills *level_out and *headline_out and returns 1.            */
uint8_t scene_requires_maze(uint8_t scene_id, const MazeLevel** level_out, const char** headline_out) BANKED;

/* Show the headline, then run the puzzle. Blocks until the player solves it. */
void run_maze_puzzle(const MazeLevel* level, const char* headline) BANKED;

#endif /* MAZE_GB_H */
