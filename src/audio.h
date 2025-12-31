// SPDX-License-Identifier: MIT
// Music Player Plugin - Audio Playback

#pragma once

#include <stdint.h>
#include <stdbool.h>

// Initialize audio subsystem
// Returns 0 on success, -1 on failure
int audio_init(void);

// Cleanup audio subsystem
void audio_cleanup(void);

// Start playing an MP3 file
// path: full path to the MP3 file
void audio_play_file(const char* path);

// Stop current playback
void audio_stop(void);

// Pause playback
void audio_pause(void);

// Resume playback after pause
void audio_resume(void);

// Set volume (0-100)
void audio_set_volume(uint8_t volume);

// Check if current song has finished playing
bool audio_is_finished(void);

// Get current playback position in milliseconds
uint32_t audio_get_position_ms(void);

// Process audio - call this regularly from the service loop
// Decodes and plays audio frames
// Returns true if still playing, false if song finished or stopped
bool audio_process(void);
