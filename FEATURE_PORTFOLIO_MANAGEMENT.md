# Portfolio Management Documentation

## Overview

Portfolio Management tracks user stock holdings, average purchase prices, and calculates gains/losses. **This feature has a critical bug** where portfolios are not persisted across requests, causing sell operations to fail.

---

## Feature Summary

| Property | Value |
|----------|-------|
| **Purpose** | Track user stock holdings and cost basis |
| **Data Storage** | In-memory (portfolio_t array) + optional file persistence |
| **Per-User Data** | Holdings array with quantity and average purchase price |
| **Critical Issue** | Portfolio recreated on each request (not persistent) |
| **Max Holdings** | 100 stocks per user |
| **Thread Safety** | Mutex-protected |

---

## Portfolio Data Structure

### Portfolio Definition

```c
typedef struct {
    uint32_t user_id;           // User who owns this portfolio
    holding_t* holdings;        // Array of holdings
    int holding_count;          // Current number of holdings
    int capacity;               // Allocated capacity
} portfolio_t;
```

### Holding Definition

```c
typedef struct {
    uint16_t stock_id;          // Stock identifier
    uint32_t quantity;          // Number of shares owned
    double average_purchase_price; // Weighted average cost per share
} holding_t;
```

### Example Portfolio State

```
Portfolio for user_id=1 (admin):
┌─────────────────────────────────────┐
│ portfolio[1]                        │
├─────────────────────────────────────┤
│ user_id: 1                          │
│                                     │
│ holdings[]:                         │
│   [0] stock_id: 1 (AAPL)            │
│        quantity: 5                  │
│        avg_price: 150.50            │
│                                     │
│   [1] stock_id: 2 (GOOGL)           │
│        quantity: 2                  │
│        avg_price: 2850.00           │
│                                     │
│   [2] stock_id: 3 (MSFT)            │
│        quantity: 10                 │
│        avg_price: 350.00            │
│                                     │
│ holding_count: 3                    │
│ capacity: 100                       │
└─────────────────────────────────────┘

Total Value = (5×150.50) + (2×2850.00) + (10×350.00)
            = 752.50 + 5700.00 + 3500.00
            = $9,952.50
```

---

## Portfolio Manager API

### Initialization

```c
bool portfolio_db_init(void);
```

**Purpose**: Initialize portfolio database at server startup
**Returns**: true on success, false on failure
**Side Effects**: 
- Allocates mutex
- Loads existing portfolios from file (if exists)

### Shutdown

```c
void portfolio_db_destroy(void);
```

**Purpose**: Clean up portfolio database at server shutdown
**Side Effects**: 
- Saves all portfolios to file (persistence)
- Frees all portfolio memory
- Destroys mutex

### Get User Portfolio

```c
portfolio_t* portfolio_db_get(uint32_t user_id);
```

**Purpose**: Retrieve portfolio for a user
**Parameter**: User ID
**Returns**: Pointer to portfolio_t or NULL if not found
**CRITICAL ISSUE**: Creates fresh empty portfolio each call, doesn't maintain state!

### Add Holding

```c
bool portfolio_db_add_holding(uint32_t user_id, 
                             uint16_t stock_id, 
                             uint32_t quantity, 
                             double price);
```

**Purpose**: Add or update holding in portfolio
**Parameters**:
- user_id: Which user's portfolio
- stock_id: Which stock
- quantity: Number of shares
- price: Purchase price per share

**Returns**: true on success, false on failure

**Behavior**:
```
If holding already exists:
  - Update quantity (add to existing)
  - Recalculate average purchase price:
    new_avg = (old_qty × old_price + new_qty × new_price) / 
              (old_qty + new_qty)

If holding doesn't exist:
  - Add new holding
  - Set average price = purchase price
```

**Example**: 
```
Existing: 2 AAPL @ $100 avg
New buy: 3 AAPL @ $110

Result:
  total_qty = 2 + 3 = 5
  new_avg = (2×100 + 3×110) / 5 = 530 / 5 = $106 avg
```

### Remove Holding

```c
bool portfolio_db_remove_holding(uint32_t user_id, 
                                uint16_t stock_id, 
                                uint32_t quantity);
```

**Purpose**: Reduce or remove holding from portfolio
**Parameters**:
- user_id: Which user's portfolio
- stock_id: Which stock to reduce
- quantity: Number of shares to remove

**Returns**: true on success, false on failure (not enough shares)

