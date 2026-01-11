# Sell Stock Feature Documentation

## Overview

The Sell Stock feature allows authenticated users to liquidate shares from their portfolio. It includes validation of holdings, market compliance, and balance updates with full transaction auditing.

---

## Feature Summary

| Property | Value |
|----------|-------|
| **Message Type** | CMSG_SELL_STOCK (0x12) → SMSG_SELL_STOCK_SUCCESS (0x94) or SMSG_SELL_STOCK_FAIL (0x95) |
| **Requires Auth** | Yes |
| **Modifies State** | Yes (balance, holdings, stock quantity, transaction record) |
| **Atomic** | Yes (all-or-nothing transaction) |
| **Critical Issue** | Portfolio persistence - requires active session |

---

## Request/Response Specification

### Client Request (CMSG_SELL_STOCK)

```c
typedef struct {
    uint16_t stock_id;          // Stock identifier to sell
    uint32_t quantity;          // Number of shares to sell
    double price;               // Price per share offered by market
} sell_stock_request_t;
```

**Total Size**: 10 bytes (2 + 4 + 8)

### Server Response - Success (SMSG_SELL_STOCK_SUCCESS)

```c
typedef struct {
    uint8_t status;             // 0 = success
    uint32_t order_id;          // Unique transaction ID (1002, 1003, ...)
    double total_amount;        // Total proceeds (quantity × price)
    double gain_loss;           // Profit or loss (sell_total - purchase_total)
    double new_balance;         // Updated account balance
} sell_stock_response_t;
```

**Total Size**: 25 bytes (1 + 4 + 8 + 8 + 8)

### Server Response - Failure (SMSG_SELL_STOCK_FAIL)

```c
typedef struct {
    uint8_t status;             // 1 = failure
    char error_message[256];    // Detailed error reason
} sell_stock_fail_response_t;
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
               "Stock          │ 4. Check Holdings│
               not found"      │ Do we own any?   │
                               └────────┬─────────┘
                                       │
                               ┌───────┴────────┐
                               │ NO/NOT OWNED   │ YES/OWNED
                               ▼               ▼
                           [ERROR]         ┌──────────────────┐
                           "You don't      │ 5. Quantity Check│
                           own this        │ Enough shares?   │
                           stock"          └────────┬─────────┘
                                                   │
                                           ┌───────┴────────┐
                                           │ NO             │ YES
                                           ▼               ▼
                                       [ERROR]         ┌──────────────────┐
                                       "Insufficient   │ 6. Update Balance│
                                       holdings"       │ Add proceeds     │
                                                       └────────┬─────────┘
                                                               │
                                                               ▼
                                                       ┌──────────────────┐
                                                       │ 7. Remove Holding
                                                       │ From portfolio   │
                                                       └────────┬─────────┘
                                                               │
                                                               ▼
                                                       ┌──────────────────┐
                                                       │ 8. Return Shares │
                                                       │ To stock pool    │
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
                                                       │ & gain/loss      │
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

// 4. Get user's portfolio (CRITICAL: Must be persistent!)
portfolio = portfolio_db_get(session->user_id);
if (!portfolio) {
    return ERROR_PORTFOLIO_NOT_FOUND;
}

// 5. Check if user owns the stock
holding = portfolio_find_holding(portfolio, request->stock_id);
if (!holding) {
    return ERROR_STOCK_NOT_OWNED;
}

// 6. Check if user has enough shares
if (holding->quantity < request->quantity) {
    return ERROR_INSUFFICIENT_HOLDINGS;
}

// 7. Calculate proceeds and gain/loss
double proceeds = request->quantity * request->price;
double purchase_total = holding->quantity * holding->average_purchase_price;
double gain_loss = proceeds - purchase_total;

// 8. Update balance (add proceeds)
current_balance = account_db_get_balance(session->user_id);
new_balance = current_balance + proceeds;
account_db_update_balance(session->user_id, new_balance);

// 9. Remove holding from portfolio (or reduce if partial sell)
if (holding->quantity == request->quantity) {
    portfolio_db_remove_holding(session->user_id, request->stock_id, request->quantity);
} else {
    // Partial sell: reduce quantity
    portfolio_db_remove_holding(session->user_id, request->stock_id, request->quantity);
}

// 10. Return shares to stock pool
stock_db_update_quantity(request->stock_id, 
    stock->quantity + request->quantity
);

// 11. Record transaction
order_id = transaction_db_record(
    session->user_id,
    TRANS_SELL,
    request->stock_id,
    request->quantity,
    request->price
);

// 12. Prepare response
response->status = SUCCESS;
response->order_id = order_id;
response->total_amount = proceeds;
response->gain_loss = gain_loss;
response->new_balance = new_balance;

// 13. Send response
send_response(client_fd, response);
```

