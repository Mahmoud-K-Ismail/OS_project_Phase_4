#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "shell_utils.h"

#define PORT 8080
#define BUFFER_SIZE 4096
#define BACKLOG 16
#define MAX_TASKS 512
#define FIRST_ROUND_QUANTUM 3
#define NEXT_ROUND_QUANTUM 7
#define MAX_LABEL_LEN 64
#define MAX_SIMULATED_BURST 120

typedef enum {
    TASK_SHELL_COMMAND = 0,
    TASK_PROGRAM_DEMO,
    TASK_PROGRAM_GENERIC
} task_type_t;

typedef struct client_context {
    int client_fd;
    int client_id;
    char client_ip[INET_ADDRSTRLEN];
    int client_port;
    int connected;
    int ref_count;
    pthread_mutex_t lock;
} client_context_t;

typedef struct scheduler_task {
    int task_id;
    task_type_t type;
    char command[BUFFER_SIZE];
    char label[MAX_LABEL_LEN];
    int total_burst_time;
    int remaining_time;
    int rounds_completed;
    int iterations_completed;
    bool cancelled;
    long long arrival_seq;
    time_t created_at;
    client_context_t* client;
} scheduler_task_t;

typedef struct scheduler_queue {
    scheduler_task_t* tasks[MAX_TASKS];
    size_t count;
    long long next_arrival_seq;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} scheduler_queue_t;

typedef struct scheduler_stats {
    int total_created;
    int total_completed;
    int completed_shell;
    int completed_program;
} scheduler_stats_t;

static scheduler_queue_t g_queue = {
    .tasks = {0},
    .count = 0,
    .next_arrival_seq = 0,
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .cond = PTHREAD_COND_INITIALIZER};
static scheduler_stats_t g_stats = {0};
static pthread_mutex_t g_stats_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t g_scheduler_thread;
static int g_scheduler_running = 1;
static pthread_mutex_t g_id_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_next_task_id = 0;
static int g_next_client_id = 0;

static int create_server_socket(void);
static void* scheduler_main(void* arg);
static void* client_thread_main(void* arg);
static void handle_client_commands(client_context_t* client);
static scheduler_task_t* create_task_from_command(const char* command, client_context_t* client, char* error_buf, size_t error_size);
static bool try_parse_demo_request(const char* command, scheduler_task_t* task, char* error_buf, size_t error_size);
static bool try_parse_program_keyword(const char* command, scheduler_task_t* task, char* error_buf, size_t error_size);
static int enqueue_task(scheduler_task_t* task);
static scheduler_task_t* select_next_task(int last_task_id);
static void destroy_task(scheduler_task_t* task);
static void remove_client_tasks(client_context_t* client);
static void run_shell_task(scheduler_task_t* task);
static void run_program_task(scheduler_task_t* task);
static void update_stats_on_enqueue(task_type_t type);
static void update_stats_on_completion(task_type_t type);
static void maybe_print_scheduler_summary(void);
static void trim_trailing_whitespace(char* str);
static int is_blank_line(const char* str);
static int send_buffer(client_context_t* client, const char* data, size_t length);
static int send_string(client_context_t* client, const char* data);
static int send_formatted(client_context_t* client, const char* fmt, ...);
static void client_context_ref(client_context_t* client);
static void client_context_unref(client_context_t* client);
static void client_context_disconnect(client_context_t* client);
static int client_context_is_connected(client_context_t* client);
char* execute_command_with_capture(const char* command, int* success);

