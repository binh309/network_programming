#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include "event_loop.h"
#include "connection_manager.h"
#include "portfolio_manager.h"
#include "../data/account_db.h"
#include "../data/stock_db.h"
#include "../data/portfolio_db.h"
#include "../features/market.h"

#define SERVER_PORT 8888

// Main entry point
int main(int argc, char* argv[]) {
    int port = SERVER_PORT;
    if (argc > 1) {
        port = atoi(argv[1]);
    }

    // 1. Initialize Data Layer
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

    // 4. Start Background Threads
    pthread_t market_tid;
    pthread_create(&market_tid, NULL, market_update_thread, NULL);
    pthread_detach(market_tid);

    // 5. Initialize and Run Event Loop
    event_loop_ctx_t* loop = event_loop_init(port);
    if (!loop) {
        fprintf(stderr, "Failed to initialize event loop\n");
        return 1;
    }

    event_loop_run(loop);

    // 6. Cleanup
    event_loop_shutdown(loop);
    connection_mgr_destroy();
    portfolio_mgr_cleanup();
    portfolio_db_destroy();

    return 0;
}