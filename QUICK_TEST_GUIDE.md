# Quick Test Guide for Implemented Fixes

## Setup

```bash
# Compile both server and client
cd /home/admin/laptrinhmang/server_folder
make clean && make server

cd /home/admin/laptrinhmang/client_app
make clean && make client
```

---

## Test 1: Market Thread Graceful Shutdown ✅

**What it tests:** Server cleanly shuts down and doesn't hang on market thread.

**Steps:**
1. Start server in terminal 1:
   ```bash
   cd /home/admin/laptrinhmang/server_folder
   ./server
   ```
   Look for: `[MARKET] Market simulation thread started.`

2. In terminal 2, let it run for ~1 minute, then press **Ctrl+C**
   
3. Look for these messages in server output:
   ```
   [SERVER] Initiating shutdown...
   [MARKET] Shutdown signal sent to market thread
   [EVENT_LOOP] Shutting down event loop
   [MARKET] Market simulation thread shutting down
   [SERVER] Shutdown complete
   ```

**Expected behavior:** Server exits cleanly within 5-10 seconds (not hanging)

---

## Test 2: Connection Idle Timeout ✅

**What it tests:** Idle connections are closed after 5 minutes.

**Steps:**
1. Start server in terminal 1:
   ```bash
   cd /home/admin/laptrinhmang/server_folder
   ./server
   ```

2. Start client in terminal 2:
   ```bash
   cd /home/admin/laptrinhmang/client_app
   ./client
   ```

3. Don't send any commands - let the connection sit idle for 5+ minutes

4. Watch server output - you should see:
   ```
   [EVENT_LOOP] ⏱ Closing idle connection on fd X (no activity for 300 seconds)
   ```

**Expected behavior:** Connection closes after 5 minutes of inactivity

---

## Test 3: Logout Portfolio Cleanup ✅

**What it tests:** User portfolio is cleared from memory on logout.

**Steps:**
1. Start server in terminal 1
2. Start client in terminal 2
3. Run these commands:
   ```
   login admin password123
   logout
   ```

4. Look for message in server output:
   ```
   [PORTFOLIO_MGR] Clearing portfolio for user 1 from memory
   ```

**Expected behavior:** Portfolio is cleared from memory (but still on disk)

---

## Test 4: Portfolio Persistence (Buy/Sell) ✅

**What it tests:** Buy operations save to disk, and data persists across restarts.

**Steps:**

1. Start server in terminal 1
2. Start client in terminal 2

3. Test buy operation:
   ```
   login admin password123
   view                    # View available stocks
   buy 1 5 150 MARKET      # Buy 5 shares of AAPL at market price
   mystock                 # Should show 5 AAPL shares
   logout
   quit
   ```

4. Restart server (Ctrl+C, then `./server` again)

5. Start new client and verify persistence:
   ```
   login admin password123
   mystock                 # Should STILL show 5 AAPL shares!
   ```

**Expected behavior:** Portfolio persists across server restart

---

## Test 5: Complete Trading Flow ✅

**What it tests:** All fixes work together in a realistic scenario.

**Steps:**

1. Terminal 1 - Start server:
   ```bash
   ./server
   ```

2. Terminal 2 - Start client:
   ```bash
   ./client
   ```

3. Execute trading sequence:
   ```
   register newtrader mypassword     # Create new account
   login newtrader mypassword
   balance                           # Check starting balance
   view                              # See available stocks
   buy 2 10 100 MARKET              # Buy 10 MSFT @ market
   buy 3 5 2850 MARKET              # Buy 5 GOOGL @ market
   mystock                          # See portfolio
   balance                          # See new balance
   sell 2 3 395 MARKET              # Sell 3 MSFT @ market
   mystock                          # Updated portfolio
   logout
   quit
   ```

**Expected messages:**
- Buy success messages
- Sell success messages (only works if portfolio persists!)
- Portfolio shows updated holdings
- No errors or crashes

---

## Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| "Connection reset" after 5 min | Idle timeout working correctly | Expected behavior |
| Server hangs on Ctrl+C | Market thread didn't stop | Recompile with `make clean && make server` |
| Portfolio empty after restart | Persistence not working | Check `data/portfolios.txt` file exists |
| "User not found" on login | Account DB issue | Check `data/accounts.txt` format |
| Client won't connect | Server not running | Start server first: `./server` |

---

## Verification Checklist

After running all tests, verify:

- [ ] Server starts successfully
- [ ] Client connects and can login
- [ ] Buy operations work and save to disk
- [ ] Portfolio persists across server restart
- [ ] Logout clears portfolio from memory
- [ ] Idle connections are closed after 5 minutes
- [ ] Server shuts down cleanly with Ctrl+C
- [ ] No compilation errors or warnings
- [ ] No segmentation faults or crashes

---

## Log Monitoring

To see detailed logs during testing:

**Terminal 1 (Server):**
```bash
./server 2>&1 | tee server.log
```
This saves logs to both stdout and `server.log` for later review.

**Monitor key events:**
```bash
# Watch for idle timeout
grep "Closing idle" server.log

# Watch for portfolio operations
grep "PORTFOLIO_MGR" server.log

# Watch for market updates
grep "MARKET" server.log

# Watch for errors
grep "ERROR\|error\|failed\|Failed" server.log
```

---

## Notes

- Default idle timeout: 5 minutes (300 seconds)
- Can be changed in [server_folder/core/event_loop.c](server_folder/core/event_loop.c) line 160
- Portfolio persists to `data/portfolios.txt`
- Account data in `data/accounts.txt`
- Stock data in `data/stocks.txt`

