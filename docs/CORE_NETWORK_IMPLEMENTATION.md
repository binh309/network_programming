# Core & Network Layer Implementation

## Overview

The server follows a **3-Layer Architecture** designed to separate low-level I/O from high-level business logic. This separation ensures that the core event loop remains non-blocking and scalable.

### Architecture Layers

1.  **Layer 1: Network I/O** (`network/`) - Handles raw bytes and socket lifecycle.
2.  **Layer 2: Protocol** (`network/`) - Converts bytes to structured packets.
3.  **Layer 3: Orchestration** (`core/`) - Manages connections and coordinates data flow.

---

## 📂 Directory: `network/` (Layers 1 & 2)

This directory contains stateless utilities for handling TCP/IP communication and the binary protocol.

### 1. Socket Lifecycle (`socket_io.c`)
**Responsibility**: Wraps system calls for socket management.
- **`socket_io_create_server(port)`**: Creates a non-blocking TCP socket, binds it to the port, and starts listening.
- **`socket_io_accept_connection(listen_fd)`**: Accepts a new client connection.
- **`socket_io_set_nonblocking(fd)`**: Sets the `O_NONBLOCK` flag using `fcntl`, crucial for the epoll-based event loop.

### 2. Raw Data Transfer (`network_receive.c`, `network_send.c`)
**Responsibility**: Safe reading and writing of raw bytes.
- **`network_receive(fd, buffer, size)`**: 
  - Reads available bytes from the socket.
  - Handles `EAGAIN`/`EWOULDBLOCK` (no data currently available) gracefully.
  - Detects EOF (client disconnection).
- **`network_send(fd, buffer, length)`**: 
  - Ensures all bytes are sent, handling partial writes.
  - Uses a mutex (`send_mutex`) to ensure thread safety if multiple threads attempt to write to the same socket.

### 3. Protocol Handling (`packet_parser.c`, `packet_builder.c`)
**Responsibility**: Serialization and Deserialization.
- **`packet_parser_has_complete_message(buffer, size)`**: 
  - Peeks at the header to determine the expected packet length.
  - Returns the total size if a full packet is in the buffer, or 0 if incomplete.
- **`packet_parser_deserialize(buffer, packet)`**: 
  - Converts network byte order (Big Endian) to host byte order.
  - Extracts the header and body into a `packet_t` struct.
- **`packet_builder_serialize(packet, buffer)`**: 
  - Converts host byte order to network byte order.
  - Packs the struct into a contiguous byte array for transmission.

---

## 📂 Directory: `core/` (Layer 3)

This directory contains the stateful components that run the server application.

### 1. The Entry Point (`server.c`)
**Responsibility**: Initialization and Cleanup.
1. Initializes Databases (Account, Stock, Portfolio).
2. Initializes the **Connection Manager**.
3. Starts background threads (e.g., Market Simulation).
4. Initializes the **Portfolio Manager** (Persistence Layer).
5. Initializes and runs the **Event Loop**.
6. Handles shutdown and resource cleanup.

### 2. Event Loop (`event_loop.c`)
**Responsibility**: The "Heart" of the server.
- Uses `epoll` to monitor multiple file descriptors (sockets) simultaneously.
- **Flow**:
  1. Waits for events (`epoll_wait`).
  2. If event on **Listening Socket**: Calls `socket_io_accept_connection` and registers new client.
  3. If event on **Client Socket**: Delegates to `request_handler_process`.
  4. Handles disconnections (`EPOLLRDHUP`).

### 3. Connection Manager (`connection_manager.c`)
**Responsibility**: State tracking.
- Maintains a global array of `connection_t` structures.
- **`connection_t`** stores:
  - `client_socket`: The file descriptor.
  - `read_buffer`: A persistent buffer for handling partial packets (TCP fragmentation).
  - `user_id` / `is_logged_in`: Session state.
- Thread-safe access via `connections_mutex`.

