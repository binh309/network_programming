# Buy Stock Feature Documentation

## Overview

The Buy Stock feature allows authenticated users to purchase shares of available stocks. The feature includes comprehensive validation, balance verification, and portfolio management.

---

## Feature Summary

| Property | Value |
|----------|-------|
| **Message Type** | CMSG_BUY_STOCK (0x11) → SMSG_BUY_STOCK_SUCCESS (0x92) or SMSG_BUY_STOCK_FAIL (0x93) |
| **Requires Auth** | Yes |
| **Modifies State** | Yes (balance, holdings, stock quantity, transaction record) |
| **Atomic** | Yes (all-or-nothing transaction) |

---

## Request/Response Specification

### Client Request (CMSG_BUY_STOCK)

```c
typedef struct {
    uint16_t stock_id;          // Stock identifier (1=AAPL, 2=GOOGL, etc.)
    uint32_t quantity;          // Number of shares to buy
    double price;               // Price per share offered by client
} buy_stock_request_t;
```

**Total Size**: 10 bytes (2 + 4 + 8)

### Server Response - Success (SMSG_BUY_STOCK_SUCCESS)

```c
typedef struct {
    uint8_t status;             // 0 = success
    uint32_t order_id;          // Unique transaction ID (1001, 1002, ...)
    double total_amount;        // Total cost (quantity × price)
    double new_balance;         // Updated account balance
} buy_stock_response_t;
```

**Total Size**: 21 bytes (1 + 4 + 8 + 8)

### Server Response - Failure (SMSG_BUY_STOCK_FAIL)

```c
typedef struct {
    uint8_t status;             // 1 = failure
    char error_message[256];    // Detailed error reason
} buy_stock_fail_response_t;
```

---

## Execution Flow

### Step-by-Step Processing

```
┌─────────────────────────────────┐
│ 1. Receive Request              │
│    Parse header & body          │
└──────────────┬──────────────────┘
               │
               ▼
┌─────────────────────────────────┐
│ 2. Authentication Check         │
│    Is user logged in?           │
│    (Check session_mgr)          │
└──────────────┬──────────────────┘
               │
       ┌───────┴───────┐
       │ NO            │ YES
       ▼               ▼
   [ERROR]        ┌────────────────────┐
   "Not           │ 3. Stock Validation│
   authenticated" │    Exists?         │
                  │    Valid ID?       │
                  └────────┬───────────┘
                           │
                   ┌───────┴────────┐
                   │ INVALID        │ VALID
                   ▼               ▼
               [ERROR]         ┌──────────────────┐
               "Stock          │ 4. Balance Check │
               not found"      │ Has enough $?    │
                               └────────┬─────────┘
                                       │
                               ┌───────┴────────┐
                               │ NO             │ YES
                               ▼               ▼
                           [ERROR]         ┌──────────────────┐
                           "Insufficient   │ 5. Availability  │
                           balance"        │ Enough shares?   │
                                           └────────┬─────────┘
                                                   │
                                           ┌───────┴────────┐
                                           │ NO             │ YES
                                           ▼               ▼
                                       [ERROR]         ┌──────────────────┐
                                       "Stock          │ 6. Update Balance│
                                       unavailable"    │ Deduct from acc. │
                                                       └────────┬─────────┘
                                                               │
                                                               ▼
                                                       ┌──────────────────┐
                                                       │ 7. Update Holdings
                                                       │ Add to portfolio  │
                                                       └────────┬─────────┘
                                                               │
                                                               ▼
                                                       ┌──────────────────┐
                                                       │ 8. Update Stocks │
                                                       │ Decrement qty    │
                                                       └────────┬─────────┘
                                                               │
                                                               ▼
                                                       ┌──────────────────┐
                                                       │ 9. Record Trans. │
                                                       │ Log transaction  │
                                                       └────────┬─────────┘
                                                               │
                                                               ▼
                                                       ┌──────────────────┐
                                                       │ 10. Send Success │
                                                       │ Return order_id  │
                                                       └──────────────────┘
```

