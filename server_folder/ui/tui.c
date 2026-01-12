/**
 * tui.c - Terminal User Interface Implementation
 * 
 * Implements ncurses-based dashboard with fallback to simple mode.
 */

#include "tui.h"
#include "stats.h"
#include "admin.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <ctype.h>
#include <sys/ioctl.h>
#include <termios.h>

// Try to use ncurses
#ifdef __has_include
#if __has_include(<ncurses.h>)
#include <ncurses.h>
#define HAS_NCURSES 1
#else
#define HAS_NCURSES 0
#endif
#else
// Fallback: try to include
#include <ncurses.h>
#define HAS_NCURSES 1
#endif

// Color pairs
#define COLOR_TITLE     1
#define COLOR_SUCCESS   2
#define COLOR_WARNING   3
#define COLOR_ERROR     4
#define COLOR_INFO      5
#define COLOR_GRAPH1    6
#define COLOR_GRAPH2    7
#define COLOR_BORDER    8

// Layout dimensions (will be calculated based on terminal size)
static int term_width = 0;
static int term_height = 0;
static int stats_height = 0;
static int graph_height = 0;
static int logs_height = 0;
static int input_height = 2;

// Windows
#if HAS_NCURSES
static WINDOW* win_stats = NULL;
static WINDOW* win_graph = NULL;
static WINDOW* win_logs = NULL;
static WINDOW* win_input = NULL;
static WINDOW* win_overlay = NULL;
#endif

// State
static bool tui_running = false;
static bool use_ncurses = false;
static bool shutdown_requested = false;
static pthread_mutex_t tui_lock = PTHREAD_MUTEX_INITIALIZER;

// Log buffer (circular)
typedef struct {
    char text[TUI_LOG_LINE_LEN];
    log_type_t type;
    time_t timestamp;
} log_entry_t;

static log_entry_t log_buffer[TUI_LOG_MAX_LINES];
static int log_head = 0;
static int log_count = 0;
static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;

// Admin input buffer
static char input_buffer[ADMIN_CMD_MAX_LEN] = {0};
static int input_pos = 0;
static bool in_overlay_mode = false;

// Saved stdout/stderr for restoration
static int saved_stdout_fd = -1;
static int saved_stderr_fd = -1;
static FILE* dev_null = NULL;

// Forward declarations
static void draw_stats_panel(void);
static void draw_graph_panel(void);
static void draw_logs_panel(void);
static void draw_input_panel(void);
static void draw_overlay(const char* title, const char* content);
static void hide_overlay(void);
static void handle_input(int ch);
static void calculate_layout(void);
static void simple_mode_run(void);

#if HAS_NCURSES
static void init_colors(void) {
    if (has_colors()) {
        start_color();
        use_default_colors();
        
        init_pair(COLOR_TITLE, COLOR_CYAN, -1);
        init_pair(COLOR_SUCCESS, COLOR_GREEN, -1);
        init_pair(COLOR_WARNING, COLOR_YELLOW, -1);
        init_pair(COLOR_ERROR, COLOR_RED, -1);
        init_pair(COLOR_INFO, COLOR_WHITE, -1);
        init_pair(COLOR_GRAPH1, COLOR_GREEN, -1);
        init_pair(COLOR_GRAPH2, COLOR_YELLOW, -1);
        init_pair(COLOR_BORDER, COLOR_BLUE, -1);
    }
}

static void create_windows(void) {
    calculate_layout();
    
    // Stats panel (top-left, about 40% width)
    int stats_width = term_width * 40 / 100;
    win_stats = newwin(stats_height, stats_width, 0, 0);
    
    // Graph panel (top-right, remaining width)
    int graph_width = term_width - stats_width;
    win_graph = newwin(stats_height, graph_width, 0, stats_width);
    
    // Logs panel (middle)
    win_logs = newwin(logs_height, term_width, stats_height, 0);
    
    // Input panel (bottom)
    win_input = newwin(input_height, term_width, stats_height + logs_height, 0);
    
    // Overlay window (full screen, initially hidden)
    win_overlay = newwin(term_height, term_width, 0, 0);
}

static void destroy_windows(void) {
    if (win_stats) { delwin(win_stats); win_stats = NULL; }
    if (win_graph) { delwin(win_graph); win_graph = NULL; }
    if (win_logs) { delwin(win_logs); win_logs = NULL; }
    if (win_input) { delwin(win_input); win_input = NULL; }
    if (win_overlay) { delwin(win_overlay); win_overlay = NULL; }
}
#endif

