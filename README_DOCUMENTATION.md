# Stock Trading System - Complete Documentation Index

Welcome to the comprehensive documentation for the Stock Trading System. This system is a client-server network application that allows users to buy, sell, and manage stock portfolios with real-time balance tracking.

---

## 📚 Documentation Files

### Core Architecture

#### [NETWORK_ARCHITECTURE.md](NETWORK_ARCHITECTURE.md) **START HERE**
Foundational documentation covering:
- **Protocol Design**: TCP/IP client-server model with atomic message sending
- **Packet Structure**: 5-byte header + variable body format
- **Communication Flow**: Request-response cycle with request_id matching
- **Message Types**: 16 message types (register, login, buy, sell, balance, etc.)
- **Session Management**: How user authentication is tracked per connection
- **Error Handling**: Error response patterns and common scenarios
- **Data Flow Diagrams**: Visual representation of buy/sell operations
- **Threading Model**: Epoll multiplexing with mutex-protected resources
- **Performance Metrics**: Expected timing and limitations
- **Security Considerations**: Current and recommended improvements

**Read this first to understand the overall system architecture.**

---

### Complex Features (Separate Documentation)

#### [FEATURE_BUY_STOCK.md](FEATURE_BUY_STOCK.md)
Complete documentation for stock purchase feature:
- **Request/Response Specification**: Exact binary packet formats
- **Execution Flow**: 10-step validation and processing pipeline
- **Data Structure Flow**: Before/after states of all databases
- **Error Scenarios**: All 4 error conditions with examples
- **Database Modifications**: Impact on accounts, stocks, portfolio, transactions
- **Concurrency**: Mutex protection and transaction atomicity
- **Testing Scenarios**: 4 test cases including concurrent purchases
- **Client Interface**: How users invoke the buy command
- **Integration Points**: Dependencies on other systems

**Key Metric**: Buy operation tested working ✓

---

#### [FEATURE_SELL_STOCK.md](FEATURE_SELL_STOCK.md)
Complete documentation for stock liquidation feature:
- **Request/Response Specification**: Exact binary formats
- **Execution Flow**: 10-step validation pipeline
- **Gain/Loss Calculation**: Cost basis and profit/loss computation
- **Error Scenarios**: 5 error conditions including critical bug
- **Database Modifications**: Impact on all systems
- **Concurrency**: Atomic transaction requirements
- **Testing Scenarios**: 5 test cases
- **Client Interface**: Sell command usage
- **Known Issues**: ⚠️ CRITICAL: Portfolio persistence bug (NOT FIXED)
- **Integration Points**: Sells depend on persistent portfolio

**Key Issue**: Sell fails because portfolio doesn't persist between requests ✗

---

#### [FEATURE_SESSION_MANAGEMENT.md](FEATURE_SESSION_MANAGEMENT.md)
Complete documentation for user authentication and session tracking:
- **Session Lifecycle**: From connection to disconnection
- **Session Data Structure**: Socket → user_id mapping
- **Session Manager API**: Init, add, get, logout operations
- **Authentication Flow**: Login process with credential verification
- **Authorization Pattern**: How all commands check if user is authenticated
- **Thread Safety**: Mutex protection for concurrent access
- **Session Storage**: Linear search through session array
- **Integration**: How every command uses session data
- **Multi-Connection Example**: 3 concurrent clients
- **Security Considerations**: Current gaps and recommended improvements
- **Testing Scenarios**: Login, multi-user, logout, unauthorized access
- **Common Issues**: Troubleshooting guide

**Key Feature**: Enables authenticated trading operations

---

#### [FEATURE_PORTFOLIO_MANAGEMENT.md](FEATURE_PORTFOLIO_MANAGEMENT.md)
Complete documentation for stock holding tracking:
- **Portfolio Data Structure**: Holdings array with cost basis
- **Portfolio Manager API**: Get, add, remove operations
- **CRITICAL ISSUE**: Non-persistence of portfolios between requests
- **Persistence (File-Based)**: CSV format for storage
- **Buy Operation Impact**: How buy adds to portfolio
- **Sell Operation Check**: Why sell fails (portfolio is empty)
- **Average Purchase Price**: Weighted average calculation
- **Common Operations**: Display, value calculation, P&L
- **Testing Scenarios**: Purchase sequences, partial sells
- **Thread Safety**: Mutex protection
- **Migration Path**: How to fix the persistence bug

