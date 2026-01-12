/**
 * synth.c - Load Synthesizer Main
 * 
 * A stress testing tool for the stock trading server
 */

#include "synth.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <getopt.h>
#include <pthread.h>
#include <termios.h>
#include <fcntl.h>

// Worker thread function (defined in worker.c)
extern void* worker_thread(void* arg);

// Global stats for signal handler
static synth_stats_t* g_stats = NULL;

// Terminal state for raw input
static struct termios orig_termios;
static bool term_raw = false;

// ANSI colors
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_BOLD    "\033[1m"

static void print_usage(const char* prog) {
    printf("Usage: %s [options]\n\n", prog);
    printf("Options:\n");
    printf("  -h, --host HOST       Server hostname (default: %s)\n", DEFAULT_HOST);
    printf("  -p, --port PORT       Server port (default: %d)\n", DEFAULT_PORT);
    printf("  -c, --clients N       Number of clients (default: %d, max: %d)\n", 
           DEFAULT_CLIENTS, MAX_CLIENTS);
    printf("  -d, --duration SEC    Test duration in seconds (default: %d)\n", DEFAULT_DURATION);
    printf("  -r, --rate N          Target orders/sec (0 = unlimited)\n");
    printf("  -v, --verbose         Verbose output\n");
    printf("  -s, --seed N          Random seed for reproducibility\n");
    printf("      --help            Show this help\n");
    printf("\n");
    printf("During test:\n");
    printf("  SPACE  Pause/Resume\n");
    printf("  Q      Quit\n");
    printf("\n");
    printf("Example:\n");
    printf("  %s --host 192.168.1.100 --port 8080 --clients 50 --duration 120\n", prog);
}

static void restore_terminal(void) {
    if (term_raw) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
        term_raw = false;
    }
}

static void set_raw_terminal(void) {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(restore_terminal);
    
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    term_raw = true;
}

static void signal_handler(int sig) {
    (void)sig;
    if (g_stats) {
        g_stats->running = false;
    }
}

static void print_progress(synth_stats_t* stats, synth_config_t* config) {
    double elapsed = stats_get_elapsed(stats);
    int remaining = config->duration_sec - (int)elapsed;
    if (remaining < 0) remaining = 0;
    
    const char* phase_name;
    switch (stats->current_phase) {
        case PHASE_CONNECT:   phase_name = "CONNECT"; break;
        case PHASE_SUSTAINED: phase_name = "SUSTAINED"; break;
        case PHASE_BURST:     phase_name = "BURST"; break;
        case PHASE_MIXED:     phase_name = "MIXED"; break;
        case PHASE_RAMP_DOWN: phase_name = "RAMP_DOWN"; break;
        default:              phase_name = "RUNNING"; break;
    }
    
    printf("\r%s[%s %02d:%02d]%s Clients: %s%d%s/%d | Orders: %s%lu%s | "
           "Rate: %s%.1f%s/s | Fail: %s%lu%s | Lat: %s%.1fms%s   ",
           COLOR_CYAN, phase_name, (int)elapsed / 60, (int)elapsed % 60, COLOR_RESET,
           COLOR_GREEN, stats->clients_connected, COLOR_RESET, config->num_clients,
           COLOR_BOLD, stats->orders_sent, COLOR_RESET,
           COLOR_YELLOW, stats_get_throughput(stats), COLOR_RESET,
           stats->orders_rejected > 0 ? COLOR_RED : COLOR_RESET, stats->orders_rejected, COLOR_RESET,
           COLOR_BLUE, stats_get_latency_avg(stats), COLOR_RESET);
    
    if (stats->paused) {
        printf("%s[PAUSED]%s", COLOR_RED, COLOR_RESET);
    }
    
    fflush(stdout);
}

