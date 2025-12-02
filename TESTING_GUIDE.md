# Phase 4 Testing Guide

## Prerequisites

Before testing, ensure:
1. **Project compiles successfully**: Run `make clean && make` - all executables should build without errors
2. **Demo program is built**: The `demo` executable should exist
3. **Server port is available**: Port 8080 should be free (or modify PORT in server.c)

## Test Setup

### Terminal Setup
You'll need **4 terminal windows**:
- **Terminal 1**: Server (shows scheduling logs)
- **Terminal 2**: Client 1
- **Terminal 3**: Client 2  
- **Terminal 4**: Client 3

### Build Commands
```bash
cd /home/mki4895/OS_project_Phase_4
make clean
make
```

---

## Test Scenario 1: Three Concurrent Shell Commands

### Objective
Verify that shell commands execute immediately without preemption, even when multiple clients send commands simultaneously.

### Setup
1. **Terminal 1 (Server)**: `./server`
   - Should show: `| Hello, Server Started |`
   
2. **Terminal 2 (Client 1)**: `./client`
   - Wait for "Connected to a server"
   - Send: `pwd`
   
3. **Terminal 3 (Client 2)**: `./client`
   - Wait for "Connected to a server"
   - Send: `mkdir a` (or `ls` if 'a' already exists)
   
4. **Terminal 4 (Client 3)**: `./client`
   - Wait for "Connected to a server"
   - Send: `echo hello`

### Expected Server Output
```
| Hello, Server Started |
[5]<<< client connected
[6]<<< client connected
[7]<<< client connected
[5]>>> pwd
(5)--- created (-1)
(5)--- started (-1)
[5]<<< 44 bytes sent
(5)--- ended (-1)
[6]>>> mkdir a
(6)--- created (-1)
(6)--- started (-1)
[6]<<< 53 bytes sent
(6)--- ended (-1)
[7]>>> echo hello
(7)--- created (-1)
(7)--- started (-1)
[7]<<< 7 bytes sent
(7)--- ended (-1)
```

### Expected Client Outputs
- **Client 1**: Should show the current directory path (e.g., `/mnt/c/Users/Desktop/OS/Project/Code`)
- **Client 2**: Should show `mkdir` success/error message or directory listing
- **Client 3**: Should show `hello`

### Key Observations
✅ **Shell commands have burst time -1** (shown in created/started/ended logs)  
✅ **No "waiting" or "running" states** - commands execute immediately  
✅ **No preemption** - each command completes before next starts  
✅ **Commands execute in order received** (or very quickly in sequence)

---

## Test Scenario 2: Mixed Commands and Programs

### Objective
Verify scheduling algorithm with:
- One shell command (should execute immediately)
- Two demo programs (should be scheduled with preemption)

### Setup
1. **Terminal 1 (Server)**: `./server`
   
2. **Terminal 2 (Client 1)**: `./client`
   - Send: `pwd`
   
3. **Terminal 3 (Client 2)**: `./client`
   - Send: `./demo 12` (12-second program)
   
4. **Terminal 4 (Client 3)**: `./client`
   - Send: `./demo 12` (12-second program)

### Expected Server Output Pattern
```
| Hello, Server Started |
[5]<<< client connected
[6]<<< client connected
[7]<<< client connected
[5]>>> pwd
(5)--- created (-1)
(5)--- started (-1)
[5]<<< 44 bytes sent
(5)--- ended (-1)
[6]>>> ./demo 12
(6)--- created (12)
(6)--- started (12)
(6)--- waiting (9)    # Preempted, remaining time shown
(6)--- running (9)    # Resumed
(6)--- waiting (2)    # Preempted again
(6)--- running (2)    # Resumed
[6]<<< 134 bytes sent
(6)--- ended (6)      # Final remaining time (should be 0 or small)
[7]>>> ./demo 12
(7)--- created (12)
(7)--- started (12)
(7)--- waiting (9)    # Preempted
(7)--- running (9)    # Resumed
[7]<<< 134 bytes sent
(7)--- ended (6)      # Final remaining time
```