### Code Logic Flow

```c
// 1. Get session (implicitly done by dispatcher)
session = get_session_from_fd(client_fd);

// 2. Validate authentication
if (!session || !session->is_logged_in) {
    return ERROR_NOT_AUTHENTICATED;
}

// 3. Validate stock exists
stock = stock_db_get_by_id(request->stock_id);
if (!stock) {
    return ERROR_STOCK_NOT_FOUND;
}

// 4. Calculate required amount
double required = request->quantity * request->price;

// 5. Check balance
current_balance = account_db_get_balance(session->user_id);
if (current_balance < required) {
    return ERROR_INSUFFICIENT_BALANCE;
}

// 6. Check stock availability
if (stock->quantity < request->quantity) {
    return ERROR_STOCK_UNAVAILABLE;
}

// 7. Update balance (atomic operation protected by mutex)
new_balance = current_balance - required;
account_db_update_balance(session->user_id, new_balance);

// 8. Add holding to portfolio
portfolio_db_add_holding(
    session->user_id, 
    request->stock_id, 
    request->quantity, 
    request->price
);

// 9. Decrement stock quantity
stock_db_update_quantity(request->stock_id, 
    stock->quantity - request->quantity
);

// 10. Record transaction
order_id = transaction_db_record(
    session->user_id,
    TRANS_BUY,
    request->stock_id,
    request->quantity,
    request->price
);

// 11. Prepare response
response->status = SUCCESS;
response->order_id = order_id;
response->total_amount = required;
response->new_balance = new_balance;

// 12. Send response
send_response(client_fd, response);
```

---

## Data Structure Flow

### Initial State
```
Account:
├─ user_id: 1
├─ balance: $10,000.00
├─ username: "admin"

Stock (AAPL):
├─ id: 1
├─ symbol: "AAPL"
├─ quantity: 1000
└─ price: $150.50

Portfolio (user 1):
└─ (empty)
```

### After Buying 5 AAPL @ $150.50
```
Account:
├─ user_id: 1
├─ balance: $9,247.50  ← Deducted by $752.50
├─ username: "admin"

Stock (AAPL):
├─ id: 1
├─ symbol: "AAPL"
├─ quantity: 995        ← Decreased from 1000
└─ price: $150.50

Portfolio (user 1):
└─ Holdings:
   └─ AAPL:
      ├─ quantity: 5
      ├─ avg_price: $150.50
      └─ total_cost: $752.50

Transaction Record (Order #1001):
├─ order_id: 1001
├─ user_id: 1
├─ type: BUY
├─ stock_id: 1
├─ quantity: 5
├─ price: $150.50
└─ timestamp: 2026-01-11 14:23:45
```

---

## Error Scenarios & Handling

### Error 1: User Not Authenticated

```
Scenario: User tries to buy without logging in first

Client sends: CMSG_BUY_STOCK (not logged in)
         │
         ▼
Server: Check session → is_logged_in = false
         │
         ▼
Response: SMSG_BUY_STOCK_FAIL
         status = 1
         error = "User not authenticated. Please login first."
```

**Fix**: Client must login before trading

### Error 2: Stock Not Found

```
Scenario: User tries to buy stock_id=99 (doesn't exist)

Server: stock_db_get_by_id(99)
         │
         ▼
Returns: NULL
         │
         ▼
Response: SMSG_BUY_STOCK_FAIL
         status = 1
         error = "Stock not found."
```

**Fix**: Client should call CMSG_VIEW_STOCKS to get valid stock IDs

### Error 3: Insufficient Balance

```
Scenario: User balance = $500, tries to buy 5 shares @ $150.50
         Required = 5 × $150.50 = $752.50

Server: Check balance → 500 < 752.50? YES
         │
         ▼
Response: SMSG_BUY_STOCK_FAIL
         status = 1
         error = "Insufficient balance for this transaction."
         (Recommended: need $752.50, have $500.00)
```