**Root Cause of Sell Failure**: Each request gets empty portfolio, holdings freed at end

---

## 🗺️ Quick Navigation

### By Task

**"I want to understand the whole system"**
1. Read [NETWORK_ARCHITECTURE.md](NETWORK_ARCHITECTURE.md)
2. Skim each feature doc

**"I want to fix the sell feature"**
1. Read [FEATURE_SELL_STOCK.md](FEATURE_SELL_STOCK.md) "Known Issues"
2. Read [FEATURE_PORTFOLIO_MANAGEMENT.md](FEATURE_PORTFOLIO_MANAGEMENT.md) "Critical Issue"
3. Implement portfolio_manager.c with persistent storage

**"I want to add a new trading feature"**
1. Read [NETWORK_ARCHITECTURE.md](NETWORK_ARCHITECTURE.md) sections:
   - Message Types (add new type to protocol.h)
   - Communication Flow (pattern for handlers)
   - Error Handling (pattern for responses)
2. Follow pattern from [FEATURE_BUY_STOCK.md](FEATURE_BUY_STOCK.md)

**"I want to improve security"**
1. Read [NETWORK_ARCHITECTURE.md](NETWORK_ARCHITECTURE.md) "Security Considerations"
2. Read [FEATURE_SESSION_MANAGEMENT.md](FEATURE_SESSION_MANAGEMENT.md) "Security Considerations"

**"I want to understand concurrency"**
1. [NETWORK_ARCHITECTURE.md](NETWORK_ARCHITECTURE.md) "Threading & Synchronization"
2. [FEATURE_BUY_STOCK.md](FEATURE_BUY_STOCK.md) "Concurrency & Thread Safety"

### By Component

| Component | File | Status |
|-----------|------|--------|
| Network Protocol | [NETWORK_ARCHITECTURE.md](NETWORK_ARCHITECTURE.md) | ✅ Complete |
| Buy Feature | [FEATURE_BUY_STOCK.md](FEATURE_BUY_STOCK.md) | ✅ Working |
| Sell Feature | [FEATURE_SELL_STOCK.md](FEATURE_SELL_STOCK.md) | ❌ Broken (portfolio bug) |
| Session Management | [FEATURE_SESSION_MANAGEMENT.md](FEATURE_SESSION_MANAGEMENT.md) | ✅ Working |
| Portfolio Storage | [FEATURE_PORTFOLIO_MANAGEMENT.md](FEATURE_PORTFOLIO_MANAGEMENT.md) | ❌ Non-persistent |

---

## 🔧 System Architecture Overview

```
┌─────────────────────────────────────────────────────────┐
│ Client (client_app/client.c)                            │
│ - Interactive command interface                         │
│ - Sends requests over TCP/IP                            │
│ - Receives responses and displays results               │
└────────────────┬────────────────────────────────────────┘
                 │
                 │ TCP/IP
                 │ (Atomic packets: header + payload)
                 │
┌────────────────▼────────────────────────────────────────┐
│ Server (core/server.c)                                  │
│ - Epoll event loop                                      │
│ - Handles multiple concurrent clients                   │
│ - Routes messages to handlers                           │
└────────────────┬────────────────────────────────────────┘
                 │
    ┌────────────┴────────────┐
    │                         │
    ▼                         ▼
┌──────────────────┐   ┌──────────────────┐
│ Dispatcher       │   │ Session Manager  │
│ (features/      │   │ (core/          │
│  dispatcher.c)  │   │  session_       │
│                 │   │  manager.c)     │
│ Routes messages │   │                  │
│ by type (0x11,  │   │ Tracks authenticated
│ 0x12, etc.)     │   │ users per socket
└────────┬────────┘   └──────────────────┘
         │
    ┌────┴─────────────────────────┐
    │                              │
    ▼                              ▼
┌─────────────────┐         ┌─────────────────┐
│ Command Handler │         │ Data Layer      │
│ - buy_stock.c   │         │ - account_db.c  │
│ - sell_stock.c  │         │ - stock_db.c    │
│ - market.c      │         │ - portfolio_db.c│
│ - balance.c     │         │ - transaction_db│
│ - view_stocks.c │         └─────────────────┘
│ - see_my_stocks │
│ - login.c       │
│ - register.c    │
└─────────────────┘

Network Layer:
  - packet.c: Message serialization
  - socket.c: TCP connection handling
  - receive.c: Incoming data buffering
  - send.c: Message transmission
  - protocol.h: Message types and structures
```