### Expected Client Outputs
- **Client 1**: Directory path (immediate)
- **Client 2**: `Demo 1/12` through `Demo 12/12` (may be interleaved with Client 3)
- **Client 3**: `Demo 1/12` through `Demo 12/12` (may be interleaved with Client 2)

### Key Observations
✅ **Shell command (pwd) executes first** with burst time -1  
✅ **Demo programs show process IDs** (12 in this case)  
✅ **"waiting" and "running" states** alternate as processes are preempted  
✅ **Remaining time decreases** with each round  
✅ **Scheduling summary** at bottom (blue highlighted text) showing process sequence  
✅ **Both demos complete** - total output should be 12 lines each

### Scheduling Behavior to Verify
- **First Round Quantum**: 3 seconds
- **Subsequent Round Quantum**: 7 seconds
- **SJRF**: Process with shortest remaining time should run first
- **Preemption**: Current process stops when shorter job arrives
- **No consecutive selection**: Same process shouldn't run twice in a row (unless only one left)

---

## Test Scenario 3: Three Concurrent Demo Programs

### Objective
Verify scheduling algorithm with three programs of different durations, testing SJRF prioritization.

### Setup
1. **Terminal 1 (Server)**: `./server`
   
2. **Terminal 2 (Client 1)**: `./client`
   - Send: `./demo 12` (longest - 12 seconds)
   
3. **Terminal 3 (Client 2)**: `./client`
   - Send: `./demo 10` (medium - 10 seconds)
   
4. **Terminal 4 (Client 3)**: `./client`
   - Send: `./demo 8` (shortest - 8 seconds)

### Expected Server Output Pattern
```
| Hello, Server Started |
[5]<<< client connected
[6]<<< client connected
[7]<<< client connected
[5]>>> ./demo 12
(5)--- created (12)
(5)--- started (12)
(5)--- waiting (11)    # Preempted by shorter job
(5)--- running (11)    # Resumed
(5)--- waiting (4)     # Preempted again
(5)--- running (4)     # Resumed
[5]<<< 134 bytes sent
(5)--- ended (0)       # Should complete
[6]>>> ./demo 10
(6)--- created (10)
(6)--- started (10)
(6)--- waiting (10)    # Preempted
(6)--- running (10)    # Resumed
(6)--- waiting (3)     # Preempted
(6)--- running (3)     # Resumed
[6]<<< 112 bytes sent
(6)--- ended (0)       # Should complete
[7]>>> ./demo 8
(7)--- created (8)
(7)--- started (8)
(7)--- waiting (5)     # Preempted
(7)--- running (5)     # Resumed
[7]<<< 82 bytes sent
(7)--- ended (0)       # Should complete first (shortest)
```

### Expected Client Outputs
- **Client 1**: `Demo 1/12` through `Demo 12/12` (completes last)
- **Client 2**: `Demo 1/10` through `Demo 10/10` (completes second)
- **Client 3**: `Demo 1/8` through `Demo 8/8` (should complete first due to SJRF)