---

## Data Structure Flow

### State Before Sell

```
Account (user 1):
├─ balance: $9,247.50
└─ username: "admin"

Stock (AAPL):
├─ id: 1
├─ quantity: 995

Portfolio (user 1):
├─ Holding: AAPL
│  ├─ quantity: 5
│  ├─ avg_price: $150.50
│  └─ total_cost: $752.50

Transaction History:
└─ Order #1001: Buy 5 AAPL @ $150.50
```

### Sell 3 AAPL @ $160.00

```
Calculation:
  Proceeds = 3 × $160.00 = $480.00
  Purchase cost = 3 × $150.50 = $451.50
  Gain = $480.00 - $451.50 = $28.50
  New balance = $9,247.50 + $480.00 = $9,727.50
```

### State After Sell

```
Account (user 1):
├─ balance: $9,727.50  ← Increased by $480.00
└─ username: "admin"

Stock (AAPL):
├─ id: 1
├─ quantity: 998        ← Increased from 995 (returned 3 shares)

Portfolio (user 1):
├─ Holding: AAPL
│  ├─ quantity: 2       ← Decreased from 5
│  ├─ avg_price: $150.50 (unchanged)
│  └─ remaining_cost: $301.00 (2 × $150.50)

Transaction History:
├─ Order #1001: Buy 5 AAPL @ $150.50
└─ Order #1002: Sell 3 AAPL @ $160.00 (Gain: +$28.50)
```

---

## Error Scenarios & Handling

### Error 1: User Not Authenticated

```
Scenario: User tries to sell without logging in

Client sends: CMSG_SELL_STOCK (not logged in)
         │
         ▼
Server: Check session → is_logged_in = false
         │
         ▼
Response: SMSG_SELL_STOCK_FAIL
         status = 1
         error = "User not authenticated. Please login first."
```

### Error 2: Stock Not Found

```
Scenario: User tries to sell stock_id=99 (doesn't exist)

Server: stock_db_get_by_id(99)
         │
         ▼
Returns: NULL
         │
         ▼
Response: SMSG_SELL_STOCK_FAIL
         status = 1
         error = "Stock not found."
```

### Error 3: Stock Not Owned (Critical Issue)

```
Scenario: User tries to sell AAPL but portfolio is empty
         (This is the KNOWN BUG - portfolio not persistent!)

Server: portfolio_db_get(user_id)
         │
         ▼
Returns: Empty portfolio (created fresh this request!)
         │
         ▼
Response: SMSG_SELL_STOCK_FAIL
         status = 1
         error = "You don't own this stock."

ROOT CAUSE:
  Each request calls portfolio_create() → new empty portfolio
  Previous request's holdings were freed at end of request
  Result: User can't sell despite owning shares!
```

**Fix Required**: Implement portfolio_manager to maintain portfolios across requests

### Error 4: Insufficient Holdings

```
Scenario: User owns 3 AAPL, tries to sell 5 AAPL

Server: holding->quantity = 3, request->quantity = 5
         3 < 5? YES
         │
         ▼
Response: SMSG_SELL_STOCK_FAIL
         status = 1
         error = "Insufficient holdings. You own 3 shares."
```

### Error 5: Portfolio Not Found

```
Scenario: Portfolio system corrupted/not initialized

Server: portfolio_db_get() returns NULL
         │
         ▼
Response: SMSG_SELL_STOCK_FAIL
         status = 1
         error = "Portfolio error. Contact support."
```

---

## Database Modifications

### Affected Tables/Files

#### 1. accounts.txt (Account Balance)

```
Before:  1|admin|password|9247.50
After:   1|admin|password|9727.50
         ↑                ↑
         (unchanged)      (increased by $480.00)
```

#### 2. stocks.txt (Stock Inventory)

```
Before:  1|AAPL|Apple Inc|995|160.00
After:   1|AAPL|Apple Inc|998|160.00
         ↑              ↑
         (unchanged)    (increased by 3, returned shares)
```

#### 3. In-Memory Portfolio (portfolio_db)

```
Before:  portfolio[1].holdings = [
           {stock_id: 1, qty: 5, avg_price: 150.50}
         ]

After:   portfolio[1].holdings = [
           {stock_id: 1, qty: 2, avg_price: 150.50}
         ]
         (3 shares removed)
```

#### 4. Transaction Log

