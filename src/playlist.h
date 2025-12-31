// SPDX-License-Identifier: MIT
// Music Player Plugin - Playlist Management

#pragma once

#include <stdbool.h>

// Initialize playlist by scanning /sd/music for MP3 files
// Returns 0 on success, -1 if no music directory or no files found
int playlist_init(void);

// Cleanup playlist resources
void playlist_cleanup(void);

// Advance to next song (loops at end)
void playlist_next(void);

// Go to previous song or restart current
// If within 10 seconds of start, goes to previous song
// Otherwise restarts current song
void playlist_prev_or_restart(void);

// Get current song filename (just the filename, not full path)
const char* playlist_get_current_filename(void);

// Get full path to current song
// Returns pointer to static buffer - do not free
const char* playlist_get_current_path(void);
