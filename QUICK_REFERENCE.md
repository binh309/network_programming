# Quick Reference - Trading Commands

## Getting Started

### 1. Login
```
>>> login test_user test_user
[CLIENT] Sent login request
╔════════════ LOGIN SUCCESS ════════════╗
║ Login successful! Balance: $100,000.00
╚═══════════════════════════════════════╝
```

### 2. Check Your Balance
```
>>> balance
╔════════════ YOUR BALANCE ════════════╗
║ Balance: $100,000.00
╚═══════════════════════════════════════╝
```

### 3. View Available Stocks
```
>>> stocks
========== AVAILABLE STOCKS ==========
Stock Count: 3

  [1] AAPL - Price: $150.25 | Available: 1000 shares
  [2] GOOGL - Price: $2800.50 | Available: 500 shares
  [3] MSFT - Price: $330.75 | Available: 800 shares
=====================================
```

### 4. Buy Stock
```
>>> buy 1 100 150.25
[CLIENT] Sent buy request
✓ SUCCESS: Successfully bought 100 AAPL @ $150.25. Order ID: 1001
```

**Format:** `buy <stock_id> <quantity> <price_per_share>`
- stock_id: 1=AAPL, 2=GOOGL, 3=MSFT
- quantity: Number of shares
- price_per_share: Price per share

**Errors:**
- Not authenticated
- Insufficient balance
- Stock not found
- Insufficient stock available

### 5. Sell Stock
```
>>> sell 1 50 155.00
[CLIENT] Sent sell request
✓ SUCCESS: Successfully sold 50 AAPL @ $155.00. Order ID: 1002
```

**Format:** `sell <stock_id> <quantity> <price_per_share>`

**Errors:**
- Not authenticated
- Don't own the stock
- Insufficient holdings

### 6. Check Status
```
>>> status
========== CLIENT STATUS ==========
Connected: YES
Authenticated: YES
Username: test_user
===================================
```

---

## Example Trading Session

```
>>> login alice alice
[CLIENT] Sent login request
╔════════════ LOGIN SUCCESS ════════════╗
║ Login successful! Balance: $100,000.00
╚═══════════════════════════════════════╝
[INFO] You are now logged in. Type 'stocks' to view available stocks.

>>> stocks
========== AVAILABLE STOCKS ==========
Stock Count: 3

  [1] AAPL - Price: $150.00 | Available: 1000 shares
  [2] GOOGL - Price: $2800.00 | Available: 500 shares
  [3] MSFT - Price: $330.00 | Available: 800 shares
=====================================

>>> balance
╔════════════ YOUR BALANCE ════════════╗
║ Balance: $100,000.00
╚═══════════════════════════════════════╝

>>> buy 1 100 150.00
[CLIENT] Sent buy request
✓ SUCCESS: Successfully bought 100 AAPL @ $150.00. Order ID: 1001

>>> buy 2 5 2800.00
[CLIENT] Sent buy request
✓ SUCCESS: Successfully bought 5 GOOGL @ $2800.00. Order ID: 1002

>>> balance
╔════════════ YOUR BALANCE ════════════╗
║ Balance: $70,000.00
╚═══════════════════════════════════════╝

>>> sell 1 50 155.00
[CLIENT] Sent sell request
✓ SUCCESS: Successfully sold 50 AAPL @ $155.00. Order ID: 1003

>>> balance
╔════════════ YOUR BALANCE ════════════╗
║ Balance: $77,750.00
╚═══════════════════════════════════════╝
```

---

## Error Scenarios

### Insufficient Balance
```
>>> buy 2 100 2800.00
[CLIENT] Sent buy request
✗ ERROR: Insufficient balance. Need $280,000.00, have $100,000.00
```

### Stock Not Found
```
>>> buy 99 10 100.00
[CLIENT] Sent buy request
✗ ERROR: Stock not found
```

### Don't Own Stock
```
>>> sell 3 10 330.00
[CLIENT] Sent sell request
✗ ERROR: You don't own this stock
```

### Insufficient Holdings
```
>>> buy 1 100 150.00
[Successfully bought...]
>>> sell 1 200 155.00
[CLIENT] Sent sell request
✗ ERROR: Insufficient holdings. Need 200, have 100
```

### Not Authenticated
```
>>> balance
[ERROR] Must login first!

>>> buy 1 10 150.00
[ERROR] Must login first!
```

---

## Help Command
```
>>> help
========== TRADING CLIENT MENU ==========
  login <user> <pass>         - Login
  stocks                      - View stocks
  buy <id> <qty> <price>      - Buy stock
  sell <id> <qty> <price>     - Sell stock
  balance                     - Check balance
  status                      - Show status
  help                        - Show menu
  quit                        - Exit
========================================
```

---

## Tips

1. **Always check balance first** - Know how much you can spend
2. **Check available stocks** - See current prices and quantities
3. **Keep track of order IDs** - Can be used for transaction history
4. **Use realistic prices** - Check actual stock prices before trading
5. **Verify holdings** - Cannot sell what you don't own
6. **Test error handling** - Try invalid commands to see helpful messages

---

## Transaction Recording

All buy and sell operations are recorded with:
- Unique Order ID
- User ID
- Transaction type (BUY/SELL)
- Stock ID
- Quantity
- Price per share
- Total amount
- Timestamp

This data can be used for:
- Transaction history
- Trade confirmation
- Tax reporting
- Audit trails
- Performance analysis
