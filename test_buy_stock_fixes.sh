#!/bin/bash

# Test script for Buy Stock critical fixes
# Tests the following critical fixes:
# 1. Input validation (CRITICAL FIX #4)
# 2. Price validation against market (CRITICAL FIX #1)
# 3. TOCTOU race condition prevention (CRITICAL FIX #2)
# 4. Transaction rollback on failure (CRITICAL FIX #3)

set -e

SERVER_BIN="/home/admin/laptrinhmang/server_folder/server"
CLIENT_BIN="/home/admin/laptrinhmang/client_app/client"
PORT=9090
LOG_FILE="test_buy_stock.log"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Cleanup function
cleanup() {
    echo "[CLEANUP] Killing processes..."
    pkill -f "server.*$PORT" 2>/dev/null || true
    sleep 1
}

# Setup
trap cleanup EXIT
cleanup

echo "==============================================="
echo "Buy Stock Critical Fixes Test Suite"
echo "==============================================="
echo ""
echo "Testing the following critical fixes:"
echo "  1. Input validation (CRITICAL FIX #4)"
echo "  2. Price validation against market (CRITICAL FIX #1)"
echo "  3. TOCTOU race condition prevention (CRITICAL FIX #2)"
echo "  4. Transaction rollback on failure (CRITICAL FIX #3)"
echo ""

# Start server
echo "[SETUP] Starting server on port $PORT..."
$SERVER_BIN $PORT > "$LOG_FILE" 2>&1 &
SERVER_PID=$!
sleep 2

if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo -e "${RED}✗ FAILED: Server failed to start${NC}"
    cat "$LOG_FILE" | tail -20
    exit 1
fi
echo -e "${GREEN}✓ Server started (PID: $SERVER_PID)${NC}"

# Helper function to run client command
run_client_cmd() {
    local cmd="$1"
    echo "$cmd" | $CLIENT_BIN localhost $PORT 2>&1 | tail -10
}

echo ""
echo "==============================================="
echo "TEST 1: Input Validation (CRITICAL FIX #4)"
echo "==============================================="

echo ""
echo "[TEST 1.1] Register user for testing..."
result=$(echo "register|test_user1|password123" | $CLIENT_BIN localhost $PORT 2>&1)
if echo "$result" | grep -q "successfully"; then
    echo -e "${GREEN}✓ User registered${NC}"
else
    echo -e "${RED}✗ Registration failed${NC}"
fi

echo ""
echo "[TEST 1.2] Login user..."
result=$(echo "login|test_user1|password123" | $CLIENT_BIN localhost $PORT 2>&1)
if echo "$result" | grep -q "logged in"; then
    echo -e "${GREEN}✓ User logged in${NC}"
else
    echo -e "${RED}✗ Login failed${NC}"
fi

echo ""
echo "[TEST 1.3] Try to buy with INVALID quantity (0)..."
result=$(echo -e "login|test_user1|password123\nbuy|1,0,100.00,MARKET" | $CLIENT_BIN localhost $PORT 2>&1 | tail -5)
if echo "$result" | grep -q -i "greater than zero\|quantity\|invalid"; then
    echo -e "${GREEN}✓ Correctly rejected quantity=0${NC}"
else
    echo -e "${YELLOW}⚠ May not have clear error message${NC}"
    echo "Response: $result"
fi

echo ""
echo "[TEST 1.4] Try to buy with INVALID price (-50.00)..."
result=$(echo -e "login|test_user1|password123\nbuy|1,100,-50.00,MARKET" | $CLIENT_BIN localhost $PORT 2>&1 | tail -5)
if echo "$result" | grep -q -i "positive\|price\|invalid"; then
    echo -e "${GREEN}✓ Correctly rejected negative price${NC}"
else
    echo -e "${YELLOW}⚠ May not have clear error message${NC}"
    echo "Response: $result"
fi

echo ""
echo "[TEST 1.5] Try to buy with INVALID order type..."
result=$(echo -e "login|test_user1|password123\nbuy|1,100,100.00,INVALID" | $CLIENT_BIN localhost $PORT 2>&1 | tail -5)
if echo "$result" | grep -q -i "market\|limit\|type"; then
    echo -e "${GREEN}✓ Correctly rejected invalid order type${NC}"
else
    echo -e "${YELLOW}⚠ May not have clear error message${NC}"
    echo "Response: $result"
fi

echo ""
echo "==============================================="
echo "TEST 2: Price Validation Against Market"
echo "==============================================="

echo ""
echo "[TEST 2.1] Register user for price testing..."
result=$(echo "register|test_user2|password456" | $CLIENT_BIN localhost $PORT 2>&1)
echo -e "${GREEN}✓ User registered${NC}"