int main(void) {
    signal(SIGPIPE, SIG_IGN);

    int server_fd = create_server_socket();
    if (server_fd == -1) {
        fprintf(stderr, "[ERROR] Failed to start server.\n");
        return EXIT_FAILURE;
    }

    if (pthread_create(&g_scheduler_thread, NULL, scheduler_main, NULL) != 0) {
        perror("[ERROR] Failed to start scheduler thread");
        close(server_fd);
        return EXIT_FAILURE;
    }

    printf("[INFO] Phase 4 server started on port %d. Waiting for clients...\n", PORT);
    fflush(stdout);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd == -1) {
            perror("[ERROR] accept failed");
            continue;
        }

        client_context_t* client = calloc(1, sizeof(client_context_t));
        if (client == NULL) {
            perror("[ERROR] Failed to allocate client context");
            close(client_fd);
            continue;
        }

        pthread_mutex_init(&client->lock, NULL);
        client->client_fd = client_fd;
        client->client_port = ntohs(client_addr.sin_port);
        inet_ntop(AF_INET, &(client_addr.sin_addr), client->client_ip, sizeof(client->client_ip));
        client->connected = 1;
        client->ref_count = 1; // reference held by client thread

        pthread_mutex_lock(&g_id_mutex);
        g_next_client_id++;
        client->client_id = g_next_client_id;
        pthread_mutex_unlock(&g_id_mutex);

        printf("[INFO] Client #%d connected from %s:%d\n", client->client_id, client->client_ip, client->client_port);
        fflush(stdout);

        pthread_t client_thread;
        if (pthread_create(&client_thread, NULL, client_thread_main, client) != 0) {
            perror("[ERROR] Failed to create client thread");
            client_context_disconnect(client);
            client_context_unref(client);
            continue;
        }
        pthread_detach(client_thread);
    }

    g_scheduler_running = 0;
    pthread_cond_broadcast(&g_queue.cond);
    pthread_join(g_scheduler_thread, NULL);
    close(server_fd);
    return EXIT_SUCCESS;
}

static int create_server_socket(void) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("[ERROR] socket");
        return -1;
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("[ERROR] setsockopt");
        close(server_fd);
        return -1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("[ERROR] bind");
        close(server_fd);
        return -1;
    }

    if (listen(server_fd, BACKLOG) == -1) {
        perror("[ERROR] listen");
        close(server_fd);
        return -1;
    }

    return server_fd;
}

static void* client_thread_main(void* arg) {
    client_context_t* client = (client_context_t*)arg;
    handle_client_commands(client);
    client_context_disconnect(client);
    remove_client_tasks(client);
    printf("[INFO] Client #%d disconnected (%s:%d).\n", client->client_id, client->client_ip, client->client_port);
    fflush(stdout);
    client_context_unref(client);
    return NULL;
}

static void handle_client_commands(client_context_t* client) {
    char buffer[BUFFER_SIZE];

    while (client_context_is_connected(client)) {
        memset(buffer, 0, sizeof(buffer));
        ssize_t bytes_received = recv(client->client_fd, buffer, sizeof(buffer) - 1, 0);

        if (bytes_received <= 0) {
            if (bytes_received < 0) {
                perror("[ERROR] recv");
            }
            break;
        }

        buffer[bytes_received] = '\0';
        trim_trailing_whitespace(buffer);

        if (is_blank_line(buffer)) {
            continue;
        }

        printf("[RECEIVED] Client #%d (%s:%d) sent: %s\n", client->client_id, client->client_ip, client->client_port, buffer);
        fflush(stdout);

        if (strcmp(buffer, "exit") == 0) {
            send_string(client, "\n");
            break;
        }

        char error_buf[128] = {0};
        scheduler_task_t* task = create_task_from_command(buffer, client, error_buf, sizeof(error_buf));
        if (task == NULL) {
            if (strlen(error_buf) == 0) {
                strncpy(error_buf, "Error: Failed to allocate task\n", sizeof(error_buf) - 1);
            }
            send_string(client, error_buf);
            continue;
        }

        if (enqueue_task(task) != 0) {
            send_string(client, "Error: Scheduler queue is full. Please retry later.\n");
            destroy_task(task);
            continue;
        }
    }
}

static scheduler_task_t* create_task_from_command(const char* command, client_context_t* client, char* error_buf, size_t error_size) {
    scheduler_task_t* task = calloc(1, sizeof(scheduler_task_t));
    if (task == NULL) {
        snprintf(error_buf, error_size, "Error: Unable to allocate task.\n");
        return NULL;
    }

    pthread_mutex_lock(&g_id_mutex);
    g_next_task_id++;
    task->task_id = g_next_task_id;
    pthread_mutex_unlock(&g_id_mutex);

    strncpy(task->command, command, sizeof(task->command) - 1);
    task->client = client;
    task->created_at = time(NULL);

    bool parsed_program = false;
    if (try_parse_demo_request(command, task, error_buf, error_size)) {
        parsed_program = true;
    } else if (try_parse_program_keyword(command, task, error_buf, error_size)) {
        parsed_program = true;
    }

    if (!parsed_program) {
        task->type = TASK_SHELL_COMMAND;
        snprintf(task->label, sizeof(task->label), "shell");
        task->total_burst_time = -1; // shell commands always win priority
        task->remaining_time = 0;
    }

    client_context_ref(client);
    return task;
}