static void calculate_layout(void) {
#if HAS_NCURSES
    if (use_ncurses) {
        getmaxyx(stdscr, term_height, term_width);
    } else
#endif
    {
        struct winsize w;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
            term_width = w.ws_col;
            term_height = w.ws_row;
        } else {
            term_width = 80;
            term_height = 24;
        }
    }
    
    // Calculate panel heights
    // Stats + Graph: ~55% of screen
    // Logs: ~35% of screen
    // Input: 2 lines
    stats_height = term_height * 55 / 100;
    if (stats_height < 15) stats_height = 15;
    
    input_height = 2;
    logs_height = term_height - stats_height - input_height;
    if (logs_height < 5) logs_height = 5;
    
    // Graph uses same height as stats
    graph_height = stats_height;
}

int tui_init(void) {
    // Initialize stats first
    if (stats_init() < 0) {
        fprintf(stderr, "[TUI] Failed to init stats\n");
        return -1;
    }
    
    // Initialize admin
    if (admin_init() < 0) {
        fprintf(stderr, "[TUI] Failed to init admin\n");
        return -1;
    }
    
#if HAS_NCURSES
    // Initialize ncurses FIRST so we can get proper terminal dimensions
    if (initscr() == NULL) {
        fprintf(stderr, "[TUI] Failed to init ncurses, using simple mode\n");
        use_ncurses = false;
        tui_running = true;
        return 0;
    }
    
    // Now we can use ncurses to get terminal size
    use_ncurses = true;
    calculate_layout();
    
    // Check if we have enough terminal space
    if (term_width < 80 || term_height < 24) {
        endwin();
        fprintf(stderr, "[TUI] Terminal too small (%dx%d), need at least 80x24\n", 
                term_width, term_height);
        fprintf(stderr, "[TUI] Falling back to simple mode\n");
        use_ncurses = false;
        tui_running = true;
        return 0;
    }
    
    // Configure ncurses
    cbreak();               // Disable line buffering
    noecho();               // Don't echo input
    keypad(stdscr, TRUE);   // Enable function keys
    nodelay(stdscr, TRUE);  // Non-blocking input
    curs_set(1);            // Show cursor
    
    init_colors();
    create_windows();
    
    // Do initial draw immediately
    draw_stats_panel();
    draw_graph_panel();
    draw_logs_panel();
    draw_input_panel();
    
    // Note: We don't redirect stdout/stderr - instead, code should use tui_log()
#else
    calculate_layout();
    use_ncurses = false;
#endif
    
    tui_running = true;
    return 0;
}

void tui_cleanup(void) {
    tui_running = false;
    
#if HAS_NCURSES
    if (use_ncurses) {
        destroy_windows();
        endwin();
        
        // Restore stdout/stderr
        if (saved_stdout_fd >= 0) {
            dup2(saved_stdout_fd, STDOUT_FILENO);
            close(saved_stdout_fd);
            saved_stdout_fd = -1;
        }
        if (saved_stderr_fd >= 0) {
            dup2(saved_stderr_fd, STDERR_FILENO);
            close(saved_stderr_fd);
            saved_stderr_fd = -1;
        }
        if (dev_null) {
            fclose(dev_null);
            dev_null = NULL;
        }
    }
#endif
    
    admin_cleanup();
    stats_cleanup();
}

void tui_request_shutdown(void) {
    shutdown_requested = true;
}

bool tui_is_running(void) {
    return tui_running && !shutdown_requested;
}

bool tui_is_graphical(void) {
    return use_ncurses;
}

void tui_log(log_type_t type, const char* format, ...) {
    pthread_mutex_lock(&log_lock);
    
    log_entry_t* entry = &log_buffer[log_head];
    entry->type = type;
    entry->timestamp = time(NULL);
    
    va_list args;
    va_start(args, format);
    vsnprintf(entry->text, TUI_LOG_LINE_LEN, format, args);
    va_end(args);
    
    log_head = (log_head + 1) % TUI_LOG_MAX_LINES;
    if (log_count < TUI_LOG_MAX_LINES) {
        log_count++;
    }
    
    pthread_mutex_unlock(&log_lock);
}

