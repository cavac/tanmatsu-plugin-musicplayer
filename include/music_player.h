// SPDX-License-Identifier: MIT
// Music Player Plugin - Shared Types and State

#pragma once

#include <stdint.h>
#include <stdbool.h>

// Maximum playlist entries
#define MAX_PLAYLIST_ENTRIES 256
#define MAX_FILENAME_LENGTH 128

// Music directory path
#define MUSIC_DIR "/sd/music"

// Playback state
typedef enum {
    PLAYBACK_STOPPED = 0,
    PLAYBACK_PLAYING,
    PLAYBACK_PAUSED,
} playback_state_t;

// Song info structure
typedef struct {
    char filename[MAX_FILENAME_LENGTH];
    uint32_t duration_ms;  // 0 if unknown
} song_info_t;

// Playlist structure
typedef struct {
    song_info_t songs[MAX_PLAYLIST_ENTRIES];
    int count;
    int current_index;
} playlist_t;

// Global music player state
typedef struct {
    playlist_t playlist;
    playback_state_t state;
    uint32_t song_start_time;  // When current song started (tick ms)
    uint32_t current_position_ms;
    uint8_t volume;  // 0-100
} music_player_state_t;

// Global state accessor
music_player_state_t* music_player_get_state(void);