```
New entry:
  order_id: 1002
  user_id: 1
  type: SELL
  stock_id: 1
  quantity: 3
  price: 160.00
  timestamp: 2026-01-11 15:30:22
```

---

## Gain/Loss Calculation

### Cost Basis Tracking

Each holding maintains average purchase price:

```
Purchase History:
  Order 1: Buy 2 AAPL @ $100.00 = $200.00
  Order 2: Buy 3 AAPL @ $110.00 = $330.00
  
  Total holdings: 5 AAPL
  Total cost: $530.00
  Average price: $530.00 / 5 = $106.00/share
```

### Selling Formula

```
If selling 3 out of 5 AAPL @ $120:
  
  Proceeds = 3 × $120.00 = $360.00
  Cost basis for 3 shares = 3 × $106.00 = $318.00
  Gain = $360.00 - $318.00 = $42.00 (11.76% gain)
  
Response:
  total_amount: $360.00 (proceeds)
  gain_loss: +$42.00 (displayed as green/profit)
```

### Loss Scenario

```
If selling 2 AAPL @ $95:
  
  Proceeds = 2 × $95.00 = $190.00
  Cost basis for 2 shares = 2 × $106.00 = $212.00
  Gain = $190.00 - $212.00 = -$22.00 (loss)
  
Response:
  total_amount: $190.00 (proceeds)
  gain_loss: -$22.00 (displayed as red/loss)
```

---

## Concurrency & Thread Safety

### Mutex Protection

```
Operation                          Mutex Used
─────────────────────────────────────────────
Read balance                       account_mutex
Write balance                      account_mutex
Read portfolio                     portfolio_mutex
Write portfolio                    portfolio_mutex
Read stock quantity                stock_mutex
Write stock quantity               stock_mutex
Generate order_id                  transaction_mutex
```

### Transaction Atomicity

All operations must execute atomically:

```c
pthread_mutex_lock(&balance_mutex);
pthread_mutex_lock(&portfolio_mutex);
pthread_mutex_lock(&stock_mutex);
pthread_mutex_lock(&transaction_mutex);

// Critical section: All operations atomic
balance += proceeds;
portfolio_remove_holding(...);
stock_qty += quantity;
order_id = transaction_record(...);

pthread_mutex_unlock(&transaction_mutex);
pthread_mutex_unlock(&stock_mutex);
pthread_mutex_unlock(&portfolio_mutex);
pthread_mutex_unlock(&balance_mutex);
```

**Why Atomic?**
- Prevents: Two clients sell simultaneously, both check holdings pass, both remove (negative holdings!)
- Prevents: Sells from one user showing up in another user's balance

---

## Testing Scenarios

### Test 1: Successful Partial Sell

```
Setup:
  - User owns 5 AAPL @ avg $150.50
  - Balance: $9,247.50
  - Current price: $160.00

Action:
  Client sends: sell AAPL 3 160.00
  
Expected Result:
  ✓ Order ID: 1002
  ✓ Proceeds: $480.00
  ✓ Gain: +$28.50 (3 × ($160 - $150.50))
  ✓ New balance: $9,727.50
  ✓ Remaining holdings: 2 AAPL @ $150.50
  ✓ Stock qty: 998 (995 + 3)
```

### Test 2: Complete Position Liquidation

```
Setup:
  - User owns 5 AAPL @ avg $150.50
  - Current price: $155.00

Action:
  Client sends: sell AAPL 5 155.00
  
Expected Result:
  ✓ Order ID: 1003
  ✓ Proceeds: $775.00
  ✓ Gain: +$22.50 (5 × ($155 - $150.50))
  ✓ Remaining holdings: 0 AAPL (position closed)
  ✓ Portfolio no longer shows AAPL
```

### Test 3: Loss on Sale

```
Setup:
  - User owns 3 AAPL @ avg $150.00
  - Current price: $140.00

Action:
  Client sends: sell AAPL 3 140.00
  
Expected Result:
  ✓ Order ID: 1004
  ✓ Proceeds: $420.00
  ✓ Loss: -$30.00 (3 × ($140 - $150))
  ✓ Gain_loss field shows negative
```

### Test 4: Insufficient Holdings

```
Setup:
  - User owns 2 AAPL
  - Attempts to sell 5 AAPL

Action:
  Client sends: sell AAPL 5 150.00
  
Expected Result:
  ✗ Error: "Insufficient holdings. You own 2 shares."
  ✗ Balance unchanged
  ✗ No transaction created
```

### Test 5: Stock Not Owned (KNOWN BUG)

