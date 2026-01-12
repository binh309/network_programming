/**
 * stats.c - Statistics collection for load synthesizer
 */

#include "synth.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void stats_init(synth_stats_t* stats) {
    memset(stats, 0, sizeof(synth_stats_t));
    pthread_mutex_init(&stats->lock, NULL);
    stats->latency_min = UINT64_MAX;
    stats->running = true;
    clock_gettime(CLOCK_MONOTONIC, &stats->start_time);
    stats->phase_start = stats->start_time;
}

void stats_record_connect(synth_stats_t* stats, bool success) {
    pthread_mutex_lock(&stats->lock);
    if (success) {
        stats->clients_connected++;
    } else {
        stats->clients_failed++;
    }
    pthread_mutex_unlock(&stats->lock);
}

void stats_record_order(synth_stats_t* stats, bool success, uint64_t latency_us) {
    pthread_mutex_lock(&stats->lock);
    stats->orders_sent++;
    
    if (success) {
        stats->orders_success++;
    } else {
        stats->orders_rejected++;
    }
    
    // Latency tracking
    stats->latency_sum += latency_us;
    stats->latency_count++;
    
    if (latency_us < stats->latency_min) stats->latency_min = latency_us;
    if (latency_us > stats->latency_max) stats->latency_max = latency_us;
    
    // Store sample for percentile calculation (reservoir sampling)
    if (stats->sample_count < 1000) {
        stats->latency_samples[stats->sample_count++] = latency_us;
    } else {
        // Random replacement
        int idx = rand() % 1000;
        stats->latency_samples[idx] = latency_us;
    }
    
    pthread_mutex_unlock(&stats->lock);
}

void stats_record_disconnect(synth_stats_t* stats) {
    pthread_mutex_lock(&stats->lock);
    stats->clients_disconnected++;
    pthread_mutex_unlock(&stats->lock);
}

double stats_get_elapsed(synth_stats_t* stats) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec - stats->start_time.tv_sec) + 
           (now.tv_nsec - stats->start_time.tv_nsec) / 1e9;
}

double stats_get_throughput(synth_stats_t* stats) {
    double elapsed = stats_get_elapsed(stats);
    if (elapsed < 0.001) return 0.0;
    return stats->orders_sent / elapsed;
}

double stats_get_latency_avg(synth_stats_t* stats) {
    if (stats->latency_count == 0) return 0.0;
    return (double)stats->latency_sum / stats->latency_count / 1000.0;  // Convert to ms
}

// Comparison function for qsort
static int compare_uint64(const void* a, const void* b) {
    uint64_t va = *(const uint64_t*)a;
    uint64_t vb = *(const uint64_t*)b;
    if (va < vb) return -1;
    if (va > vb) return 1;
    return 0;
}

static double get_percentile(synth_stats_t* stats, double percentile) {
    if (stats->sample_count == 0) return 0.0;
    
    // Copy and sort samples
    uint64_t sorted[1000];
    int count = stats->sample_count;
    memcpy(sorted, stats->latency_samples, count * sizeof(uint64_t));
    qsort(sorted, count, sizeof(uint64_t), compare_uint64);
    
    int idx = (int)(percentile / 100.0 * count);
    if (idx >= count) idx = count - 1;
    
    return sorted[idx] / 1000.0;  // Convert to ms
}

double stats_get_latency_p50(synth_stats_t* stats) {
    return get_percentile(stats, 50);
}

double stats_get_latency_p95(synth_stats_t* stats) {
    return get_percentile(stats, 95);
}

double stats_get_latency_p99(synth_stats_t* stats) {
    return get_percentile(stats, 99);
}