**Fix**: User must deposit more funds or buy fewer shares

### Error 4: Insufficient Stock Availability

```
Scenario: Stock AAPL has 3 shares available
         User tries to buy 5 shares

Server: Check qty → available 3 < requested 5? YES
         │
         ▼
Response: SMSG_BUY_STOCK_FAIL
         status = 1
         error = "Not enough shares available. (3 available)"
```

**Fix**: Reduce order quantity or wait for more stock

---

## Database Modifications

### Affected Tables/Files

#### 1. accounts.txt (Account Balance)
```
Before:  1|admin|password|10000.00
After:   1|admin|password|9247.50
         ↑                ↑
         (unchanged)      (decreased by $752.50)
```

#### 2. stocks.txt (Stock Inventory)
```
Before:  1|AAPL|Apple Inc|1000|150.50
After:   1|AAPL|Apple Inc|995|150.50
         ↑              ↑
         (unchanged)    (decreased by 5)
```

#### 3. In-Memory Portfolio (portfolio_db)
```
Before:  portfolio[1] = {holdings: empty}
After:   portfolio[1] = {
           holdings: [
             {stock_id: 1, qty: 5, avg_price: 150.50}
           ]
         }
```

#### 4. Transaction Log
```
New entry:
  order_id: 1001
  user_id: 1
  type: BUY
  stock_id: 1
  quantity: 5
  price: 150.50
  timestamp: 2026-01-11 14:23:45
```

---

## Concurrency & Thread Safety

### Mutex Protection

```
Operation                          Mutex Used
─────────────────────────────────────────────
Read balance                       account_mutex
Write balance                      account_mutex
Read stock quantity                stock_mutex
Write stock quantity               stock_mutex
Add holding to portfolio           portfolio_mutex
Generate order_id                  transaction_mutex
```

### Transaction Atomicity

All operations execute in atomic block:

```c
pthread_mutex_lock(&balance_mutex);
pthread_mutex_lock(&portfolio_mutex);
pthread_mutex_lock(&stock_mutex);
pthread_mutex_lock(&transaction_mutex);

// Critical section: All operations atomic
balance -= amount;
portfolio_add_holding(...);
stock_qty -= quantity;
order_id = transaction_record(...);

pthread_mutex_unlock(&transaction_mutex);
pthread_mutex_unlock(&stock_mutex);
pthread_mutex_unlock(&portfolio_mutex);
pthread_mutex_unlock(&balance_mutex);
```

**Reason**: Prevents race conditions where:
- Two clients buy simultaneously
- Balance is checked by both, passes
- Both deduct (overdraft!)

---

## Testing Scenarios

### Test 1: Successful Purchase

```
Setup:
  - User authenticated (user_id = 1)
  - Balance: $10,000
  - AAPL available: 1000 shares @ $150.50

Action:
  Client sends: buy AAPL 5 150.50
  
Expected Result:
  ✓ Order ID: 1001
  ✓ New balance: $9,247.50
  ✓ Stock qty: 995
  ✓ Portfolio: 5 AAPL @ $150.50 avg
  ✓ Response message: "Order #1001 executed successfully"
```

### Test 2: Insufficient Balance

```
Setup:
  - User balance: $500
  - Required: 5 × $150.50 = $752.50

Action:
  Client sends: buy AAPL 5 150.50
  
Expected Result:
  ✗ Error: "Insufficient balance for this transaction."
  ✗ Balance unchanged: $500
  ✗ Portfolio unchanged: (empty)
```

### Test 3: Stock Not Available

```
Setup:
  - AAPL available: 3 shares
  - User requests: 5 shares

Action:
  Client sends: buy AAPL 5 150.50
  
Expected Result:
  ✗ Error: "Not enough shares available. (3 available)"
  ✗ No order created
  ✗ Balance unchanged
```

