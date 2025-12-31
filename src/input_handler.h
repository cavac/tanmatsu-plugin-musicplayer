// SPDX-License-Identifier: MIT
// Music Player Plugin - Input Handler

#pragma once

// Initialize input handler and register hook
// Returns 0 on success, -1 on failure
int input_handler_init(void);

// Cleanup input handler and unregister hook
void input_handler_cleanup(void);
