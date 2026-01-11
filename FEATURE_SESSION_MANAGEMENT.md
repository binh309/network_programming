# Session Management Documentation

## Overview

Session Management maintains authenticated user context across multiple requests in a single TCP connection. It tracks which user is logged in on each client connection and enforces authentication requirements for trading operations.

---

## Feature Summary

| Property | Value |
|----------|-------|
| **Purpose** | Track authenticated users per TCP connection |
| **Scope** | Per-connection basis (socket → user_id mapping) |
| **Lifetime** | From login to logout/disconnect |
| **Max Sessions** | 100 concurrent clients |
| **Thread Safety** | Mutex-protected |

---

## Session Lifecycle

```
┌──────────────────────────────────┐
│ 1. Connection Established        │
│    - Client connects to server   │
│    - New session created         │
│    - Default state: logged out   │
└──────────────┬───────────────────┘
               │
    ┌──────────▼──────────┐
    │ Not logged in       │◄──────┐
    │ is_logged_in=false  │       │
    │ user_id=0           │       │
    └──────────┬──────────┘       │
               │                  │
               │ Client sends     │
               │ LOGIN request    │
               │                  │
┌──────────────▼───────────────────────────┐
│ 2. Authentication Processing            │
│    - Verify username/password           │
│    - Check accounts.txt                 │
│    - Load user_id and username          │
└──────────────┬───────────────────────────┘
               │
       ┌───────┴─────────┐
       │                 │
       ▼ Invalid         ▼ Valid
    [FAIL]        [SUCCESS]
    │              │
    │              ├─→ Set is_logged_in=true
    │              ├─→ Set user_id
    │              ├─→ Set username
    │              └─→ Store in session[]
    │
    └──────────────┐
                   │
                   ▼
        ┌─────────────────────┐
        │ Authenticated       │
        │ is_logged_in=true   │◄──────┐
        │ user_id > 0         │       │
        │ username="admin"    │       │
        └─────────┬───────────┘       │
                  │                   │
                  │ Can execute       │
                  │ any command       │
                  │                   │
                  ├─ View stocks ──┐  │
                  ├─ Buy stock ────┤  │
                  ├─ Sell stock ───┤  │
                  ├─ Check balance─┤  │
                  └─ View holdings─┤  │
                                   │  │
                  ┌────────────────┘  │
                  │                   │
                  ▼ User sends        │
               LOGOUT                 │
                  │                   │
        ┌─────────▼────────────────────┘
        │ 3. Logout
        │    - Clear session data
        │    - is_logged_in = false
        │    - user_id = 0
        │
        └──────► Back to "Not logged in"
                  (can login again)

        ┌─────────────────────────────┐
        │ 4. Connection Closed        │
        │    - Client disconnects     │
        │    - Session cleaned up     │
        │    - Resources freed        │
        │    - Socket closed          │
        └─────────────────────────────┘
```

---

## Session Data Structure

```c
typedef struct {
    int client_socket;          // Socket file descriptor
    uint32_t user_id;           // Unique user ID from accounts
    char username[32];          // Username for logging
    bool is_logged_in;          // Authentication flag
    char read_buffer[4096];     // Buffer for receiving messages
    int read_offset;            // Current position in buffer
} session_t;
```

### Field Descriptions

| Field | Type | Purpose | Set By | Used By |
|-------|------|---------|--------|---------|
| client_socket | int | Socket FD | socket accept | all commands |
| user_id | uint32 | User identifier | login handler | account_db, portfolio_db |
| username | char[32] | User name | login handler | logging only |
| is_logged_in | bool | Auth status | login/logout | all feature handlers |
| read_buffer | char[4096] | Message buffer | receive handler | packet parsing |
| read_offset | int | Buffer position | receive handler | partial message handling |

---

## Session Manager API

### Initialization

```c
int session_mgr_init(void);
```

**Purpose**: Initialize session manager at server startup
**Returns**: 0 on success, -1 on failure
**Side Effects**: Allocates mutex, initializes session array

