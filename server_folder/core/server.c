#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

#include "event_loop.h"
#include "connection_manager.h"
#include "portfolio_manager.h"
#include "../data/account_db.h"
#include "../data/stock_db.h"
#include "../data/portfolio_db.h"
#include "../features/market.h"
#include "../ui/tui.h"
#include "../ui/stats.h"

#define SERVER_PORT 8888

// Global event loop for signal handling
static event_loop_ctx_t* g_loop = NULL;
static volatile int g_running = 1;

// Signal handler
static void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
    tui_request_shutdown();
}

// Event loop thread
static void* event_loop_thread(void* arg) {
    event_loop_ctx_t* loop = (event_loop_ctx_t*)arg;
    
    while (g_running) {
        event_loop_step(loop, 100);  // 100ms timeout
    }
    
    return NULL;
}

// Main entry point
int main(int argc, char* argv[]) {
    int port = SERVER_PORT;
    int use_tui = 1;
    
    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--no-tui") == 0) {
            use_tui = 0;
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (argv[i][0] != '-') {
            port = atoi(argv[i]);
        }
    }
    
    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // 1. Initialize Data Layer
    if (!use_tui) {
        printf("[SERVER] Initializing databases...\n");
    }
    
    if (!account_db_init(NULL) || !stock_db_init(NULL) || !portfolio_db_init()) {
        fprintf(stderr, "Failed to initialize databases\n");
        return 1;
    }
    
    // 2. Initialize Connection Manager
    if (connection_mgr_init() < 0) {
        fprintf(stderr, "Failed to initialize connection manager\n");
        return 1;
    }
    
    // 3. Initialize Portfolio Manager
    if (portfolio_mgr_init() < 0) {
        fprintf(stderr, "Failed to initialize portfolio manager\n");
        return 1;
    }
    
    // 4. Initialize Event Loop
    g_loop = event_loop_init(port);
    if (!g_loop) {
        fprintf(stderr, "Failed to initialize event loop\n");
        return 1;
    }
    
    // Mark loop as running for event_loop_step
    g_loop->running = 1;
    
    // 5. Initialize TUI first (before threads that might call tui_log)
    if (use_tui) {
        if (tui_init() < 0) {
            fprintf(stderr, "Failed to initialize TUI\n");
            use_tui = 0;  // Fall back to non-TUI mode
        }
    }
    
    // 6. Start Background Threads AFTER TUI is initialized
    pthread_t market_tid;
    pthread_create(&market_tid, NULL, market_update_thread, NULL);
    pthread_detach(market_tid);
    
    // 7. Start Event Loop in separate thread
    pthread_t event_tid;
    pthread_create(&event_tid, NULL, event_loop_thread, g_loop);
    
    // 8. Run TUI (or simple mode)
    if (use_tui) {
        // Log startup
        tui_log(LOG_SUCCESS, "Server started on port %d", port);
        tui_log(LOG_INFO, "Databases initialized");
        tui_log(LOG_INFO, "Event loop running");
        tui_log(LOG_INFO, "Market simulation active");
        
        // Run TUI (blocks until shutdown)
        tui_run();
        
        // Cleanup TUI
        tui_cleanup();
    } else {
        // Simple mode without TUI
        printf("[SERVER] Running on port %d\n", port);
        printf("[SERVER] Press Ctrl+C to exit\n");
        
        while (g_running) {
            sleep(1);
        }
    }
    
    // 8. Shutdown
    printf("\n[SERVER] Initiating shutdown...\n");
    
    g_running = 0;
    market_stop();
    
    // Wait for event loop thread
    pthread_join(event_tid, NULL);
    
    event_loop_shutdown(g_loop);
    connection_mgr_destroy();
    portfolio_mgr_cleanup();
    portfolio_db_destroy();
    
    printf("[SERVER] Shutdown complete\n");
    
    return 0;
}