#!/bin/bash
# Quick test script for debugging login issue

echo "====== STEP 1: Recompiling Server ======"
cd /mnt/d/Bình/network_programming/project_final/server_folder
make clean
make server
if [ $? -ne 0 ]; then
    echo "Server compilation failed!"
    exit 1
fi
echo "✓ Server compiled successfully"

echo ""
echo "====== STEP 2: Recompiling Client ======"
cd /mnt/d/Bình/network_programming/project_final/client_app
make clean
make client
if [ $? -ne 0 ]; then
    echo "Client compilation failed!"
    exit 1
fi
echo "✓ Client compiled successfully"

echo ""
echo "====== STEP 3: Starting Server (in background) ======"
cd /mnt/d/Bình/network_programming/project_final/server_folder
./server > /tmp/server.log 2>&1 &
SERVER_PID=$!
sleep 2
echo "✓ Server started with PID $SERVER_PID"

echo ""
echo "====== STEP 4: Testing login with client ======"
cd /mnt/d/Bình/network_programming/project_final/client_app
(echo "login admin password123"; sleep 2; echo "quit") | timeout 10 ./client 2>&1 | tee /tmp/client.log

echo ""
echo "====== STEP 5: Server logs ======"
echo "Last 30 lines of server output:"
tail -30 /tmp/server.log

echo ""
echo "====== STEP 6: Cleanup ======"
kill $SERVER_PID 2>/dev/null
echo "✓ Test complete"