### Shutdown

```c
void session_mgr_destroy(void);
```

**Purpose**: Clean up session manager at server shutdown
**Side Effects**: Destroys mutex, frees all sessions

### Add New Session

```c
session_t* session_mgr_add(int client_socket);
```

**Purpose**: Create new session for incoming client
**Parameter**: Socket file descriptor from accept()
**Returns**: Pointer to new session_t or NULL if full
**Initializes**:
- is_logged_in = false
- user_id = 0
- username = ""
- read_offset = 0

### Get Existing Session

```c
session_t* session_mgr_get(int client_socket);
```

**Purpose**: Retrieve session by socket
**Parameter**: Client socket FD
**Returns**: Pointer to session_t or NULL if not found
**Usage**: Called by every command handler to verify authentication

### Logout Session

```c
void session_mgr_logout(int client_socket);
```

**Purpose**: Clear session on logout or disconnect
**Parameter**: Client socket FD
**Side Effects**: Resets session to logged-out state

### Authenticate Session

```c
int session_mgr_authenticate(int client_socket, uint32_t user_id, 
                             const char* username);
```

**Purpose**: Mark session as authenticated after login
**Parameters**:
- client_socket: Socket FD
- user_id: User ID from accounts.txt
- username: Username for logging
**Returns**: 0 on success, -1 if session not found

---

## Authentication Flow

### Login Request Processing

```
┌─────────────────────────────────────┐
│ Client sends: CMSG_LOGIN            │
│  payload: {username, password}      │
└──────────────┬──────────────────────┘
               │
               ▼
┌─────────────────────────────────────┐
│ Server receives packet              │
│ Routes to login handler             │
└──────────────┬──────────────────────┘
               │
               ▼
┌─────────────────────────────────────┐
│ Login Handler:                      │
│ 1. Get session by socket_fd         │
│    session = session_mgr_get(fd)    │
└──────────────┬──────────────────────┘
               │
       ┌───────▼────────┐
       │ Session found? │
       └───┬────────┬───┘
           │        │
         NO       YES
         │         │
    [ERROR]       │
                  ▼
        ┌──────────────────────────┐
        │ 2. Verify credentials   │
        │    call account_db_get()│
        └──────────┬───────────────┘
                   │
           ┌───────▼────────┐
           │ Creds valid?   │
           └───┬────────┬───┘
               │        │
             NO       YES
             │         │
        [FAIL]         │
                       ▼
            ┌────────────────────────┐
            │ 3. Mark authenticated │
            │    session_mgr_       │
            │    authenticate()     │
            │    - user_id = X      │
            │    - username = name  │
            │    - is_logged_in=true│
            └────────┬──────────────┘
                     │
                     ▼
            ┌────────────────────────┐
            │ 4. Send success        │
            │    SMSG_LOGIN_SUCCESS  │
            │    with user_id        │
            └────────────────────────┘
```

### Authorization Check Pattern

All command handlers use this pattern:

```c
void handle_command_request(int client_socket, 
                           const packet_t* request) {
    // 1. Get session
    session_t* session = session_mgr_get(client_socket);
    
    // 2. Check authentication
    if (!session || !session->is_logged_in) {
        // Not authenticated - reject with error
        send_error(client_socket, "Not authenticated");
        return;
    }
    
    // 3. Now safe to use session->user_id
    uint32_t user_id = session->user_id;
    
    // 4. Continue with request processing
    // ... perform operation using user_id ...
}
```

---

## Thread Safety

### Mutex Protection

```c
pthread_mutex_t session_mutex = PTHREAD_MUTEX_INITIALIZER;

// Operations on session array protected by mutex
pthread_mutex_lock(&session_mutex);
    session_t* s = session_mgr_get(fd);  // Read
    session_mgr_authenticate(fd, id, name);  // Write
pthread_mutex_unlock(&session_mutex);
```

### Race Condition Prevention

