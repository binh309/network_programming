# Core & Network Layer File Reference

This document provides a detailed explanation of every file in the `core/` and `network/` directories, describing their specific responsibilities within the 3-Layer Architecture.

---

## 📂 Core Layer (`server_folder/core/`)
**Responsibility:** Orchestration, State Management, and Event Coordination.

### 1. `server.c` (The Entry Point)
- **Role:** Application bootstrapper.
- **What it does:**
  - Initializes all databases (Account, Stock, Portfolio).
  - Initializes the Connection Manager and Portfolio Manager.
  - Starts background threads (e.g., Market Simulation).
  - Creates the Event Loop context.
  - Runs the main loop (`event_loop_run`).
  - Handles cleanup on shutdown.

### 2. `event_loop.c` (The Heart)
- **Role:** Asynchronous event multiplexer.
- **What it does:**
  - Wraps the Linux `epoll` system call.
  - Monitors the listening socket for new connections.
  - Monitors client sockets for incoming data or disconnections.
  - **State Validation:** Checks if a connection is in a valid state (e.g., `CONN_READY`) before processing events.
  - Delegates actual processing to `socket_io` (for accepts) or `request_handler` (for data).

### 3. `request_handler.c` (The Brain)
- **Role:** Processing pipeline for a single request.
- **What it does:**
  - **Step 1:** Calls `network_receive` to read raw bytes from the socket.
  - **Step 2:** Acquires a **per-connection mutex** to ensure thread safety.
  - **Step 3:** Appends data to the connection's internal buffer.
  - **Step 4:** Uses `packet_parser` to check if a full message has arrived.
  - **Step 5:** Deserializes the packet and calls the `dispatcher` (Feature Layer).
  - **Step 6:** Consumes the processed bytes from the buffer.
  - Handles DoS protection by detecting oversized packets.

### 4. `connection_manager.c` (The State Keeper)
- **Role:** Manages the lifecycle and state of active connections.
- **What it does:**
  - Allocates and frees `connection_t` structures.
  - Maintains the global list of active clients.
  - **State Machine:** Manages transitions (e.g., `ACCEPTING` → `READY` → `PROCESSING`).
  - **Buffering:** Manages the read buffer for TCP fragmentation handling.
  - Integrates with `connection_hash` for fast lookups.

### 5. `connection_hash.c` (The Accelerator)
- **Role:** O(1) Lookup optimization.
- **What it does:**
  - Implements a hash table mapping Socket File Descriptors (FD) to `connection_t` pointers.
  - Replaces the old O(n) linear search, making the server scalable to thousands of connections.
  - Handles collisions via chaining.

### 6. `portfolio_manager.c` (The Persistence Layer)
- **Role:** Manages user portfolios across requests.
- **What it does:**
  - Solves the "stateless" bug where portfolios were lost between requests.
  - Maintains a global registry of loaded portfolios.
  - Ensures thread-safe access to user holdings via `portfolio_lock`.
  - Loads/Saves portfolios to disk (persistence).

### 7. `session_manager.c` (Deprecated)
- **Role:** Legacy file.
- **Status:** Empty/Deprecated. Functionality has been merged into `connection_manager.c`.

---

## 📂 Network Layer (`server_folder/network/`)
**Responsibility:** I/O, Protocol Parsing, and Serialization.

### 1. `socket_io.c` (The Hardware Interface)
- **Role:** Low-level socket wrapper.
- **What it does:**
  - Calls system functions: `socket()`, `bind()`, `listen()`, `accept()`.
  - Sets sockets to **Non-Blocking Mode** (`O_NONBLOCK`) using `fcntl`.
  - This is the only file that touches the OS socket API directly for creation/acceptance.

### 2. `network_receive.c` (The Reader)
- **Role:** Safe wrapper for `read()`.
- **What it does:**
  - Reads bytes from a non-blocking socket.
  - Translates OS errors (`EAGAIN`, `EWOULDBLOCK`) into internal status codes (`RECV_NO_DATA`).
  - Detects `EOF` (client disconnection).

### 3. `network_send.c` (The Writer)
- **Role:** Safe wrapper for `write()`.
- **What it does:**
  - Writes bytes to a socket.
  - Handles partial writes (loops until all data is sent).
  - Uses a **mutex** (`send_mutex`) to prevent multiple threads from writing to the same socket simultaneously (interleaved bytes).

### 4. `packet_parser.c` (The Decoder)
- **Role:** Bytes → Struct.
- **What it does:**
  - **Validation:** Checks if the buffer contains a full packet header.
  - **Security:** Checks if the declared packet size exceeds `MAX_PACKET_SIZE` (DoS protection).
  - **Deserialization:** Converts raw network bytes (Big Endian) into a C struct (`packet_t`).

### 5. `packet_builder.c` (The Encoder)
- **Role:** Struct → Bytes.
- **What it does:**
  - **Serialization:** Converts a C struct (`packet_t`) into a raw byte stream.
  - Handles Network Byte Order conversion (`htons`, `htonl`).
  - Ensures the output buffer is large enough.

### 6. `packet.c` (The Convenience Wrapper)
- **Role:** High-level packet utility.
- **What it does:**
  - Combines `packet_builder` and `network_send`.
  - Provides `send_packet()`: Takes a struct, serializes it, and sends it in one go.
  - Provides `create_packet()`: Helper to populate struct fields.

### 7. `network_errors.h` (The Contract)
- **Role:** Error definitions.
- **What it does:**
  - Defines enums like `RECV_EOF`, `PARSE_SIZE_EXCEEDED`.
  - Ensures all layers speak the same error language, removing ambiguity (e.g., does -1 mean error or closed?).

### 8. `protocol.h` (The Dictionary)
- **Role:** Protocol definitions.
- **What it does:**
  - Defines the binary packet structure (`packet_header_t`).
  - Defines Message Types (e.g., `CMSG_BUY_STOCK = 0x11`).
  - Defines constants like `MAX_BODY_LEN`.

---

## 🔄 How They Work Together (The Flow)

When a client sends a "Buy Stock" command:

1.  **OS/Hardware**: Data arrives at NIC, kernel buffers it.
2.  **`event_loop.c`**: `epoll_wait` wakes up, sees data on socket. Calls `request_handler`.
3.  **`request_handler.c`**:
    *   Calls **`network_receive.c`** to get bytes.
    *   Locks the connection via **`connection_manager.c`**.
    *   Calls **`packet_parser.c`** to check for full packet & security.
    *   Calls **`packet_parser.c`** to deserialize bytes to `packet_t`.
    *   Unlocks and calls `dispatcher`.
4.  **`dispatcher.c`** (Feature): Routes to `buy_stock.c`.
5.  **`buy_stock.c`** (Feature):
    *   Checks logic, updates DB.
    *   Creates response packet using **`packet.c`**.
6.  **`packet.c`**:
    *   Calls **`packet_builder.c`** to serialize response.
    *   Calls **`network_send.c`** to push bytes to wire.

---

## 📊 Summary of Improvements (vs Old Code)

| Feature | Old Implementation | New Implementation | File Responsible |
| :--- | :--- | :--- | :--- |
| **Lookup** | O(n) Loop | O(1) Hash Table | `connection_hash.c` |
| **Safety** | Global Lock | Per-Connection Mutex | `connection_manager.c` |
| **Security** | None | Size Validation | `packet_parser.c` |
| **State** | Implicit | Explicit State Machine | `connection_manager.c` |
| **Persistence**| None (Buggy) | Persistent Manager | `portfolio_manager.c` |
| **Errors** | Ambiguous ints | Explicit Enums | `network_errors.h` |