#if HAS_NCURSES
static void draw_box_with_title(WINDOW* win, const char* title, int color_pair) {
    int height, width;
    getmaxyx(win, height, width);
    (void)height;  // Unused
    
    wattron(win, COLOR_PAIR(color_pair));
    box(win, 0, 0);
    wattroff(win, COLOR_PAIR(color_pair));
    
    if (title) {
        wattron(win, COLOR_PAIR(COLOR_TITLE) | A_BOLD);
        mvwprintw(win, 0, 2, " %s ", title);
        wattroff(win, COLOR_PAIR(COLOR_TITLE) | A_BOLD);
    }
}

static void draw_stats_panel(void) {
    if (!win_stats) return;
    
    werase(win_stats);
    draw_box_with_title(win_stats, "SERVER STATISTICS", COLOR_BORDER);
    
    int row = 2;
    int col = 2;
    
    // Uptime and time
    char uptime[64];
    stats_get_uptime(uptime, sizeof(uptime));
    
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char timestr[32];
    strftime(timestr, sizeof(timestr), "%H:%M:%S", tm_info);
    
    wattron(win_stats, COLOR_PAIR(COLOR_TITLE));
    mvwprintw(win_stats, row++, col, "Uptime: %s | Time: %s", uptime, timestr);
    wattroff(win_stats, COLOR_PAIR(COLOR_TITLE));
    row++;
    
    // Connection stats (simplified)
    wattron(win_stats, A_BOLD);
    mvwprintw(win_stats, row++, col, "CONNECTIONS");
    wattroff(win_stats, A_BOLD);
    
    mvwprintw(win_stats, row++, col, "  Active:      %u", g_stats.active_connections);
    row++;
    
    // Trading stats with value
    wattron(win_stats, A_BOLD);
    mvwprintw(win_stats, row++, col, "TRADING ACTIVITY");
    wattroff(win_stats, A_BOLD);
    
    uint64_t total_processed = g_stats.total_orders + g_stats.failed_orders;
    mvwprintw(win_stats, row++, col, "  Processed:   %lu", total_processed);
    mvwprintw(win_stats, row++, col, "  Filled:      %lu", g_stats.total_orders);
    
    if (g_stats.failed_orders > 0) {
        wattron(win_stats, COLOR_PAIR(COLOR_WARNING));
        mvwprintw(win_stats, row++, col, "  Rejected:    %lu", g_stats.failed_orders);
        wattroff(win_stats, COLOR_PAIR(COLOR_WARNING));
    }
    
    // Format value traded nicely
    double value = g_stats.total_value_traded;
    if (value >= 1000000) {
        mvwprintw(win_stats, row++, col, "  Value:       $%.2fM", value / 1000000.0);
    } else if (value >= 1000) {
        mvwprintw(win_stats, row++, col, "  Value:       $%.2fK", value / 1000.0);
    } else {
        mvwprintw(win_stats, row++, col, "  Value:       $%.2f", value);
    }
    
    wattron(win_stats, COLOR_PAIR(g_stats.success_rate >= 99 ? COLOR_SUCCESS : (g_stats.success_rate >= 90 ? COLOR_WARNING : COLOR_ERROR)));
    mvwprintw(win_stats, row++, col, "  Success:     %.1f%%", g_stats.success_rate);
    wattroff(win_stats, COLOR_PAIR(g_stats.success_rate >= 99 ? COLOR_SUCCESS : (g_stats.success_rate >= 90 ? COLOR_WARNING : COLOR_ERROR)));
    row++;
    
    // Performance stats
    wattron(win_stats, A_BOLD);
    mvwprintw(win_stats, row++, col, "PERFORMANCE (5s avg)");
    wattroff(win_stats, A_BOLD);
    
    mvwprintw(win_stats, row++, col, "  Throughput:  %.2f/s", g_stats.orders_per_sec);
    
    int latency_color = g_stats.latency_avg < 10 ? COLOR_SUCCESS : 
                        (g_stats.latency_avg < 50 ? COLOR_WARNING : COLOR_ERROR);
    wattron(win_stats, COLOR_PAIR(latency_color));
    if (g_stats.latency_avg > 0) {
        mvwprintw(win_stats, row++, col, "  Latency:     %.2f ms", g_stats.latency_avg);
    } else {
        mvwprintw(win_stats, row++, col, "  Latency:     --");
    }
    wattroff(win_stats, COLOR_PAIR(latency_color));
    
    mvwprintw(win_stats, row++, col, "  Memory:      %.2f MB", 
              g_stats.memory_usage / (1024.0 * 1024.0));
    row++;
    
    // Top stocks by value traded
    stats_update_top_stocks();
    wattron(win_stats, A_BOLD);
    mvwprintw(win_stats, row++, col, "TOP STOCKS (by value)");
    wattroff(win_stats, A_BOLD);
    
    for (int i = 0; i < TOP_STOCKS_COUNT && g_stats.top_stocks[i].value_traded > 0; i++) {
        double val = g_stats.top_stocks[i].value_traded;
        if (val >= 1000000) {
            mvwprintw(win_stats, row++, col, "  %d. %-5s $%.1fM (%u)", 
                      i + 1, g_stats.top_stocks[i].symbol, val / 1000000.0,
                      g_stats.top_stocks[i].order_count);
        } else if (val >= 1000) {
            mvwprintw(win_stats, row++, col, "  %d. %-5s $%.1fK (%u)", 
                      i + 1, g_stats.top_stocks[i].symbol, val / 1000.0,
                      g_stats.top_stocks[i].order_count);
        } else {
            mvwprintw(win_stats, row++, col, "  %d. %-5s $%.0f (%u)", 
                      i + 1, g_stats.top_stocks[i].symbol, val,
                      g_stats.top_stocks[i].order_count);
        }
    }
    
    wrefresh(win_stats);
}

