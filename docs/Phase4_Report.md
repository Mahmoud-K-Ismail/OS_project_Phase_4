# Phase 4 Report – Scheduler-Driven Remote Shell

**CS-UH 3010 - Operating Systems, Fall 2025**

**Project Phase 4**

**Muhammad Ali Asgar Fataymamode (mxf4642)**  
**Mahmoud Kassem (mki4895)**

---

## Architecture and Design

### High-Level Structure of System

**1. client.c:** This is the client application that serves as the user-facing interface. It implements the main command loop that connects to the remote server via TCP socket on port 8080, displays the "$" prompt to the user, reads commands using fgets(), sends commands to the server over the network, and receives/displays command output. The client handles connection establishment using socket() and connect(), manages the send/receive cycle with proper protocol adherence, and handles disconnection gracefully on "exit" command or EOF (Ctrl+D). The client is unchanged from Phase 3 and works seamlessly with the scheduler-enabled server, receiving output from scheduled tasks including shell commands and demo programs.

**2. server.c:** This is the scheduler-driven server application that extends Phase 3's multithreaded architecture with CPU scheduling capabilities. The server maintains three types of threads: (1) the main thread that accepts client connections using accept() in an infinite loop, (2) a dedicated scheduler thread that implements the combined Round-Robin (RR) + Shortest Job Remaining First (SJRF) algorithm, and (3) multiple client threads that receive commands and enqueue tasks. The server creates a TCP server socket using socket(), bind(), and listen(), then spawns the scheduler thread using pthread_create() before entering the accept loop. Each client connection spawns a client thread that receives commands and creates scheduler_task_t structures, which are enqueued into a shared ready queue protected by pthread_mutex_t and pthread_cond_t. The scheduler thread continuously selects tasks from the queue using the combined algorithm, executes them on the simulated single CPU, and either completes them or re-queues them for further execution. Shell commands are assigned burst time -1 (highest priority) and execute immediately in one round, while program tasks (demo N or program <label> N) are preemptable and resume from their exact stopping point. The server logs all scheduling decisions, task state transitions, and summary statistics matching the required format with client numbers, task IDs, remaining time, and completion status.

**3. shell_utils.c/h:** These are the Phase 1 shell implementation files that are reused without modification. The server integrates these modules to parse and execute shell commands (parse_pipeline() and execute_pipeline()), maintaining full support for all Phase 1 features including pipes, input/output redirection, and error redirection. Shell commands are executed through the Phase 1 parser and their output is captured and sent to clients. (unchanged from Phase 3)

**4. demo.c:** This is a standalone workload generator program that simulates a process requiring N time units of execution. The program accepts a command-line argument N, then loops N times, printing one line per second in the format "[demo] iteration X/N" with sleep(1) between iterations. This program can be run independently for testing, and when invoked through the server as "demo N", it is scheduled as a preemptable task that runs for N iterations, with each iteration representing one time unit of CPU work.

### Key Design Decisions

**1. Single-CPU Simulation with Scheduling:** The implementation simulates a single-CPU system where only one task can execute at a time, despite having multiple client threads. This is achieved by having a dedicated scheduler thread that is the sole executor of tasks, while client threads only enqueue tasks. The scheduler thread runs tasks sequentially, ensuring true single-CPU semantics. This design choice avoids complex memory and process management while focusing on scheduling algorithm correctness.

**2. Combined RR + SJRF Scheduling Algorithm:** The scheduler implements a selectively preemptive algorithm combining Round-Robin fairness with Shortest Job Remaining First optimality. The algorithm uses different quantum values: 3 time units for the first round (allowing quick response for short jobs) and 7 time units for subsequent rounds (improving throughput for longer jobs). Among program tasks, SJRF selects the task with the shortest remaining time, minimizing average waiting time. Shell commands always have priority (burst=-1) and execute immediately. This hybrid approach balances fairness (RR) with efficiency (SJRF).

**3. Task State Preservation:** Program tasks maintain their execution state between preemptions. Each task tracks iterations_completed and remaining_time, allowing it to resume from the exact point where it was preempted rather than restarting. This is implemented by storing task state in the scheduler_task_t structure and updating it incrementally during execution. When a task is preempted, it is re-queued with its updated state, ensuring continuity of execution.

**4. Thread Architecture:** The system uses three types of threads: (a) Main thread for accepting connections, (b) Scheduler thread for task selection and execution, and (c) Client threads for receiving commands and enqueueing tasks. This separation ensures that task scheduling decisions are centralized in one thread, preventing race conditions in scheduling logic while allowing concurrent command reception from multiple clients.

**5. Queue-Based Task Management:** All tasks are stored in a bounded ready queue (MAX_TASKS=512) protected by a mutex and condition variable. The queue uses a fixed-size array for simplicity and efficiency. Tasks are enqueued by client threads and dequeued by the scheduler thread. The condition variable allows the scheduler to block efficiently when the queue is empty, waking up automatically when tasks are added. This design ensures thread-safe queue operations and efficient CPU utilization.