### Key Observations
✅ **Shortest job (demo 8) should complete first** (SJRF algorithm)  
✅ **Processes are preempted** when shorter jobs are available  
✅ **Remaining time tracked correctly** - decreases with each execution  
✅ **All processes complete** - no deadlocks or infinite loops  
✅ **Scheduling summary** shows process execution sequence (e.g., `P6-(3)-P7-(6)-P6-(13)-P7-(20)-P6-(22)-P5-(26)-P5-(31)`)  
✅ **No process runs twice consecutively** (unless it's the only one left)

### Scheduling Algorithm Verification
1. **SJRF Priority**: Demo 8 should start/finish before Demo 10, which should finish before Demo 12
2. **Quantum Enforcement**: 
   - First round: 3 seconds max per process
   - Subsequent rounds: 7 seconds max per process
3. **Preemption**: When a shorter job arrives, current job should be preempted
4. **Resume**: Preempted jobs resume from where they stopped (remaining time preserved)

---

## Additional Test Cases

### Test Case 4: Single Long-Running Program
**Setup**: One client sends `./demo 20`  
**Expected**: Should complete without issues, showing all states (created, started, waiting, running, ended)

### Test Case 5: Rapid Sequential Commands
**Setup**: One client sends multiple shell commands quickly: `pwd`, `ls`, `echo test`  
**Expected**: All execute immediately, no queuing for shell commands

### Test Case 6: Mixed with Different Timing
**Setup**: 
- Client 1: `./demo 5` (short)
- Client 2: `pwd` (shell command)
- Client 3: `./demo 15` (long)
**Expected**: 
- `pwd` executes immediately
- `./demo 5` completes before `./demo 15`
- SJRF prioritizes shorter demo

### Test Case 7: Client Disconnection
**Setup**: Client sends `./demo 10`, then disconnect before completion  
**Expected**: Server should clean up the task, remove it from queue, log appropriately

---

## Common Issues to Watch For

### ❌ Problems to Identify

1. **Compilation Errors**
   - Fix all warnings and errors before testing
   - Ensure pthread library is linked (`-pthread` flag)

2. **Server Crashes**
   - Check for segmentation faults
   - Verify mutex/semaphore usage (no deadlocks)
   - Check memory management (no leaks)

3. **Incorrect Scheduling**
   - Shell commands should NOT be queued (burst time -1)
   - Shorter jobs should run before longer ones (SJRF)
   - Processes should not run consecutively (unless only one left)

4. **Output Format Issues**
   - Server logs must match expected format exactly
   - Client outputs should be clean and readable
   - Demo output: `Demo X/Y` format (one per second)

5. **Timing Issues**
   - Demo should print one line per second (not all at once)
   - Quantum times should be respected (3s first round, 7s others)
   - Remaining time should decrease correctly

6. **State Tracking**
   - "created", "started", "waiting", "running", "ended" states should be logged
   - Process IDs should be consistent
   - Remaining time should be accurate

---

## Verification Checklist

Before submitting, verify:

- [ ] All three test scenarios run successfully
- [ ] Server logs match expected format
- [ ] Shell commands execute immediately (no queuing)
- [ ] Demo programs are scheduled correctly
- [ ] SJRF algorithm prioritizes shorter jobs
- [ ] Quantum times are enforced (3s/7s)
- [ ] Processes resume from correct point (remaining time preserved)
- [ ] No deadlocks or crashes
- [ ] Client disconnection is handled gracefully
- [ ] All processes complete successfully
- [ ] Scheduling summary is displayed at end
- [ ] Code compiles without errors/warnings
- [ ] Demo program prints one line per second

---

## Debugging Tips

1. **Add verbose logging** (if needed) to see queue state
2. **Check mutex locks** - ensure no deadlocks
3. **Verify quantum calculation** - print quantum values
4. **Monitor remaining time** - ensure it decreases correctly
5. **Test with smaller values first** (e.g., `./demo 3` instead of `./demo 12`)

---

## Expected Output Format Summary

### Server Log Format
- Connection: `[ID]<<< client connected`
- Command received: `[ID]>>> <command>`
- Task created: `(ID)--- created (burst_time)`
- Task started: `(ID)--- started (burst_time)`
- Task waiting: `(ID)--- waiting (remaining_time)`
- Task running: `(ID)--- running (remaining_time)`
- Bytes sent: `[ID]<<< X bytes sent`
- Task ended: `(ID)--- ended (final_remaining_time)`
- Summary: Process sequence at bottom (blue highlighted)

### Client Output Format
- Connection message: `Connected to a server`
- Command prompt: `>>>`
- Command output: (varies by command)
- Demo output: `Demo X/Y` (one per second)

---

Good luck with your testing! 🚀
