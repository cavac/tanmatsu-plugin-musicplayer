// SPDX-License-Identifier: MIT
// Music Player Plugin - Playlist Management

#include "playlist.h"
#include "../include/music_player.h"
#include "tanmatsu_plugin.h"
#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <strings.h>

static char current_path_buffer[256];

static bool is_mp3_file(const char* filename) {
    size_t len = strlen(filename);
    if (len < 4) return false;
    const char* ext = filename + len - 4;
    return (strcasecmp(ext, ".mp3") == 0);
}

int playlist_init(void) {
    music_player_state_t* state = music_player_get_state();

    // Check if music directory exists
    struct stat st;
    if (stat(MUSIC_DIR, &st) != 0 || !S_ISDIR(st.st_mode)) {
        asp_log_warn("musicplayer", "Music directory not found: %s", MUSIC_DIR);
        return -1;
    }

    // Scan for MP3 files
    DIR* dir = opendir(MUSIC_DIR);
    if (!dir) {
        asp_log_error("musicplayer", "Failed to open music directory");
        return -1;
    }

    state->playlist.count = 0;
    state->playlist.current_index = 0;

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL && state->playlist.count < MAX_PLAYLIST_ENTRIES) {
        // Check if it's a regular file with .mp3 extension
        if (entry->d_type == DT_REG && is_mp3_file(entry->d_name)) {
            strncpy(state->playlist.songs[state->playlist.count].filename,
                    entry->d_name, MAX_FILENAME_LENGTH - 1);
            state->playlist.songs[state->playlist.count].filename[MAX_FILENAME_LENGTH - 1] = '\0';
            state->playlist.songs[state->playlist.count].duration_ms = 0;
            state->playlist.count++;
        }
    }

    closedir(dir);

    if (state->playlist.count == 0) {
        asp_log_warn("musicplayer", "No MP3 files found in %s", MUSIC_DIR);
        return -1;
    }

    // Sort playlist alphabetically (simple bubble sort for small lists)
    for (int i = 0; i < state->playlist.count - 1; i++) {
        for (int j = 0; j < state->playlist.count - i - 1; j++) {
            if (strcasecmp(state->playlist.songs[j].filename,
                          state->playlist.songs[j + 1].filename) > 0) {
                song_info_t temp = state->playlist.songs[j];
                state->playlist.songs[j] = state->playlist.songs[j + 1];
                state->playlist.songs[j + 1] = temp;
            }
        }
    }

    asp_log_info("musicplayer", "Loaded %d songs into playlist", state->playlist.count);
    return 0;
}

void playlist_cleanup(void) {
    music_player_state_t* state = music_player_get_state();
    state->playlist.count = 0;
    state->playlist.current_index = 0;
}

void playlist_next(void) {
    music_player_state_t* state = music_player_get_state();
    if (state->playlist.count == 0) return;

    state->playlist.current_index++;
    if (state->playlist.current_index >= state->playlist.count) {
        state->playlist.current_index = 0;
    }
}

void playlist_prev_or_restart(void) {
    music_player_state_t* state = music_player_get_state();
    if (state->playlist.count == 0) return;

    uint32_t now = asp_plugin_get_tick_ms();
    uint32_t elapsed = now - state->song_start_time;

    // If within 10 seconds of start, go to previous song
    if (elapsed < 10000) {
        state->playlist.current_index--;
        if (state->playlist.current_index < 0) {
            state->playlist.current_index = state->playlist.count - 1;
        }
    }
    // Otherwise the caller will just restart the current song
}

const char* playlist_get_current_filename(void) {
    music_player_state_t* state = music_player_get_state();
    if (state->playlist.count == 0) return NULL;
    return state->playlist.songs[state->playlist.current_index].filename;
}

const char* playlist_get_current_path(void) {
    const char* filename = playlist_get_current_filename();
    if (!filename) return NULL;

    snprintf(current_path_buffer, sizeof(current_path_buffer),
             "%s/%s", MUSIC_DIR, filename);
    return current_path_buffer;
}