**Scenario**: Multiple threads handle requests from same client

```
Thread 1: Read session->is_logged_in    ← Should be locked!
Thread 2: Modify session (logout)
Result: Thread 1 uses stale data!

Solution: Lock entire read-modify-write sequence
```

**Correct Implementation**:
```c
pthread_mutex_lock(&session_mutex);
    session = session_mgr_get(fd);       // Locked read
    if (!session->is_logged_in) {        // Locked read
        send_error(...);
    }
pthread_mutex_unlock(&session_mutex);
```

---

## Session Storage

### Internal Data Structure

```c
#define MAX_SESSIONS 100

static session_t sessions[MAX_SESSIONS];
static pthread_mutex_t session_mutex;
```

### Session Lookup (Linear Search)

```c
session_t* session_mgr_get(int client_socket) {
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (sessions[i].client_socket == client_socket) {
            return &sessions[i];
        }
    }
    return NULL;  // Not found
}
```

**Performance**: O(n) linear search
**Limitation**: Works well for 100 sessions, would need hash table for 1000+

---

## Integration with Commands

### Command Handler Pattern

Every command follows this structure:

```c
void handle_command_request(int client_socket, const packet_t* request) {
    // 1. Retrieve session
    session = session_mgr_get(client_socket);
    
    // 2. Validate authentication
    if (!session || !session->is_logged_in) {
        ERROR("Not authenticated");
        return;
    }
    
    // 3. Get authenticated user_id
    user_id = session->user_id;
    
    // 4. Execute command with user_id
    result = execute_command(user_id, request_data);
    
    // 5. Send response
    send_response(client_socket, result);
}
```

### Example: Buy Stock

```
Client command:    "buy AAPL 5 150.50"
       ↓
Socket event:      Data available on client_socket
       ↓
Handler:           handle_buy_stock_request(client_socket, packet)
       ↓
Auth check:        session = session_mgr_get(client_socket)
                   if (!session->is_logged_in) REJECT
       ↓
Get user:          user_id = session->user_id (e.g., 1)
       ↓
Process:           Buy 5 AAPL for user_id=1
                   ├─ Check balance for user 1
                   ├─ Add to portfolio for user 1
                   ├─ Record transaction for user 1
       ↓
Response:          Send success with order_id
```

---

## Session Cleanup

### On Logout

```c
void handle_logout_request(int client_socket) {
    session_mgr_logout(client_socket);
    // Session is now:
    // - is_logged_in = false
    // - user_id = 0
    // - username = ""
    // Socket remains connected, client can login again
}
```

### On Disconnect

```c
// In main event loop:
if (event indicates disconnect) {
    session_mgr_logout(client_socket);
    close(client_socket);
}
```

---

## Multi-Connection Example

### Scenario: 3 Concurrent Clients

```
┌──────────────────────────────────────────┐
│ sessions[] array                         │
├──────────────────────────────────────────┤
│ [0] client_socket=4, user_id=1 (admin)   │
│     is_logged_in=true, username="admin"  │
│                                          │
│ [1] client_socket=5, user_id=0          │
│     is_logged_in=false, username=""      │
│                                          │
│ [2] client_socket=6, user_id=2 (alice)   │
│     is_logged_in=true, username="alice"  │
│                                          │
│ ...                                      │
│ [99] (empty)                             │
└──────────────────────────────────────────┘

Client Connections:
┌─────────────────┐     ┌─────────────────┐     ┌──────────────────┐
│ Client socket 4 │     │ Client socket 5 │     │ Client socket 6  │
├─────────────────┤     ├─────────────────┤     ├──────────────────┤
│ Logged in as    │     │ Not logged in   │     │ Logged in as     │
│ user_id=1       │     │ (new connection)│     │ user_id=2        │
│ (admin)         │     │                 │     │ (alice)          │
│                 │     │                 │     │                  │
│ Can execute:    │     │ Can execute:    │     │ Can execute:     │
│ - buy           │     │ - login         │     │ - buy            │
│ - sell          │     │ - register      │     │ - sell           │
│ - balance       │     │                 │     │ - balance        │
│ - logout        │     │                 │     │ - logout         │
└─────────────────┘     └─────────────────┘     └──────────────────┘

Each client operates independently with isolated session!
```

