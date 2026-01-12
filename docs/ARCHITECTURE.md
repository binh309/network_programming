# System Architecture

## Overview

Multi-threaded C-based trading system with:
- Event-driven server using epoll
- Persistent portfolio management across sessions
- Atomic stock operations preventing race conditions
- Complete transaction audit trail

## System Diagram

```
┌─────────────────┐         ┌──────────────────┐
│   Client App    │◄────────►│  Trading Server  │
│  (client.c)     │          │  (server.c)      │
└─────────────────┘         └──────────────────┘
                                    │
                    ┌───────────────┼───────────────┐
                    │               │               │
                ┌───▼───┐      ┌───▼───┐      ┌───▼───┐
                │ Event │      │ Account│      │ Stock │
                │ Loop  │      │Database│      │ Database
                └───┬───┘      └───────┘      └───────┘
                    │
                ┌───▼────────────────────┐
                │ Portfolio Manager      │
                │ (Persistent In-Memory) │
                └───┬────────────────────┘
                    │
                ┌───▼────────────────────┐
                │ Disk Persistence       │
                │ (portfolios.txt etc)   │
                └────────────────────────┘
```

## Core Components

### Server Core (`server_folder/core/`)

**Event Loop** (`event_loop.c`)
- epoll-based asynchronous I/O
- Handles multiple concurrent connections
- Dispatches requests to appropriate handlers

**Connection Manager** (`connection_manager.c`)
- Hash table for O(1) socket lookup
- Per-connection state tracking
- User authentication tracking

**Portfolio Manager** (`portfolio_manager.c`) - **KEY FIX**
- Maintains in-memory portfolio storage
- Syncs with disk via `portfolio_mgr_reload_user()`
- Called after every buy/sell to maintain consistency

**Request Handler** (`request_handler.c`)
- Parses incoming packets
- Routes to appropriate feature handler
- Serializes responses

### Data Layer (`server_folder/data/`)

**Account Database** (`account_db.c`)
- User account storage
- Login credentials
- Account balances

**Stock Database** (`stock_db.c`) - **ATOMIC OPERATIONS**
- Stock pricing information
- Inventory levels (bid/ask volumes)
- Atomic check-and-update operations with per-stock mutexes

**Portfolio Database** (`portfolio_db.c`)
- User stock holdings
- Average purchase cost
- Disk persistence (portfolios.txt)

**Transaction Database** (`transaction_db.c`)
- Complete transaction audit trail
- Buy/sell execution history
- Timestamps and order details

### Features (`server_folder/features/`)

**Trading Operations**
- Buy Stock (`buy_stock.c`) - With portfolio reload
- Sell Stock (`sell_stock.c`) - With portfolio reload

**User Management**
- Login (`login.c`)
- Register (`register.c`)

**Information Display**
- View Stocks (`view_stocks.c`)
- See My Stocks (`see_my_stocks.c`)
- See Balance (`see_balance.c`)

**Market Operations**
- Market Simulation (`market.c`)
- Request Dispatcher (`dispatcher.c`)

### Network Layer (`server_folder/network/`)

**Packet Protocol** (`packet.c/h`)
- Binary packet serialization
- 6-byte header + variable body
- Packet types for all operations

**Packet Parser** (`packet_parser.c`)
- Deserializes incoming packets
- Validates packet format

**Packet Builder** (`packet_builder.c`)
- Serializes response packets
- Formats data for transmission

**Socket I/O** (`socket_io.c`)
- Low-level socket operations
- Buffered read/write

**Network Communication** (`network_send.c`, `network_receive.c`)
- Handles send/receive loops
- Manages packet boundaries

## Data Flow

### Buy Operation

```
1. Client sends: BUY packet
   ↓
2. Server parses packet
   ↓
3. Validate (qty, price, account balance)
   ↓
4. Lock connection mutex
   ↓
5. Check stock inventory (atomic with lock)
   ↓
6. Deduct from balance
   ↓
7. Add to portfolio (portfolio_db)
   ↓
8. Deduct from stock inventory (atomic)
   ↓
9. Record transaction
   ↓
10. ⭐ portfolio_mgr_reload_user() - SYNC MANAGER (FIX)
    ↓
11. Unlock connection mutex
    ↓
12. Send success response
    ↓
13. Client shows: "Order Filled"
```

### Login with Portfolio Retrieval

```
1. Client sends: LOGIN packet
   ↓
2. Server validates credentials
   ↓
3. Create connection
   ↓
4. portfolio_mgr_get_or_create(user_id)
   ├─ Check if in memory → YES: return
   └─ Check if in memory → NO:
      └─ Load from disk via portfolio_db_get()
         └─ Add to in-memory manager
   ↓
5. Return portfolio to client
```

## Thread Safety

**Mutex Protection**:
- `connection_lock` - Per-connection state
- `portfolio_lock` - Portfolio manager array
- Per-stock mutexes - Individual stock updates

**Lock Pattern**:
```c
pthread_mutex_lock(&lock);
if (validate_condition()) {
    perform_operation();
}
pthread_mutex_unlock(&lock);
```

**Lock Ordering** (prevents deadlock):
1. Connection mutex (outermost)
2. Portfolio mutex
3. Stock mutexes (innermost)

## Transaction Atomicity

**All-or-Nothing Execution**:
1. Validate all preconditions before any updates
2. Execute all updates atomically under lock
3. Record transaction after all updates succeed
4. Rollback on any failure (restore balance → portfolio → stock)
5. Reload portfolio in manager

**Rollback Example** (Buy Fails at Stock Deduction):
```
1. Deduct balance: ✅ Done
2. Add to portfolio: ✅ Done
3. Deduct from stock: ❌ FAIL
   → Rollback: Add balance back
   → Rollback: Remove from portfolio
   → Return error response
```

## Database Files

**accounts.txt**
```
Format: user_id,username,password,balance
Locked while reading/writing
```

**stocks.txt**
```
Format: symbol,bid_price,ask_price,last_price,volume
Updated by market simulation every 5 seconds
```

**portfolios.txt**
```
Format: user_id: stock_id,qty,avg_cost stock_id,qty,avg_cost ...
Persisted after each buy/sell
```

**transactions.txt**
```
Format: order_id,user_id,type,stock_id,qty,price,timestamp
Complete audit trail
```

## Network Protocol

### Packet Format
```
Byte 0: Packet Type (1 byte)
Bytes 1-4: Request ID (4 bytes, network byte order)
Bytes 5-8: Body Length (4 bytes, network byte order)
Bytes 9+: Body (variable length, format depends on packet type)
```

### Packet Types
- `0x02` - Login
- `0x03` - Register
- `0x10` - View Stocks
- `0x11` - Buy Stock
- `0x12` - Sell Stock
- `0x13` - See My Stocks
- `0x14` - See Balance
- `0x81-0x94` - Response packets (0x80 + request type)

## Performance Characteristics

**Connection Lookup**: O(1) - Hash table
**Portfolio Access**: O(1) - Direct array index
**Stock Update**: O(1) - Atomic with per-stock lock
**Transaction Recording**: O(1) - Append to file

**Concurrent Connections**: Tested up to 10+ simultaneous
**Max Users**: Configurable (default 100)
**Max Stocks**: Configurable (default 50)

## Scalability Improvements

**Current Bottlenecks**:
- File I/O for transaction recording
- Global portfolio lock on reload

**Potential Improvements**:
- Use SQLite for better concurrency
- Implement connection pooling
- Batch transaction recording
- Add caching for frequently accessed data
- Use write-ahead logging
