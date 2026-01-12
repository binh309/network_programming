#ifndef MARKET_H
#define MARKET_H

// Market updater thread
void* market_update_thread(void* arg);

// Gracefully shutdown market thread
void market_stop(void);

#endif