static bool try_parse_demo_request(const char* command, scheduler_task_t* task, char* error_buf, size_t error_size) {
    char keyword[16];
    long iterations = 0;
    if (sscanf(command, "%15s %ld", keyword, &iterations) != 2) {
        return false;
    }

    if (strcmp(keyword, "demo") != 0) {
        return false;
    }

    if (iterations <= 0 || iterations > MAX_SIMULATED_BURST) {
        snprintf(error_buf, error_size, "Error: demo requires 1-%d iterations.\n", MAX_SIMULATED_BURST);
        return false;
    }

    task->type = TASK_PROGRAM_DEMO;
    snprintf(task->label, sizeof(task->label), "demo");
    task->total_burst_time = (int)iterations;
    task->remaining_time = (int)iterations;
    return true;
}

static bool try_parse_program_keyword(const char* command, scheduler_task_t* task, char* error_buf, size_t error_size) {
    char keyword[16];
    char name[MAX_LABEL_LEN];
    long iterations = 0;
    if (sscanf(command, "%15s %63s %ld", keyword, name, &iterations) != 3) {
        return false;
    }

    if (strcmp(keyword, "program") != 0) {
        return false;
    }

    if (iterations <= 0 || iterations > MAX_SIMULATED_BURST) {
        snprintf(error_buf, error_size, "Error: program burst must be 1-%d.\n", MAX_SIMULATED_BURST);
        return false;
    }

    task->type = TASK_PROGRAM_GENERIC;
    snprintf(task->label, sizeof(task->label), "%s", name);
    task->total_burst_time = (int)iterations;
    task->remaining_time = (int)iterations;
    return true;
}

static int enqueue_task(scheduler_task_t* task) {
    pthread_mutex_lock(&g_queue.mutex);
    if (g_queue.count >= MAX_TASKS) {
        pthread_mutex_unlock(&g_queue.mutex);
        return -1;
    }

    task->arrival_seq = ++g_queue.next_arrival_seq;
    g_queue.tasks[g_queue.count++] = task;
    update_stats_on_enqueue(task->type);
    pthread_cond_broadcast(&g_queue.cond);
    size_t pending = g_queue.count;
    pthread_mutex_unlock(&g_queue.mutex);

    printf("[QUEUE] Added Task #%d (%s) from Client #%d. Pending=%zu\n", task->task_id, task->label, task->client->client_id, pending);
    fflush(stdout);
    return 0;
}

/**
 * Selects the next task from the ready queue using combined RR + SJRF algorithm.
 * 
 * Scheduling Policy:
 * 1. Shell commands (burst=-1) always have highest priority and execute immediately
 * 2. Among program tasks, use Shortest Job Remaining First (SJRF)
 * 3. Fairness rule: same task cannot be selected twice consecutively (unless it's the only one)
 * 4. Tie-breaking: when remaining times are equal, use First-Come-First-Served (arrival_seq)
 * 
 * @param last_task_id The task ID that was executed in the previous scheduling decision
 * @return Pointer to the selected task, or NULL if queue is empty
 */
