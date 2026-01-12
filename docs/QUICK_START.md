# Quick Start Guide

## 3-Step Setup

### Step 1: Start Server (Terminal 1)
```bash
cd /home/admin/laptrinhmang/server_folder
./server
```

### Step 2: Run Tests (Terminal 2)
```bash
bash /tmp/comprehensive_test.sh
```

### Step 3: Interactive Testing (Terminal 3 - Optional)
```bash
cd /home/admin/laptrinhmang/client_app
./client 127.0.0.1 8888
```

## Verification Commands

Check if server is running:
```bash
netstat -tln | grep 8888
```

View live server logs:
```bash
tail -f /tmp/server.log
```

View portfolio data:
```bash
cat server_folder/data/portfolios.txt
```

View account balances:
```bash
cat server_folder/data/accounts.txt
```

View transaction history:
```bash
cat server_folder/data/transactions.txt
```

Stop the server:
```bash
pkill -9 server
```

## Interactive Client Commands

Once connected, available commands:
```
login <username> <password>          # Login
register <username> <password>       # Register new account
buy <id> <qty> <price> <type>       # Buy stock (type: MARKET|LIMIT)
sell <id> <qty> <price> <type>      # Sell stock
stocks                               # View available stocks
my_stocks                            # View your portfolio
balance                              # Check your balance
status                               # Show connection status
help                                 # Show help menu
logout                               # Logout
quit                                 # Exit
```

## Test Credentials

```
Username: admin
Password: password123

Username: trader1
Password: pass456

Username: investor
Password: secure789

Username: daytrader
Password: trading123
```

## Expected Test Results

All 10 tests should pass:
- ✅ TEST 1: Login Authentication
- ✅ TEST 2: Buy Stock Execution
- ✅ TEST 3: Portfolio Persistence (CRITICAL FIX)
- ✅ TEST 4: Balance Updates
- ✅ TEST 5: Sell with Persistent Holdings (CRITICAL FIX)
- ✅ TEST 6: Portfolio After Sell (CRITICAL FIX)
- ✅ TEST 7: Insufficient Holdings Validation
- ✅ TEST 8: Input Validation
- ✅ TEST 9: Multi-Stock Buying
- ✅ TEST 10: Multi-Stock Portfolio

## Troubleshooting

**Port already in use:**
```bash
pkill -9 server
sleep 1
./server
```

**Connection refused:**
```bash
# Make sure server is running in another terminal
netstat -tln | grep 8888
```

**Database errors:**
```bash
# Delete corrupted files
rm server_folder/data/*.txt

# Restart server - fresh files will be created
./server
```