---

## 📊 Message Type Reference

### Authentication
| Type | Code | Direction | Purpose |
|------|------|-----------|---------|
| REGISTER | 0x01 | C→S | Create account |
| LOGIN | 0x02 | C→S | Authenticate |
| LOGOUT | 0x03 | C→S | End session |

### Trading
| Type | Code | Direction | Purpose |
|------|------|-----------|---------|
| VIEW_STOCKS | 0x10 | C→S | List available |
| BUY_STOCK | 0x11 | C→S | Purchase shares |
| SELL_STOCK | 0x12 | C→S | Liquidate shares |
| VIEW_MY_STOCKS | 0x13 | C→S | Portfolio |
| SEE_BALANCE | 0x14 | C→S | Check balance |

### Success Responses (0x80+)
| Type | Code | Response To |
|------|------|-------------|
| REGISTER_SUCCESS | 0x81 | Register |
| LOGIN_SUCCESS | 0x83 | Login |
| VIEW_STOCKS_DATA | 0x90 | View Stocks |
| BUY_SUCCESS | 0x92 | Buy |
| SELL_SUCCESS | 0x94 | Sell |
| MY_STOCKS_DATA | 0x96 | View Portfolio |
| BALANCE_DATA | 0x98 | Check Balance |

### Failure Responses (0x80+, odd)
| Type | Code | Response To |
|------|------|-------------|
| REGISTER_FAIL | 0x82 | Register |
| LOGIN_FAIL | 0x84 | Login |
| BUY_FAIL | 0x93 | Buy |
| SELL_FAIL | 0x95 | Sell |
| ERROR | 0xFF | Any |

---

## 🔍 Detailed Message Flows

### Flow 1: Login

```
1. Client: "login admin password"
   → CMSG_LOGIN (0x02)
   
2. Server:
   ├─ Dispatcher routes to login.c
   ├─ Verify in accounts.txt
   ├─ Create session entry
   └─ Set is_logged_in=true
   
3. Server: SMSG_LOGIN_SUCCESS (0x83)
   ├─ user_id: 1
   └─ username: "admin"
   
4. Client: "✓ Logged in as admin"
```

### Flow 2: Buy Stock

```
1. Client: "buy AAPL 5 150.50"
   → CMSG_BUY_STOCK (0x11)
   
2. Server:
   ├─ Check session: is_logged_in=true ✓
   ├─ Validate stock: AAPL exists ✓
   ├─ Check balance: $10,000 ≥ $752.50 ✓
   ├─ Check availability: 1000 ≥ 5 ✓
   ├─ Update balance: $10,000 → $9,247.50
   ├─ Add holding: portfolio[1] += 5 AAPL
   ├─ Update stock qty: 1000 → 995
   └─ Record transaction: Order #1001
   
3. Server: SMSG_BUY_STOCK_SUCCESS (0x92)
   ├─ order_id: 1001
   ├─ total_amount: $752.50
   └─ new_balance: $9,247.50
   
4. Client:
   "✓ Order #1001 executed"
   "  Bought 5 AAPL for $752.50"
```

### Flow 3: Sell Stock (BROKEN)

```
1. Client: "sell AAPL 3 160.00"
   → CMSG_SELL_STOCK (0x12)
   
2. Server:
   ├─ Check session: is_logged_in=true ✓
   ├─ Validate stock: AAPL exists ✓
   ├─ Get portfolio: portfolio[1]
   │  ├─ PROBLEM: Empty! (was freed from previous request)
   │  └─ Should have: 5 AAPL
   ├─ Check holdings: NOT FOUND ✗
   └─ Error: "You don't own this stock"
   
3. Server: SMSG_SELL_STOCK_FAIL (0x95)
   └─ error: "You don't own this stock"
   
4. Client:
   "✗ Error: You don't own this stock"
   "  But user bought 5 shares! Bug in portfolio persistence!"
```

