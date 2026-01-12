/**
 * tui.h - Terminal User Interface for Server
 * 
 * Main TUI interface providing real-time monitoring dashboard
 * with stats, graphs, logs, and admin command interface.
 */

#ifndef TUI_H
#define TUI_H

#include <stdbool.h>

// TUI configuration
#define TUI_REFRESH_MS      200     // UI refresh rate in milliseconds
#define TUI_SAMPLE_MS       1000    // Stats sample rate (1 second for smoother graph)
#define TUI_LOG_MAX_LINES   100     // Maximum log lines to keep
#define TUI_LOG_LINE_LEN    256     // Maximum length per log line

// Log entry types
typedef enum {
    LOG_INFO,
    LOG_SUCCESS,
    LOG_WARNING,
    LOG_ERROR,
    LOG_CONNECTION
} log_type_t;

/**
 * Initialize the TUI system
 * @return 0 on success, -1 on failure (falls back to simple mode)
 */
int tui_init(void);

/**
 * Cleanup TUI system
 */
void tui_cleanup(void);

/**
 * Start the TUI (runs in main thread, blocks)
 * This starts the UI refresh loop
 */
void tui_run(void);

/**
 * Request TUI shutdown (thread-safe)
 */
void tui_request_shutdown(void);

/**
 * Check if TUI is running
 * @return true if TUI is active
 */
bool tui_is_running(void);

/**
 * Check if TUI is in ncurses mode
 * @return true if using ncurses, false if fallback mode
 */
bool tui_is_graphical(void);

/**
 * Add a log entry (thread-safe)
 * @param type Log type for coloring
 * @param format Printf-style format string
 */
void tui_log(log_type_t type, const char* format, ...);

/**
 * Force a refresh of the display
 */
void tui_refresh(void);

/**
 * Server-wide logging function that routes to TUI or printf
 * Use this instead of printf() throughout the codebase
 * @param format Printf-style format string
 */
void server_log(const char* format, ...);

/**
 * Server-wide debug logging (for verbose debug messages)
 * These are suppressed when TUI is active
 */
void server_debug(const char* format, ...);

#endif // TUI_H