static void draw_graph_panel(void) {
    if (!win_graph) return;
    
    werase(win_graph);
    draw_box_with_title(win_graph, "PERFORMANCE GRAPH (5 min)", COLOR_BORDER);
    
    int height, width;
    getmaxyx(win_graph, height, width);
    
    // Available drawing area
    int graph_area_height = (height - 4) / 2;  // Split for two graphs
    int graph_area_width = width - 4;
    
    if (graph_area_height < 3 || graph_area_width < 20) {
        mvwprintw(win_graph, 2, 2, "Graph area too small");
        wrefresh(win_graph);
        return;
    }
    
    // Find min/max for auto-scaling
    double max_throughput = 1.0;  // Minimum to avoid zero scale
    double max_latency = 10.0;
    
    pthread_mutex_lock(&g_stats.lock);
    
    for (int i = 0; i < g_stats.sample_count; i++) {
        int idx = (g_stats.sample_head - 1 - i + GRAPH_DATA_POINTS) % GRAPH_DATA_POINTS;
        if (g_stats.samples[idx].throughput > max_throughput) {
            max_throughput = g_stats.samples[idx].throughput;
        }
        if (g_stats.samples[idx].latency_avg > max_latency) {
            max_latency = g_stats.samples[idx].latency_avg;
        }
    }
    
    // Draw throughput graph
    int graph1_start = 2;
    wattron(win_graph, COLOR_PAIR(COLOR_GRAPH1));
    mvwprintw(win_graph, graph1_start, 2, "Orders/sec (max: %.1f)", max_throughput);
    wattroff(win_graph, COLOR_PAIR(COLOR_GRAPH1));
    
    // Draw baseline for throughput graph
    int baseline1_y = graph1_start + graph_area_height;
    wattron(win_graph, COLOR_PAIR(COLOR_BORDER));
    for (int x = 2; x < width - 2; x++) {
        mvwaddch(win_graph, baseline1_y, x, '-');
    }
    wattroff(win_graph, COLOR_PAIR(COLOR_BORDER));
    
    // Draw graph from right to left (newest on right)
    int samples_to_show = graph_area_width < g_stats.sample_count ? 
                          graph_area_width : g_stats.sample_count;
    
    for (int x = 0; x < samples_to_show; x++) {
        int sample_idx = (g_stats.sample_head - 1 - x + GRAPH_DATA_POINTS) % GRAPH_DATA_POINTS;
        double value = g_stats.samples[sample_idx].throughput;
        
        int screen_x = width - 3 - x;
        if (screen_x < 2) break;
        
        if (value > 0) {
            int bar_height = (int)((value / max_throughput) * (graph_area_height - 1));
            if (bar_height < 1) bar_height = 1;
            if (bar_height >= graph_area_height) bar_height = graph_area_height - 1;
            
            wattron(win_graph, COLOR_PAIR(COLOR_GRAPH1));
            for (int y = 0; y <= bar_height; y++) {
                int screen_y = graph1_start + graph_area_height - 1 - y;
                mvwaddch(win_graph, screen_y, screen_x, y == bar_height ? '*' : '|');
            }
            wattroff(win_graph, COLOR_PAIR(COLOR_GRAPH1));
        }
    }
    
    // Draw latency graph
    int graph2_start = graph1_start + graph_area_height + 2;
    wattron(win_graph, COLOR_PAIR(COLOR_GRAPH2));
    mvwprintw(win_graph, graph2_start, 2, "Latency ms (max: %.1f)", max_latency);
    wattroff(win_graph, COLOR_PAIR(COLOR_GRAPH2));
    
    // Draw baseline for latency graph
    int baseline2_y = graph2_start + graph_area_height;
    wattron(win_graph, COLOR_PAIR(COLOR_BORDER));
    for (int x = 2; x < width - 2; x++) {
        mvwaddch(win_graph, baseline2_y, x, '-');
    }
    wattroff(win_graph, COLOR_PAIR(COLOR_BORDER));
    
    for (int x = 0; x < samples_to_show; x++) {
        int sample_idx = (g_stats.sample_head - 1 - x + GRAPH_DATA_POINTS) % GRAPH_DATA_POINTS;
        double value = g_stats.samples[sample_idx].latency_avg;
        
        int screen_x = width - 3 - x;
        if (screen_x < 2) break;
        
        if (value > 0) {
            int bar_height = (int)((value / max_latency) * (graph_area_height - 1));
            if (bar_height < 1) bar_height = 1;
            if (bar_height >= graph_area_height) bar_height = graph_area_height - 1;
            
            // Color based on latency level
            int color = value < 50 ? COLOR_SUCCESS : (value < 200 ? COLOR_WARNING : COLOR_ERROR);
            wattron(win_graph, COLOR_PAIR(color));
            for (int y = 0; y <= bar_height; y++) {
                int screen_y = graph2_start + graph_area_height - 1 - y;
                mvwaddch(win_graph, screen_y, screen_x, y == bar_height ? '*' : '|');
            }
            wattroff(win_graph, COLOR_PAIR(color));
        }
    }
    
    pthread_mutex_unlock(&g_stats.lock);
    
    wrefresh(win_graph);
}