static scheduler_task_t* select_next_task(int last_task_id) {
    scheduler_task_t* best = NULL;
    int best_index = -1;

    // Phase 1: Prioritize shell commands (burst time = -1, highest priority)
    // Shell commands must execute immediately without preemption
    // Among multiple shell commands, use FCFS based on arrival sequence
    for (size_t i = 0; i < g_queue.count; ++i) {
        scheduler_task_t* candidate = g_queue.tasks[i];
        if (candidate->type == TASK_SHELL_COMMAND) {
            if (best == NULL || candidate->arrival_seq < best->arrival_seq) {
                best = candidate;
                best_index = (int)i;
            }
        }
    }

    // Phase 2: If no shell commands, apply SJRF (Shortest Job Remaining First)
    // Select the task with the smallest remaining_time
    // This minimizes average waiting time and ensures shorter jobs complete first
    if (best == NULL) {
        for (size_t i = 0; i < g_queue.count; ++i) {
            scheduler_task_t* candidate = g_queue.tasks[i];
            // Compare remaining_time: smaller is better (SJRF principle)
            // If equal, use arrival_seq for FCFS tie-breaking
            if (best == NULL || candidate->remaining_time < best->remaining_time ||
                (candidate->remaining_time == best->remaining_time && candidate->arrival_seq < best->arrival_seq)) {
                best = candidate;
                best_index = (int)i;
            }
        }
    }

    // Phase 3: Fairness enforcement - prevent same task from running twice in a row
    // This ensures Round-Robin fairness among tasks with equal remaining times
    // Exception: if it's the only task in queue, it can run again
    if (best != NULL && best->task_id == last_task_id && g_queue.count > 1) {
        scheduler_task_t* second = NULL;
        int second_index = -1;

        // First, check for shell commands (they always have priority even in fairness check)
        for (size_t i = 0; i < g_queue.count; ++i) {
            scheduler_task_t* candidate = g_queue.tasks[i];
            if (candidate->task_id == last_task_id) {
                continue;  // Skip the task that just ran
            }
            if (candidate->type == TASK_SHELL_COMMAND) {
                if (second == NULL || candidate->arrival_seq < second->arrival_seq) {
                    second = candidate;
                    second_index = (int)i;
                }
            }
        }

        // If no shell command found, use SJRF for program tasks
        // Find the next best program task (excluding the one that just ran)
        if (second == NULL) {
            for (size_t i = 0; i < g_queue.count; ++i) {
                scheduler_task_t* candidate = g_queue.tasks[i];
                if (candidate->task_id == last_task_id) {
                    continue;  // Skip the task that just ran
                }

                // Apply SJRF: select shortest remaining time
                // Tie-break with arrival_seq (FCFS)
                if (second == NULL || candidate->remaining_time < second->remaining_time ||
                    (candidate->remaining_time == second->remaining_time && candidate->arrival_seq < second->arrival_seq)) {
                    second = candidate;
                    second_index = (int)i;
                }
            }
        }

        // Use the alternative task if found, otherwise keep the original (only one task case)
        if (second != NULL) {
            best = second;
            best_index = second_index;
        }
    }

    if (best == NULL) {
        return NULL;
    }

    g_queue.count--;
    for (size_t i = (size_t)best_index; i < g_queue.count; ++i) {
        g_queue.tasks[i] = g_queue.tasks[i + 1];
    }

    return best;
}

static void destroy_task(scheduler_task_t* task) {
    if (task == NULL) {
        return;
    }
    if (task->client) {
        client_context_unref(task->client);
    }
    free(task);
}

static void remove_client_tasks(client_context_t* client) {
    pthread_mutex_lock(&g_queue.mutex);
    size_t write_index = 0;
    for (size_t i = 0; i < g_queue.count; ++i) {
        scheduler_task_t* task = g_queue.tasks[i];
        if (task->client == client) {
            printf("[QUEUE] Removing pending Task #%d for disconnected Client #%d\n", task->task_id, client->client_id);
            destroy_task(task);
            continue;
        }
        g_queue.tasks[write_index++] = task;
    }
    g_queue.count = write_index;
    pthread_mutex_unlock(&g_queue.mutex);
}

/**
 * Main scheduler thread function - implements the CPU scheduler simulation.
 * 
 * This function runs in a dedicated thread and simulates a single-CPU scheduler:
 * - Waits for tasks to arrive in the ready queue
 * - Selects the next task using combined RR + SJRF algorithm
 * - Executes the task for its allocated quantum
 * - Preempts and re-queues tasks that haven't completed
 * - Removes completed tasks from the system
 * 
 * The scheduler ensures:
 * - Only one task runs at a time (single-CPU simulation)
 * - Shell commands complete in one round (never preempted)
 * - Program tasks are preempted at quantum boundaries
 * - Tasks resume from where they stopped (not restarting)
 * 
 * @param arg Unused thread argument
 * @return NULL on exit
 */
