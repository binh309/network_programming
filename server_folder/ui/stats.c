/**
 * stats.c - Server Statistics Collection Implementation
 */

#include "stats.h"
#include "../data/stock_db.h"
#include "../data/account_db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/resource.h>

// Global stats instance
server_stats_t g_stats;

// Per-stock order tracking (for top stocks)
static uint32_t stock_order_counts[100];  // Max 100 stocks
static double stock_value_traded[100];    // Value traded per stock
static uint32_t user_order_counts[1000];  // Max 1000 users

int stats_init(void) {
    memset(&g_stats, 0, sizeof(server_stats_t));
    memset(stock_order_counts, 0, sizeof(stock_order_counts));
    memset(stock_value_traded, 0, sizeof(stock_value_traded));
    memset(user_order_counts, 0, sizeof(user_order_counts));
    
    if (pthread_mutex_init(&g_stats.lock, NULL) != 0) {
        return -1;
    }
    
    g_stats.start_time = time(NULL);
    clock_gettime(CLOCK_MONOTONIC, &g_stats.last_sample_time);
    
    return 0;
}

void stats_cleanup(void) {
    pthread_mutex_destroy(&g_stats.lock);
}

void stats_record_connection(void) {
    pthread_mutex_lock(&g_stats.lock);
    g_stats.active_connections++;
    g_stats.total_connections++;
    pthread_mutex_unlock(&g_stats.lock);
}

void stats_record_disconnection(void) {
    pthread_mutex_lock(&g_stats.lock);
    if (g_stats.active_connections > 0) {
        g_stats.active_connections--;
    }
    pthread_mutex_unlock(&g_stats.lock);
}

void stats_record_order(bool is_buy, double latency_ms, uint16_t stock_id, uint32_t user_id, double value) {
    pthread_mutex_lock(&g_stats.lock);
    
    g_stats.total_orders++;
    g_stats.total_value_traded += value;
    
    if (is_buy) {
        g_stats.buy_orders++;
    } else {
        g_stats.sell_orders++;
    }
    
    // Interval accumulators
    g_stats.interval_orders++;
    g_stats.interval_latency_sum += latency_ms;
    g_stats.interval_latency_count++;
    g_stats.interval_value += value;
    
    // Track per-stock volume and value
    if (stock_id > 0 && stock_id < 100) {
        stock_order_counts[stock_id]++;
        stock_value_traded[stock_id] += value;
    }
    
    // Track per-user orders
    if (user_id > 0 && user_id < 1000) {
        user_order_counts[user_id]++;
    }
    
    // Update success rate
    uint64_t total = g_stats.total_orders + g_stats.failed_orders;
    if (total > 0) {
        g_stats.success_rate = (double)g_stats.total_orders / total * 100.0;
    }
    
    pthread_mutex_unlock(&g_stats.lock);
}

void stats_record_failed_order(double latency_ms) {
    pthread_mutex_lock(&g_stats.lock);
    g_stats.failed_orders++;
    
    // Failed orders still count for throughput (server processed the request)
    g_stats.interval_orders++;
    g_stats.interval_latency_sum += latency_ms;
    g_stats.interval_latency_count++;
    // Note: Don't add to interval_value - failed orders have no trade value
    
    // Update success rate
    uint64_t total = g_stats.total_orders + g_stats.failed_orders;
    if (total > 0) {
        g_stats.success_rate = (double)g_stats.total_orders / total * 100.0;
    }
    pthread_mutex_unlock(&g_stats.lock);
}

void stats_take_sample(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    
    pthread_mutex_lock(&g_stats.lock);
    
    // Calculate elapsed time since last sample
    double elapsed_sec = (now.tv_sec - g_stats.last_sample_time.tv_sec) +
                         (now.tv_nsec - g_stats.last_sample_time.tv_nsec) / 1e9;
    
    if (elapsed_sec < 0.001) elapsed_sec = 0.001;  // Prevent division by zero
    
    // Calculate instant throughput for this interval
    double instant_throughput = g_stats.interval_orders / elapsed_sec;
    
    // Calculate instant average latency
    double instant_latency = 0;
    if (g_stats.interval_latency_count > 0) {
        instant_latency = g_stats.interval_latency_sum / g_stats.interval_latency_count;
    }
    
    // Store in rolling window
    g_stats.rolling_throughput[g_stats.rolling_index] = instant_throughput;
    g_stats.rolling_latency[g_stats.rolling_index] = instant_latency;
    g_stats.rolling_index = (g_stats.rolling_index + 1) % ROLLING_WINDOW_SIZE;
    
    // Calculate 5-second rolling average for smoother display
    double avg_throughput = 0;
    double avg_latency = 0;
    int latency_samples = 0;
    for (int i = 0; i < ROLLING_WINDOW_SIZE; i++) {
        avg_throughput += g_stats.rolling_throughput[i];
        if (g_stats.rolling_latency[i] > 0) {
            avg_latency += g_stats.rolling_latency[i];
            latency_samples++;
        }
    }
    avg_throughput /= ROLLING_WINDOW_SIZE;
    if (latency_samples > 0) {
        avg_latency /= latency_samples;
    }
    
    g_stats.orders_per_sec = avg_throughput;
    g_stats.latency_avg = avg_latency;
    
    // Store sample in circular buffer (using smoothed values)
    stats_sample_t* sample = &g_stats.samples[g_stats.sample_head];
    sample->throughput = avg_throughput;
    sample->latency_avg = avg_latency;
    sample->orders_count = g_stats.interval_orders;
    sample->value_traded = g_stats.interval_value;
    
    // Advance circular buffer
    g_stats.sample_head = (g_stats.sample_head + 1) % GRAPH_DATA_POINTS;
    if (g_stats.sample_count < GRAPH_DATA_POINTS) {
        g_stats.sample_count++;
    }
    
    // Reset interval accumulators
    g_stats.interval_orders = 0;
    g_stats.interval_latency_sum = 0;
    g_stats.interval_latency_count = 0;
    g_stats.interval_value = 0;
    g_stats.last_sample_time = now;
    
    // Update memory usage (live)
    g_stats.memory_usage = stats_get_memory_usage();
    
    pthread_mutex_unlock(&g_stats.lock);
}