**Behavior**:
```
If quantity < holding.quantity:
  - Reduce holdings by quantity
  - Keep average price (unchanged)
  - Example: Own 5 AAPL, sell 2 → 3 remain @ same avg price

If quantity == holding.quantity:
  - Remove entire holding
  - Delete from array
  - Shift remaining holdings

If quantity > holding.quantity:
  - Return false (error)
```

### Free Portfolio

```c
void portfolio_db_free(portfolio_t* portfolio);
```

**Purpose**: Release portfolio memory
**Parameter**: Portfolio to free
**Side Effects**: Deallocates holdings array

---

## Critical Issue: Non-Persistence

### The Problem

```
Request 1: User buys 5 AAPL
  ├─ portfolio_db_get(user_id)  → Creates new portfolio_t
  ├─ portfolio_db_add_holding(1, 100, 5, 150.50)
  ├─ [Process buy]
  └─ [Response sent]
     └─ portfolio freed! ✗

Request 2: User tries to sell 3 AAPL (same client, different request)
  ├─ portfolio_db_get(user_id)  → Creates FRESH empty portfolio_t
  ├─ Find holding → NOT FOUND (previous request's data was freed!)
  ├─ Error: "You don't own this stock"
  └─ [Response sent]
```

### Root Cause

Current implementation:
```c
portfolio_t* portfolio_db_get(uint32_t user_id) {
    // Creates NEW portfolio every call!
    portfolio_t* p = malloc(sizeof(portfolio_t));
    p->user_id = user_id;
    p->holdings = malloc(sizeof(holding_t) * INITIAL_CAPACITY);
    // ... initialize ...
    return p;
}

// After request completes:
portfolio_db_free(portfolio);  // Frees the portfolio!
```

Result: Holdings lost between requests!

### The Fix (Not Yet Implemented)

Need persistent portfolio manager:

```c
// Global portfolio storage
#define MAX_USERS 1000
static portfolio_t* global_portfolios[MAX_USERS];
static pthread_mutex_t portfolio_mutex = PTHREAD_MUTEX_INITIALIZER;

portfolio_t* portfolio_mgr_get(uint32_t user_id) {
    pthread_mutex_lock(&portfolio_mutex);
    
    // Check if already loaded
    if (global_portfolios[user_id] == NULL) {
        // Create and load from file
        global_portfolios[user_id] = portfolio_create(user_id);
        portfolio_load_from_file(global_portfolios[user_id]);
    }
    
    portfolio_t* result = global_portfolios[user_id];
    pthread_mutex_unlock(&portfolio_mutex);
    return result;
}

// DON'T free portfolios anymore - they persist!
```

**Status**: 🔴 **NOT IMPLEMENTED** - This is blocking sell operations!

---

## Portfolio Persistence (File-Based)

### Storage Format

**Filename**: `portfolio_db.txt` (in server_folder/)

**Format**: CSV with headers
```csv
user_id,holding_id,stock_id,quantity,avg_price
1,0,1,5,150.50
1,1,2,2,2850.00
1,2,3,10,350.00
2,0,1,3,155.00
2,1,2,5,2820.00
```

### Persistence Functions

```c
bool portfolio_db_load(void);
```

**Purpose**: Load portfolios from file at startup
**Returns**: true on success
**Behavior**:
- Read portfolio_db.txt
- Parse CSV format
- Populate in-memory portfolios

```c
bool portfolio_db_persist(void);
```

**Purpose**: Save portfolios to file
**Returns**: true on success
**Behavior**:
- Iterate all portfolios
- Write to portfolio_db.txt in CSV format
- Create backup

---

## Buy Operation: Portfolio Impact

### Scenario: Buy 5 AAPL @ $150.50

```
Before:
  Portfolio (user 1):
    holdings: []
    
Action:
  buy AAPL 5 150.50
  
Processing:
  1. portfolio_db_get(1) → New empty portfolio
  2. portfolio_db_add_holding(1, 1, 5, 150.50)
     ├─ No existing holding
     └─ Create new: {stock_id: 1, qty: 5, avg: 150.50}
  3. Send response: ✓ Order #1001
  4. [Request ends]
  5. portfolio_db_free() → Portfolios freed! ✗
  
After:
  Portfolio (user 1):
    holdings: [] ← LOST! Should be [{stock_id: 1, qty: 5, ...}]
    
Result:
  ✓ Balance updated ($10,000 → $9,247.50)
  ✓ Stock qty updated (1000 → 995)
  ✗ Portfolio empty (holdings discarded)
  ✗ Can't sell later (holdings not found)
```

