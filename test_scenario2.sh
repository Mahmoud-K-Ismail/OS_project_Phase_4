#!/bin/bash
# Test Scenario 2: 3 concurrent clients - one shell command + two demo programs

echo "=========================================="
echo "Test Scenario 2: Mixed Workload"
echo "=========================================="
echo ""
echo "This test verifies scheduling with:"
echo "  - Client 1: Shell command (ls)"
echo "  - Client 2: demo 5"
echo "  - Client 3: demo 8"
echo ""
echo "Starting server in background..."
./server > server_scenario2.log 2>&1 &
SERVER_PID=$!
sleep 2

echo "Server started (PID: $SERVER_PID)"
echo "Starting 3 clients..."
echo ""

# Client 1: Shell command (should execute immediately)
(echo "ls -la | head -3"; sleep 1; echo "exit") | ./client > client1_scenario2.log 2>&1 &
CLIENT1_PID=$!

# Client 2: demo 5 (needs time for 5 iterations + scheduling overhead)
(echo "demo 5"; sleep 25; echo "exit") | ./client > client2_scenario2.log 2>&1 &
CLIENT2_PID=$!

# Client 3: demo 8 (needs time for 8 iterations + scheduling overhead)
(echo "demo 8"; sleep 30; echo "exit") | ./client > client3_scenario2.log 2>&1 &
CLIENT3_PID=$!

echo "All 3 clients started."
echo "Shell command should execute first, then demo jobs will be scheduled."
echo "Waiting for completion (this will take ~30 seconds)..."
wait $CLIENT1_PID $CLIENT2_PID $CLIENT3_PID

sleep 2
echo "Stopping server..."
kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null

echo ""
echo "=========================================="
echo "Test Complete!"
echo "=========================================="
echo "Server log: server_scenario2.log"
echo "Client 1 log: client1_scenario2.log"
echo "Client 2 log: client2_scenario2.log"
echo "Client 3 log: client3_scenario2.log"
echo ""
echo "Check server_scenario2.log for scheduling algorithm behavior."