### Test 4: Concurrent Purchases

```
Setup:
  - AAPL available: 5 shares
  - User A balance: $2000 (can buy 5)
  - User B balance: $2000 (can buy 5)
  - Both send buy order simultaneously

Expected Result:
  ✓ One user succeeds (gets 5 shares)
  ✓ One user fails (stock unavailable)
  ✓ No race condition/overdraft
  ✓ Both mutexes protect critical sections
```

---

## Client Command Interface

### Usage

```
trading> buy AAPL 5 150.50
```

### Parsing

```c
// Input: "buy AAPL 5 150.50"
// Parse:
cmd = "buy"
args = "AAPL 5 150.50"

// Extract from args:
sscanf(args, "%s %u %lf", symbol, &quantity, &price);
// symbol = "AAPL"
// quantity = 5
// price = 150.50
```

### Client Implementation

```c
void cmd_buy_stock(const char* args) {
    // 1. Parse arguments
    char symbol[32];
    uint32_t quantity;
    double price;
    sscanf(args, "%31s %u %lf", symbol, &quantity, &price);
    
    // 2. Lookup stock_id from symbol
    stock_id = get_stock_id_by_symbol(symbol);
    
    // 3. Create request packet
    buy_stock_request_t req = {
        .stock_id = stock_id,
        .quantity = quantity,
        .price = price
    };
    
    // 4. Send request
    char buffer[sizeof(header) + sizeof(req)];
    // ... combine and send ...
    
    // 5. Receive response
    buy_stock_response_t resp;
    // ... receive and parse ...
    
    // 6. Display result
    if (resp.status == 0) {
        printf("✓ Order #%u executed\n", resp.order_id);
        printf("  Total: $%.2f\n", resp.total_amount);
        printf("  New balance: $%.2f\n", resp.new_balance);
    } else {
        printf("✗ Error: %s\n", error_msg);
    }
}
```

---

## Performance Metrics

### Expected Performance

| Operation | Time | Notes |
|-----------|------|-------|
| Database lookup | < 1ms | Linear search on small dataset |
| Balance update | < 0.5ms | Direct file I/O |
| Portfolio update | < 1ms | Array insertion |
| Transaction record | < 1ms | Append to transaction log |
| **Total request** | < 5ms | Typical end-to-end |

### Bottlenecks

1. **File I/O** (accounts.txt, stocks.txt): Could use database
2. **Linear search**: Stock DB uses array, could use hash table
3. **Mutex contention**: Multiple simultaneous purchases lock resources

---

## Integration Points

### Depends On

- **Session Manager**: Validates user authentication
- **Account DB**: Reads/updates balance
- **Stock DB**: Validates stock exists, updates quantity
- **Portfolio DB**: Records user holdings
- **Transaction DB**: Creates audit trail
- **Packet Layer**: Sends response

### Called By

- **Dispatcher**: Routes CMSG_BUY_STOCK messages
- **Server main loop**: Receives client request

### Data Flow

```
Client Request
    ↓
Dispatcher (route message type)
    ↓
handle_buy_stock_request()
    ├─→ session_mgr_get()
    ├─→ stock_db_get_by_id()
    ├─→ account_db_get_balance()
    ├─→ account_db_update_balance()
    ├─→ portfolio_db_add_holding()
    ├─→ stock_db_update_quantity()
    ├─→ transaction_db_record()
    └─→ send_packet()
    ↓
Server Response
    ↓
Client Display
```

---

## Future Enhancements

1. **Price Matching**: Validate market price matches client price
2. **Partial Orders**: Support fractional shares
3. **Order Queue**: Implement order book with market matching
4. **Dividend Support**: Record dividend payments
5. **Short Selling**: Allow selling shares not owned (margin)
6. **Stop Loss**: Automatic sell at specified price