static void* scheduler_main(void* arg) {
    (void)arg;
    int last_task_id = -1;  // Track last executed task for fairness rule

    // Main scheduling loop - continues until server shutdown
    while (g_scheduler_running) {
        // Acquire queue lock to safely access shared queue state
        pthread_mutex_lock(&g_queue.mutex);
        
        // Wait for tasks to arrive if queue is empty
        // Condition variable allows efficient blocking until a task is enqueued
        while (g_queue.count == 0 && g_scheduler_running) {
            pthread_cond_wait(&g_queue.cond, &g_queue.mutex);
        }

        // Check if scheduler should shut down
        if (!g_scheduler_running) {
            pthread_mutex_unlock(&g_queue.mutex);
            break;
        }

        // Select next task using combined RR + SJRF algorithm
        // This is the core scheduling decision point
        scheduler_task_t* task = select_next_task(last_task_id);
        pthread_mutex_unlock(&g_queue.mutex);  // Release lock before execution

        // If no task selected (shouldn't happen, but safety check)
        if (task == NULL) {
            continue;
        }

        // Check if client disconnected while task was in queue
        // If so, cancel the task and skip execution
        if (!client_context_is_connected(task->client)) {
            printf("[SCHED] Dropping Task #%d because Client #%d disconnected.\n", task->task_id, task->client->client_id);
            destroy_task(task);
            continue;
        }

        // Log task dispatch for monitoring and debugging
        printf("[SCHED] Dispatching Task #%d (%s) for Client #%d. Remaining=%d\n", task->task_id, task->label, task->client->client_id, task->remaining_time);
        fflush(stdout);

        // Execute task based on type
        // Shell commands: execute immediately, complete in one round
        // Program tasks: execute for quantum, may be preempted
        if (task->type == TASK_SHELL_COMMAND) {
            run_shell_task(task);
            task->remaining_time = 0;  // Shell commands always complete
        } else {
            run_program_task(task);  // May be preempted after quantum
        }

        // Update last_task_id for fairness rule enforcement
        last_task_id = task->task_id;

        // Check if task needs to be re-queued (preempted, not completed)
        // Shell commands never reach here (remaining_time = 0)
        if (task->remaining_time > 0 && !task->cancelled) {
            // Re-queue the task for further execution
            // Update arrival sequence to maintain FCFS ordering among tasks with same remaining time
            pthread_mutex_lock(&g_queue.mutex);
            task->arrival_seq = ++g_queue.next_arrival_seq;
            g_queue.tasks[g_queue.count++] = task;
            pthread_cond_broadcast(&g_queue.cond);  // Wake up scheduler if it was waiting
            pthread_mutex_unlock(&g_queue.mutex);
            printf("[SCHED] Task #%d preempted. Remaining=%d\n", task->task_id, task->remaining_time);
            fflush(stdout);
            continue;  // Continue to next scheduling cycle
        }

        if (task->cancelled) {
            printf("[SCHED] Task #%d cancelled.\n", task->task_id);
        } else {
            printf("[SCHED] Task #%d completed.\n", task->task_id);
        }
        fflush(stdout);

        update_stats_on_completion(task->type);
        destroy_task(task);
        maybe_print_scheduler_summary();
    }

    return NULL;
}

static void run_shell_task(scheduler_task_t* task) {
    printf("[EXEC] Running shell command for Client #%d: %s\n", task->client->client_id, task->command);
    fflush(stdout);

    int success = 0;
    char* output = execute_command_with_capture(task->command, &success);

    if (output == NULL) {
        send_string(task->client, "Error: Server failed to execute command.\n");
        return;
    }

    if (strlen(output) == 0) {
        send_string(task->client, "\n");
    } else {
        send_string(task->client, output);
    }

    free(output);
}

/**
 * Executes a program task (demo or generic program) for its allocated quantum.
 * 
 * Quantum Allocation (Round-Robin component):
 * - First round: 3 time units (allows quick response for short jobs)
 * - Subsequent rounds: 7 time units (better throughput for longer jobs)
 * 
 * Execution Model:
 * - Each iteration represents one time unit (simulated with sleep(1))
 * - Task state is preserved between preemptions (iterations_completed, remaining_time)
 * - Task resumes from exact point where it stopped (not restarting)
 * 
 * Preemption:
 * - Task runs for 'slice' time units (min of quantum and remaining_time)
 * - After quantum expires, task is preempted and re-queued
 * - If remaining_time < quantum, task completes in this round
 * 
 * @param task The program task to execute
 */