---

## Security Considerations

### Current Security

1. ✅ **Authentication Required**: Trading needs login
2. ✅ **Session Isolation**: Each socket gets own session
3. ✅ **Logout Support**: Can clear session
4. ⚠️ **No Timeout**: Sessions never expire
5. ⚠️ **No Encryption**: Password sent in plaintext

### Recommended Improvements

1. **Session Timeout**: Logout after 30 mins of inactivity
2. **Password Hashing**: Use bcrypt instead of plaintext
3. **TLS/SSL**: Encrypt all network traffic
4. **Token Auth**: Replace username/password with JWT
5. **Rate Limiting**: Limit login attempts
6. **Audit Log**: Track all authentications

---

## Common Issues

### Issue 1: "Not authenticated" Error

**Symptom**: User gets "not authenticated" error even after login

**Cause**: Session not found (wrong socket) or is_logged_in=false

**Diagnosis**:
```c
// Check if session exists
session = session_mgr_get(fd);
if (!session) printf("Session not found!\n");

// Check authentication status
if (!session->is_logged_in) printf("Not logged in!\n");
```

**Solution**: Ensure socket FD is correct and login completed successfully

### Issue 2: Multiple Users on Same Socket

**Symptom**: Two users' transactions mixed up on one connection

**Cause**: Session object reused without proper logout

**Prevention**: Always logout before login with different user

### Issue 3: Memory Leaks

**Symptom**: Memory usage grows over time

**Cause**: Sessions not freed on disconnect

**Prevention**: Call session_mgr_logout() for every disconnection

---

## Testing Sessions

### Test 1: Single User Login

```
Client:    "login admin password"
Server:    Verify in accounts.txt
Response:  SMSG_LOGIN_SUCCESS (user_id=1)
Session:   session[?].is_logged_in = true
Check:     Next command should work
```

### Test 2: Multiple Concurrent Users

```
Client 1:  "login admin password"    → user_id=1 ✓
Client 2:  "login alice password123" → user_id=2 ✓
Client 1:  "balance"                 → Shows admin's balance ✓
Client 2:  "balance"                 → Shows alice's balance ✓
(Each sees their own data)
```

### Test 3: Logout and Relogin

```
Client:    "login admin password"  → Success ✓
Client:    "balance"               → $10,000 ✓
Client:    "logout"                → Cleared ✓
Client:    "balance"               → Error: "Not authenticated" ✓
Client:    "login admin password"  → Success again ✓
```

### Test 4: Unauthorized Access

```
Client:    (don't login)
Client:    "buy AAPL 5 100"         → Error: "Not authenticated" ✓
Client:    "balance"                → Error: "Not authenticated" ✓
```

---

## Performance

### Current Implementation

| Operation | Time | Notes |
|-----------|------|-------|
| session_mgr_get() | O(n) | Linear search through 100 sessions |
| session_mgr_authenticate() | O(1) | Direct array write |
| Lookup w/ 100 sessions | < 1μs | Negligible |
| Lookup w/ 10,000 sessions | ~100μs | Starting to slow |

### Optimization Opportunity

```c
// Current: O(n) linear search
session_t* session_mgr_get(int client_socket) {
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (sessions[i].client_socket == client_socket) {
            return &sessions[i];
        }
    }
    return NULL;
}

// Optimized: O(1) hash table
// Use socket FD as index in hash table
```

---

## References

- [Buy Stock Feature](FEATURE_BUY_STOCK.md) - Uses session for auth
- [Sell Stock Feature](FEATURE_SELL_STOCK.md) - Uses session for auth
- [Network Architecture](NETWORK_ARCHITECTURE.md) - Overall system design