---

## 🐛 Known Issues & Status

### Issue 1: Portfolio Non-Persistence ⚠️ CRITICAL

**Severity**: High - Blocks core sell functionality
**Status**: Unfixed
**Impact**: Users can't sell stocks they own
**Root Cause**: `portfolio_db_get()` creates new empty portfolio each request
**Locations**: 
- [FEATURE_SELL_STOCK.md](FEATURE_SELL_STOCK.md#known-issues--workarounds)
- [FEATURE_PORTFOLIO_MANAGEMENT.md](FEATURE_PORTFOLIO_MANAGEMENT.md#critical-issue-non-persistence)
**Fix Required**: Implement portfolio_manager.c with persistent storage

### Issue 2: No Password Hashing ⚠️ SECURITY

**Severity**: Medium - Security risk
**Status**: Unfixed
**Impact**: Plaintext passwords in accounts.txt
**Locations**:
- [NETWORK_ARCHITECTURE.md](NETWORK_ARCHITECTURE.md#security-considerations)
- [FEATURE_SESSION_MANAGEMENT.md](FEATURE_SESSION_MANAGEMENT.md#recommended-improvements)
**Fix Required**: Use bcrypt or similar

### Issue 3: No Session Timeout ⚠️ SECURITY

**Severity**: Low - Sessions never expire
**Status**: Unfixed
**Impact**: Indefinite session access
**Fix Required**: Logout after 30 mins inactivity

### Issue 4: No SSL/TLS 🔴 SECURITY

**Severity**: High - No encryption
**Status**: Unfixed
**Impact**: All traffic visible on network
**Fix Required**: Add TLS layer

---

## 📈 Testing Status

### Tested & Working ✅
- [x] Login authentication
- [x] Check balance
- [x] View stocks catalog
- [x] Buy operation (balance, stock qty, transaction recorded)
- [x] TCP message delivery (atomic sends fixed)
- [x] Multiple concurrent clients
- [x] Error handling on invalid stock

### Partially Working 🟡
- [x] Sell operation logic (code correct)
- [ ] Sell portfolio lookup (fails - portfolio empty)

### Not Tested ❌
- [ ] End-to-end buy → sell → buy again workflow
- [ ] Portfolio persistence across server restart
- [ ] Concurrent buy/sell operations
- [ ] Edge cases (negative balance attempts, max holdings)

---

## 🚀 Getting Started

### For Users
1. Start server: `make -C server_folder && ./server_folder/server`
2. Start client: `./client_app/client`
3. Commands:
   - `login admin password` - Start trading
   - `view` - See available stocks
   - `balance` - Check account
   - `buy AAPL 5 150.50` - Buy 5 shares @ $150.50
   - `sell AAPL 3 160.00` - Sell 3 shares @ $160.00
   - `portfolio` - View holdings

### For Developers

**To understand the system:**
1. Read [NETWORK_ARCHITECTURE.md](NETWORK_ARCHITECTURE.md) (30 min)
2. Read feature documentation (15 min each)
3. Read source code: Look at buy_stock.c as reference (30 min)

**To add a new feature:**
1. Define message type in protocol.h
2. Create handler file (features/new_feature.c)
3. Add route in dispatcher.c
4. Add database functions if needed
5. Add client command in client.c
6. Update Makefile
7. Test

**To fix the portfolio bug:**
1. Create core/portfolio_manager.h
2. Implement global portfolio storage
3. Replace portfolio_db_get() calls
4. Add persistence (load/save to file)
5. Test buy → sell workflow

---

## 📞 Questions & Debugging

### "How does buy work end-to-end?"
See [FEATURE_BUY_STOCK.md](FEATURE_BUY_STOCK.md) "Execution Flow" and "Data Flow Diagram"

### "Why can't I sell after buying?"
Portfolio not maintained between requests. See [FEATURE_PORTFOLIO_MANAGEMENT.md](FEATURE_PORTFOLIO_MANAGEMENT.md) "Critical Issue"

### "How is authentication handled?"
See [FEATURE_SESSION_MANAGEMENT.md](FEATURE_SESSION_MANAGEMENT.md) "Authentication Flow"

### "What happens if two clients buy simultaneously?"
Mutex protection ensures atomic operations. See [NETWORK_ARCHITECTURE.md](NETWORK_ARCHITECTURE.md) "Threading & Synchronization"

### "What errors can buy return?"
See [FEATURE_BUY_STOCK.md](FEATURE_BUY_STOCK.md) "Error Scenarios & Handling"

---

## 📝 File Structure

```
project_final/
├── NETWORK_ARCHITECTURE.md          ← Core system design
├── FEATURE_BUY_STOCK.md            ← Buy operation
├── FEATURE_SELL_STOCK.md           ← Sell operation (broken)
├── FEATURE_SESSION_MANAGEMENT.md   ← User authentication
├── FEATURE_PORTFOLIO_MANAGEMENT.md ← Holdings tracking (broken)
│
├── client_app/
│   ├── client.c                    ← User interface
│   └── Makefile
│
└── server_folder/
    ├── core/
    │   ├── server.c                ← Event loop
    │   ├── session_manager.c       ← Session tracking
    │   └── session_manager.h
    │
    ├── network/
    │   ├── protocol.h              ← Message types
    │   ├── packet.c/h              ← Serialization
    │   ├── socket.c/h              ← TCP
    │   ├── send.c/h                ← Sending
    │   └── receive.c/h             ← Receiving
    │
    ├── data/
    │   ├── account_db.c/h          ← User balance
    │   ├── stock_db.c/h            ← Stock catalog
    │   ├── portfolio_db.c/h        ← Holdings (NON-PERSISTENT BUG)
    │   ├── transaction_db.c/h      ← Order log
    │   ├── accounts.txt            ← Users CSV
    │   └── stocks.txt              ← Stocks CSV
    │
    ├── model/
    │   ├── portfolio.h             ← Data structures
    │   ├── account.h
    │   ├── stock.h
    │   ├── transaction.h
    │   ├── user.h
    │   └── error.h
    │
    ├── features/
    │   ├── dispatcher.c            ← Route messages
    │   ├── buy_stock.c/h           ← Buy handler
    │   ├── sell_stock.c/h          ← Sell handler
    │   ├── see_balance.c/h         ← Balance
    │   ├── see_my_stocks.c/h       ← Portfolio view
    │   ├── view_stocks.c/h         ← Catalog
    │   ├── market.c/h              ← Market data
    │   ├── login.c/h               ← Login
    │   ├── register.c/h            ← Registration
    │   └── login.h
    │
    ├── Makefile
    └── server
```

---

## 📚 Documentation Standards

All documentation files follow consistent structure:

1. **Overview** - 1-2 sentence summary
2. **Feature Summary** - Quick reference table
3. **Request/Response Spec** - Exact binary formats
4. **Execution Flow** - Step-by-step processing
5. **Data Structure Flow** - Before/after states
6. **Error Scenarios** - All failure cases
7. **Database Modifications** - What changes
8. **Concurrency** - Thread safety
9. **Testing Scenarios** - Test cases
10. **Integration Points** - Dependencies

This structure provides:
- ✅ Quick reference (Feature Summary table)
- ✅ Deep understanding (Execution Flow)
- ✅ Error handling (Error Scenarios)
- ✅ Testing guidance (Testing Scenarios)
- ✅ Integration info (Integration Points)

---

## 🎯 Next Steps

**Immediate**: Fix portfolio persistence bug blocking sell operations
**Short-term**: Add more features (dividends, margin, options)
**Medium-term**: Improve security (TLS, password hashing, session timeout)
**Long-term**: Scale to handle 1000+ concurrent users

---

**Last Updated**: January 11, 2026
**Status**: Feature-complete except for portfolio persistence bug
**Test Results**: Buy works ✓, Sell fails ✗ due to portfolio bug
