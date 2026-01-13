# Load Synthesizer for Stock Trading Server

A high-performance stress testing tool for load testing the stock trading server. The synthesizer spawns multiple concurrent client threads that simulate realistic trading behavior with order placement, execution tracking, and detailed performance metrics.

## Features

- **Multi-threaded Architecture**: Spawn up to 100 concurrent simulated clients
- **Realistic Trading Patterns**: Workers buy and sell stocks with intelligent holdings tracking
- **Real-time Progress Display**: Live updates with orders/sec, latency, and success rates
- **Comprehensive Metrics**: Latency percentiles (p50, p95, p99), throughput, and success rate
- **Interactive Controls**: Pause/resume and quit during test execution
- **Rate Limiting**: Optional target orders/sec rate limiting
- **Reproducibility**: Seed option for deterministic test runs

## Building

```bash
cd load_synth
make clean && make
```

## Usage

```bash
./synth [options]
```

### Options

| Option | Short | Description | Default |
|--------|-------|-------------|---------|
| `--host HOST` | `-h` | Server hostname | `127.0.0.1` |
| `--port PORT` | `-p` | Server port | `8888` |
| `--clients N` | `-c` | Number of concurrent clients | `20` |
| `--duration SEC` | `-d` | Test duration in seconds | `60` |
| `--rate N` | `-r` | Target orders/sec (0 = unlimited) | `0` |
| `--verbose` | `-v` | Enable verbose output | off |
| `--seed N` | `-s` | Random seed for reproducibility | time-based |
| `--help` | | Show help message | |

### Interactive Controls

During test execution:
- **SPACE**: Pause/Resume the test
- **Q**: Quit immediately

## Examples

### Basic Test
```bash
./synth
```
Runs with default settings: 20 clients for 60 seconds.

### High Load Test
```bash
./synth --clients 50 --duration 120
```
50 concurrent clients for 2 minutes.

### Remote Server Test
```bash
./synth --host 192.168.1.100 --port 8080 --clients 30 --duration 60
```

### Rate-Limited Test
```bash
./synth --clients 20 --rate 100 --duration 60
```
Limits throughput to approximately 100 orders/second total.

### Reproducible Test
```bash
./synth --seed 12345 --clients 10 --duration 30
```
Same seed produces same order sequence for debugging.

## Test Account Setup

Before running the load synthesizer, you must set up test accounts on the server:

1. Start the server
2. In the server admin console, run:
   ```
   setup_test
   ```
   This creates 100 test accounts (`test1` through `test100`) with:
   - User IDs: 9001-9100
   - Initial balance: $100,000,000 each
   - Stock volumes reset to 1,000,000

The synthesizer workers use credentials matching their worker ID:
- Worker 0 → `test1` / `test1`
- Worker 1 → `test2` / `test2`
- etc.

## Output

### Live Progress

```
[SUSTAINED 01:23] Clients: 20/20 | Orders: 12345 | Rate: 156.2/s | Fail: 0 | Lat: 2.3ms
```

Shows:
- **Phase**: Current test phase (CONNECT, SUSTAINED, RAMP_DOWN, etc.)
- **Time**: Elapsed minutes:seconds
- **Clients**: Connected vs. target clients
- **Orders**: Total orders sent
- **Rate**: Current throughput (orders/second)
- **Fail**: Number of rejected orders
- **Lat**: Average latency

### Final Report

```
================================================================
                     LOAD TEST REPORT
================================================================

Duration:        60.0s
Clients:         20 connected, 0 failed

ORDERS
  Total sent:     9876
  Successful:     9876 (100.0%)
  Rejected:       0 (0.0%)

THROUGHPUT
  Average:        164.6 orders/sec

LATENCY
  Average:        2.34 ms
  Median (p50):   1.89 ms
  p95:            4.56 ms
  p99:            8.23 ms
  Min:            0.45 ms
  Max:            23.12 ms

================================================================
✓ EXCELLENT - Server handled load with 100.0% success rate
```

## How It Works

### Worker Thread Lifecycle

1. **Connect**: Each worker establishes a TCP connection to the server
2. **Login**: Authenticates using test account credentials
3. **Fetch Stocks**: First worker retrieves available stock list
4. **Trading Loop**: 
   - Select random stock and quantity (1-10 shares)
   - Decide buy or sell based on local holdings
   - Execute order and track latency
   - Record success/failure
5. **Disconnect**: Clean logout and socket close

### Holdings Tracking

Workers maintain local holdings to ensure realistic trading:
- Workers start with zero holdings
- Only sell stocks they have bought
- If they have holdings, 50% chance to sell
- Sell quantity is capped at holdings amount

This prevents artificial sell failures due to selling non-existent positions.

### Metrics Collection

- **Thread-safe statistics**: All metrics use mutex protection
- **Latency sampling**: First 1000 latencies stored for percentile calculation
- **Real-time aggregation**: Throughput calculated from total orders / elapsed time

## Interpreting Results

### Success Rate
- **≥99%**: ✓ EXCELLENT - Server handles load well
- **90-99%**: ● GOOD - Some rejections, acceptable under stress
- **<90%**: ✗ DEGRADED - High rejection rate, investigate cause

### Common Rejection Causes

1. **Insufficient Balance**: Account ran out of money
2. **Risk Limits**: Exceeded per-stock or total share limits
3. **Insufficient Stock Volume**: Market ran out of shares
4. **Portfolio Errors**: Server-side portfolio management issues

Use the server's `rejections` admin command to see rejection breakdown.

### Latency Guidelines
- **<5ms avg**: Excellent
- **5-20ms avg**: Good
- **>20ms avg**: May indicate server overload

## Architecture

```
┌─────────────────────────────────────────────────┐
│                  Main Thread                     │
│   - Parse arguments                             │
│   - Display progress                            │
│   - Handle keyboard input                       │
│   - Collect and report statistics               │
└─────────────────────────────────────────────────┘
                      │
         ┌────────────┼────────────┐
         ▼            ▼            ▼
   ┌──────────┐ ┌──────────┐ ┌──────────┐
   │ Worker 0 │ │ Worker 1 │ │ Worker N │
   │          │ │          │ │          │
   │ Connect  │ │ Connect  │ │ Connect  │
   │ Login    │ │ Login    │ │ Login    │
   │ Trade    │ │ Trade    │ │ Trade    │
   │ Logout   │ │ Logout   │ │ Logout   │
   └──────────┘ └──────────┘ └──────────┘
         │            │            │
         ▼            ▼            ▼
   ┌─────────────────────────────────────────────┐
   │              Stock Trading Server            │
   │                  (port 8888)                 │
   └─────────────────────────────────────────────┘
```

## Files

| File | Description |
|------|-------------|
| `synth.c` | Main entry point, CLI parsing, progress display |
| `synth.h` | Types, configuration, and function prototypes |
| `worker.c` | Worker thread implementation |
| `stats.c` | Statistics collection and calculation |
| `packet.c/h` | Network packet serialization |
| `protocol.h` | Protocol constants (shared with server) |

## Troubleshooting

### "Connection refused"
- Ensure the server is running
- Check host and port settings

### "Login failed"
- Run `setup_test` in server admin console
- Ensure test accounts exist (test1-test100)

### High rejection rate
- Check server's risk limits
- Verify test account balances
- Use `rejections` command on server to see breakdown

### Low throughput
- Check network latency
- Increase client count
- Verify server CPU isn't bottlenecked

## License

Part of the Stock Trading Server project.