static void draw_logs_panel(void) {
    if (!win_logs) return;
    
    werase(win_logs);
    draw_box_with_title(win_logs, "ACTIVITY LOG", COLOR_BORDER);
    
    int height, width;
    getmaxyx(win_logs, height, width);
    
    int max_lines = height - 2;
    int row = 1;
    
    pthread_mutex_lock(&log_lock);
    
    int start_idx = log_count > max_lines ? log_count - max_lines : 0;
    
    for (int i = start_idx; i < log_count && row < height - 1; i++) {
        int idx = (log_head - log_count + i + TUI_LOG_MAX_LINES) % TUI_LOG_MAX_LINES;
        log_entry_t* entry = &log_buffer[idx];
        
        struct tm* tm = localtime(&entry->timestamp);
        char timestr[16];
        strftime(timestr, sizeof(timestr), "%H:%M:%S", tm);
        
        int color;
        char prefix;
        switch (entry->type) {
            case LOG_SUCCESS:    color = COLOR_SUCCESS; prefix = '+'; break;
            case LOG_WARNING:    color = COLOR_WARNING; prefix = '!'; break;
            case LOG_ERROR:      color = COLOR_ERROR;   prefix = 'X'; break;
            case LOG_CONNECTION: color = COLOR_INFO;    prefix = '*'; break;
            default:             color = COLOR_INFO;    prefix = '-'; break;
        }
        
        mvwprintw(win_logs, row, 2, "[%s] ", timestr);
        wattron(win_logs, COLOR_PAIR(color));
        wprintw(win_logs, "%c ", prefix);
        wattroff(win_logs, COLOR_PAIR(color));
        
        // Truncate message if needed
        int remaining = width - 16;
        if ((int)strlen(entry->text) > remaining) {
            char truncated[256];
            strncpy(truncated, entry->text, remaining - 3);
            truncated[remaining - 3] = '\0';
            strcat(truncated, "...");
            wprintw(win_logs, "%s", truncated);
        } else {
            wprintw(win_logs, "%s", entry->text);
        }
        
        row++;
    }
    
    pthread_mutex_unlock(&log_lock);
    
    wrefresh(win_logs);
}