static void run_program_task(scheduler_task_t* task) {
    // Determine quantum based on round number (RR with different quanta)
    // First round uses smaller quantum (3) for better response time
    // Subsequent rounds use larger quantum (7) for better throughput
    int quantum = (task->rounds_completed == 0) ? FIRST_ROUND_QUANTUM : NEXT_ROUND_QUANTUM;
    
    // Calculate actual execution slice (don't exceed remaining work)
    // If task has less work remaining than quantum, execute only what's needed
    int slice = (task->remaining_time < quantum) ? task->remaining_time : quantum;

    // Log execution start for monitoring
    printf("[RUN] Task #%d (%s) executing for %d units. Remaining before=%d\n", task->task_id, task->label, slice, task->remaining_time);
    fflush(stdout);

    // Execute task for 'slice' iterations (simulating CPU time)
    // Each iteration represents one time unit of work
    for (int i = 0; i < slice; ++i) {
        // Check if client disconnected during execution
        // If so, cancel task to prevent wasted computation
        if (!client_context_is_connected(task->client)) {
            task->cancelled = true;
            break;
        }

        // Update task progress: one iteration completed, one time unit consumed
        task->iterations_completed++;
        task->remaining_time--;

        // Send progress update to client (one line per iteration)
        // Format: [demo][task X] iteration Y/Z
        // This demonstrates the task is running and making progress
        if (send_formatted(task->client,
                           "[demo][task %d] iteration %d/%d\n",
                           task->task_id,
                           task->iterations_completed,
                           task->total_burst_time) == -1) {
            // Send failed (client disconnected or network error)
            task->cancelled = true;
            break;
        }

        // Simulate one time unit of CPU work
        // In real system, this would be actual computation
        // Here we use sleep(1) to simulate time passing
        sleep(1);
    }

    // Increment round counter after quantum expires
    // This tracks how many scheduling rounds this task has participated in
    task->rounds_completed++;

    if (task->remaining_time == 0 && !task->cancelled) {
        send_formatted(task->client,
                       "[scheduler] Task %d (%s) completed after %d rounds.\n",
                       task->task_id,
                       task->label,
                       task->rounds_completed);
    } else if (!task->cancelled) {
        send_formatted(task->client,
                       "[scheduler] Task %d preempted. Remaining=%d\n",
                       task->task_id,
                       task->remaining_time);
    }
}

static void update_stats_on_enqueue(task_type_t type) {
    (void)type;
    pthread_mutex_lock(&g_stats_mutex);
    g_stats.total_created++;
    pthread_mutex_unlock(&g_stats_mutex);
}

static void update_stats_on_completion(task_type_t type) {
    pthread_mutex_lock(&g_stats_mutex);
    g_stats.total_completed++;
    if (type == TASK_SHELL_COMMAND) {
        g_stats.completed_shell++;
    } else {
        g_stats.completed_program++;
    }
    pthread_mutex_unlock(&g_stats_mutex);
}

static void maybe_print_scheduler_summary(void) {
    pthread_mutex_lock(&g_queue.mutex);
    size_t pending = g_queue.count;
    pthread_mutex_unlock(&g_queue.mutex);

    if (pending != 0) {
        return;
    }

    pthread_mutex_lock(&g_stats_mutex);
    printf("[SUMMARY] Completed %d/%d tasks (shell=%d, program=%d).\n",
           g_stats.total_completed,
           g_stats.total_created,
           g_stats.completed_shell,
           g_stats.completed_program);
    fflush(stdout);
    pthread_mutex_unlock(&g_stats_mutex);
}

static void trim_trailing_whitespace(char* str) {
    size_t len = strlen(str);
    while (len > 0 && isspace((unsigned char)str[len - 1])) {
        str[len - 1] = '\0';
        len--;
    }
}

static int is_blank_line(const char* str) {
    if (str == NULL) {
        return 1;
    }
    while (*str != '\0') {
        if (!isspace((unsigned char)*str)) {
            return 0;
        }
        str++;
    }
    return 1;
}

static int send_buffer(client_context_t* client, const char* data, size_t length) {
    if (length == 0) {
        return 0;
    }

    int fd = -1;
    pthread_mutex_lock(&client->lock);
    if (client->connected) {
        fd = client->client_fd;
    }
    pthread_mutex_unlock(&client->lock);

    if (fd == -1) {
        return -1;
    }

    size_t total = 0;
    while (total < length) {
        ssize_t sent = send(fd, data + total, length - total, 0);
        if (sent == -1) {
            if (errno == EINTR) {
                continue;
            }
            perror("[ERROR] send");
            client_context_disconnect(client);
            return -1;
        }
        total += (size_t)sent;
    }

    return 0;
}

static int send_string(client_context_t* client, const char* data) {
    return send_buffer(client, data, strlen(data));
}

static int send_formatted(client_context_t* client, const char* fmt, ...) {
    char buffer[BUFFER_SIZE];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    return send_string(client, buffer);
}

static void client_context_ref(client_context_t* client) {
    pthread_mutex_lock(&client->lock);
    client->ref_count++;
    pthread_mutex_unlock(&client->lock);
}

