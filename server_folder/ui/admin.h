/**
 * admin.h - Admin Command Interface
 * 
 * Handles admin commands for the server CLI.
 */

#ifndef ADMIN_H
#define ADMIN_H

#include <stdbool.h>
#include <stddef.h>

// Maximum command length
#define ADMIN_CMD_MAX_LEN 256

// Maximum history size
#define ADMIN_HISTORY_SIZE 100

// Command result structure
typedef struct {
    bool success;
    char message[1024];
    bool needs_confirmation;
    char confirmation_cmd[ADMIN_CMD_MAX_LEN];
} admin_result_t;

// Admin command history
typedef struct {
    char commands[ADMIN_HISTORY_SIZE][ADMIN_CMD_MAX_LEN];
    int count;
    int current;  // Current position when navigating
} admin_history_t;

// Global history instance
extern admin_history_t g_admin_history;

/**
 * Initialize admin command system
 * @return 0 on success, -1 on failure
 */
int admin_init(void);

/**
 * Cleanup admin system
 */
void admin_cleanup(void);

/**
 * Execute an admin command
 * @param command The command string to execute
 * @param result Output result structure
 * @return true if command was valid, false otherwise
 */
bool admin_execute(const char* command, admin_result_t* result);

/**
 * Add command to history
 * @param command Command to add
 */
void admin_history_add(const char* command);

/**
 * Get previous command from history
 * @return Previous command or NULL
 */
const char* admin_history_prev(void);

/**
 * Get next command from history
 * @return Next command or NULL
 */
const char* admin_history_next(void);

/**
 * Reset history navigation position
 */
void admin_history_reset_nav(void);

/**
 * Get help text for all commands
 * @param buffer Output buffer
 * @param size Buffer size
 */
void admin_get_help(char* buffer, size_t size);

#endif // ADMIN_H
