// SPDX-License-Identifier: MIT
// Music Player Plugin - Audio Playback
// Uses minimp3 for MP3 decoding and ASP audio API for output
// Runs decoding in a separate pthread with larger stack to handle minimp3's stack usage

#include "audio.h"
#include "../include/music_player.h"
#include "tanmatsu_plugin.h"
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>

// Include minimp3 implementation
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#include "minimp3.h"

// ASP audio API - available to plugins
extern int asp_audio_set_rate(uint32_t rate_hz);
extern int asp_audio_get_volume(float* out_percentage);
extern int asp_audio_set_volume(float percentage);
extern int asp_audio_set_amplifier(bool enabled);
extern int asp_audio_stop(void);
extern int asp_audio_start(void);
extern int asp_audio_write(void* samples, size_t samples_size, int64_t timeout_ms);

// Buffer sizes
#define READ_BUFFER_SIZE    (16 * 1024)  // 16KB read buffer for file I/O (PSRAM)
#define MAX_FRAME_SIZE      (1152 * 2)   // Max samples per MP3 frame (stereo)
#define PCM_BUFFER_SIZE     (MAX_FRAME_SIZE * sizeof(int16_t))  // 4608 bytes

// Decoder thread stack size - minimp3 needs ~10KB+ of stack
#define DECODER_STACK_SIZE  (32 * 1024)

// Audio state
static mp3dec_t* g_mp3_decoder = NULL;
static FILE* g_current_file = NULL;
static volatile bool g_playing = false;
static volatile bool g_paused = false;
static volatile bool g_song_finished = false;
static volatile uint64_t g_samples_written = 0;
static volatile uint32_t g_sample_rate = 44100;
static bool g_audio_initialized = false;

// PCM buffer in internal SRAM for DMA (16-byte aligned)
static int16_t pcm_buffer[MAX_FRAME_SIZE] __attribute__((aligned(16)));

// Heap-allocated buffers (PSRAM)
static uint8_t* read_buffer = NULL;
static size_t buffer_pos = 0;
static size_t buffer_len = 0;

// Decoder thread
static pthread_t decoder_thread;
static volatile bool g_thread_running = false;
static volatile bool g_thread_should_stop = false;

// Path for decoder thread to play
static char g_pending_path[256];
static volatile bool g_new_file_pending = false;

// Debug counter for fill_buffer
static int g_fill_count = 0;

// Fill read buffer from file
static size_t fill_buffer(void) {
    if (!g_current_file) {
        return 0;
    }

    // Move remaining data to start of buffer
    if (buffer_pos > 0 && buffer_len > buffer_pos) {
        memmove(read_buffer, read_buffer + buffer_pos, buffer_len - buffer_pos);
        buffer_len -= buffer_pos;
        buffer_pos = 0;
    } else if (buffer_pos > 0) {
        buffer_len = 0;
        buffer_pos = 0;
    }

    // Read more data
    size_t space = READ_BUFFER_SIZE - buffer_len;
    if (space > 0) {
        size_t bytes_read = fread(read_buffer + buffer_len, 1, space, g_current_file);
        buffer_len += bytes_read;
        g_fill_count++;
    }

    return buffer_len - buffer_pos;
}

// Track if we've logged format for current file
static bool g_format_logged = false;

// MP3 decode loop - decode frames and write PCM to audio output
static void decode_loop(void) {
    mp3dec_frame_info_t info;
    int samples;

    while (g_playing && !g_paused && !g_thread_should_stop) {
        // Ensure we have data in buffer
        size_t available = fill_buffer();
        if (available < 4) {
            // End of file
            g_song_finished = true;
            asp_log_info("musicplayer", "Song finished (EOF)");
            break;
        }

        // Decode one frame
        samples = mp3dec_decode_frame(g_mp3_decoder,
                                       read_buffer + buffer_pos,
                                       buffer_len - buffer_pos,
                                       pcm_buffer, &info);

        if (info.frame_bytes > 0) {
            buffer_pos += info.frame_bytes;
        }

        if (samples > 0) {
            // Log format on first successful decode
            if (!g_format_logged) {
                g_sample_rate = info.hz;
                asp_log_info("musicplayer", "Format: %d Hz, %d ch, %d kbps",
                            info.hz, info.channels, info.bitrate_kbps);
                asp_audio_set_rate(info.hz);
                g_format_logged = true;
            }

            // Attenuate samples to ~70% to avoid overdrive
            int total_samples = samples * info.channels;
            for (int i = 0; i < total_samples; i++) {
                pcm_buffer[i] = (int16_t)((pcm_buffer[i] * 70) / 100);
            }

            // Write to audio output (samples * channels * bytes per sample)
            size_t bytes = samples * info.channels * sizeof(int16_t);
            asp_audio_write(pcm_buffer, bytes, 500);
            g_samples_written += samples;
        } else if (info.frame_bytes == 0) {
            // Need more data or invalid frame, try to refill
            if (fill_buffer() == 0) {
                g_song_finished = true;
                asp_log_info("musicplayer", "Song finished (no more data)");
                break;
            }
        }
    }
}

