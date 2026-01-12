#ifndef EVENT_LOOP_H
#define EVENT_LOOP_H

/**
 * @brief Event loop context
 *
 * Encapsulates the epoll multiplexer and related state
 */
typedef struct {
    int epoll_fd;       // epoll file descriptor
    int listen_fd;      // Server listening socket
    int port;           // Port number for logging/debugging
    volatile int running;  // Whether event loop is running
} event_loop_ctx_t;

/**
 * @brief Initialize the event loop
 *
 * Creates server socket, sets up epoll, accepts first connections
 * Runs synchronously (blocks until event_loop_shutdown called)
 *
 * @param port The port to listen on
 * @return Pointer to event_loop_ctx_t on success, NULL on error
 */
event_loop_ctx_t* event_loop_init(int port);

/**
 * @brief Run the event loop
 *
 * Main loop: epoll_wait → handle events → process requests
 * Blocks until shutdown is called
 *
 * @param loop The event loop context
 */
void event_loop_run(event_loop_ctx_t* loop);

/**
 * @brief Shutdown the event loop
 *
 * Stops the loop, closes sockets, frees memory
 *
 * @param loop The event loop context
 */
void event_loop_shutdown(event_loop_ctx_t* loop);

/**
 * @brief Single step of the event loop
 *
 * Process events once with given timeout (for threaded operation)
 *
 * @param loop The event loop context
 * @param timeout_ms Timeout in milliseconds
 */
void event_loop_step(event_loop_ctx_t* loop, int timeout_ms);

#endif // EVENT_LOOP_H
