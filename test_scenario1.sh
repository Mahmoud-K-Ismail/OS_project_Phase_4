#!/bin/bash
# Test Scenario 1: 3 concurrent clients all running single shell commands

echo "=========================================="
echo "Test Scenario 1: Three Shell Commands"
echo "=========================================="
echo ""
echo "This test verifies that shell commands execute immediately"
echo "without preemption when multiple clients send commands."
echo ""
echo "Starting server in background..."
./server > server_scenario1.log 2>&1 &
SERVER_PID=$!
sleep 2

echo "Server started (PID: $SERVER_PID)"
echo "Starting 3 clients..."
echo ""

# Client 1: ls command
(echo "ls -la | head -5"; sleep 1; echo "exit") | ./client > client1_scenario1.log 2>&1 &
CLIENT1_PID=$!

# Client 2: pwd command
(echo "pwd"; sleep 1; echo "exit") | ./client > client2_scenario1.log 2>&1 &
CLIENT2_PID=$!

# Client 3: echo command
(echo "echo 'Hello from client 3'"; sleep 1; echo "exit") | ./client > client3_scenario1.log 2>&1 &
CLIENT3_PID=$!

echo "All 3 clients started. Waiting for completion..."
wait $CLIENT1_PID $CLIENT2_PID $CLIENT3_PID

sleep 2
echo "Stopping server..."
kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null

echo ""
echo "=========================================="
echo "Test Complete!"
echo "=========================================="
echo "Server log: server_scenario1.log"
echo "Client 1 log: client1_scenario1.log"
echo "Client 2 log: client2_scenario1.log"
echo "Client 3 log: client3_scenario1.log"
echo ""
echo "Check server_scenario1.log for scheduling details and summary."