static void print_report(synth_stats_t* stats, synth_config_t* config) {
    double elapsed = stats_get_elapsed(stats);
    double success_rate = stats->orders_sent > 0 
        ? 100.0 * stats->orders_success / stats->orders_sent 
        : 0.0;
    
    printf("\n\n");
    printf("%s", COLOR_CYAN);
    printf("================================================================\n");
    printf("                     LOAD TEST REPORT                           \n");
    printf("================================================================%s\n", COLOR_RESET);
    
    printf("\n%sDuration:%s        %.1fs\n", COLOR_BOLD, COLOR_RESET, elapsed);
    printf("%sClients:%s         %d connected, %d failed\n", 
           COLOR_BOLD, COLOR_RESET,
           stats->clients_connected, stats->clients_failed);
    
    printf("\n%sORDERS%s\n", COLOR_BOLD, COLOR_RESET);
    printf("  Total sent:     %lu\n", stats->orders_sent);
    printf("  %sSuccessful:%s     %s%lu (%.1f%%)%s\n", 
           COLOR_BOLD, COLOR_RESET,
           COLOR_GREEN, stats->orders_success, 
           stats->orders_sent > 0 ? 100.0 * stats->orders_success / stats->orders_sent : 0.0,
           COLOR_RESET);
    printf("  %sRejected:%s       %s%lu (%.1f%%)%s\n", 
           COLOR_BOLD, COLOR_RESET,
           stats->orders_rejected > 0 ? COLOR_RED : COLOR_RESET,
           stats->orders_rejected,
           stats->orders_sent > 0 ? 100.0 * stats->orders_rejected / stats->orders_sent : 0.0,
           COLOR_RESET);
    
    if (stats->connection_errors > 0) {
        printf("  %sConn Errors:%s    %s%lu%s\n", 
               COLOR_BOLD, COLOR_RESET,
               COLOR_RED, stats->connection_errors, COLOR_RESET);
    }
    
    printf("\n%sTHROUGHPUT%s\n", COLOR_BOLD, COLOR_RESET);
    printf("  Average:        %.1f orders/sec\n", stats_get_throughput(stats));
    if (config->target_rate > 0) {
        printf("  Target:         %d orders/sec\n", config->target_rate);
    }
    
    printf("\n%sLATENCY%s\n", COLOR_BOLD, COLOR_RESET);
    printf("  Average:        %.2f ms\n", stats_get_latency_avg(stats));
    printf("  Median (p50):   %.2f ms\n", stats_get_latency_p50(stats));
    printf("  p95:            %.2f ms\n", stats_get_latency_p95(stats));
    printf("  p99:            %.2f ms\n", stats_get_latency_p99(stats));
    if (stats->latency_count > 0) {
        printf("  Min:            %.2f ms\n", stats->latency_min / 1000.0);
        printf("  Max:            %.2f ms\n", stats->latency_max / 1000.0);
    }
    
    printf("\n%s", COLOR_CYAN);
    printf("================================================================%s\n", COLOR_RESET);
    
    // Summary line
    if (success_rate >= 99.0) {
        printf("%s✓ EXCELLENT%s - Server handled load with %.1f%% success rate\n",
               COLOR_GREEN, COLOR_RESET, success_rate);
    } else if (success_rate >= 90.0) {
        printf("%s● GOOD%s - Server handled load with %.1f%% success rate\n",
               COLOR_YELLOW, COLOR_RESET, success_rate);
    } else {
        printf("%s✗ DEGRADED%s - High rejection rate (%.1f%% success)\n",
               COLOR_RED, COLOR_RESET, success_rate);
    }
}