static void draw_input_panel(void) {
    if (!win_input) return;
    
    werase(win_input);
    
    int height, width;
    getmaxyx(win_input, height, width);
    (void)height;
    
    // Draw separator line
    wattron(win_input, COLOR_PAIR(COLOR_BORDER));
    mvwhline(win_input, 0, 0, ACS_HLINE, width);
    wattroff(win_input, COLOR_PAIR(COLOR_BORDER));
    
    // Draw prompt
    wattron(win_input, COLOR_PAIR(COLOR_TITLE) | A_BOLD);
    mvwprintw(win_input, 1, 1, "admin> ");
    wattroff(win_input, COLOR_PAIR(COLOR_TITLE) | A_BOLD);
    
    // Draw current input
    wprintw(win_input, "%s", input_buffer);
    
    // Position cursor
    wmove(win_input, 1, 8 + input_pos);
    
    wrefresh(win_input);
}

static void draw_overlay(const char* title, const char* content) {
    if (!win_overlay) return;
    
    in_overlay_mode = true;
    
    werase(win_overlay);
    draw_box_with_title(win_overlay, title, COLOR_BORDER);
    
    int height, width;
    getmaxyx(win_overlay, height, width);
    
    // Draw content line by line
    int row = 2;
    int col = 2;
    int max_width = width - 4;
    
    const char* line = content;
    while (*line && row < height - 2) {
        const char* end = strchr(line, '\n');
        int len = end ? (int)(end - line) : (int)strlen(line);
        
        if (len > max_width) len = max_width;
        
        mvwaddnstr(win_overlay, row, col, line, len);
        row++;
        
        if (end) {
            line = end + 1;
        } else {
            break;
        }
    }
    
    // Draw footer
    wattron(win_overlay, COLOR_PAIR(COLOR_INFO));
    mvwprintw(win_overlay, height - 2, col, "Press ENTER to return, or type another command");
    wattroff(win_overlay, COLOR_PAIR(COLOR_INFO));
    
    wrefresh(win_overlay);
}

static void hide_overlay(void) {
    in_overlay_mode = false;
    
    // Redraw all panels
    draw_stats_panel();
    draw_graph_panel();
    draw_logs_panel();
    draw_input_panel();
}

static void handle_input(int ch) {
    if (ch == ERR) return;
    
    switch (ch) {
        case KEY_UP:
            // History navigation
            {
                const char* prev = admin_history_prev();
                if (prev) {
                    strncpy(input_buffer, prev, ADMIN_CMD_MAX_LEN - 1);
                    input_pos = strlen(input_buffer);
                }
            }
            break;
            
        case KEY_DOWN:
            // History navigation
            {
                const char* next = admin_history_next();
                if (next) {
                    strncpy(input_buffer, next, ADMIN_CMD_MAX_LEN - 1);
                    input_pos = strlen(input_buffer);
                }
            }
            break;
            
        case KEY_LEFT:
            if (input_pos > 0) input_pos--;
            break;
            
        case KEY_RIGHT:
            if (input_pos < (int)strlen(input_buffer)) input_pos++;
            break;
            
        case KEY_BACKSPACE:
        case 127:
        case 8:
            if (input_pos > 0) {
                memmove(&input_buffer[input_pos - 1], &input_buffer[input_pos], 
                        strlen(&input_buffer[input_pos]) + 1);
                input_pos--;
            }
            break;
            
        case KEY_DC:  // Delete
            if (input_pos < (int)strlen(input_buffer)) {
                memmove(&input_buffer[input_pos], &input_buffer[input_pos + 1],
                        strlen(&input_buffer[input_pos + 1]) + 1);
            }
            break;
            
        case '\n':
        case KEY_ENTER:
            if (in_overlay_mode && strlen(input_buffer) == 0) {
                // Just return from overlay
                hide_overlay();
            } else if (strlen(input_buffer) > 0) {
                // Execute command
                admin_result_t result;
                admin_execute(input_buffer, &result);
                admin_history_add(input_buffer);
                admin_history_reset_nav();
                
                // Check for shutdown
                if (strstr(input_buffer, "shutdown") != NULL && result.success) {
                    shutdown_requested = true;
                }
                
                // Clear input
                memset(input_buffer, 0, sizeof(input_buffer));
                input_pos = 0;
                
                // Show result in overlay
                draw_overlay(result.success ? "SUCCESS" : "RESULT", result.message);
            } else if (in_overlay_mode) {
                hide_overlay();
            }
            break;
            
        case 27:  // ESC
            if (in_overlay_mode) {
                hide_overlay();
            }
            memset(input_buffer, 0, sizeof(input_buffer));
            input_pos = 0;
            break;
            
        default:
            // Add printable character
            if (isprint(ch) && input_pos < ADMIN_CMD_MAX_LEN - 1) {
                int len = strlen(input_buffer);
                if (len < ADMIN_CMD_MAX_LEN - 1) {
                    memmove(&input_buffer[input_pos + 1], &input_buffer[input_pos],
                            len - input_pos + 1);
                    input_buffer[input_pos] = ch;
                    input_pos++;
                }
            }
            break;
    }
}
#endif

