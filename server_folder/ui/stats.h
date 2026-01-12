/**
 * stats.h - Server Statistics Collection
 * 
 * Thread-safe statistics collection for monitoring server performance.
 * Uses atomic operations where possible for lock-free access.
 */

#ifndef STATS_H
#define STATS_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <pthread.h>

// Graph window configuration
#define GRAPH_WINDOW_SECS   300     // 5 minutes total window
#define GRAPH_SAMPLE_MS     1000    // Sample every 1 second for graph
#define GRAPH_DATA_POINTS   (GRAPH_WINDOW_SECS)  // 300 samples (5 min * 60s = 300, but we use 1s intervals)

// Rolling window for smoothing (5 seconds)
#define ROLLING_WINDOW_SIZE 5

// Top N tracking
#define TOP_STOCKS_COUNT    5
#define TOP_TRADERS_COUNT   5

// Stats structure for a single sample
typedef struct {
    double throughput;      // Orders per second (5s average)
    double latency_avg;     // Average latency in ms (5s average)
    uint32_t orders_count;  // Orders in this sample period
    double value_traded;    // Total value traded in this period
} stats_sample_t;

// Top stock entry
typedef struct {
    uint16_t stock_id;
    char symbol[16];
    uint32_t order_count;
    double value_traded;    // Total value traded for this stock
} top_stock_t;

// Top trader entry
typedef struct {
    uint32_t user_id;
    char username[64];
    uint32_t order_count;
} top_trader_t;

// Main server statistics structure
typedef struct {
    // Connection stats
    uint32_t active_connections;
    uint32_t total_connections;
    double connections_per_sec;
    
    // Trading stats
    uint64_t total_orders;
    uint64_t buy_orders;
    uint64_t sell_orders;
    uint64_t failed_orders;
    double success_rate;
    double orders_per_sec;
    double total_value_traded;  // Cumulative value traded
    
    // Performance stats
    double latency_avg;
    double latency_p95;
    double latency_p99;
    uint64_t memory_usage;
    
    // Uptime
    time_t start_time;
    
    // Graph data (circular buffer)
    stats_sample_t samples[GRAPH_DATA_POINTS];
    int sample_head;        // Current write position
    int sample_count;       // Number of samples collected
    
    // Top performers
    top_stock_t top_stocks[TOP_STOCKS_COUNT];
    top_trader_t top_traders[TOP_TRADERS_COUNT];
    
    // Internal tracking for rate calculations
    uint64_t last_orders_count;
    uint64_t last_connections_count;
    struct timespec last_sample_time;
    
    // Current interval accumulators
    uint32_t interval_orders;
    double interval_latency_sum;
    uint32_t interval_latency_count;
    double interval_value;      // Value traded in current interval
    
    // Rolling window for smoothing (last 5 seconds)
    double rolling_throughput[ROLLING_WINDOW_SIZE];
    double rolling_latency[ROLLING_WINDOW_SIZE];
    int rolling_index;
    
    // Rejection reason tracking
    uint64_t reject_insufficient_balance;
    uint64_t reject_risk_limit;
    uint64_t reject_insufficient_stock;
    uint64_t reject_insufficient_holdings;
    uint64_t reject_other;
    
    // Thread safety
    pthread_mutex_t lock;
} server_stats_t;

// Global stats instance
extern server_stats_t g_stats;

/**
 * Initialize statistics system
 * @return 0 on success, -1 on failure
 */
int stats_init(void);

/**
 * Cleanup statistics system
 */
void stats_cleanup(void);

/**
 * Record a new connection
 */
void stats_record_connection(void);

/**
 * Record a connection closed
 */
void stats_record_disconnection(void);

/**
 * Record a completed order
 * @param is_buy true for buy orders, false for sell
 * @param latency_ms Order processing latency in milliseconds
 * @param stock_id Stock that was traded
 * @param user_id User who placed the order
 * @param value Total value of the trade (price * quantity)
 */
void stats_record_order(bool is_buy, double latency_ms, uint16_t stock_id, uint32_t user_id, double value);

/**
 * Record a failed order (still counts for throughput)
 * @param latency_ms Order processing latency in milliseconds
 */
// Rejection reason enum
typedef enum {
    REJECT_INSUFFICIENT_BALANCE,
    REJECT_RISK_LIMIT,
    REJECT_INSUFFICIENT_STOCK,
    REJECT_INSUFFICIENT_HOLDINGS,
    REJECT_OTHER
} reject_reason_t;

void stats_record_failed_order(double latency_ms, reject_reason_t reason);

/**
 * Take a sample for graphing (called every GRAPH_SAMPLE_MS)
 */
void stats_take_sample(void);

/**
 * Get formatted uptime string
 * @param buffer Output buffer
 * @param size Buffer size
 */
void stats_get_uptime(char* buffer, size_t size);

/**
 * Update top stocks ranking
 */
void stats_update_top_stocks(void);

/**
 * Update top traders ranking
 */
void stats_update_top_traders(void);

/**
 * Get current memory usage
 * @return Memory usage in bytes
 */
uint64_t stats_get_memory_usage(void);

#endif // STATS_H