static void client_context_unref(client_context_t* client) {
    int should_free = 0;
    pthread_mutex_lock(&client->lock);
    client->ref_count--;
    if (client->ref_count == 0) {
        should_free = 1;
    }
    pthread_mutex_unlock(&client->lock);

    if (should_free) {
        pthread_mutex_destroy(&client->lock);
        free(client);
    }
}

static void client_context_disconnect(client_context_t* client) {
    int fd_to_close = -1;
    pthread_mutex_lock(&client->lock);
    if (client->connected) {
        client->connected = 0;
        fd_to_close = client->client_fd;
        client->client_fd = -1;
    }
    pthread_mutex_unlock(&client->lock);

    if (fd_to_close != -1) {
        shutdown(fd_to_close, SHUT_RDWR);
        close(fd_to_close);
    }
}

static int client_context_is_connected(client_context_t* client) {
    pthread_mutex_lock(&client->lock);
    int connected = client->connected;
    pthread_mutex_unlock(&client->lock);
    return connected;
}

// Existing execute_command_with_capture implementation retained from Phase 3
char* execute_command_with_capture(const char* command, int* success) {
    int stdout_pipe[2];
    int stderr_pipe[2];

    if (pipe(stdout_pipe) == -1) {
        perror("Error: pipe creation failed for stdout");
        *success = 0;
        return NULL;
    }

    if (pipe(stderr_pipe) == -1) {
        perror("Error: pipe creation failed for stderr");
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        *success = 0;
        return NULL;
    }

    pid_t pid = fork();
    if (pid == -1) {
        perror("Error -- fork failed");
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        *success = 0;
        return NULL;
    }

    if (pid == 0) {
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);

        char* cmd_copy = malloc(strlen(command) + 1);
        if (cmd_copy == NULL) {
            fprintf(stderr, "Error: Memory allocation failed\n");
            exit(EXIT_FAILURE);
        }
        strcpy(cmd_copy, command);

        pipeline_t* pipeline = parse_pipeline(cmd_copy);
        free(cmd_copy);
        if (pipeline == NULL) {
            fprintf(stderr, "Error -- Invalid command: %s\n", command);
            exit(EXIT_FAILURE);
        }

        int status = execute_pipeline(pipeline);
        free_pipeline(pipeline);
        exit(status);
    }

    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    char stdout_buffer[BUFFER_SIZE];
    char stderr_buffer[BUFFER_SIZE];
    ssize_t stdout_bytes = 0;
    ssize_t stderr_bytes = 0;

    size_t total_size = BUFFER_SIZE * 2;
    char* output = malloc(total_size);
    if (output == NULL) {
        perror("Error: malloc failed for output");
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        waitpid(pid, NULL, 0);
        *success = 0;
        return NULL;
    }
    output[0] = '\0';
    size_t output_len = 0;

    while ((stdout_bytes = read(stdout_pipe[0], stdout_buffer, BUFFER_SIZE - 1)) > 0) {
        while (output_len + stdout_bytes + 1 > total_size) {
            total_size *= 2;
            char* new_output = realloc(output, total_size);
            if (new_output == NULL) {
                perror("Error: realloc failed");
                free(output);
                close(stdout_pipe[0]);
                close(stderr_pipe[0]);
                waitpid(pid, NULL, 0);
                *success = 0;
                return NULL;
            }
            output = new_output;
        }
        memcpy(output + output_len, stdout_buffer, stdout_bytes);
        output_len += stdout_bytes;
        output[output_len] = '\0';
    }

    while ((stderr_bytes = read(stderr_pipe[0], stderr_buffer, BUFFER_SIZE - 1)) > 0) {
        while (output_len + stderr_bytes + 1 > total_size) {
            total_size *= 2;
            char* new_output = realloc(output, total_size);
            if (new_output == NULL) {
                perror("Error: realloc failed");
                free(output);
                close(stdout_pipe[0]);
                close(stderr_pipe[0]);
                waitpid(pid, NULL, 0);
                *success = 0;
                return NULL;
            }
            output = new_output;
        }
        memcpy(output + output_len, stderr_buffer, stderr_bytes);
        output_len += stderr_bytes;
        output[output_len] = '\0';
    }

    close(stdout_pipe[0]);
    close(stderr_pipe[0]);

    int status;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status)) {
        int exit_status = WEXITSTATUS(status);
        *success = (exit_status == 0) ? 1 : 0;
    } else {
        *success = 0;
    }

    return output;
}
