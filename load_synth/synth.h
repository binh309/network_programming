/**
 * synth.h - Load Synthesizer Types and Configuration
 */

#ifndef SYNTH_H
#define SYNTH_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <time.h>

// Default configuration
#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_PORT 8888
#define DEFAULT_CLIENTS 20
#define DEFAULT_DURATION 60
#define DEFAULT_RATE 0  // 0 = as fast as possible

// Test account configuration (must match server)
#define TEST_ACCOUNT_ID_START 9001
#define TEST_ACCOUNT_COUNT 100

// Maximum values
#define MAX_CLIENTS 100
#define MAX_STOCKS 20

// Phases
typedef enum {
    PHASE_CONNECT,
    PHASE_SUSTAINED,
    PHASE_BURST,
    PHASE_MIXED,
    PHASE_RAMP_DOWN,
    PHASE_DONE
} phase_t;

// Configuration
typedef struct {
    char host[256];
    uint16_t port;
    int num_clients;
    int duration_sec;
    int target_rate;     // orders/sec (0 = unlimited)
    bool verbose;
    uint32_t seed;
} synth_config_t;

// Statistics (thread-safe)
typedef struct {
    pthread_mutex_t lock;
    
    // Connection stats
    int clients_connected;
    int clients_failed;
    int clients_disconnected;
    
    // Order stats
    uint64_t orders_sent;
    uint64_t orders_success;
    uint64_t orders_rejected;
    uint64_t connection_errors;
    
    // Latency tracking (in microseconds)
    uint64_t latency_sum;
    uint64_t latency_count;
    uint64_t latency_min;
    uint64_t latency_max;
    uint64_t latency_samples[1000];  // For percentile calculation
    int sample_count;
    
    // Timing
    struct timespec start_time;
    struct timespec phase_start;
    
    // Current phase
    phase_t current_phase;
    bool paused;
    bool running;
} synth_stats_t;

// Worker state
typedef struct {
    int worker_id;
    int sockfd;
    char username[32];
    char password[32];
    uint32_t user_id;
    bool connected;
    bool logged_in;
    
    // Random state
    unsigned int rand_state;
    
    // Local holdings tracking (to avoid selling what we don't have)
    uint32_t holdings[MAX_STOCKS];  // holdings[stock_idx] = quantity owned
    
    // Reference to config and stats
    synth_config_t* config;
    synth_stats_t* stats;
} worker_t;

// Stock info (loaded from server)
typedef struct {
    uint16_t stock_id;
    char symbol[16];
    double bid;
    double ask;
} stock_info_t;

// Global stock list
extern stock_info_t g_stocks[MAX_STOCKS];
extern int g_num_stocks;

// Function prototypes
void stats_init(synth_stats_t* stats);
void stats_record_connect(synth_stats_t* stats, bool success);
void stats_record_order(synth_stats_t* stats, bool success, uint64_t latency_us);
void stats_record_disconnect(synth_stats_t* stats);
double stats_get_elapsed(synth_stats_t* stats);
double stats_get_throughput(synth_stats_t* stats);
double stats_get_latency_avg(synth_stats_t* stats);
double stats_get_latency_p50(synth_stats_t* stats);
double stats_get_latency_p95(synth_stats_t* stats);
double stats_get_latency_p99(synth_stats_t* stats);

#endif // SYNTH_H
