#!/bin/bash
# Run all three test scenarios sequentially

echo "=========================================="
echo "Running All Phase 4 Test Scenarios"
echo "=========================================="
echo ""

# Make sure scripts are executable
chmod +x test_scenario1.sh test_scenario2.sh test_scenario3.sh

echo "Running Test Scenario 1..."
./test_scenario1.sh
echo ""
echo "Press Enter to continue to Scenario 2..."
read

echo "Running Test Scenario 2..."
./test_scenario2.sh
echo ""
echo "Press Enter to continue to Scenario 3..."
read

echo "Running Test Scenario 3..."
./test_scenario3.sh

echo ""
echo "=========================================="
echo "All Tests Complete!"
echo "=========================================="
echo ""
echo "Review the log files:"
echo "  - server_scenario*.log (server scheduling logs)"
echo "  - client*_scenario*.log (client outputs)"
echo ""

