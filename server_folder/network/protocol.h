#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

// Message Types
#define MSG_LOGIN_REQUEST         0x01
#define MSG_LOGIN_RESPONSE        0x02
#define MSG_VIEW_STOCKS_REQUEST   0x03
#define MSG_VIEW_STOCKS_RESPONSE  0x04
#define MSG_STOCK_UPDATE          0x05
#define MSG_BUY_STOCK_REQUEST     0x07
#define MSG_BUY_STOCK_RESPONSE    0x08
#define MSG_SELL_STOCK_REQUEST    0x09
#define MSG_SELL_STOCK_RESPONSE   0x0A
#define MSG_SEE_BALANCE_REQUEST   0x0F
#define MSG_SEE_BALANCE_RESPONSE  0x10

// Status Codes
#define STATUS_SUCCESS            0x00
#define STATUS_FAILED             0x01
#define STATUS_NOT_AUTHENTICATED  0x01
#define STATUS_STOCK_NOT_FOUND    0x02
#define STATUS_INSUFFICIENT_BALANCE   0x03
#define STATUS_INSUFFICIENT_STOCK    0x04
#define STATUS_SERVER_ERROR       0x05
#define STATUS_INSUFFICIENT_HOLDINGS  0x03
#define STATUS_STOCK_NOT_IN_PORTFOLIO 0x04
#define STATUS_USER_NOT_FOUND     0x02

// Fixed Header: [Type(1b)][Length(2b)]
#pragma pack(push, 1)
struct packet_header {
    uint8_t type;
    uint16_t length; 
};

struct login_payload {
    char username[32];
    char password[32];
};

struct login_response {
    uint8_t status;
    char message[64];
};

struct view_stocks_request {
    uint8_t reserved;  // For alignment
};

struct stock_info {
    uint16_t stock_id;
    char symbol[16];
    double current_price;
    uint32_t available_quantity;
};

struct view_stocks_response {
    uint8_t status;
    uint16_t stock_count;
    // Followed by: struct stock_info stocks[stock_count]
};

struct market_update {
    uint16_t stock_id;
    char symbol[16];
    double new_price;
    double price_change_percent;
};

// Buy Stock Request/Response
struct buy_stock_request {
    uint16_t stock_id;
    uint32_t quantity;
    double price_per_unit;
};

struct buy_stock_response {
    uint8_t status;
    uint32_t order_id;
    uint16_t message_length;
    char message[256];
};

// Sell Stock Request/Response
struct sell_stock_request {
    uint16_t stock_id;
    uint32_t quantity;
    double price_per_unit;
};

struct sell_stock_response {
    uint8_t status;
    uint32_t order_id;
    uint16_t message_length;
    char message[256];
};

// See Balance Request/Response
struct see_balance_request {
    uint8_t reserved;
};

struct see_balance_response {
    uint8_t status;
    double balance;
    uint16_t message_length;
    char message[128];
};
#pragma pack(pop)

#endif