int main(int argc, char* argv[]) {
    synth_config_t config = {
        .port = DEFAULT_PORT,
        .num_clients = DEFAULT_CLIENTS,
        .duration_sec = DEFAULT_DURATION,
        .target_rate = DEFAULT_RATE,
        .verbose = false,
        .seed = time(NULL)
    };
    strncpy(config.host, DEFAULT_HOST, sizeof(config.host));
    
    // Parse arguments
    static struct option long_options[] = {
        {"host", required_argument, 0, 'h'},
        {"port", required_argument, 0, 'p'},
        {"clients", required_argument, 0, 'c'},
        {"duration", required_argument, 0, 'd'},
        {"rate", required_argument, 0, 'r'},
        {"verbose", no_argument, 0, 'v'},
        {"seed", required_argument, 0, 's'},
        {"help", no_argument, 0, 'H'},
        {0, 0, 0, 0}
    };
    
    int opt;
    while ((opt = getopt_long(argc, argv, "h:p:c:d:r:vs:", long_options, NULL)) != -1) {
        switch (opt) {
            case 'h':
                strncpy(config.host, optarg, sizeof(config.host) - 1);
                break;
            case 'p':
                config.port = atoi(optarg);
                break;
            case 'c':
                config.num_clients = atoi(optarg);
                if (config.num_clients > MAX_CLIENTS) {
                    config.num_clients = MAX_CLIENTS;
                    fprintf(stderr, "Warning: Capped clients at %d\n", MAX_CLIENTS);
                }
                break;
            case 'd':
                config.duration_sec = atoi(optarg);
                break;
            case 'r':
                config.target_rate = atoi(optarg);
                break;
            case 'v':
                config.verbose = true;
                break;
            case 's':
                config.seed = atoi(optarg);
                break;
            case 'H':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }
    
    // Seed random
    srand(config.seed);
    
    // Print banner
    printf("%s", COLOR_CYAN);
    printf("================================================================\n");
    printf("              STOCK TRADING LOAD SYNTHESIZER                    \n");
    printf("================================================================%s\n", COLOR_RESET);
    printf("Target:      %s:%d\n", config.host, config.port);
    printf("Clients:     %d\n", config.num_clients);
    printf("Duration:    %ds\n", config.duration_sec);
    printf("Rate:        %s\n", config.target_rate > 0 ? 
           (char[32]){0} : "unlimited");
    if (config.target_rate > 0) {
        printf("             %d orders/sec\n", config.target_rate);
    }
    printf("\n");
    printf("Press %sSPACE%s to pause, %sQ%s to quit\n", 
           COLOR_YELLOW, COLOR_RESET, COLOR_YELLOW, COLOR_RESET);
    printf("================================================================\n\n");
    
    // Initialize stats
    synth_stats_t stats;
    stats_init(&stats);
    g_stats = &stats;
    
    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Setup raw terminal for keyboard input
    set_raw_terminal();
    
    // Create workers
    worker_t workers[MAX_CLIENTS];
    pthread_t threads[MAX_CLIENTS];
    
    memset(workers, 0, sizeof(workers));
    
    stats.current_phase = PHASE_CONNECT;
    
    // Launch worker threads
    for (int i = 0; i < config.num_clients; i++) {
        workers[i].worker_id = i;
        workers[i].sockfd = -1;
        workers[i].config = &config;
        workers[i].stats = &stats;
        
        if (pthread_create(&threads[i], NULL, worker_thread, &workers[i]) != 0) {
            fprintf(stderr, "Failed to create worker thread %d\n", i);
        }
        
        // Stagger connections slightly
        usleep(10000);  // 10ms between connection attempts
    }
    
    stats.current_phase = PHASE_SUSTAINED;
    
    // Main loop - progress display and keyboard handling
    double start_time = stats_get_elapsed(&stats);
    
    while (stats.running) {
        // Check for timeout
        double elapsed = stats_get_elapsed(&stats) - start_time;
        if (elapsed >= config.duration_sec) {
            stats.running = false;
            break;
        }
        
        // Check for keyboard input
        char c;
        if (read(STDIN_FILENO, &c, 1) == 1) {
            if (c == ' ') {
                stats.paused = !stats.paused;
                if (stats.paused) {
                    printf("\n%s>>> PAUSED - Press SPACE to resume, Q to quit <<<%s\n",
                           COLOR_YELLOW, COLOR_RESET);
                } else {
                    printf("\n%s>>> RESUMED <<<%s\n", COLOR_GREEN, COLOR_RESET);
                }
            } else if (c == 'q' || c == 'Q') {
                printf("\n%s>>> STOPPING <<<%s\n", COLOR_RED, COLOR_RESET);
                stats.running = false;
                break;
            }
        }
        
        // Print progress
        print_progress(&stats, &config);
        
        usleep(200000);  // Update every 200ms
    }
    
    // Signal all workers to stop
    stats.running = false;
    stats.paused = false;
    
    stats.current_phase = PHASE_RAMP_DOWN;
    printf("\n\nWaiting for workers to disconnect...\n");
    
    // Wait for all workers
    for (int i = 0; i < config.num_clients; i++) {
        pthread_join(threads[i], NULL);
    }
    
    stats.current_phase = PHASE_DONE;
    
    // Restore terminal
    restore_terminal();
    
    // Print final report
    print_report(&stats, &config);
    
    return 0;
}