void stats_get_uptime(char* buffer, size_t size) {
    time_t now = time(NULL);
    time_t uptime = now - g_stats.start_time;
    
    int days = uptime / 86400;
    int hours = (uptime % 86400) / 3600;
    int minutes = (uptime % 3600) / 60;
    int seconds = uptime % 60;
    
    if (days > 0) {
        snprintf(buffer, size, "%dd %02dh %02dm %02ds", days, hours, minutes, seconds);
    } else if (hours > 0) {
        snprintf(buffer, size, "%dh %02dm %02ds", hours, minutes, seconds);
    } else {
        snprintf(buffer, size, "%dm %02ds", minutes, seconds);
    }
}

void stats_update_top_stocks(void) {
    pthread_mutex_lock(&g_stats.lock);
    
    // Sort by value traded (descending)
    for (int i = 0; i < TOP_STOCKS_COUNT; i++) {
        double max_value = 0;
        int max_idx = -1;
        
        for (int j = 1; j < 100; j++) {
            if (stock_value_traded[j] > max_value) {
                // Check if already in top list
                bool already_listed = false;
                for (int k = 0; k < i; k++) {
                    if (g_stats.top_stocks[k].stock_id == (uint16_t)j) {
                        already_listed = true;
                        break;
                    }
                }
                if (!already_listed) {
                    max_value = stock_value_traded[j];
                    max_idx = j;
                }
            }
        }
        
        if (max_idx >= 0) {
            g_stats.top_stocks[i].stock_id = max_idx;
            g_stats.top_stocks[i].order_count = stock_order_counts[max_idx];
            g_stats.top_stocks[i].value_traded = max_value;
            
            // Get stock symbol
            stock_t* stock = stock_db_get_by_id(max_idx);
            if (stock) {
                strncpy(g_stats.top_stocks[i].symbol, stock->symbol, 15);
            } else {
                snprintf(g_stats.top_stocks[i].symbol, 16, "STOCK%d", max_idx);
            }
        } else {
            g_stats.top_stocks[i].stock_id = 0;
            g_stats.top_stocks[i].order_count = 0;
            g_stats.top_stocks[i].value_traded = 0;
            g_stats.top_stocks[i].symbol[0] = '\0';
        }
    }
    
    pthread_mutex_unlock(&g_stats.lock);
}

void stats_update_top_traders(void) {
    pthread_mutex_lock(&g_stats.lock);
    
    // Simple selection sort for top N traders
    for (int i = 0; i < TOP_TRADERS_COUNT; i++) {
        uint32_t max_count = 0;
        int max_idx = -1;
        
        for (int j = 1; j < 1000; j++) {
            if (user_order_counts[j] > max_count) {
                // Check if already in top list
                bool already_listed = false;
                for (int k = 0; k < i; k++) {
                    if (g_stats.top_traders[k].user_id == (uint32_t)j) {
                        already_listed = true;
                        break;
                    }
                }
                if (!already_listed) {
                    max_count = user_order_counts[j];
                    max_idx = j;
                }
            }
        }
        
        if (max_idx >= 0) {
            g_stats.top_traders[i].user_id = max_idx;
            g_stats.top_traders[i].order_count = max_count;
            
            // Get username
            account_t* account = account_db_get_by_id(max_idx);
            if (account) {
                strncpy(g_stats.top_traders[i].username, account->username, 63);
            } else {
                snprintf(g_stats.top_traders[i].username, 64, "user%d", max_idx);
            }
        } else {
            g_stats.top_traders[i].user_id = 0;
            g_stats.top_traders[i].order_count = 0;
            g_stats.top_traders[i].username[0] = '\0';
        }
    }
    
    pthread_mutex_unlock(&g_stats.lock);
}

uint64_t stats_get_memory_usage(void) {
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        return usage.ru_maxrss * 1024;  // Convert KB to bytes
    }
    return 0;
}
