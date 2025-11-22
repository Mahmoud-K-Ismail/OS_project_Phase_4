# Phase 4: Scheduler-Driven Remote Shell

## Overview

Phase 4 upgrades the distributed shell from earlier phases with a server-side CPU scheduler. All client commands are modeled as tasks that compete for a single simulated CPU. Tasks are dispatched by a dedicated scheduler thread that combines Shortest-Job-Remaining-First (SJRF) with a selectively preemptive Round-Robin (RR) policy:

- First quantum: 3 seconds/time units (fast response for short jobs)
- Subsequent quanta: 7 seconds/time units (better throughput for long jobs)
- Shell commands are given burst time `-1`, which makes them run immediately and finish within a single round
- Program commands (`demo N` or `program <label> N`) are preemptable jobs that resume from the exact iteration where they stopped
- A task cannot be scheduled twice in a row (unless it is the only remaining job), ensuring fairness among waiting clients

The result is a single-core simulation where multiple remote clients can enqueue commands concurrently without ever running more than one job at a time.

## Components

| File | Description |
| --- | --- |
| `server.c` | Accepts clients, enqueues requests, and runs the scheduler/CPU simulation |
| `client.c` | Text-based shell that forwards user commands to the scheduler-enabled server |
| `shell_utils.c/.h` | Phase 1 parsing/execution engine used for real shell commands |
| `demo.c` | Stand-alone workload generator that prints one line per second for `N` seconds |
| `myshell.c` | Local shell from Phase 1 (unchanged) |
| `tests/` | Input files used by previous phases, still useful for regression tests |

## Building

```bash
# Build every deliverable (myshell, scheduler server, client, demo)
make all

# Individual builds
make server
make client
make myshell
make demo

# Cleanup
make clean
make rebuild   # clean + build all
```

All targets compile with `-Wall -Wextra -std=c99 -pedantic` and link the server with pthreads.

## Running the System

1. **Start the server** (terminal A)
   ```bash
   ./server
   ```
   The server prints detailed logs describing queue operations, scheduling decisions, task state changes, and rolling summaries whenever the ready queue becomes empty.

2. **Launch one or more clients** (terminal B/C/D)
   ```bash
   ./client                # localhost:8080
   ./client <ip> <port>    # optional remote testing
   ```

3. **Interact using two command families**

   - **Shell commands** (regular Unix commands, pipes, redirections). Example:
     ```
     $ ls -l | grep .c
     ```
     These jobs are inserted into the queue with burst `-1`, immediately dispatched, executed through the Phase 1 parser, and their captured stdout/stderr is streamed back to the client.

   - **Program commands** (scheduled, preemptable workloads):
     - `demo N` &rarr; creates a simulated process that runs for `N` iterations/seconds. Output is streamed once per second (`[demo][task X] iteration i/N`).
     - `program <label> N` &rarr; identical to `demo` but lets you tag the job in logs. Useful for scripting complex test scenarios.

   All outputs are still newline-terminated text, so the original Phase 2/3 client works without modification.

4. **Exit**
   ```
   $ exit
   ```
   The client sends `exit`, the server acknowledges, removes pending jobs for that client, and frees the associated scheduler tasks.

## Scheduling Details

- **Task Metadata**: Each task keeps the client id, command string, total burst, remaining burst, rounds executed, and status flags. A reference-counted client context prevents dangling pointers when clients leave.
- **Queue Structure**: A bounded ready queue protected by a mutex/condition variable. Shell commands skip to the front. Among program tasks we pick the job with the smallest remaining time; ties fall back to arrival order.
- **Selective Preemption**: `demo/program` jobs run for a quantum and then either complete or get re-queued. Shell commands never re-enter the queue—they run to completion in one CPU slice.
- **Fairness Guard**: The most recently executed task cannot be dispatched twice in a row unless it is the only job left, satisfying the "no back-to-back wins" requirement.
- **Logging**: Every enqueue, dispatch, completion, cancellation, and summary event prints to the server console with the client id, command label, and remaining burst. These logs mirror the screenshots shared in the project guidelines.

## Testing Scenarios

| Scenario | Steps |
| --- | --- |
| **1. Three shell commands** | Launch three clients, each issues a single `ls`/`pwd` style command. Observe that each command gets its own task id, runs immediately, and prints `[SUMMARY] Completed 3/3 tasks ...` once the queue drains. |
| **2. Mixed workloads** | Client A runs `ls`, Client B runs `demo 5`, Client C runs `demo 8`. Shell command finishes first, then SJRF alternates between the two demo jobs while enforcing the 3s/7s RR quanta. |
| **3. All demo jobs** | Start three clients with `demo 3`, `demo 6`, `demo 9`. Watch how the scheduler always picks the shortest remaining job, preempts longer ones at the end of each quantum, and resumes them exactly where they left off. |

You can also build and run the stand-alone `demo` program to better visualize the expected per-second output before integrating it into the scheduler:
```bash
./demo 4
```

## Directory Layout

```
OS_Project_Phase_4/
├── client.c
├── server.c
├── demo.c
├── myshell.c
├── shell_utils.c / shell_utils.h
├── Makefile
├── README.md
└── tests/
```

## Troubleshooting & Notes

- The ready queue holds up to 512 pending tasks. If it fills up, clients receive a retry message without crashing the server.
- The server ignores `SIGPIPE`, so a client that disconnects abruptly simply causes its tasks to be cancelled and cleaned up.
- `demo`/`program` jobs are capped at `MAX_SIMULATED_BURST = 120` to keep demos reasonable. This limit is configurable at the top of `server.c`.
- The underlying Phase 1 shell implementation remains untouched, so regression tests from previous phases still pass.
- A rolling summary is printed every time the queue becomes empty: `[SUMMARY] Completed X/Y tasks (shell=A, program=B)` which mirrors the grading screenshots.

## Next Steps

- Record the demo video showcasing the three required scenarios.
- Update the written report (see `docs/Phase4_Report.md`) with the architecture, implementation highlights, testing evidence, challenges, and division of labor.