// Start playing a new file (called from decoder thread)
static void start_new_file(const char* path) {
    // Close any existing file
    if (g_current_file) {
        fclose(g_current_file);
        g_current_file = NULL;
    }

    // Open new file
    g_current_file = fopen(path, "rb");
    if (!g_current_file) {
        asp_log_error("musicplayer", "Failed to open: %s", path);
        g_playing = false;
        return;
    }

    // Reset decoder state
    mp3dec_init(g_mp3_decoder);
    buffer_pos = 0;
    buffer_len = 0;

    g_samples_written = 0;
    g_song_finished = false;
    g_format_logged = false;  // Reset for new file
    g_fill_count = 0;  // Reset debug counter
    g_paused = false;
    g_playing = true;

    // Force I2S channel reset: stop, reconfigure, start
    asp_audio_stop();
    asp_audio_set_rate(44100);  // Will be updated when we decode first frame
    asp_audio_start();

    // Enable amplifier and set volume
    asp_audio_set_amplifier(true);
    music_player_state_t* state = music_player_get_state();
    asp_audio_set_volume((float)state->volume);

    asp_log_info("musicplayer", "Playing: %s", path);
}

// Decoder thread main function
static void* decoder_thread_func(void* arg) {
    (void)arg;
    asp_log_info("musicplayer", "Decoder thread started");

    while (!g_thread_should_stop) {
        // Check for new file to play
        if (g_new_file_pending) {
            g_new_file_pending = false;
            start_new_file(g_pending_path);
        }

        // Decode if playing
        if (g_playing && !g_paused) {
            decode_loop();
        } else {
            // Sleep when idle
            asp_plugin_delay_ms(20);
        }
    }

    asp_log_info("musicplayer", "Decoder thread exiting");
    return NULL;
}

int audio_init(void) {
    // Allocate buffers on heap (PSRAM) - PCM buffer is static in SRAM
    read_buffer = (uint8_t*)malloc(READ_BUFFER_SIZE);
    g_mp3_decoder = (mp3dec_t*)malloc(sizeof(mp3dec_t));

    if (!read_buffer || !g_mp3_decoder) {
        asp_log_error("musicplayer", "Failed to allocate buffers");
        if (read_buffer) free(read_buffer);
        if (g_mp3_decoder) free(g_mp3_decoder);
        read_buffer = NULL;
        g_mp3_decoder = NULL;
        return -1;
    }

    // Initialize MP3 decoder
    mp3dec_init(g_mp3_decoder);

    // Create decoder thread with larger stack
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, DECODER_STACK_SIZE);

    g_thread_should_stop = false;
    int err = pthread_create(&decoder_thread, &attr, decoder_thread_func, NULL);
    pthread_attr_destroy(&attr);

    if (err != 0) {
        asp_log_error("musicplayer", "Failed to create decoder thread: %d", err);
        free(read_buffer);
        free(g_mp3_decoder);
        read_buffer = NULL;
        g_mp3_decoder = NULL;
        return -1;
    }

    g_thread_running = true;
    g_audio_initialized = true;

    // Note: Don't call asp_audio_start() - I2S channel is already enabled by BSP

    asp_log_info("musicplayer", "Audio initialized (32KB decoder stack)");
    return 0;
}

void audio_cleanup(void) {
    if (!g_audio_initialized) {
        return;
    }

    // Signal thread to stop
    g_thread_should_stop = true;
    g_playing = false;
    g_paused = false;
    g_new_file_pending = false;

    // Wait for decoder thread to exit
    if (g_thread_running) {
        pthread_join(decoder_thread, NULL);
        g_thread_running = false;
    }

    // Close file if open
    if (g_current_file) {
        fclose(g_current_file);
        g_current_file = NULL;
    }

    // Mute output
    asp_audio_set_amplifier(false);

    // Free heap buffers (PCM buffer is static)
    free(read_buffer);
    free(g_mp3_decoder);
    read_buffer = NULL;
    g_mp3_decoder = NULL;

    g_audio_initialized = false;
}

void audio_play_file(const char* path) {
    // Stop current playback - decoder thread will close file
    g_playing = false;
    g_paused = false;

    // Wait a bit for decoder thread to notice and stop
    asp_plugin_delay_ms(30);

    // Signal decoder thread to play new file
    strncpy(g_pending_path, path, sizeof(g_pending_path) - 1);
    g_pending_path[sizeof(g_pending_path) - 1] = '\0';
    g_new_file_pending = true;
}

void audio_stop(void) {
    g_playing = false;
    g_paused = false;
    g_new_file_pending = false;
    asp_audio_set_amplifier(false);
}

void audio_pause(void) {
    g_paused = true;
    asp_audio_set_amplifier(false);
}

void audio_resume(void) {
    if (g_playing) {
        g_paused = false;
        asp_audio_set_amplifier(true);
    }
}

void audio_set_volume(uint8_t volume) {
    if (volume > 100) volume = 100;
    asp_audio_set_volume((float)volume);
}

bool audio_is_finished(void) {
    return g_song_finished;
}

uint32_t audio_get_position_ms(void) {
    if (g_sample_rate == 0) return 0;
    return (uint32_t)((g_samples_written * 1000ULL) / g_sample_rate);
}

bool audio_process(void) {
    // Processing is now done in decoder thread
    // Just return current state
    return g_playing && !g_song_finished;
}