---

## Sell Operation: Portfolio Check

### Scenario: Sell 3 AAPL @ $160.00

```
Before:
  (User bought 5 AAPL in previous request)
  Portfolio (user 1):
    holdings: [] ← BUG: Should have 5 AAPL!
    
Action:
  sell AAPL 3 160.00
  
Processing:
  1. portfolio_db_get(1) → New empty portfolio
  2. portfolio_find_holding(portfolio, 1)
     └─ Returns NULL (holdings array empty)
  3. Check result:
     if (!holding) {
         ERROR: "You don't own this stock"
     }
  4. Send error response
  
After:
  ✗ Error: "You don't own this stock"
  ✗ No transaction created
  ✗ Balance unchanged
```

---

## Thread Safety

### Mutex Protection

```c
static pthread_mutex_t portfolio_mutex = PTHREAD_MUTEX_INITIALIZER;

// Protected operations
pthread_mutex_lock(&portfolio_mutex);
    portfolio = portfolio_db_get(user_id);      // Read
    portfolio_db_add_holding(...);              // Write
pthread_mutex_unlock(&portfolio_mutex);
```

### Race Condition Prevention

**Scenario**: Two concurrent buys of same stock

```
Thread 1: Buy 5 AAPL
  ├─ Get holding → avg_price = 150
  ├─ Calculate new avg (50 shares)
  └─ Update

Thread 2: Simultaneously buy 3 AAPL
  ├─ Get holding → avg_price = 150 (STALE!)
  ├─ Calculate with wrong average
  └─ Corrupts data!

Solution: Lock entire operation
```

### Correct Implementation

```c
pthread_mutex_lock(&portfolio_mutex);
    // Atomic operation: read → calculate → write
    holding = find_holding(...);
    if (!holding) create_holding();
    new_avg = calculate_avg(holding->qty, holding->avg_price, new_qty, new_price);
    holding->quantity += new_qty;
    holding->average_purchase_price = new_avg;
pthread_mutex_unlock(&portfolio_mutex);
```

---

## Average Purchase Price Calculation

### Weighted Average Formula

When adding new shares:

```
new_avg = (existing_qty × existing_avg + new_qty × new_price) / 
          (existing_qty + new_qty)
```

### Example Sequence

```
Step 1: Buy 2 AAPL @ $100
  Portfolio: {qty: 2, avg_price: 100.00}
  Total cost: 2 × 100 = $200

Step 2: Buy 3 AAPL @ $110
  Calculation:
    new_avg = (2 × 100 + 3 × 110) / (2 + 3)
            = (200 + 330) / 5
            = 530 / 5
            = $106.00 avg
  
  Portfolio: {qty: 5, avg_price: 106.00}
  Total cost: 5 × 106 = $530 ✓

Step 3: Sell 1 AAPL @ $120
  Proceeds: 1 × 120 = $120
  Cost basis: 1 × 106 = $106
  Gain: $120 - $106 = $14
  
  Portfolio: {qty: 4, avg_price: 106.00} (avg unchanged!)
  Total cost: 4 × 106 = $424
```

---

## Common Operations

### View All Holdings

```c
void portfolio_display(portfolio_t* portfolio) {
    for (int i = 0; i < portfolio->holding_count; i++) {
        holding_t* h = &portfolio->holdings[i];
        
        // Get current stock price
        stock_t* s = stock_db_get_by_id(h->stock_id);
        
        double current_value = h->quantity * s->price;
        double cost_basis = h->quantity * h->average_purchase_price;
        double gain_loss = current_value - cost_basis;
        double gain_loss_pct = (gain_loss / cost_basis) * 100;
        
        printf("%-10s  %5u  $%-8.2f  $%-8.2f  $%-8.2f (%+.1f%%)\n",
               s->symbol,
               h->quantity,
               h->average_purchase_price,
               s->price,
               gain_loss,
               gain_loss_pct);
    }
}
```

### Calculate Total Portfolio Value

```c
double portfolio_get_total_value(portfolio_t* portfolio) {
    double total = 0;
    for (int i = 0; i < portfolio->holding_count; i++) {
        holding_t* h = &portfolio->holdings[i];
        stock_t* s = stock_db_get_by_id(h->stock_id);
        total += h->quantity * s->price;
    }
    return total;
}
```

### Calculate Total Gain/Loss

