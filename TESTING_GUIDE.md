# Phase 4 Testing Guide

This guide provides step-by-step instructions for testing all three required scenarios.

## Prerequisites

1. Build all components:
   ```bash
   make clean && make all
   ```

2. Verify executables exist:
   - `./server`
   - `./client`
   - `./demo`

## Test Scenario 1: Three Concurrent Shell Commands

**Objective**: Verify that shell commands execute immediately without preemption.

### Steps:

1. **Terminal 1 - Start the server:**
   ```bash
   ./server
   ```
   You should see: `[INFO] Phase 4 server started on port 8080. Waiting for clients...`

2. **Terminal 2 - Start Client 1:**
   ```bash
   ./client
   ```
   At the prompt, type:
   ```
   ls -la | head -5
   ```
   Then type `exit`

3. **Terminal 3 - Start Client 2 (while Client 1 is still connected):**
   ```bash
   ./client
   ```
   At the prompt, type:
   ```
   pwd
   ```
   Then type `exit`

4. **Terminal 4 - Start Client 3 (while others are connected):**
   ```bash
   ./client
   ```
   At the prompt, type:
   ```
   echo "Hello from client 3"
   ```
   Then type `exit`

### Expected Behavior:
- Each shell command executes immediately when sent
- Server logs show tasks being created, dispatched, and completed
- No preemption occurs (shell commands complete in one round)
- Summary shows: `[SUMMARY] Completed 3/3 tasks (shell=3, program=0)`

---

## Test Scenario 2: Mixed Workload

**Objective**: Verify scheduling with one shell command and two demo programs.

### Steps:

1. **Terminal 1 - Start the server:**
   ```bash
   ./server
   ```

2. **Terminal 2 - Start Client 1 (Shell Command):**
   ```bash
   ./client
   ```
   Type:
   ```
   ls -la | head -3
   ```
   Then `exit`

3. **Terminal 3 - Start Client 2 (Demo 5):**
   ```bash
   ./client
   ```
   Type:
   ```
   demo 5
   ```
   Wait for completion, then `exit`

4. **Terminal 4 - Start Client 3 (Demo 8):**
   ```bash
   ./client
   ```
   Type:
   ```
   demo 8
   ```
   Wait for completion, then `exit`

### Expected Behavior:
- Shell command executes first (immediate execution)
- Demo 5 and Demo 8 are scheduled using SJRF
- Demo 5 (shorter) should complete before Demo 8
- Server logs show preemption and scheduling decisions
- Output shows iterations: `[demo][task X] iteration Y/Z`
- Summary shows: `[SUMMARY] Completed 3/3 tasks (shell=1, program=2)`

---

## Test Scenario 3: All Demo Programs

**Objective**: Verify SJRF scheduling with three demo programs of different lengths.

### Steps:

1. **Terminal 1 - Start the server:**
   ```bash
   ./server
   ```

2. **Terminal 2 - Start Client 1 (Demo 3):**
   ```bash
   ./client
   ```
   Type:
   ```
   demo 3
   ```
   Wait for completion, then `exit`

3. **Terminal 3 - Start Client 2 (Demo 6):**
   ```bash
   ./client
   ```
   Type:
   ```
   demo 6
   ```
   Wait for completion, then `exit`

4. **Terminal 4 - Start Client 3 (Demo 9):**
   ```bash
   ./client
   ```
   Type:
   ```
   demo 9
   ```
   Wait for completion, then `exit`

### Expected Behavior:
- Scheduler uses SJRF: shortest remaining time first
- Demo 3 completes first (3 iterations)
- Demo 6 completes next (6 iterations)
- Demo 9 completes last (9 iterations)
- Server logs show scheduling decisions based on remaining time
- Tasks are preempted and resumed correctly
- Summary shows: `[SUMMARY] Completed 3/3 tasks (shell=0, program=3)`

---

## Automated Testing

You can also run automated tests:

```bash
# Run individual scenarios
./test_scenario1.sh
./test_scenario2.sh
./test_scenario3.sh

# Or run all scenarios sequentially
./run_all_tests.sh
```

Check the generated log files for detailed output.

---

## What to Look For in Server Logs

1. **Client Connections:**
   ```
   [INFO] Client #X connected from 127.0.0.1:XXXXX
   ```

2. **Task Enqueueing:**
   ```
   [QUEUE] Added Task #X (shell/demo) from Client #Y. Pending=Z
   ```

3. **Task Dispatching:**
   ```
   [SCHED] Dispatching Task #X (shell/demo) for Client #Y. Remaining=N
   ```

4. **Task Execution:**
   ```
   [EXEC] Running shell command for Client #X: <command>
   [RUN] Task #X (demo) executing for N units. Remaining before=M
   ```

5. **Task Completion/Preemption:**
   ```
   [SCHED] Task #X completed.
   [SCHED] Task #X preempted. Remaining=N
   ```

6. **Summary:**
   ```
   [SUMMARY] Completed X/Y tasks (shell=A, program=B)
   ```

---

## Tips for Demo Video

1. **Clear Terminal Windows**: Make sure terminal text is readable
2. **Show Server Logs**: Keep server terminal visible to show scheduling decisions
3. **Explain Output**: Point out:
   - Client numbers in logs
   - Task IDs and scheduling order
   - Remaining time updates
   - Preemption behavior
   - Summary statistics
4. **Test Each Scenario**: Run all three scenarios clearly
5. **Camera Overlay**: Both team members should be visible in bottom corner

---

## Troubleshooting

- **Port already in use**: Kill any existing server process
  ```bash
   lsof -ti:8080 | xargs kill -9
   ```

- **Client can't connect**: Make sure server is running first

- **Tasks not scheduling**: Check server logs for errors

- **Demo not printing**: Verify demo program was built correctly