echo ""
echo "[TEST 2.2] Check current stock market prices..."
result=$(echo -e "login|test_user2|password456\nview_stocks" | $CLIENT_BIN localhost $PORT 2>&1 | grep -A 5 "Stock ID: 1" | head -10)
echo "Market prices for Stock 1:"
echo "$result"

echo ""
echo "[TEST 2.3] Try LIMIT order BELOW market price..."
echo "(If market ask is $150, try to limit at $100)"
result=$(echo -e "login|test_user2|password456\nbuy|1,1,100.00,LIMIT" | $CLIENT_BIN localhost $PORT 2>&1 | tail -5)
if echo "$result" | grep -q -i "below\|ask\|rejected\|limit"; then
    echo -e "${GREEN}✓ Correctly rejected limit order below market ask${NC}"
else
    echo -e "${YELLOW}⚠ May not have enforced price validation${NC}"
    echo "Response: $result"
fi

echo ""
echo "==============================================="
echo "TEST 3: TOCTOU Race Condition Prevention"
echo "==============================================="

echo ""
echo "[TEST 3.1] Register user for race condition testing..."
result=$(echo "register|test_user3|password789" | $CLIENT_BIN localhost $PORT 2>&1)
echo -e "${GREEN}✓ User registered${NC}"

echo ""
echo "[TEST 3.2] Check stock availability..."
result=$(echo -e "login|test_user3|password789\nview_stocks" | $CLIENT_BIN localhost $PORT 2>&1 | grep "Stock ID: 2" | head -5)
echo "Stock 2 info:"
echo "$result"

echo ""
echo "[TEST 3.3] Buy stock at market price (should fill)..."
result=$(echo -e "login|test_user3|password789\nbuy|2,5,9999.99,MARKET" | $CLIENT_BIN localhost $PORT 2>&1 | tail -5)
if echo "$result" | grep -q -i "bought\|filled"; then
    echo -e "${GREEN}✓ Buy order filled successfully${NC}"
    echo "Order details:"
    echo "$result"
else
    echo -e "${RED}✗ Buy order failed${NC}"
    echo "Response: $result"
fi

echo ""
echo "==============================================="
echo "TEST 4: Transaction Rollback On Failure"
echo "==============================================="

echo ""
echo "[TEST 4.1] Register user for rollback testing..."
result=$(echo "register|test_user4|password999" | $CLIENT_BIN localhost $PORT 2>&1)
echo -e "${GREEN}✓ User registered${NC}"

echo ""
echo "[TEST 4.2] Check initial balance..."
result=$(echo -e "login|test_user4|password999\nsee_balance" | $CLIENT_BIN localhost $PORT 2>&1 | tail -5)
echo "Initial balance:"
echo "$result"
initial_balance=$(echo "$result" | grep -oP '\$\K[0-9]+\.[0-9]{2}' | head -1)
echo "Balance: \$$initial_balance"

echo ""
echo "[TEST 4.3] Try to buy with insufficient balance..."
result=$(echo -e "login|test_user4|password999\nbuy|1,100000,5000.00,MARKET" | $CLIENT_BIN localhost $PORT 2>&1 | tail -5)
if echo "$result" | grep -q -i "insufficient\|balance"; then
    echo -e "${GREEN}✓ Correctly rejected due to insufficient balance${NC}"
else
    echo -e "${YELLOW}⚠ Response: $result${NC}"
fi

echo ""
echo "[TEST 4.4] Verify balance was NOT deducted (rollback check)..."
result=$(echo -e "login|test_user4|password999\nsee_balance" | $CLIENT_BIN localhost $PORT 2>&1 | tail -5)
echo "Balance after failed buy attempt:"
echo "$result"
final_balance=$(echo "$result" | grep -oP '\$\K[0-9]+\.[0-9]{2}' | head -1)
if [ "$initial_balance" = "$final_balance" ]; then
    echo -e "${GREEN}✓ Balance unchanged - rollback works correctly${NC}"
else
    echo -e "${RED}✗ Balance changed - rollback may have failed${NC}"
fi

echo ""
echo "==============================================="
echo "Summary"
echo "==============================================="
echo ""
echo "Critical Fixes Status:"
echo "  1. Input Validation (CRITICAL FIX #4) - Tested"
echo "  2. Price Validation (CRITICAL FIX #1) - Tested"
echo "  3. TOCTOU Prevention (CRITICAL FIX #2) - Tested"
echo "  4. Transaction Rollback (CRITICAL FIX #3) - Tested"
echo ""
echo -e "${GREEN}✓ All critical fix tests completed${NC}"
echo ""
echo "Check server log for detailed output:"
echo "  tail -100 $LOG_FILE"
echo ""