### 4. Request Handler (`request_handler.c`)
**Responsibility**: The "Brain" that connects layers.
- Implements the processing pipeline for a single socket event:
  1. **Receive**: Calls `network_receive` to append data to the connection's buffer.
  2. **Check**: Loops calling `packet_parser_has_complete_message`.
  3. **Parse**: Calls `packet_parser_deserialize` for complete packets.
  4. **Dispatch**: Passes the parsed `packet_t` to `dispatcher_handle_message` (Feature Layer).
  5. **Buffer Management**: Shifts remaining bytes in the buffer to the front after processing.

### 5. Portfolio Manager (`portfolio_manager.c`)
**Responsibility**: Manages user portfolio lifecycle and persistence.
- **`portfolio_mgr_get_or_create(user_id)`**: 
  - Retrieves the existing portfolio for a user or creates a new one.
  - Ensures portfolios persist in memory across multiple requests (fixing the previous stateless bug).
  - Thread-safe access via `portfolio_lock`.
- **`portfolio_mgr_cleanup()`**: Handles cleanup of portfolio resources on server shutdown.

---

## 🔄 Data Flow Example

### Scenario: Client sends "BUY AAPL"

1.  **Network Layer (Hardware/OS)**
    - Bytes arrive at the network card and are buffered by the OS kernel.
    - `epoll` notifies `event_loop.c` that the socket is readable.

2.  **Core Layer (`event_loop.c`)**
    - Detects the event.
    - Calls `request_handler_process(fd)`.

3.  **Core Layer (`request_handler.c`)**
    - Calls `network_receive()`.
    - **`network_receive`** reads bytes into `conn->read_buffer`.
    - Calls `packet_parser_has_complete_message()`.
    - **`packet_parser`** sees a header saying "Length: 20".
    - If buffer has >= 25 bytes (5 header + 20 body), it returns true.
    - Calls `packet_parser_deserialize()`.
    - **`packet_parser`** returns a `packet_t` struct.
    - Calls `dispatcher_handle_message()`.

4.  **Feature Layer (`dispatcher.c` -> `buy_stock.c`)**
    - Validates logic.
    - Updates databases.
    - Creates a response `packet_t`.
    - Calls `send_packet()`.

5.  **Network Layer (`packet.c` -> `network_send.c`)**
    - **`packet_builder`** serializes response to bytes.
    - **`network_send`** writes bytes to the socket.

---

## 🛠 Key Implementation Details

### Non-Blocking I/O Handling
The server uses non-blocking sockets. This means `read()` and `write()` return immediately.
- **Read**: If no data is available, `errno` is set to `EAGAIN`. `network_receive` catches this and returns 0, telling the handler to wait for the next epoll event.
- **Write**: `network_send` loops until all data is sent. (Note: In a production HTTP server, partial writes would be buffered, but for this protocol, we assume atomic writes for simplicity or block briefly).

### Partial Packet Handling
TCP is a stream protocol, not a packet protocol. One `read()` might return:
- Half a packet.
- One and a half packets.
- Two packets.

**Solution in `request_handler.c`**:
```c
// 1. Append new data to existing buffer
conn->read_offset += bytes_read;

// 2. Loop while we have full packets
while (1) {
    size_t size = packet_parser_has_complete_message(buffer, offset);
    if (size == 0) break; // Wait for more data

    process_packet();

    // 3. Move remaining data to start of buffer
    memmove(buffer, buffer + size, remaining);
    offset -= size;
}
```

### Thread Safety
- **Connection Manager**: Protected by `connections_mutex`.
- **Sending**: Protected by `send_mutex` in `network_send.c` to prevent interleaved bytes if multiple threads try to send to the same socket (e.g., market update thread vs request handler thread).
- **Databases**: Each DB module (`account_db`, `stock_db`, etc.) manages its own internal mutex.
- **Portfolio Manager**: Protected by `portfolio_lock` to ensure atomic access to user portfolios.