```
Setup:
  - User owns 5 AAPL (from previous buy)
  - New session started (portfolio not maintained)

Action:
  Client sends: sell AAPL 3 150.00
  
Expected Result (CURRENT - BROKEN):
  ✗ Error: "You don't own this stock."
  
Expected Result (AFTER FIX):
  ✓ Order ID: 1005
  ✓ Proceeds: $450.00
  ✓ Remaining: 2 AAPL
```

---

## Client Command Interface

### Usage

```
trading> sell AAPL 3 160.00
```

### Parsing

```c
// Input: "sell AAPL 3 160.00"
// Parse:
cmd = "sell"
args = "AAPL 3 160.00"

// Extract from args:
sscanf(args, "%s %u %lf", symbol, &quantity, &price);
// symbol = "AAPL"
// quantity = 3
// price = 160.00
```

### Client Implementation

```c
void cmd_sell_stock(const char* args) {
    // 1. Parse arguments
    char symbol[32];
    uint32_t quantity;
    double price;
    sscanf(args, "%31s %u %lf", symbol, &quantity, &price);
    
    // 2. Lookup stock_id from symbol
    stock_id = get_stock_id_by_symbol(symbol);
    
    // 3. Create request packet
    sell_stock_request_t req = {
        .stock_id = stock_id,
        .quantity = quantity,
        .price = price
    };
    
    // 4. Send request
    char buffer[sizeof(header) + sizeof(req)];
    // ... combine and send ...
    
    // 5. Receive response
    sell_stock_response_t resp;
    // ... receive and parse ...
    
    // 6. Display result
    if (resp.status == 0) {
        printf("✓ Order #%u executed\n", resp.order_id);
        printf("  Proceeds: $%.2f\n", resp.total_amount);
        printf("  Gain/Loss: $%.2f\n", resp.gain_loss);
        printf("  New balance: $%.2f\n", resp.new_balance);
    } else {
        printf("✗ Error: %s\n", error_msg);
    }
}
```

---

## Integration Points

### Depends On

- **Session Manager**: Validates user authentication
- **Account DB**: Reads/updates balance
- **Stock DB**: Validates stock exists, updates quantity
- **Portfolio DB**: Reads holdings, updates portfolio
- **Transaction DB**: Creates audit trail
- **Packet Layer**: Sends response

### Called By

- **Dispatcher**: Routes CMSG_SELL_STOCK messages
- **Server main loop**: Receives client request

### Data Flow

```
Client Request
    ↓
Dispatcher (route message type)
    ↓
handle_sell_stock_request()
    ├─→ session_mgr_get()
    ├─→ stock_db_get_by_id()
    ├─→ portfolio_db_get()           ← CRITICAL: Must maintain portfolio!
    ├─→ portfolio_find_holding()
    ├─→ account_db_get_balance()
    ├─→ account_db_update_balance()
    ├─→ portfolio_db_remove_holding()
    ├─→ stock_db_update_quantity()
    ├─→ transaction_db_record()
    └─→ send_packet()
    ↓
Server Response
    ↓
Client Display
```

---

## Known Issues & Workarounds

### Issue 1: Portfolio Persistence Bug

**Description**: Users can buy stocks successfully, but cannot sell them because portfolio is recreated as empty on each request.

**Root Cause**: `portfolio_db_get(user_id)` creates new portfolio each time, old holdings are freed.

**Current Workaround**: None - feature is broken

**Permanent Fix**: Implement `portfolio_manager.c` that maintains portfolios across requests:

```c
// Pseudocode for fix
portfolio_t* portfolio_manager[MAX_USERS];

portfolio_t* portfolio_mgr_get(uint32_t user_id) {
    if (portfolio_manager[user_id] == NULL) {
        portfolio_manager[user_id] = portfolio_create(user_id);
        // Load from persistent storage
        portfolio_load_from_file(portfolio_manager[user_id]);
    }
    return portfolio_manager[user_id];
}

// Call at server startup
void portfolio_mgr_init() {
    for (int i = 0; i < MAX_USERS; i++) {
        portfolio_manager[i] = NULL;
    }
}
```

**Status**: 🔴 Not Fixed - Blocking all sell operations

---

## Future Enhancements

1. **Short Selling**: Allow selling shares not owned (margin)
2. **Dividend Support**: Dividend payments before/after sell
3. **Tax Reporting**: Calculate capital gains for tax purposes
4. **Average Cost Basis**: Track multiple purchase prices
5. **Stop Loss Orders**: Automatic sell at specified price
6. **Limit Orders**: Sell only if price reaches minimum
7. **Batch Sell**: Specify which lots to sell (FIFO/LIFO)
8. **Sell Confirmation**: Require second factor before large sells