void tui_run(void) {
#if HAS_NCURSES
    if (!use_ncurses) {
        simple_mode_run();
        return;
    }
    
    struct timespec last_refresh, last_sample, now;
    clock_gettime(CLOCK_MONOTONIC, &last_refresh);
    last_sample = last_refresh;
    
    while (!shutdown_requested) {
        // Handle input
        int ch = getch();
        handle_input(ch);
        
        // Check if it's time to refresh
        clock_gettime(CLOCK_MONOTONIC, &now);
        double elapsed_ms = (now.tv_sec - last_refresh.tv_sec) * 1000.0 +
                            (now.tv_nsec - last_refresh.tv_nsec) / 1e6;
        double sample_elapsed_ms = (now.tv_sec - last_sample.tv_sec) * 1000.0 +
                                   (now.tv_nsec - last_sample.tv_nsec) / 1e6;
        
        // Take stats sample every TUI_SAMPLE_MS (1 second)
        if (sample_elapsed_ms >= TUI_SAMPLE_MS) {
            stats_take_sample();
            last_sample = now;
        }
        
        // Refresh UI every TUI_REFRESH_MS (200ms)
        if (elapsed_ms >= TUI_REFRESH_MS) {
            // Redraw panels (but not overlay if it's showing)
            if (!in_overlay_mode) {
                draw_stats_panel();
                draw_graph_panel();
                draw_logs_panel();
            }
            draw_input_panel();
            
            last_refresh = now;
        }
        
        // Small sleep to prevent CPU spinning
        usleep(10000);  // 10ms
    }
#else
    simple_mode_run();
#endif
}

static void simple_mode_run(void) {
    // Fallback mode: just print stats periodically
    printf("\n=== Trading Server (Simple Mode) ===\n");
    printf("Terminal too small for graphical UI.\n");
    printf("Showing basic stats every 5 seconds.\n");
    printf("Press Ctrl+C to exit.\n\n");
    
    while (!shutdown_requested) {
        stats_take_sample();
        
        char uptime[64];
        stats_get_uptime(uptime, sizeof(uptime));
        
        printf("\r[%s] Conn: %u | Orders: %lu | Rate: %.2f/s | Latency: %.2fms    ",
               uptime,
               g_stats.active_connections,
               g_stats.total_orders,
               g_stats.orders_per_sec,
               g_stats.latency_avg);
        fflush(stdout);
        
        sleep(5);
    }
    
    printf("\n");
}

void tui_refresh(void) {
#if HAS_NCURSES
    if (use_ncurses && !in_overlay_mode) {
        draw_stats_panel();
        draw_graph_panel();
        draw_logs_panel();
        draw_input_panel();
    }
#endif
}

void server_log(const char* format, ...) {
    va_list args;
    va_start(args, format);
    
    if (use_ncurses && tui_running) {
        // Route to TUI log
        char buffer[TUI_LOG_LINE_LEN];
        vsnprintf(buffer, sizeof(buffer), format, args);
        // Strip trailing newline if present
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len-1] == '\n') {
            buffer[len-1] = '\0';
        }
        tui_log(LOG_INFO, "%s", buffer);
    } else {
        // Fall back to printf
        vprintf(format, args);
    }
    
    va_end(args);
}

void server_debug(const char* format, ...) {
    // Debug messages are suppressed when TUI is active
    if (use_ncurses && tui_running) {
        return;  // Suppress debug output in TUI mode
    }
    
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}
