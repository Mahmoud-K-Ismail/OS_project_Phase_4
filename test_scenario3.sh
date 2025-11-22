#!/bin/bash
# Test Scenario 3: 3 concurrent clients - each running demo with different N values

echo "=========================================="
echo "Test Scenario 3: All Demo Programs"
echo "=========================================="
echo ""
echo "This test verifies SJRF scheduling with:"
echo "  - Client 1: demo 3"
echo "  - Client 2: demo 6"
echo "  - Client 3: demo 9"
echo ""
echo "The scheduler should prioritize shortest remaining time."
echo ""
echo "Starting server in background..."
./server > server_scenario3.log 2>&1 &
SERVER_PID=$!
sleep 2

echo "Server started (PID: $SERVER_PID)"
echo "Starting 3 clients..."
echo ""

# Client 1: demo 3 (needs time for 3 iterations + scheduling overhead)
(echo "demo 3"; sleep 20; echo "exit") | ./client > client1_scenario3.log 2>&1 &
CLIENT1_PID=$!

# Client 2: demo 6 (needs time for 6 iterations + scheduling overhead)
(echo "demo 6"; sleep 25; echo "exit") | ./client > client2_scenario3.log 2>&1 &
CLIENT2_PID=$!

# Client 3: demo 9 (needs time for 9 iterations + scheduling overhead)
(echo "demo 9"; sleep 35; echo "exit") | ./client > client3_scenario3.log 2>&1 &
CLIENT3_PID=$!

echo "All 3 clients started."
echo "Scheduler should use SJRF: shortest job first."
echo "Waiting for completion (this will take ~35 seconds)..."
wait $CLIENT1_PID $CLIENT2_PID $CLIENT3_PID

sleep 2
echo "Stopping server..."
kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null

echo ""
echo "=========================================="
echo "Test Complete!"
echo "=========================================="
echo "Server log: server_scenario3.log"
echo "Client 1 log: client1_scenario3.log"
echo "Client 2 log: client2_scenario3.log"
echo "Client 3 log: client3_scenario3.log"
echo ""
echo "Check server_scenario3.log for SJRF scheduling behavior."