```c
double portfolio_get_total_gain_loss(portfolio_t* portfolio) {
    double total_current = 0;
    double total_cost = 0;
    
    for (int i = 0; i < portfolio->holding_count; i++) {
        holding_t* h = &portfolio->holdings[i];
        stock_t* s = stock_db_get_by_id(h->stock_id);
        
        total_current += h->quantity * s->price;
        total_cost += h->quantity * h->average_purchase_price;
    }
    
    return total_current - total_cost;
}
```

---

## Testing Scenarios

### Test 1: Single Stock Purchase and Hold

```
Action:
  buy AAPL 5 150.50
  
Expected:
  Portfolio: {stock_id: 1, qty: 5, avg: 150.50}
  
Check:
  Portfolio shows 5 AAPL @ $150.50 ✓
```

### Test 2: Multiple Buys of Same Stock

```
Action:
  buy AAPL 2 100.00
  buy AAPL 3 110.00
  
Expected:
  Portfolio: {stock_id: 1, qty: 5, avg: 106.00}
  
Check:
  Weighted average calculated correctly ✓
  Total qty = 5 ✓
```

### Test 3: Multiple Different Stocks

```
Action:
  buy AAPL 5 150.50
  buy GOOGL 2 2850.00
  buy MSFT 10 350.00
  
Expected:
  Portfolio holdings:
    - AAPL: 5 @ 150.50
    - GOOGL: 2 @ 2850.00
    - MSFT: 10 @ 350.00
  
Check:
  All holdings tracked separately ✓
  Each maintains own average price ✓
```

### Test 4: Partial Sell

```
Action:
  buy AAPL 5 150.50
  sell AAPL 2 160.00
  
Expected:
  Portfolio: {stock_id: 1, qty: 3, avg: 150.50}
  
Check:
  Quantity reduced ✓
  Average price unchanged ✓
  2 shares removed ✓
```

### Test 5: Complete Position Liquidation

```
Action:
  buy AAPL 5 150.50
  sell AAPL 5 160.00
  
Expected:
  Portfolio: no AAPL entry
  
Check:
  Holding completely removed ✓
  Portfolio empty if no other positions ✓
```

---

## Integration Points

### Used By

- **Buy Stock Handler**: Adds holdings
- **Sell Stock Handler**: Removes holdings, checks quantity
- **View My Stocks Command**: Displays holdings
- **Gain/Loss Calculations**: Uses average prices

### Depends On

- **Stock DB**: Gets current stock information
- **Account DB**: Gets balance (but portfolio tracks separately)
- **Transaction DB**: Records order history
- **Mutex**: Thread synchronization

### Data Flow

```
Buy Command
    ↓
Buy Handler
    ├─ portfolio_db_get(user_id)
    ├─ portfolio_db_add_holding(user_id, stock_id, qty, price)
    ├─ [Update balance & stock qty]
    └─ portfolio_db_free() ← FREES PORTFOLIO! BUG!
    
Sell Command (new request)
    ↓
Sell Handler
    ├─ portfolio_db_get(user_id)  ← NEW EMPTY portfolio!
    ├─ Find holding → NOT FOUND (was freed)
    └─ Error: "You don't own this stock"
```

---

## Migration Path: Fixing Non-Persistence

### Step 1: Identify Current Issue

- [x] Buy works
- [x] Portfolio created
- [x] Holdings added
- [ ] Holdings persist to next request
- [ ] Sell fails (holdings not found)

### Step 2: Create Portfolio Manager

1. Create `core/portfolio_manager.h/c`
2. Implement global portfolio array indexed by user_id
3. Load/save from file

### Step 3: Update Handlers

1. Replace `portfolio_db_get()` with `portfolio_mgr_get()`
2. Remove `portfolio_db_free()` calls
3. Let portfolios persist for session lifetime

### Step 4: Implement Persistence

1. Save to file on shutdown
2. Load from file on startup
3. Optional: Auto-save after each transaction

### Step 5: Testing

1. Buy → Check portfolio saved
2. Sell → Verify holdings found
3. New session → Portfolio maintained
4. Server restart → Portfolio recovered

---

## Future Enhancements

1. **Persistent Storage**: Database instead of CSV
2. **Dividend Tracking**: Record dividends received
3. **Stock Splits**: Adjust holdings on splits
4. **Cost Basis Methods**: FIFO, LIFO, specific ID
5. **Tax Reporting**: Capital gains/losses calculation
6. **Margin Trading**: Leverage and short positions
7. **Options**: Track option contracts
8. **Real-Time P&L**: Mark-to-market valuation
