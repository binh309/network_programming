#ifndef PROTOCOL_H
#define PROTOCOL_H

// Client-to-Server Message Types
typedef enum {
    CMSG_REGISTER = 0x01,
    CMSG_LOGIN = 0x02,
    CMSG_LOGOUT = 0x03,
    CMSG_VIEW_STOCKS = 0x10,
    CMSG_BUY_STOCK = 0x11,
    CMSG_SELL_STOCK = 0x12,
    CMSG_VIEW_MY_STOCKS = 0x13,
    CMSG_SEE_BALANCE = 0x14,
} client_msg_t;

// Server-to-Client Message Types
typedef enum {
    SMSG_REGISTER_SUCCESS = 0x81,
    SMSG_REGISTER_FAIL = 0x82,
    SMSG_LOGIN_SUCCESS = 0x83,
    SMSG_LOGIN_FAIL = 0x84,
    SMSG_LOGOUT_SUCCESS = 0x85,
    SMSG_VIEW_STOCKS_DATA = 0x90,
    SMSG_VIEW_STOCKS_FAIL = 0x91,
    SMSG_BUY_STOCK_SUCCESS = 0x92,
    SMSG_BUY_STOCK_FAIL = 0x93,
    SMSG_SELL_STOCK_SUCCESS = 0x94,
    SMSG_SELL_STOCK_FAIL = 0x95,
    SMSG_VIEW_MY_STOCKS_DATA = 0x96,
    SMSG_VIEW_MY_STOCKS_FAIL = 0x97,
    SMSG_SEE_BALANCE_DATA = 0x98,
    SMSG_SEE_BALANCE_FAIL = 0x99,
    SMSG_ERROR = 0xFF, // Generic error message
} server_msg_t;

#endif
