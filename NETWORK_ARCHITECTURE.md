# Network Architecture Documentation

## Overview

This document describes the core network architecture of the Stock Trading System. The system uses a **client-server model** with TCP/IP communication, supporting authenticated trading operations in real-time.

---

## Table of Contents

1. [Protocol Design](#protocol-design)
2. [Packet Structure](#packet-structure)
3. [Communication Flow](#communication-flow)
4. [Message Types](#message-types)
5. [Session Management](#session-management)
6. [Error Handling](#error-handling)

---

## Protocol Design

### Architecture Type
- **Model**: Client-Server TCP/IP
- **Socket Type**: SOCK_STREAM (TCP)
- **Buffering Strategy**: Combined header-payload sends to prevent TCP fragmentation
- **Concurrency**: Multi-threaded with epoll event multiplexing (server-side)

### Key Design Principles

1. **Atomic Messages**: Header and payload are combined into a single buffer before sending to prevent TCP from splitting them
2. **Request-Response Pairing**: Each client message gets a server response with matching `request_id`
3. **Stateful Sessions**: Server maintains session state per connected client
4. **Authentication Required**: Most operations require an authenticated session

---

## Packet Structure

### Packet Header (Binary Format)

```c
typedef struct {
    uint16_t request_id;    // 2 bytes - Unique message ID for request/response matching
    uint8_t type;           // 1 byte  - Message type (defined in protocol.h)
    uint16_t length;        // 2 bytes - Length of the message body
} packet_header_t;           // Total: 5 bytes
```

### Full Packet Format

```
┌─────────────────────────────────────────────────┐
│ Packet Header (5 bytes)                         │
├───────────────┬──────────┬──────────────────────┤
│ request_id(2) │ type(1)  │ length(2)            │
└───────────────┴──────────┴──────────────────────┘
                           ▼
┌─────────────────────────────────────────────────┐
│ Packet Body (variable, up to 4096 bytes)        │
│ (Structure depends on message type)             │
└─────────────────────────────────────────────────┘
```

### Sending Mechanism (Critical Pattern)

**CORRECT** - Atomic send:
```c
// Combine header and body into single buffer
char buffer[sizeof(packet_header_t) + sizeof(payload)];
memcpy(buffer, &header, sizeof(packet_header_t));
memcpy(buffer + sizeof(packet_header_t), &payload, sizeof(payload));
send(sockfd, buffer, sizeof(buffer), 0);
```

**INCORRECT** - Split sends (causes TCP buffering issues):
```c
// This can cause TCP to split the message across packets!
send(sockfd, &header, sizeof(packet_header_t), 0);
send(sockfd, &payload, sizeof(payload), 0);
```

---

## Communication Flow

### Request-Response Cycle

```
┌────────────────────────────────────────────────┐
│ Client                                         │
└─────────────┬──────────────────────────────────┘
              │
              │ 1. Create Request Packet
              │    - Generate unique request_id
              │    - Set message type (e.g., CMSG_BUY_STOCK)
              │    - Fill payload with operation data
              │
              ├─────────────────────────────────►
              │    Send atomic packet (header + body)
              │
┌─────────────▼──────────────────────────────────┐
│ Server                                         │
├────────────────────────────────────────────────┤
│ 1. Receive packet via epoll event loop        │
│ 2. Parse header to get message type           │
│ 3. Route to appropriate handler (dispatcher)  │
│ 4. Handler:                                    │
│    - Validate session authentication          │
│    - Process request                          │
│    - Generate response with same request_id   │
│ 5. Send response packet atomically            │
└─────────────┬──────────────────────────────────┘
              │
              │ 2. Response Packet
              │    (request_id matches request)
              │
              ◄─────────────────────────────────
              │    Receive response
              │
┌─────────────▼──────────────────────────────────┐
│ Client                                         │
│ 1. Match response to request by request_id    │
│ 2. Parse response body                        │
│ 3. Display results to user                    │
└────────────────────────────────────────────────┘
```

### Server Event Loop

```
┌──────────────────────────────────┐
│ Main Server Loop (epoll)         │
├──────────────────────────────────┤
│ 1. Wait for events (epoll_wait)  │
│                                  │
│ 2. For each event:               │
│    ├─ New connection:            │
│    │  └─ Accept & add to epoll   │
│    │                             │
│    └─ Data available:            │
│       ├─ Receive packet header   │
│       ├─ Receive packet body     │
│       ├─ Dispatch to handler     │
│       └─ Send response           │
│                                  │
│ 3. Loop back to epoll_wait       │
└──────────────────────────────────┘
```

---

## Message Types

### Client → Server Messages (Request)

| Type | Hex | Purpose | Requires Auth |
|------|-----|---------|---|
| CMSG_REGISTER | 0x01 | Create new account | No |
| CMSG_LOGIN | 0x02 | Authenticate user | No |
| CMSG_LOGOUT | 0x03 | End session | Yes |
| CMSG_VIEW_STOCKS | 0x10 | List available stocks | Yes |
| CMSG_BUY_STOCK | 0x11 | Purchase shares | Yes |
| CMSG_SELL_STOCK | 0x12 | Sell shares | Yes |
| CMSG_VIEW_MY_STOCKS | 0x13 | See portfolio | Yes |
| CMSG_SEE_BALANCE | 0x14 | Check account balance | Yes |

### Server → Client Messages (Response)

| Type | Hex | Success For | Contains |
|------|-----|-------------|----------|
| SMSG_REGISTER_SUCCESS | 0x81 | Register | User ID, confirmation |
| SMSG_REGISTER_FAIL | 0x82 | Register | Error message |
| SMSG_LOGIN_SUCCESS | 0x83 | Login | User ID, username |
| SMSG_LOGIN_FAIL | 0x84 | Login | Error message |
| SMSG_LOGOUT_SUCCESS | 0x85 | Logout | Confirmation |
| SMSG_VIEW_STOCKS_DATA | 0x90 | View Stocks | Stock list |
| SMSG_VIEW_STOCKS_FAIL | 0x91 | View Stocks | Error message |
| SMSG_BUY_STOCK_SUCCESS | 0x92 | Buy | Order ID, confirmation |
| SMSG_BUY_STOCK_FAIL | 0x93 | Buy | Error message |
| SMSG_SELL_STOCK_SUCCESS | 0x94 | Sell | Order ID, confirmation |
| SMSG_SELL_STOCK_FAIL | 0x95 | Sell | Error message |
| SMSG_VIEW_MY_STOCKS_DATA | 0x96 | View Portfolio | Holdings |
| SMSG_VIEW_MY_STOCKS_FAIL | 0x97 | View Portfolio | Error message |
| SMSG_SEE_BALANCE_DATA | 0x98 | See Balance | Current balance |
| SMSG_SEE_BALANCE_FAIL | 0x99 | See Balance | Error message |
| SMSG_ERROR | 0xFF | Any | Generic error |

---

## Session Management

### Session Lifecycle

```
┌─────────────────────────────────┐
│ 1. New Connection               │
│    - Client connects via TCP    │
│    - Socket accepted by server  │
│    - New session created        │
│    - is_logged_in = false       │
└──────────────┬──────────────────┘
               │
               ▼
┌─────────────────────────────────┐
│ 2. Authentication               │
│    - Client sends LOGIN request │
│    - Server verifies credentials│
│    - user_id stored in session  │
│    - username stored in session │
│    - is_logged_in = true        │
└──────────────┬──────────────────┘
               │
               ▼
┌─────────────────────────────────┐
│ 3. Active Session               │
│    - User can execute commands  │
│    - Each request checks auth   │
│    - Session data persists      │
│    - Operations recorded        │
└──────────────┬──────────────────┘
               │
               ▼
┌─────────────────────────────────┐
│ 4. Logout                       │
│    - Client sends LOGOUT        │
│    - Session cleared            │
│    - is_logged_in = false       │
│    - Can login again if needed  │
└──────────────┬──────────────────┘
               │
               ▼
┌─────────────────────────────────┐
│ 5. Disconnection                │
│    - Connection closed          │
│    - Session cleaned up         │
│    - Resources released         │
└─────────────────────────────────┘
```

### Session Data Structure

```c
typedef struct {
    int client_socket;          // Socket file descriptor
    uint32_t user_id;           // Unique user identifier
    char username[32];          // Username (for logging)
    bool is_logged_in;          // Authentication flag
    char read_buffer[4096];     // Receive buffer
    int read_offset;            // Position in buffer
} session_t;
```

### Authentication Flow

```
Client                              Server
  │                                   │
  ├──────── CMSG_LOGIN ──────────────►│
  │   (username: "admin")             │
  │   (password: "password")          │
  │                                   │
  │                                   ├─ Verify credentials
  │                                   │  against accounts.txt
  │                                   │
  │◄────── SMSG_LOGIN_SUCCESS ────────┤
  │   (user_id: 1)                    │
  │   (is_logged_in: true)            │
  │                                   │
  │  [Session now authenticated]      │
  │                                   │
  ├──── CMSG_VIEW_STOCKS ────────────►│
  │  [Requires auth]                  │
  │                                   ├─ Check session.is_logged_in
  │                                   │
  │◄──── SMSG_VIEW_STOCKS_DATA ──────┤
  │                                   │
```

---

## Error Handling

### Error Response Structure

All error responses follow this pattern:

```c
typedef struct {
    uint8_t status;           // 0 = success, 1 = failure
    char error_message[256];  // Human-readable error
} error_response_t;
```

### Common Error Scenarios

#### 1. Authentication Errors

| Scenario | Error Message |
|----------|---------------|
| Not logged in | "User not authenticated. Please login." |
| Invalid credentials | "Invalid username or password." |
| Username exists | "Username already exists." |

#### 2. Stock Trading Errors

| Scenario | Error Message |
|----------|---------------|
| Stock not found | "Stock not found." |
| Insufficient balance | "Insufficient balance for this transaction." |
| Insufficient holdings | "You don't own enough shares to sell." |
| Stock unavailable | "Not enough shares available." |

#### 3. Network Errors

| Scenario | Handling |
|----------|----------|
| Connection timeout | Client reconnects |
| Packet malformed | Discard packet, log error |
| Buffer overflow | Reject request, send error |

### Server-Side Error Validation Pattern

```c
// 1. Authentication check
if (!session || !session->is_logged_in) {
    send_error(client_socket, "User not authenticated");
    return;
}

// 2. Parameter validation
if (stock_id < 1 || quantity == 0) {
    send_error(client_socket, "Invalid parameters");
    return;
}

// 3. Business logic validation
if (account_balance < required_amount) {
    send_error(client_socket, "Insufficient balance");
    return;
}

// 4. If all checks pass, process request
// ... perform operation ...
send_success(client_socket, result);
```

---

## Data Flow Diagram: Buy Stock Example

```
User enters: "buy AAPL 5 150.50"

┌──────────────────────────────────────────┐
│ Client                                   │
├──────────────────────────────────────────┤
│ 1. Parse input                           │
│ 2. Create buy request packet             │
│    - stock_id: 1 (AAPL)                  │
│    - quantity: 5                         │
│    - price: 150.50                       │
│ 3. Add to header:                        │
│    - request_id: 42 (unique)             │
│    - type: CMSG_BUY_STOCK (0x11)         │
│    - length: sizeof(payload)             │
│ 4. Combine header + payload              │
│ 5. Send atomically                       │
└────────────────┬──────────────────────────┘
                 │
                 │ [Network transmission]
                 │ [TCP may buffer/split]
                 │
┌────────────────▼──────────────────────────┐
│ Server (Dispatcher)                      │
├──────────────────────────────────────────┤
│ 1. Receive complete packet               │
│ 2. Parse header → type = 0x11            │
│ 3. Route to handle_buy_stock_request()   │
└────────────────┬──────────────────────────┘
                 │
                 ▼
┌──────────────────────────────────────────┐
│ Buy Stock Handler                        │
├──────────────────────────────────────────┤
│ 1. Get session from socket               │
│ 2. Validate: User authenticated?         │
│ 3. Validate: Stock exists?               │
│ 4. Validate: Enough balance? ($752.50)   │
│ 5. Validate: Stock available? (1000)     │
│ 6. Update database:                      │
│    - Deduct balance                      │
│    - Add holding to portfolio            │
│    - Decrement stock quantity            │
│    - Record transaction                  │
│ 7. Create response packet:               │
│    - status: SUCCESS (0)                 │
│    - order_id: 1001                      │
│ 8. Match request_id: 42                  │
│ 9. Combine & send atomically             │
└────────────────┬──────────────────────────┘
                 │
                 │ [Network transmission]
                 │
┌────────────────▼──────────────────────────┐
│ Client (Receive Handler)                 │
├──────────────────────────────────────────┤
│ 1. Receive response                      │
│ 2. Parse header → request_id = 42 ✓      │
│ 3. Parse body → status = SUCCESS         │
│ 4. Display: "Order #1001 executed"       │
│ 5. Update local balance display          │
└──────────────────────────────────────────┘
```

---

## Threading & Synchronization

### Server Threading Model

```
┌─────────────────────────────────────┐
│ Main Thread                         │
├─────────────────────────────────────┤
│ - Listen for connections            │
│ - Create epoll instance             │
│ - Wait for socket events            │
│ - Dispatch to handler functions     │
│ - No blocking I/O                   │
└─────────────────────────────────────┘
```

### Mutex-Protected Resources

```
Resource                    Mutex
────────────────────────────────────
portfolio_db                portfolio_mutex
transaction_db              transaction_mutex
account_db                  account_mutex
stock_db                    stock_mutex
session_manager             session_mutex
```

### Thread Safety Pattern

```c
// Protected read
pthread_mutex_lock(&resource_mutex);
data = read_resource();
pthread_mutex_unlock(&resource_mutex);

// Protected write
pthread_mutex_lock(&resource_mutex);
update_resource(new_data);
pthread_mutex_unlock(&resource_mutex);
```

---

## Performance Considerations

### Optimizations Implemented

1. **Atomic Sends**: Combined header+payload reduces TCP overhead
2. **Connection Pooling**: Persistent TCP connections, no reconnect overhead
3. **Epoll Multiplexing**: Efficient event handling for many concurrent connections
4. **Buffer Reuse**: Session buffers reused for multiple requests
5. **Mutex Granularity**: Fine-grained locks for concurrent access

### Limits

| Parameter | Value | Impact |
|-----------|-------|--------|
| MAX_SESSIONS | 100 | Maximum concurrent clients |
| MAX_BODY_LEN | 4096 | Max message body size |
| BUFFER_SIZE | 4096 | Per-session read buffer |

---

## Security Considerations

### Current Implementation

1. **Authentication**: Username/password verification required for trading
2. **Session Isolation**: Each client gets isolated session context
3. **Input Validation**: All parameters validated before processing
4. **Account Protection**: Balance changes require valid user_id

### Recommendations for Production

1. **Encryption**: Add TLS/SSL for secure transmission
2. **Token-based Auth**: Replace username/password with JWT
3. **Rate Limiting**: Prevent DoS attacks
4. **Audit Logging**: Log all transactions with timestamps
5. **Database Encryption**: Encrypt sensitive data at rest

---

## References

- [Buy Stock Feature](FEATURE_BUY_STOCK.md)
- [Sell Stock Feature](FEATURE_SELL_STOCK.md)
- [Session Management](FEATURE_SESSION_MANAGEMENT.md)
- [Portfolio Management](FEATURE_PORTFOLIO_MANAGEMENT.md)