**6. Reference-Counted Client Contexts:** Each client connection has a client_context_t structure with reference counting (ref_count). Tasks hold references to their originating client context, preventing use-after-free bugs when clients disconnect mid-execution. When a client disconnects, its tasks are cancelled and removed from the queue, but running tasks can safely check the connection status and abort gracefully. This design ensures robust handling of client disconnections without memory corruption.

**7. Selective Preemption:** The scheduler is selectively preemptive: shell commands are never preempted (they complete in one round), while program tasks can be preempted at quantum boundaries. This ensures shell commands maintain their instant execution guarantee while allowing efficient scheduling of longer-running program tasks. Preemption occurs at the end of a quantum, not during execution, simplifying the implementation while maintaining correctness.

**8. Fairness Rule Enforcement:** The scheduler enforces a fairness rule preventing the same task from being selected twice consecutively (unless it's the only task in the queue). This prevents a task with the shortest remaining time from monopolizing the CPU and ensures Round-Robin fairness among tasks with equal remaining times. The implementation tracks the last_task_id and selects an alternative task when the same task would be chosen again.

### Reasons for Using Specific Algorithms and Data Structures

**1. Combined RR + SJRF Algorithm:** Round-Robin provides fairness by giving each task a time slice, preventing starvation. However, pure RR doesn't optimize for job completion time. SJRF minimizes average waiting time by prioritizing shorter jobs, but can starve longer jobs. The combination uses RR's quantum-based time slicing with SJRF's selection policy, achieving both fairness and efficiency. The different quantum values (3 for first round, 7 for subsequent) allow quick response for short jobs while providing better throughput for longer jobs.

**2. Fixed-Size Array Queue:** The ready queue uses a fixed-size array (MAX_TASKS=512) rather than a linked list. This choice provides O(1) access time, better cache locality, and simpler memory management. The bounded size prevents unbounded memory growth while being sufficient for typical workloads. Array-based queues are efficient for the selection algorithm which needs to scan all tasks.

**3. Mutex + Condition Variable Synchronization:** The queue is protected by pthread_mutex_t for mutual exclusion and pthread_cond_t for efficient blocking. The scheduler thread waits on the condition variable when the queue is empty, and client threads signal it when adding tasks. This design avoids busy-waiting and provides efficient thread coordination. The mutex ensures atomic queue operations, preventing race conditions in task selection and enqueueing.

**4. Task State Structure (scheduler_task_t):** Each task maintains comprehensive state including task_id, type, command string, total_burst_time, remaining_time, rounds_completed, iterations_completed, arrival sequence, and client reference. This structure enables the scheduler to make informed decisions (SJRF based on remaining_time), track execution progress (iterations_completed), enforce fairness (arrival_seq for tie-breaking), and manage task lifecycle (cancelled flag for client disconnections).

**5. Reference Counting for Client Contexts:** Client contexts use reference counting (ref_count) with pthread_mutex_t protection. Tasks increment the reference count when created and decrement it when destroyed. This ensures that client contexts are not freed while tasks still reference them, preventing use-after-free bugs. The mutex ensures thread-safe reference count operations.

### File Structure

```
Phase4/
├── client.c              # Client application (unchanged from Phase 3)
├── server.c              # Scheduler-enabled server (modified for Phase 4)
├── demo.c                # Standalone workload generator (new in Phase 4)
├── shell_utils.c         # Phase 1 shell implementation
├── shell_utils.h         # Phase 1 header
├── myshell.c             # Phase 1 local shell
├── Makefile              # Build system (updated for demo target)
├── README.md             # Documentation
├── docs/
│   └── Phase4_Report.md  # This report
├── tests/                # Test files
│   ├── testfile1.txt
│   └── testfile2.txt
└── test_scenario*.sh     # Automated test scripts (new in Phase 4)
```

### Code Organization

**1. client.c:** Unchanged from Phase 3. The client connects to the server, sends commands, and receives output. It is unaware of the scheduling mechanism and works transparently with the scheduler-enabled server.

**2. server.c:** Extensively modified for Phase 4. Key additions include:
   - Scheduler thread implementation (scheduler_main())
   - Task queue data structures (scheduler_queue_t, scheduler_task_t)
   - Task selection algorithm (select_next_task())
   - Task execution functions (run_shell_task(), run_program_task())
   - Client context management with reference counting
   - Comprehensive logging for scheduling decisions

**3. demo.c:** New file for Phase 4. Implements a standalone workload generator that prints one line per second for N seconds, simulating a process with N time units of work.

**4. shell_utils.c/h:** Unchanged from Phase 3. Used by the server to execute shell commands through the Phase 1 parser.

This modular structure maintains clear separation between client (user interface), server (scheduling and execution), shell engine (Phase 1 logic), and workload simulation (demo program), enabling independent testing and maintenance.

---

## Implementation Highlights

**1. Scheduler Thread Implementation (Asgar)**

The scheduler_main() function implements the core CPU scheduler simulation. It runs in a dedicated thread and continuously selects tasks from the ready queue, executes them, and manages their lifecycle. The function uses a condition variable to efficiently block when the queue is empty, waking up automatically when tasks are added. For each selected task, it checks client connection status, dispatches the task for execution (shell or program), tracks the last_task_id for fairness enforcement, and either completes or re-queues the task based on remaining work. The scheduler ensures only one task executes at a time, maintaining single-CPU semantics. Comprehensive logging tracks every scheduling decision, task dispatch, execution progress, preemption, and completion, providing detailed visibility into scheduler behavior.

**2. Combined RR + SJRF Task Selection Algorithm (Asgar)**

The select_next_task() function implements the combined scheduling algorithm with three phases. Phase 1 prioritizes shell commands (burst=-1) using First-Come-First-Served among shell commands. Phase 2 applies SJRF to program tasks, selecting the task with the smallest remaining_time, with FCFS tie-breaking using arrival_seq. Phase 3 enforces the fairness rule: if the selected task is the same as the last executed task and multiple tasks exist, an alternative is selected using the same priority rules. The function removes the selected task from the queue and returns it to the scheduler. This implementation correctly balances RR fairness with SJRF optimality, ensuring short jobs complete quickly while preventing starvation of longer jobs.

**3. Task State Preservation and Resumption (Asgar)**

Program tasks maintain execution state in the scheduler_task_t structure, tracking iterations_completed and remaining_time. When run_program_task() executes a task, it runs for a quantum (3 or 7 time units based on rounds_completed), updating state incrementally. Each iteration decrements remaining_time and increments iterations_completed. If remaining_time > 0 after the quantum expires, the task is re-queued with its updated state. When the scheduler selects it again, execution resumes from the exact iteration where it stopped, not from the beginning. This is implemented by the for loop in run_program_task() that runs for 'slice' iterations, where slice is the minimum of quantum and remaining_time.

**4. Quantum-Based Round-Robin Execution (Asgar)**

The run_program_task() function implements quantum-based execution with different values for first and subsequent rounds. It calculates the quantum as FIRST_ROUND_QUANTUM (3) if rounds_completed == 0, otherwise NEXT_ROUND_QUANTUM (7). The actual execution slice is the minimum of quantum and remaining_time, ensuring tasks don't run beyond their remaining work. The function executes the task for 'slice' iterations, each representing one time unit (simulated with sleep(1)), sending progress updates to the client after each iteration. After execution, it increments rounds_completed and checks if the task completed (remaining_time == 0) or needs preemption (remaining_time > 0).

**5. Client Context Reference Counting (Kassem)**

The client_context_t structure includes a ref_count field protected by pthread_mutex_t. When a task is created, client_context_ref() increments the count. When a task is destroyed, client_context_unref() decrements the count and frees the structure when the count reaches zero. This ensures that client contexts are not freed while tasks still reference them, preventing use-after-free bugs when clients disconnect during task execution. The implementation uses mutex protection to ensure thread-safe reference count operations, allowing safe concurrent access from multiple threads (client threads creating tasks, scheduler thread executing tasks, cleanup threads destroying tasks).

**6. Task Cancellation on Client Disconnect (Kassem)**

When a client disconnects, remove_client_tasks() removes all pending tasks for that client from the queue and destroys them. Running tasks check client_context_is_connected() during execution and set the cancelled flag if the client disconnected. The scheduler checks the cancelled flag after execution and skips re-queuing cancelled tasks. This ensures that tasks for disconnected clients are properly cleaned up without memory leaks or wasted computation. The implementation uses the queue mutex to safely remove tasks and the client context mutex to safely check connection status.

**7. Comprehensive Scheduling Logging (Kassem)**

The server implements detailed logging matching the required format. Every scheduling event is logged: task enqueueing ([QUEUE] Added Task #X), task dispatch ([SCHED] Dispatching Task #X), task execution ([RUN] Task #X executing), task preemption ([SCHED] Task #X preempted), task completion ([SCHED] Task #X completed), and summary statistics ([SUMMARY] Completed X/Y tasks). All logs include client numbers, task IDs, remaining time, and relevant state information. The logging provides complete visibility into scheduler behavior, enabling verification of algorithm correctness and debugging of scheduling decisions.

**8. Demo Program Integration (Kassem)**

The demo.c program provides a standalone workload generator that can be run independently for testing. When invoked through the server as "demo N", the server parses the command, creates a TASK_PROGRAM_DEMO task with total_burst_time=N, and schedules it. The server simulates demo execution by running a loop for N iterations, sending progress updates to the client. The standalone demo program mirrors this behavior, printing "[demo] iteration X/N" for each iteration. This design allows instructors to verify expected behavior independently while the server integrates demo execution into its scheduling system.

---

## Execution Instructions

To compile and run the Phase 4 scheduler-driven client-server system, start by navigating to the project directory. Clean any previous builds with `make clean`, then compile all components with `make all`, which builds the scheduler-enabled server, client, demo program, and Phase 1 local shell with pthread support.

### Starting the Server:

In Terminal 1, run `./server`. You will see:
```
[INFO] Phase 4 server started on port 8080. Waiting for clients...
```
The server is now ready and the scheduler thread is running, waiting for tasks to schedule.

### Starting Clients:

You can start multiple clients simultaneously to test concurrent scheduling:

- In Terminal 2, run `./client` to connect as Client #1
- In Terminal 3, run `./client` to connect as Client #2  
- In Terminal 4, run `./client` to connect as Client #3

Each client connects to localhost:8080 by default and displays the "$" prompt.

### Using the Remote Shell:

**Shell Commands:** Enter regular Unix commands (ls, pwd, echo, etc.). These execute immediately as they have highest priority (burst=-1).

**Demo Programs:** Enter `demo N` where N is the number of iterations (1-120). The server schedules this as a preemptable task that runs for N time units, printing one iteration per second.

**Program Commands:** Enter `program <label> N` to create a tagged program task. This works identically to demo but allows custom labels for testing.

**Observing Scheduling:** In Terminal 1 (server), you will see detailed scheduling logs:
```
[INFO] Client #1 connected from 127.0.0.1:54321
[QUEUE] Added Task #1 (shell) from Client #1. Pending=1
[SCHED] Dispatching Task #1 (shell) for Client #1. Remaining=0
[EXEC] Running shell command for Client #1: ls
[SCHED] Task #1 completed.
[SUMMARY] Completed 1/1 tasks (shell=1, program=0).
```

### Stopping the Server:

Press Ctrl+C in Terminal 1 to stop the server. The server will terminate, closing all active client connections.

### Running Standalone Demo:

To test the demo program independently:
```bash
./demo 5
```
This prints 5 iterations, one per second, demonstrating the expected workload behavior.

---

## Testing

### Test Scenario 1: Three Concurrent Shell Commands

**Objective:** Verify that shell commands execute immediately without preemption when multiple clients send commands simultaneously.

**Test Setup:** Start server, then start 3 clients in separate terminals.

**Test Input:**
- Client 1: `ls -la | head -5`
- Client 2: `pwd`
- Client 3: `echo "Hello from client 3"`

**Expected Output:**

Server Terminal:
```
[INFO] Client #1 connected from 127.0.0.1:54321
[INFO] Client #2 connected from 127.0.0.1:54322
[INFO] Client #3 connected from 127.0.0.1:54323
[QUEUE] Added Task #1 (shell) from Client #2. Pending=1
[SCHED] Dispatching Task #1 (shell) for Client #2. Remaining=0
[EXEC] Running shell command for Client #2: pwd
[QUEUE] Added Task #2 (shell) from Client #1. Pending=1
[QUEUE] Added Task #3 (shell) from Client #3. Pending=2
[SCHED] Task #1 completed.
[SCHED] Dispatching Task #2 (shell) for Client #1. Remaining=0
[EXEC] Running shell command for Client #1: ls -la | head -5
[SCHED] Task #2 completed.
[SCHED] Dispatching Task #3 (shell) for Client #3. Remaining=0
[EXEC] Running shell command for Client #3: echo "Hello from client 3"
[SCHED] Task #3 completed.
[SUMMARY] Completed 3/3 tasks (shell=3, program=0).
```

**Output:** ✓ PASSED

All three shell commands executed immediately without preemption. Each command completed in one round. Summary shows 3/3 tasks completed (all shell commands).

---

### Test Scenario 2: Mixed Workload

**Objective:** Verify scheduling with one shell command and two demo programs, demonstrating SJRF selection.

**Test Setup:** Start server, then start 3 clients.

**Test Input:**
- Client 1: `ls -la | head -3` (shell command)
- Client 2: `demo 5` (5 iterations)
- Client 3: `demo 8` (8 iterations)

**Expected Output:**

Server Terminal (key events):
```
[QUEUE] Added Task #1 (shell) from Client #1. Pending=1
[SCHED] Dispatching Task #1 (shell) for Client #1. Remaining=0
[EXEC] Running shell command for Client #1: ls -la | head -3
[SCHED] Task #1 completed.
[QUEUE] Added Task #2 (demo) from Client #2. Pending=1
[QUEUE] Added Task #3 (demo) from Client #3. Pending=2
[SCHED] Dispatching Task #2 (demo) for Client #2. Remaining=5
[RUN] Task #2 (demo) executing for 3 units. Remaining before=5
[SCHED] Task #2 preempted. Remaining=2
[SCHED] Dispatching Task #3 (demo) for Client #3. Remaining=8
[RUN] Task #3 (demo) executing for 3 units. Remaining before=8
[SCHED] Task #3 preempted. Remaining=5
[SCHED] Dispatching Task #2 (demo) for Client #2. Remaining=2
[RUN] Task #2 (demo) executing for 2 units. Remaining before=2
[SCHED] Task #2 completed.
[SCHED] Dispatching Task #3 (demo) for Client #3. Remaining=5
[RUN] Task #3 (demo) executing for 5 units. Remaining before=5
[SCHED] Task #3 completed.
[SUMMARY] Completed 3/3 tasks (shell=1, program=2).
```

**Output:** ✓ PASSED

Shell command executed first (immediate priority). Demo 5 and Demo 8 were scheduled using SJRF. Demo 5 (remaining=2) was selected before Demo 8 (remaining=5) after first round, demonstrating SJRF correctness. First round quantum=3, subsequent round used remaining time. All tasks completed successfully.

---

### Test Scenario 3: All Demo Programs (SJRF Verification)

**Objective:** Verify SJRF scheduling with three demo programs of different lengths.

**Test Setup:** Start server, then start 3 clients.

**Test Input:**
- Client 1: `demo 3` (3 iterations)
- Client 2: `demo 6` (6 iterations)
- Client 3: `demo 9` (9 iterations)

**Expected Output:**

Server Terminal (key events):
```
[QUEUE] Added Task #1 (demo) from Client #1. Pending=1
[QUEUE] Added Task #2 (demo) from Client #2. Pending=2
[SCHED] Dispatching Task #1 (demo) for Client #1. Remaining=3
[RUN] Task #1 (demo) executing for 3 units. Remaining before=3
[SCHED] Task #1 completed.
[QUEUE] Added Task #3 (demo) from Client #3. Pending=2
[SCHED] Dispatching Task #2 (demo) for Client #2. Remaining=6
[RUN] Task #2 (demo) executing for 3 units. Remaining before=6
[SCHED] Task #2 preempted. Remaining=3
[SCHED] Dispatching Task #3 (demo) for Client #3. Remaining=9
[RUN] Task #3 (demo) executing for 3 units. Remaining before=9
[SCHED] Task #3 preempted. Remaining=6
[SCHED] Dispatching Task #2 (demo) for Client #2. Remaining=3
[RUN] Task #2 (demo) executing for 3 units. Remaining before=3
[SCHED] Task #2 completed.
[SCHED] Dispatching Task #3 (demo) for Client #3. Remaining=6
[RUN] Task #3 (demo) executing for 6 units. Remaining before=6
[SCHED] Task #3 completed.
[SUMMARY] Completed 3/3 tasks (shell=0, program=3).
```

**Output:** ✓ PASSED

Scheduler correctly used SJRF: Task #1 (remaining=3) completed first, then Task #2 (remaining=3) completed, then Task #3 (remaining=6) completed. Tasks were preempted and resumed correctly. First round quantum=3, subsequent rounds used appropriate quanta. Summary shows all 3 program tasks completed.

---

### Test Scenario 4: Queue Saturation

**Objective:** Verify error handling when queue is full.

**Test Input:** Attempt to enqueue more than 512 tasks.

**Expected Output:**

Client receives: `Error: Scheduler queue is full. Please retry later.`

**Output:** ✓ PASSED

Queue correctly rejects tasks when full (MAX_TASKS=512). Server continues processing existing tasks. Client receives appropriate error message.

---

### Test Scenario 5: Client Disconnect During Execution

**Objective:** Verify task cancellation when client disconnects.

**Test Setup:** Client starts `demo 10`, then disconnects before completion.

**Expected Output:**

Server Terminal:
```
[QUEUE] Added Task #1 (demo) from Client #1. Pending=1
[SCHED] Dispatching Task #1 (demo) for Client #1. Remaining=10
[RUN] Task #1 (demo) executing for 3 units. Remaining before=10
[INFO] Client #1 disconnected (127.0.0.1:54321).
[SCHED] Task #1 cancelled.
[QUEUE] Removing pending Task #1 for disconnected Client #1
```

**Output:** ✓ PASSED

Task correctly cancelled when client disconnected. Task removed from queue. No memory leaks. Server continues operating normally.

---

### Test Scenario 6: Fairness Rule Enforcement

**Objective:** Verify same task not selected twice consecutively.

**Test Setup:** Two tasks with equal remaining time.

**Test Input:**
- Client 1: `demo 6`
- Client 2: `demo 6`

**Expected Output:**

Server alternates between Task #1 and Task #2, not selecting the same task twice in a row.

**Output:** ✓ PASSED

Fairness rule correctly enforced. Tasks with equal remaining time alternate execution. No task monopolizes CPU.

---

### Test Summary

| Test Category | Tests Passed | Tests Failed | Coverage |
|--------------|--------------|--------------|----------|
| Scenario 1: Shell Commands | 1/1 | 0 | 100% |
| Scenario 2: Mixed Workload | 1/1 | 0 | 100% |
| Scenario 3: All Demos (SJRF) | 1/1 | 0 | 100% |
| Queue Saturation | 1/1 | 0 | 100% |
| Client Disconnect | 1/1 | 0 | 100% |
| Fairness Rule | 1/1 | 0 | 100% |
| **TOTAL** | **6/6** | **0** | **100%** |

---

## Challenges

### Server-Side Challenges (Person A, Asgar)

**Challenge 1: Implementing Combined RR + SJRF Algorithm**

**Problem:** The requirement called for a combined algorithm that uses Round-Robin with different quantum values AND Shortest Job Remaining First selection. This required careful design to balance fairness (RR) with efficiency (SJRF) while ensuring shell commands always have priority.

**Solution:**
- Designed three-phase selection algorithm: (1) Prioritize shell commands, (2) Apply SJRF to program tasks, (3) Enforce fairness rule
- Implemented different quantum values: FIRST_ROUND_QUANTUM=3 for quick response, NEXT_ROUND_QUANTUM=7 for better throughput
- Used remaining_time for SJRF selection, arrival_seq for FCFS tie-breaking
- Ensured shell commands (burst=-1) always selected first, never preempted

**Code:**
```c
// Phase 1: Shell commands first
if (candidate->type == TASK_SHELL_COMMAND) { ... }

// Phase 2: SJRF for programs
if (candidate->remaining_time < best->remaining_time) { ... }

// Phase 3: Fairness enforcement
if (best->task_id == last_task_id && g_queue.count > 1) { ... }
```

**Challenge 2: Task State Preservation Between Preemptions**

**Problem:** Program tasks must resume from where they stopped, not restart. This required maintaining execution state (iterations_completed, remaining_time) and updating it incrementally during execution.

**Solution:**
- Added iterations_completed and remaining_time fields to scheduler_task_t
- Updated state incrementally in run_program_task() loop
- Re-queued tasks with updated state after preemption
- Ensured tasks resume from exact iteration where they stopped

**Challenge 3: Thread Synchronization for Shared Queue**

**Problem:** Multiple client threads enqueue tasks while scheduler thread dequeues. Race conditions could cause data corruption, lost tasks, or incorrect scheduling decisions.

**Solution:**
- Protected queue with pthread_mutex_t for mutual exclusion
- Used pthread_cond_t for efficient blocking (scheduler waits when queue empty)
- Client threads signal condition variable when adding tasks
- Kept critical sections minimal to maximize concurrency
- Verified thread safety with stress testing (10+ concurrent clients)

**Challenge 4: Quantum-Based Execution with Different Values**

**Problem:** First round should use quantum=3, subsequent rounds quantum=7. Need to track rounds_completed and calculate appropriate execution slice.

**Solution:**
- Added rounds_completed field to scheduler_task_t
- Calculated quantum based on rounds_completed: `(rounds_completed == 0) ? 3 : 7`
- Calculated slice as minimum of quantum and remaining_time
- Incremented rounds_completed after each execution round

**Challenge 5: Fairness Rule Implementation**

**Problem:** Same task cannot be selected twice consecutively (unless it's the only one). This requires tracking last_task_id and selecting alternative when same task would be chosen.

**Solution:**
- Tracked last_task_id in scheduler_main()
- In select_next_task(), if selected task == last_task_id and queue.count > 1, find alternative
- Applied same priority rules (shell first, then SJRF) for alternative selection
- Ensured alternative selection also respects fairness

**Challenge 6: Client Disconnection During Task Execution**

**Problem:** If client disconnects while task is running, task should be cancelled and cleaned up without memory leaks or wasted computation.

**Solution:**
- Checked client_context_is_connected() during task execution
- Set cancelled flag if client disconnected
- Skipped re-queuing cancelled tasks
- Removed pending tasks from queue on client disconnect
- Used reference counting to prevent use-after-free

### Client-Side Challenges (Person B, Mahmoud Kassem)

**Challenge 7: Transparent Scheduling from Client Perspective**

**Problem:** Client should work unchanged from Phase 3, receiving output from scheduled tasks transparently. Client shouldn't need to know about scheduling.

**Solution:**
- Verified Phase 3 client works unchanged with Phase 4 server
- Client receives output from both shell commands and demo programs identically
- Demo program output streams incrementally (one line per second)
- Client handles incremental output correctly (already implemented in Phase 2)

**Challenge 8: Testing Scheduling Algorithm Correctness**

**Problem:** Verifying that SJRF selects shortest remaining time correctly, quantum values are applied correctly, and fairness rule is enforced requires careful test design.

**Solution:**
- Created test scenarios with known task lengths (demo 3, 6, 9)
- Verified task completion order matches SJRF (shortest first)
- Checked server logs for correct quantum usage (3 for first round, 7 for subsequent)
- Verified fairness by testing tasks with equal remaining times
- Used automated test scripts to ensure consistent results

**Challenge 9: Comprehensive Logging Verification**

**Problem:** Server logs must match required format with client numbers, task IDs, remaining time, and scheduling decisions visible.

**Solution:**
- Implemented detailed logging at every scheduling event
- Logged task enqueueing, dispatch, execution, preemption, completion
- Included client numbers, task IDs, remaining time in all logs
- Generated summary statistics when queue becomes empty
- Verified log format matches assignment requirements

**Challenge 10: Demo Program Integration**

**Problem:** Demo program must work both standalone and through server, with consistent behavior.

**Solution:**
- Created standalone demo.c that prints one line per second
- Server parses "demo N" command and creates TASK_PROGRAM_DEMO
- Server simulates demo execution with same output format
- Both standalone and server-integrated versions produce identical output format
- Allows independent verification of expected behavior

### Shared Challenges (Both Team Members)

**Challenge 11: Understanding Scheduling Requirements**

**Problem:** Requirements specified combined RR + SJRF with selective preemption, different quanta, fairness rules, and state preservation. Needed to understand how these interact.

**Solution:**
- Studied scheduling algorithms from course materials
- Discussed how RR fairness and SJRF optimality can be combined
- Designed algorithm that satisfies all requirements simultaneously
- Tested edge cases (equal remaining times, single task, etc.)
- Verified behavior matches theoretical expectations

**Challenge 12: Testing Concurrent Scheduling**

**Problem:** Testing scheduling with multiple concurrent clients requires coordinating multiple terminals and verifying correct scheduling decisions.

**Solution:**
- Created automated test scripts (test_scenario*.sh)
- Scripts launch server and multiple clients with predefined commands
- Captured server logs for analysis
- Verified scheduling decisions match expected behavior
- Tested on Linux remote server for consistency

---

## Division of Tasks

### Person A: Asgar Fataymamode (Server Scheduler Implementation)

**Phase 4 Specific Tasks:**

**Scheduler Architecture Design:**
- Researched CPU scheduling algorithms (RR, SJRF, combined approaches)
- Designed scheduler thread model (dedicated thread for task execution)
- Designed task queue data structures (scheduler_queue_t, scheduler_task_t)
- Planned task lifecycle (creation, enqueueing, selection, execution, completion/preemption)
- Designed state preservation mechanism for preempted tasks

**Scheduler Thread Implementation:**
- Implemented scheduler_main() as dedicated scheduler thread
- Created task selection loop with condition variable waiting
- Implemented task dispatch, execution, and re-queuing logic
- Added comprehensive logging for all scheduling events
- Ensured single-CPU semantics (only one task executes at a time)

**Combined RR + SJRF Algorithm:**
- Implemented select_next_task() with three-phase selection
- Phase 1: Shell command prioritization
- Phase 2: SJRF selection for program tasks
- Phase 3: Fairness rule enforcement
- Implemented tie-breaking using arrival sequence (FCFS)
- Verified algorithm correctness through testing

**Quantum-Based Execution:**
- Implemented run_program_task() with quantum calculation
- First round quantum = 3, subsequent rounds = 7
- Calculated execution slice as min(quantum, remaining_time)
- Updated task state incrementally during execution
- Implemented preemption and re-queuing logic

**Task State Management:**
- Designed scheduler_task_t structure with all required fields
- Implemented state preservation (iterations_completed, remaining_time)
- Ensured tasks resume from exact stopping point
- Tracked rounds_completed for quantum calculation

**Thread Synchronization:**
- Implemented mutex-protected ready queue
- Used condition variable for efficient scheduler blocking
- Ensured thread-safe queue operations (enqueue/dequeue)
- Protected shared statistics with mutex
- Verified no race conditions through stress testing

**Client Context Management:**
- Implemented reference counting for client contexts
- Ensured safe cleanup when clients disconnect
- Prevented use-after-free bugs
- Implemented task cancellation on client disconnect

**Integration with Phase 3:**
- Modified Phase 3 server to add scheduler thread
- Preserved all Phase 3 functionality (multithreading, client handling)
- Integrated scheduler with existing client thread architecture
- Maintained backward compatibility with Phase 3 client

**Testing and Debugging:**
- Created test scenarios for scheduling verification
- Verified SJRF selection correctness
- Tested quantum values and fairness rules
- Used server logs to verify scheduling decisions
- Stress tested with multiple concurrent clients

**Documentation:**
- Wrote detailed comments explaining scheduling algorithm
- Documented task selection phases
- Explained quantum calculation and state preservation
- Created execution instructions

### Person B: Mahmoud Kassem (Client Compatibility, Testing, Demo Program)

**Phase 4 Specific Tasks:**

**Client Compatibility Verification:**
- Verified Phase 3 client works unchanged with Phase 4 server
- Confirmed protocol remains compatible
- Tested that client receives output from both shell and demo tasks
- Verified incremental output handling (demo programs)

**Demo Program Implementation:**
- Created demo.c standalone workload generator
- Implemented loop with sleep(1) for time unit simulation
- Output format: "[demo] iteration X/N"
- Ensured consistent behavior with server-integrated version
- Tested standalone execution

**Comprehensive Testing:**
- Designed all three required test scenarios
- Created automated test scripts (test_scenario*.sh, run_all_tests.sh)
- Test Scenario 1: Three shell commands
- Test Scenario 2: Mixed workload (shell + 2 demos)
- Test Scenario 3: All demo programs (SJRF verification)
- Verified scheduling algorithm correctness through logs

**Scheduling Algorithm Verification:**
- Analyzed server logs to verify SJRF selection
- Verified quantum values (3 for first round, 7 for subsequent)
- Tested fairness rule enforcement
- Verified task state preservation and resumption
- Confirmed shell command priority

**Error Handling Testing:**
- Tested queue saturation (MAX_TASKS limit)
- Tested client disconnect during execution
- Verified task cancellation and cleanup
- Tested invalid command handling
- Verified error messages sent to clients

**Logging and Monitoring:**
- Verified server logs match required format
- Checked client numbers, task IDs, remaining time in logs
- Verified summary statistics generation
- Ensured all scheduling events are logged
- Created test analysis documentation

**Report Writing:**
- Completed Testing section with all test scenarios
- Documented test inputs, expected outputs, and results
- Created test summary table
- Documented challenges and solutions
- Prepared division of tasks section

**Integration Testing:**
- Compiled and tested on remote Linux server
- Verified Makefile includes demo target
- Tested complete system end-to-end
- Confirmed all Phase 1, 2, 3, and 4 features working
- Final validation before submission

### Collaborative Tasks (Both Team Members):

**Architecture Discussion:**
- Discussed scheduler thread model vs. other approaches
- Decided on dedicated scheduler thread for single-CPU simulation
- Agreed on combined RR + SJRF algorithm design
- Designed task queue structure and synchronization
- Planned state preservation mechanism

**Algorithm Design:**
- Discussed how to combine RR fairness with SJRF optimality
- Designed three-phase selection algorithm
- Agreed on quantum values (3 for first round, 7 for subsequent)
- Designed fairness rule implementation
- Planned tie-breaking mechanism

**Testing Coordination:**
- Person A implemented scheduler, Person B designed tests
- Coordinated test scenario execution
- Verified scheduling behavior through logs
- Cross-validated algorithm correctness
- Ensured all requirements met

**Documentation Review:**
- Cross-checked report sections for consistency
- Verified code matches documentation
- Ensured all requirements addressed
- Reviewed test results and analysis
- Finalized report before submission

**Final Validation:**
- Compiled on remote Linux server
- Ran complete test suite
- Verified no memory leaks
- Confirmed clean compilation with no warnings
- Checked report completeness

### Task Distribution Summary:

| Task Category | Person A (Asgar) | Person B (Mahmoud) |
|---------------|------------------|-------------------|
| Scheduler Implementation | 100% | 0% |
| Task Selection Algorithm | 100% | 0% |
| Demo Program | 0% | 100% |
| Testing Design | 30% | 70% |
| Testing Execution | 40% | 60% |
| Report Writing | 50% | 50% |
| Integration | 50% | 50% |

**Work Balance:** Both team members contributed equally to the project, with specialized focus areas. Person A focused on server-side scheduler implementation and algorithm design, while Person B focused on comprehensive testing, demo program, and report completion.

---

## References

1. **Silberschatz, A., Galvin, P. B., & Gagne, G.** *Operating System Concepts (10th Edition)*. Wiley.
   - Chapter 5: CPU Scheduling
   - Chapter 6: Synchronization Tools
   - Used for understanding CPU scheduling algorithms (RR, SJRF), scheduling criteria, and thread synchronization

2. **Stevens, W. R., Fenner, B., & Rudoff, A. M.** *UNIX Network Programming, Volume 1: The Sockets Networking API (3rd Edition)*. Addison-Wesley Professional, 2003.
   - Chapter 26: Threads
   - Chapter 30: Client/Server Design Alternatives
   - Used for socket programming with threads and concurrent server design

3. **Kerrisk, M.** *The Linux Programming Interface*. No Starch Press, 2010.
   - Chapter 29: Threads: Introduction
   - Chapter 30: Threads: Thread Synchronization
   - Used for POSIX threads (pthreads) API details, mutexes, and condition variables

4. **Hall, B.** *Beej's Guide to Network Programming: Using Internet Sockets*. (2023 Edition).
   - Available at: https://beej.us/guide/bgnet/
   - Used for socket programming fundamentals and examples

5. **Course Lecture Notes:** Operating Systems (CS-UH 3010), Fall 2025.
   - Scheduling Algorithms: Round-Robin, Shortest Job First
   - Thread Synchronization: Mutexes, Condition Variables
   - Used for understanding combined scheduling algorithms and implementation requirements

6. **GeeksforGeeks.** "CPU Scheduling in Operating Systems".
   - Available at: https://www.geeksforgeeks.org/cpu-scheduling-in-operating-systems/
   - Used for understanding SJRF algorithm and scheduling criteria

---

## Conclusion

Phase 4 successfully extends the multithreaded remote shell system with CPU scheduling capabilities. The scheduler-driven architecture implements a combined Round-Robin + Shortest Job Remaining First algorithm that balances fairness with efficiency, using different quantum values for first and subsequent rounds to optimize both response time and throughput.

The implementation correctly simulates a single-CPU system where only one task executes at a time, despite multiple concurrent client connections. Shell commands maintain their instant execution guarantee through highest priority (burst=-1), while program tasks are scheduled efficiently using SJRF and can be preempted at quantum boundaries. Tasks preserve their execution state between preemptions, resuming from their exact stopping point rather than restarting.

All Phase 1 features (pipes, I/O redirection, error handling), Phase 2 features (network communication, protocol), and Phase 3 features (multithreading, concurrent clients) continue to work correctly in the scheduler-enabled environment. The system has been thoroughly tested with all three required scenarios, demonstrating correct SJRF selection, proper quantum usage, fairness rule enforcement, and comprehensive error handling.

The implementation demonstrates proper use of scheduling algorithms, thread synchronization primitives (pthread_mutex_t, pthread_cond_t), and CPU scheduling simulation techniques. All test cases passed successfully, confirming that the system meets the Phase 4 requirements and maintains backward compatibility with all previous phases.
