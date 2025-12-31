// SPDX-License-Identifier: MIT
// Music Player Plugin - Status Bar Widget

#pragma once

// Initialize and register status widget
// Returns 0 on success, -1 on failure
int widget_init(void);

// Cleanup and unregister status widget
void widget_cleanup(void);
