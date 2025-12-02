// server.c -- Phase 4 Server Implementation
// Multi-threaded task scheduler server that accepts client connections
// and executes shell commands and program tasks using a round-robin scheduler
// with Shortest Job Remaining First (SJRF) algorithm

#define _POSIX_C_SOURCE 200809L
// Network and socket programming headers
#include <arpa/inet.h>      // inet_ntop() for IP address conversion
#include <netinet/in.h>     // sockaddr_in structure
#include <sys/socket.h>     // socket(), bind(), listen(), accept(), send(), recv()
// Standard C library headers
#include <ctype.h>          // isspace() for whitespace checking
#include <errno.h>          // errno for error handling
#include <stdbool.h>        // bool type
#include <stdarg.h>         // va_list for variadic functions
#include <stdio.h>          // printf(), fprintf(), fgets(), etc.
#include <stdlib.h>         // malloc(), free(), exit(), etc.
#include <string.h>         // strcpy(), strcmp(), strlen(), etc.
#include <time.h>           // time() for timestamping
#include <unistd.h>         // close(), sleep(), fork(), exec(), etc.
// Threading and synchronization headers
#include <pthread.h>        // pthread_create(), mutexes, condition variables
// Process management headers
#include <signal.h>         // signal() for SIGPIPE handling
#include <sys/types.h>      // pid_t, size_t, etc.
#include <sys/wait.h>       // waitpid() for process synchronization
#include <sys/select.h>     // select() for I/O multiplexing
// Custom header for shell command parsing and execution
#include "shell_utils.h"

// Server configuration constants
#define PORT 8080                    // TCP port number for server to listen on
#define BUFFER_SIZE 4096             // Buffer size for network I/O and command storage
#define BACKLOG 16                   // Maximum number of pending connections in queue
#define MAX_TASKS 512                // Maximum number of tasks in scheduler queue
#define FIRST_ROUND_QUANTUM 3        // Time quantum for first round of program tasks (seconds)
#define NEXT_ROUND_QUANTUM 7         // Time quantum for subsequent rounds of program tasks (seconds)
#define MAX_LABEL_LEN 64             // Maximum length for task label/name
#define MAX_SIMULATED_BURST 120      // Maximum burst time for simulated program tasks


// ANSI color codes for terminal output formatting
#define COLOR_RESET   "\033[0m"      // Reset to default color
#define COLOR_RED     "\033[31m"     // Red text (for task completion)
#define COLOR_GREEN   "\033[32m"     // Green text (for task start/running)
#define COLOR_YELLOW  "\033[33m"     // Yellow text (for task waiting state)
#define COLOR_BLUE    "\033[34m"     // Blue text (for task creation/Gantt chart)
#define COLOR_CYAN    "\033[36m"     // Cyan text (unused, reserved)

// Task type enumeration
// Defines the different types of tasks the scheduler can handle
typedef enum {
    TASK_SHELL_COMMAND = 0,  // Regular shell command (highest priority, executes immediately)
    TASK_PROGRAM_DEMO,       // Demo program task (./demo N)
    TASK_PROGRAM_GENERIC    // Generic program task (program <name> <burst>)
} task_type_t;

// Client context structure
// Maintains information about a connected client and manages its lifecycle
typedef struct client_context {
    int client_fd;                      // Socket file descriptor for client connection
    int client_id;                      // Unique identifier assigned to this client
    char client_ip[INET_ADDRSTRLEN];   // Client's IP address as string
    int client_port;                    // Client's port number
    int connected;                      // Connection status flag (1 = connected, 0 = disconnected)
    int ref_count;                      // Reference counter for safe memory management
    pthread_mutex_t lock;               // Mutex for thread-safe access to client context
} client_context_t;

// Scheduler task structure
// Represents a single task in the scheduling queue with all its execution state
typedef struct scheduler_task {
    int task_id;                        // Unique task identifier
    task_type_t type;                   // Type of task (shell, demo, or generic program)
    char command[BUFFER_SIZE];          // Original command string from client
    char label[MAX_LABEL_LEN];          // Human-readable label for the task
    int total_burst_time;               // Total CPU burst time required (for program tasks)
    int remaining_time;                // Remaining CPU time needed to complete task
    int rounds_completed;               // Number of scheduling rounds this task has completed
    int iterations_completed;          // Number of iterations completed (for program tasks)
    bool cancelled;                     // Flag indicating if task was cancelled (client disconnected)
    long long arrival_seq;              // Sequence number for FIFO ordering (tie-breaker)
    time_t created_at;                  // Timestamp when task was created
    char** output_lines;                // Buffered output lines (for demo tasks)
    int total_output_lines;             // Total number of output lines buffered
    int lines_sent;                     // Number of output lines already sent to client
    client_context_t* client;           // Pointer to client that submitted this task
} scheduler_task_t;

// Scheduler queue structure
// Thread-safe queue for managing pending tasks
typedef struct scheduler_queue {
    scheduler_task_t* tasks[MAX_TASKS];  // Array of task pointers
    size_t count;                        // Current number of tasks in queue
    long long next_arrival_seq;          // Next arrival sequence number to assign
    pthread_mutex_t mutex;               // Mutex for thread-safe queue operations
    pthread_cond_t cond;                 // Condition variable for scheduler thread wake-up
} scheduler_queue_t;

// Scheduler statistics structure
// Tracks overall scheduler performance metrics
typedef struct scheduler_stats {
    int total_created;       // Total number of tasks created since server start
    int total_completed;     // Total number of tasks completed
    int completed_shell;     // Number of shell commands completed
    int completed_program;   // Number of program tasks completed
} scheduler_stats_t;

// Global scheduler queue - shared by all threads
static scheduler_queue_t g_queue = {
    .tasks = {0},
    .count = 0,
    .next_arrival_seq = 0,
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .cond = PTHREAD_COND_INITIALIZER};

// Global scheduler statistics
static scheduler_stats_t g_stats = {0};
static pthread_mutex_t g_stats_mutex = PTHREAD_MUTEX_INITIALIZER;  // Protects statistics updates

// Scheduler thread management
static pthread_t g_scheduler_thread;      // Thread handle for scheduler
static int g_scheduler_running = 1;        // Flag to control scheduler thread lifecycle

// ID generation (thread-safe)
static pthread_mutex_t g_id_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_next_task_id = 0;            // Next task ID to assign
static int g_next_client_id = 0;          // Next client ID to assign

// Timeline/Gantt chart tracking for program tasks
static int g_cumulative_time = 0;         // Cumulative execution time across all tasks
static pthread_mutex_t g_timeline_mutex = PTHREAD_MUTEX_INITIALIZER;
static char g_timeline_buffer[4096] = {0};  // Buffer for Gantt chart string representation

static int create_server_socket(void);
static void* scheduler_main(void* arg);
static void* client_thread_main(void* arg);
static void handle_client_commands(client_context_t* client);
static int execute_program_with_capture(const char* program_path, char** argv, int max_iterations, client_context_t* client, int* iterations_done);
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
static int get_cumulative_time(void);
static void add_execution_time(int seconds);
static void append_timeline(int client_id, int start_time, int end_time);
static void print_timeline(void);
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

// Timeline management functions

// Get the current cumulative execution time
// Returns: Current cumulative time in seconds
static int get_cumulative_time(void) {
    return g_cumulative_time;
}

// Add execution time to the cumulative timeline
// Parameters:
//   seconds - Number of seconds to add to cumulative time
static void add_execution_time(int seconds) {
    pthread_mutex_lock(&g_timeline_mutex);
    g_cumulative_time += seconds;
    pthread_mutex_unlock(&g_timeline_mutex);
}

// Append a timeline entry to the Gantt chart buffer
// Formats entries as: (start_time)-P{client_id}-(end_time) for first entry,
// or -P{client_id}-(end_time) for subsequent entries
// Parameters:
//   client_id - ID of the client that executed the task
//   start_time - Start time of this execution segment
//   end_time - End time of this execution segment
static void append_timeline(int client_id, int start_time, int end_time) {
    pthread_mutex_lock(&g_timeline_mutex);
    
    char entry[128];
    if (strlen(g_timeline_buffer) == 0) {
        // First entry: include (0) at the start
        snprintf(entry, sizeof(entry), "(%d)-P%d-(%d)", start_time, client_id, end_time);
    } else {
        // Subsequent entries: just add -P{client}-({end_time})
        snprintf(entry, sizeof(entry), "-P%d-(%d)", client_id, end_time);
    }
    
    if (strlen(g_timeline_buffer) + strlen(entry) < sizeof(g_timeline_buffer) - 1) {
        strcat(g_timeline_buffer, entry);
    }
    
    pthread_mutex_unlock(&g_timeline_mutex);
}

// Print the Gantt chart timeline to stdout
// Displays the execution timeline showing when each client's tasks ran
static void print_timeline(void) {
    pthread_mutex_lock(&g_timeline_mutex);
    if (strlen(g_timeline_buffer) > 0) {
        printf(COLOR_BLUE "Gantt Chart: %s" COLOR_RESET "\n", g_timeline_buffer);
        fflush(stdout);
    }
    pthread_mutex_unlock(&g_timeline_mutex);
}

// Main server entry point
// Initializes server socket, starts scheduler thread, and accepts client connections
// Returns: EXIT_SUCCESS on normal shutdown, EXIT_FAILURE on initialization error
int main(void) {
    // Ignore SIGPIPE to prevent server crash when client disconnects unexpectedly
    signal(SIGPIPE, SIG_IGN);
    
    // Create and bind server socket
    int server_fd = create_server_socket();
    if (server_fd == -1) {
        fprintf(stderr, "[ERROR] Failed to start server.\n");
        return EXIT_FAILURE;
    }
    
    // Start the scheduler thread that will process tasks from the queue
    if (pthread_create(&g_scheduler_thread, NULL, scheduler_main, NULL) != 0) {
        perror("[ERROR] Failed to start scheduler thread");
        close(server_fd);
        return EXIT_FAILURE;
    }
    
    // Print server startup message
    printf("---------------------------\n");
    printf("| Hello, Server Started |\n");
    printf("---------------------------\n");
    fflush(stdout);
    
    // Main server loop: accept client connections
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        // Accept incoming client connection (blocks until connection arrives)
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd == -1) {
            perror("[ERROR] accept failed");
            continue;
        }
        
        // Allocate and initialize client context structure
        client_context_t* client = calloc(1, sizeof(client_context_t));
        if (client == NULL) {
            perror("[ERROR] Failed to allocate client context");
            close(client_fd);
            continue;
        }
        
        // Initialize client context
        pthread_mutex_init(&client->lock, NULL);
        client->client_fd = client_fd;
        client->client_port = ntohs(client_addr.sin_port);  // Convert port from network byte order
        inet_ntop(AF_INET, &(client_addr.sin_addr), client->client_ip, sizeof(client->client_ip));
        client->connected = 1;
        client->ref_count = 1;  // Initial reference count
        
        // Assign unique client ID (thread-safe)
        pthread_mutex_lock(&g_id_mutex);
        g_next_client_id++;
        client->client_id = g_next_client_id;
        pthread_mutex_unlock(&g_id_mutex);
        
        printf("[%d]<<< client connected\n", client->client_id);
        fflush(stdout);
        
        // Create a dedicated thread to handle this client's commands
        pthread_t client_thread;
        if (pthread_create(&client_thread, NULL, client_thread_main, client) != 0) {
            perror("[ERROR] Failed to create client thread");
            client_context_disconnect(client);
            client_context_unref(client);
            continue;
        }
        // Detach thread so it cleans up automatically when done
        pthread_detach(client_thread);
    }
    
    // Cleanup (this code is unreachable in normal operation, but included for completeness)
    g_scheduler_running = 0;
    pthread_cond_broadcast(&g_queue.cond);  // Wake up scheduler to exit
    pthread_join(g_scheduler_thread, NULL);
    close(server_fd);
    return EXIT_SUCCESS;
}

// Create and configure the server socket
// Creates a TCP socket, sets SO_REUSEADDR option, binds to PORT, and starts listening
// Returns: Socket file descriptor on success, -1 on failure
static int create_server_socket(void) {
    // Create TCP socket (AF_INET = IPv4, SOCK_STREAM = TCP)
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("[ERROR] socket");
        return -1;
    }
    
    // Set SO_REUSEADDR to allow binding to port even if it's in TIME_WAIT state
    // This prevents "Address already in use" errors when restarting server quickly
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("[ERROR] setsockopt");
        close(server_fd);
        return -1;
    }
    
    // Configure server address structure
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;           // IPv4
    server_addr.sin_addr.s_addr = INADDR_ANY;    // Listen on all network interfaces
    server_addr.sin_port = htons(PORT);          // Convert port to network byte order
    
    // Bind socket to the specified address and port
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("[ERROR] bind");
        close(server_fd);
        return -1;
    }
    
    // Start listening for incoming connections
    // BACKLOG specifies maximum number of pending connections in queue
    if (listen(server_fd, BACKLOG) == -1) {
        perror("[ERROR] listen");
        close(server_fd);
        return -1;
    }
    
    return server_fd;
}

// Client thread main function
// Each client connection gets its own thread that handles all commands from that client
// Parameters:
//   arg - Pointer to client_context_t structure
// Returns: NULL (thread exit value)
static void* client_thread_main(void* arg) {
    client_context_t* client = (client_context_t*)arg;
    
    // Process commands from this client until disconnect or "exit" command
    handle_client_commands(client);
    
    // Cleanup: disconnect client, remove their tasks from queue, and release reference
    client_context_disconnect(client);
    remove_client_tasks(client);
    client_context_unref(client);
    return NULL;
}

// Handle commands from a client
// Receives commands from client, parses them, creates tasks, and enqueues them
// Continues until client disconnects or sends "exit" command
// Parameters:
//   client - Client context for this connection
static void handle_client_commands(client_context_t* client) {
    char buffer[BUFFER_SIZE];
    
    // Main command loop: receive and process commands until client disconnects
    while (client_context_is_connected(client)) {
        memset(buffer, 0, sizeof(buffer));
        
        // Receive command from client
        ssize_t bytes_received = recv(client->client_fd, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received <= 0) {
            // Connection closed or error occurred
            if (bytes_received < 0) {
                perror("[ERROR] recv");
            }
            break;
        }
        
        // Null-terminate the received string
        buffer[bytes_received] = '\0';
        trim_trailing_whitespace(buffer);
        
        // Skip blank lines
        if (is_blank_line(buffer)) {
            continue;
        }
        
        // Log received command
        printf("[%d]>>> %s\n", client->client_id, buffer);
        fflush(stdout);
        
        // Handle exit command
        if (strcmp(buffer, "exit") == 0) {
            send_string(client, "\n");
            break;
        }
        
        // Parse command and create task structure
        char error_buf[128] = {0};
        scheduler_task_t* task = create_task_from_command(buffer, client, error_buf, sizeof(error_buf));
        if (task == NULL) {
            // Task creation failed - send error message to client
            if (strlen(error_buf) == 0) {
                strncpy(error_buf, "Error: Failed to allocate task\n", sizeof(error_buf) - 1);
            }
            send_string(client, error_buf);
            continue;
        }
        
        // Enqueue task in scheduler queue
        if (enqueue_task(task) != 0) {
            // Queue is full - send error and clean up task
            send_string(client, "Error: Scheduler queue is full. Please retry later.\n");
            destroy_task(task);
            continue;
        }
    }
}

// Create a scheduler task from a client command
// Parses the command to determine task type and initializes task structure
// Parameters:
//   command - Command string from client
//   client - Client context that submitted this command
//   error_buf - Buffer to store error message if parsing fails
//   error_size - Size of error buffer
// Returns: Pointer to created task on success, NULL on failure
static scheduler_task_t* create_task_from_command(const char* command, client_context_t* client, char* error_buf, size_t error_size) {
    // Allocate task structure
    scheduler_task_t* task = calloc(1, sizeof(scheduler_task_t));
    if (task == NULL) {
        snprintf(error_buf, error_size, "Error: Unable to allocate task.\n");
        return NULL;
    }
    
    // Assign unique task ID (thread-safe)
    pthread_mutex_lock(&g_id_mutex);
    g_next_task_id++;
    task->task_id = g_next_task_id;
    pthread_mutex_unlock(&g_id_mutex);
    
    // Initialize basic task fields
    strncpy(task->command, command, sizeof(task->command) - 1);
    task->client = client;
    task->created_at = time(NULL);
    task->output_lines = NULL;
    task->total_output_lines = 0;
    task->lines_sent = 0;
    
    // Try to parse as program task (demo or generic program)
    bool parsed_program = false;
    if (try_parse_demo_request(command, task, error_buf, error_size)) {
        parsed_program = true;
    } else if (try_parse_program_keyword(command, task, error_buf, error_size)) {
        parsed_program = true;
    }
    
    // If not a program task, treat as shell command
    if (!parsed_program) {
        task->type = TASK_SHELL_COMMAND;
        snprintf(task->label, sizeof(task->label), "shell");
        task->total_burst_time = -1;  // Shell commands don't have burst time
        task->remaining_time = 0;
    }
    
    // Increment client reference count (task holds a reference to client)
    client_context_ref(client);
    return task;
}

// Try to parse command as a demo program request
// Format: "demo N" or "./demo N" where N is number of iterations
// Parameters:
//   command - Command string to parse
//   task - Task structure to populate if parsing succeeds
//   error_buf - Buffer for error message if validation fails
//   error_size - Size of error buffer
// Returns: true if command matches demo format and is valid, false otherwise
static bool try_parse_demo_request(const char* command, scheduler_task_t* task, char* error_buf, size_t error_size) {
    char keyword[16];
    long iterations = 0;
    
    // Parse command: expect keyword followed by number
    if (sscanf(command, "%15s %ld", keyword, &iterations) != 2) {
        return false;
    }
    
    // Check if keyword is "demo" or "./demo"
    if (strcmp(keyword, "demo") != 0 && strcmp(keyword, "./demo") != 0) {
        return false;
    }
    
    // Validate iteration count
    if (iterations <= 0 || iterations > MAX_SIMULATED_BURST) {
        snprintf(error_buf, error_size, "Error: demo requires 1-%d iterations.\n", MAX_SIMULATED_BURST);
        return false;
    }
    
    // Initialize task as demo program
    task->type = TASK_PROGRAM_DEMO;
    snprintf(task->label, sizeof(task->label), "demo");
    task->total_burst_time = (int)iterations;
    task->remaining_time = (int)iterations;
    return true;
}

// Try to parse command as a generic program request
// Format: "program <name> <burst>" where name is program label and burst is iterations
// Parameters:
//   command - Command string to parse
//   task - Task structure to populate if parsing succeeds
//   error_buf - Buffer for error message if validation fails
//   error_size - Size of error buffer
// Returns: true if command matches program format and is valid, false otherwise
static bool try_parse_program_keyword(const char* command, scheduler_task_t* task, char* error_buf, size_t error_size) {
    char keyword[16];
    char name[MAX_LABEL_LEN];
    long iterations = 0;
    
    // Parse command: expect "program", name, and iteration count
    if (sscanf(command, "%15s %63s %ld", keyword, name, &iterations) != 3) {
        return false;
    }
    
    // Check if keyword is "program"
    if (strcmp(keyword, "program") != 0) {
        return false;
    }
    
    // Validate iteration count
    if (iterations <= 0 || iterations > MAX_SIMULATED_BURST) {
        snprintf(error_buf, error_size, "Error: program burst must be 1-%d.\n", MAX_SIMULATED_BURST);
        return false;
    }
    
    // Initialize task as generic program
    task->type = TASK_PROGRAM_GENERIC;
    snprintf(task->label, sizeof(task->label), "%s", name);
    task->total_burst_time = (int)iterations;
    task->remaining_time = (int)iterations;
    return true;
}

// Enqueue a task in the scheduler queue
// Thread-safe operation that adds task to queue and wakes up scheduler thread
// Parameters:
//   task - Task to enqueue
// Returns: 0 on success, -1 if queue is full
static int enqueue_task(scheduler_task_t* task) {
    pthread_mutex_lock(&g_queue.mutex);
    
    // Check if queue has space
    if (g_queue.count >= MAX_TASKS) {
        pthread_mutex_unlock(&g_queue.mutex);
        return -1;
    }
    
    // Assign arrival sequence number and add to queue
    task->arrival_seq = ++g_queue.next_arrival_seq;
    g_queue.tasks[g_queue.count++] = task;
    update_stats_on_enqueue(task->type);
    
    // Wake up scheduler thread if it's waiting for tasks
    pthread_cond_broadcast(&g_queue.cond);
    pthread_mutex_unlock(&g_queue.mutex);
    
    // Log task creation
    printf(COLOR_BLUE "(%d)---- created (%d)" COLOR_RESET "\n", task->client->client_id, task->total_burst_time);
    fflush(stdout);
    return 0;
}

// Select the next task to execute using SJRF (Shortest Job Remaining First) algorithm
// Priority order:
//   1. Shell commands (highest priority, execute immediately, FIFO)
//   2. Program tasks with shortest remaining time (SJRF)
//   3. If remaining times are very close (within 2), prefer shorter total burst time
//   4. Avoid consecutive runs of the same task if other tasks are available
// Parameters:
//   last_task_id - ID of the task that just finished (to avoid consecutive runs)
// Returns: Pointer to selected task, or NULL if queue is empty
// Note: This function must be called with g_queue.mutex locked
static scheduler_task_t* select_next_task(int last_task_id) {
    scheduler_task_t* best = NULL;
    int best_index = -1;
    
    // First pass: Find shell commands (highest priority, execute immediately)
    // Shell commands are processed in FIFO order (by arrival_seq)
    for (size_t i = 0; i < g_queue.count; ++i) {
        scheduler_task_t* candidate = g_queue.tasks[i];
        if (candidate->type == TASK_SHELL_COMMAND && !candidate->cancelled) {
            if (best == NULL || candidate->arrival_seq < best->arrival_seq) {
                best = candidate;
                best_index = (int)i;
            }
        }
    }
    
    // If no shell command, find the task with shortest remaining time (SJRF)
    // When remaining times are equal or very close (within 2), prioritize by total_burst_time
    // to ensure shorter total jobs complete first
    if (best == NULL) {
        for (size_t i = 0; i < g_queue.count; ++i) {
            scheduler_task_t* candidate = g_queue.tasks[i];
            if (candidate->cancelled) {
                continue;
            }
            if (best == NULL) {
                best = candidate;
                best_index = (int)i;
            } else {
                int remaining_diff = candidate->remaining_time - best->remaining_time;
                // If remaining times are very close (within 2), prioritize by total_burst_time
                if (abs(remaining_diff) <= 2 && candidate->total_burst_time > 0 && best->total_burst_time > 0) {
                    // Prefer the one with smaller total_burst_time (shorter total job)
                    if (candidate->total_burst_time < best->total_burst_time ||
                        (candidate->total_burst_time == best->total_burst_time && 
                         candidate->arrival_seq < best->arrival_seq)) {
                        best = candidate;
                        best_index = (int)i;
                    }
                } else if (candidate->remaining_time < best->remaining_time ||
                    (candidate->remaining_time == best->remaining_time && 
                     candidate->arrival_seq < best->arrival_seq)) {
                    best = candidate;
                    best_index = (int)i;
                }
            }
        }
    }
    
    // If the selected task is the same as the last one, and there are other tasks,
    // find the second-best task to avoid consecutive runs (improves fairness)
    if (best != NULL && best->task_id == last_task_id && g_queue.count > 1) {
        scheduler_task_t* second = NULL;
        int second_index = -1;
        
        // First check for shell commands
        for (size_t i = 0; i < g_queue.count; ++i) {
            scheduler_task_t* candidate = g_queue.tasks[i];
            if (candidate->task_id == last_task_id || candidate->cancelled) {
                continue;
            }
            if (candidate->type == TASK_SHELL_COMMAND) {
                if (second == NULL || candidate->arrival_seq < second->arrival_seq) {
                    second = candidate;
                    second_index = (int)i;
                }
            }
        }
        
        // If no shell command, find the task with shortest remaining time
        // When remaining times are equal or very close (within 2), prioritize by total_burst_time
        if (second == NULL) {
            for (size_t i = 0; i < g_queue.count; ++i) {
                scheduler_task_t* candidate = g_queue.tasks[i];
                if (candidate->task_id == last_task_id || candidate->cancelled) {
                    continue;
                }
                if (second == NULL) {
                    second = candidate;
                    second_index = (int)i;
                } else {
                    int remaining_diff = candidate->remaining_time - second->remaining_time;
                    // If remaining times are very close (within 2), prioritize by total_burst_time
                    if (abs(remaining_diff) <= 2 && candidate->total_burst_time > 0 && second->total_burst_time > 0) {
                        // Prefer the one with smaller total_burst_time (shorter total job)
                        if (candidate->total_burst_time < second->total_burst_time ||
                            (candidate->total_burst_time == second->total_burst_time && 
                             candidate->arrival_seq < second->arrival_seq)) {
                            second = candidate;
                            second_index = (int)i;
                        }
                    } else if (candidate->remaining_time < second->remaining_time ||
                        (candidate->remaining_time == second->remaining_time && 
                         candidate->arrival_seq < second->arrival_seq)) {
                        second = candidate;
                        second_index = (int)i;
                    }
                }
            }
        }
        
        if (second != NULL) {
            best = second;
            best_index = second_index;
        }
    }
    
    if (best == NULL) {
        return NULL;
    }
    
    // Remove the selected task from the queue (shift remaining tasks)
    g_queue.count--;
    for (size_t i = (size_t)best_index; i < g_queue.count; ++i) {
        g_queue.tasks[i] = g_queue.tasks[i + 1];
    }
    return best;
}

// Destroy a task and free all associated resources
// Releases client reference, frees output buffer, and deallocates task structure
// Parameters:
//   task - Task to destroy (can be NULL)
static void destroy_task(scheduler_task_t* task) {
    if (task == NULL) {
        return;
    }
    
    // Release reference to client context
    if (task->client) {
        client_context_unref(task->client);
    }
    
    // Free buffered output lines (for demo tasks)
    if (task->output_lines) {
        for (int i = 0; i < task->total_output_lines; i++) {
            free(task->output_lines[i]);
        }
        free(task->output_lines);
    }
    
    // Free task structure itself
    free(task);
}

// Remove all tasks belonging to a specific client from the queue
// Called when a client disconnects to clean up their pending tasks
// Parameters:
//   client - Client whose tasks should be removed
static void remove_client_tasks(client_context_t* client) {
    pthread_mutex_lock(&g_queue.mutex);
    
    // Compact queue by removing tasks belonging to this client
    size_t write_index = 0;
    for (size_t i = 0; i < g_queue.count; ++i) {
        scheduler_task_t* task = g_queue.tasks[i];
        if (task->client == client) {
            // Destroy task belonging to disconnected client
            destroy_task(task);
            continue;
        }
        // Keep task from other clients
        g_queue.tasks[write_index++] = task;
    }
    g_queue.count = write_index;
    pthread_mutex_unlock(&g_queue.mutex);
}

// Main scheduler thread function
// Continuously selects and executes tasks from the queue using round-robin with SJRF
// Implements time-sliced execution with different quanta for first and subsequent rounds
// Parameters:
//   arg - Unused (required by pthread interface)
// Returns: NULL (thread exit value)
static void* scheduler_main(void* arg) {
    (void)arg;
    int last_task_id = -1;  // Track last executed task to avoid consecutive runs
    
    // Main scheduler loop
    while (g_scheduler_running) {
        pthread_mutex_lock(&g_queue.mutex);
        
        // Wait for tasks to become available
        while (g_queue.count == 0 && g_scheduler_running) {
            pthread_cond_wait(&g_queue.cond, &g_queue.mutex);
        }
        
        // Check if scheduler should stop
        if (!g_scheduler_running) {
            pthread_mutex_unlock(&g_queue.mutex);
            break;
        }
        
        // Select next task using SJRF algorithm
        scheduler_task_t* task = select_next_task(last_task_id);
        pthread_mutex_unlock(&g_queue.mutex);
        
        if (task == NULL) {
            continue;
        }
        
        // Check if client is still connected
        if (!client_context_is_connected(task->client)) {
            destroy_task(task);
            continue;
        }
        
        // Log task start
        int start_value = (task->type == TASK_SHELL_COMMAND) ? -1 : task->remaining_time;
        printf(COLOR_GREEN "(%d)---- started (%d)" COLOR_RESET "\n", task->client->client_id, start_value);
        fflush(stdout);
        
        // Track how much time will be used in this execution
        int time_before = task->remaining_time;
        
        // Execute task based on type
        if (task->type == TASK_SHELL_COMMAND) {
            run_shell_task(task);
            task->remaining_time = 0;  // Shell commands complete immediately
        } else {
            run_program_task(task);  // Program tasks run for a time quantum
        }
        
        // For program tasks, add execution time to timeline/Gantt chart
        if (task->type != TASK_SHELL_COMMAND) {
            // Calculate how much time was actually used
            int time_used = time_before - task->remaining_time;
            
            // Add this execution time to cumulative time
            int start_time = get_cumulative_time();
            add_execution_time(time_used);
            // Record end time (after adding new time)
            int end_time = get_cumulative_time();
    
            // Append to timeline with both start and end times
            append_timeline(task->client->client_id, start_time, end_time);
        }
        
        // Log task state after execution
        if (task->remaining_time > 0 && !task->cancelled) {
            // Task still has work remaining - will be re-queued
            printf(COLOR_YELLOW "(%d)---- waiting (%d)" COLOR_RESET "\n", task->client->client_id, task->remaining_time);
        } else if (!task->cancelled) {
            // Task completed successfully
            // Calculate total bytes sent for program tasks
            if (task->type != TASK_SHELL_COMMAND) {
                // Each demo line is "Demo X/N\n" - calculate total bytes
                size_t total_bytes = 0;
                for (int i = 0; i < task->lines_sent; i++) {
                    total_bytes += strlen(task->output_lines[i]);
                }
                printf("[%d]<<< %zu bytes sent\n", task->client->client_id, total_bytes);
            }
            
            int end_value = (task->type == TASK_SHELL_COMMAND) ? -1 : 0;
            printf(COLOR_RED "(%d)---- ended (%d)" COLOR_RESET "\n", task->client->client_id, end_value);
            
            // Send end marker to client for program tasks
            if (task->type != TASK_SHELL_COMMAND) {
                send_string(task->client, "<<END>>\n");
            }
        }

        fflush(stdout);
        
        last_task_id = task->task_id;
        
        // Re-queue task if it still has work remaining
        if (task->remaining_time > 0 && !task->cancelled) {
            pthread_mutex_lock(&g_queue.mutex);
            task->arrival_seq = ++g_queue.next_arrival_seq;  // Update arrival sequence
            g_queue.tasks[g_queue.count++] = task;
            pthread_cond_broadcast(&g_queue.cond);
            pthread_mutex_unlock(&g_queue.mutex);
            continue;
        }
        
        // Task completed - update statistics
        update_stats_on_completion(task->type);
        
        // Print Gantt chart when queue is empty and this was a program task
        pthread_mutex_lock(&g_queue.mutex);
        int pending = g_queue.count;
        pthread_mutex_unlock(&g_queue.mutex);
        
        if (pending == 0 && task->type != TASK_SHELL_COMMAND) {
            print_timeline();
        }
        
        // Clean up completed task
        destroy_task(task);
    }
    return NULL;
}

// Execute a shell command task
// Runs the command using shell_utils and sends output to client
// Parameters:
//   task - Shell command task to execute
static void run_shell_task(scheduler_task_t* task) {
    int success = 0;
    
    // Execute command and capture output
    char* output = execute_command_with_capture(task->command, &success);
    if (output == NULL) {
        send_string(task->client, "Error: Server failed to execute command.\n");
        return;
    }
    
    // Send output to client
    size_t output_len = strlen(output);
    if (output_len == 0) {
        // Empty output - send newline
        send_string(task->client, "\n");
        printf("[%d]<<< 1 bytes sent\n", task->client->client_id);
    } else {
        send_string(task->client, output);
        printf("[%d]<<< %zu bytes sent\n", task->client->client_id, output_len);
    }
    fflush(stdout);
    free(output);
}

// Buffer demo program output by running it once and capturing all output
// This allows incremental sending of output lines during time-sliced execution
// Parameters:
//   task - Demo task to buffer output for
// Returns: 0 on success, -1 on failure
static int buffer_demo_output(scheduler_task_t* task) {
    // If already buffered, nothing to do
    if (task->output_lines != NULL) {
        return 0;
    }
    
    // Convert burst time to string for demo program argument
    char burst_str[32];
    snprintf(burst_str, sizeof(burst_str), "%d", task->total_burst_time);
    
    // Create pipe for capturing demo output
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("[ERROR] pipe for demo buffering");
        return -1;
    }
    
    // Fork process to run demo program
    pid_t pid = fork();
    if (pid == -1) {
        perror("[ERROR] fork for demo buffering");
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    
    if (pid == 0) {
        // Child process: redirect stdout/stderr to pipe and exec demo
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        execl("./demo", "demo", burst_str, NULL);
        fprintf(stderr, "Error: Failed to execute ./demo\n");
        exit(EXIT_FAILURE);
    }
    
    // Parent process: read output from pipe
    close(pipefd[1]);
    FILE* stream = fdopen(pipefd[0], "r");
    if (stream == NULL) {
        perror("[ERROR] fdopen");
        close(pipefd[0]);
        kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
        return -1;
    }
    
    // Allocate buffer for output lines
    int capacity = task->total_burst_time + 10;  // Extra capacity for safety
    task->output_lines = malloc(capacity * sizeof(char*));
    if (task->output_lines == NULL) {
        fclose(stream);
        kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
        return -1;
    }
    
    // Read all output lines into buffer
    char line[BUFFER_SIZE];
    int line_count = 0;
    while (fgets(line, sizeof(line), stream) != NULL && line_count < capacity) {
        task->output_lines[line_count] = malloc(strlen(line) + 1);
        if (task->output_lines[line_count] == NULL) {
            // Memory allocation failed - clean up and return error
            for (int i = 0; i < line_count; i++) {
                free(task->output_lines[i]);
            }
            free(task->output_lines);
            task->output_lines = NULL;
            fclose(stream);
            kill(pid, SIGTERM);
            waitpid(pid, NULL, 0);
            return -1;
        }
        strcpy(task->output_lines[line_count], line);
        line_count++;
    }
    
    // Initialize task output tracking
    task->total_output_lines = line_count;
    task->lines_sent = 0;
    
    // Clean up
    fclose(stream);
    waitpid(pid, NULL, 0);
    return 0;
}

// Execute a program task for one time quantum
// Uses different quantum sizes for first round vs subsequent rounds
// Parameters:
//   task - Program task to execute
static void run_program_task(scheduler_task_t* task) {
    // Determine time quantum: first round uses smaller quantum, later rounds use larger
    int quantum = (task->rounds_completed == 0) ? FIRST_ROUND_QUANTUM : NEXT_ROUND_QUANTUM;
    // Actual slice is minimum of quantum and remaining time
    int slice = (task->remaining_time < quantum) ? task->remaining_time : quantum;
    
    printf(COLOR_GREEN "(%d)---- running (%d)" COLOR_RESET "\n", task->client->client_id, slice);
    fflush(stdout);
    
    if (task->type == TASK_PROGRAM_DEMO) {
        // Demo task: buffer output if not already done, then send lines incrementally
        if (task->output_lines == NULL) {
            if (buffer_demo_output(task) != 0) {
                task->cancelled = true;
                send_string(task->client, "Error: Failed to execute demo\n");
                return;
            }
        }
        
        // Send 'slice' number of output lines to client
        int lines_to_send = slice;
        int lines_sent_this_round = 0;
        
        while (lines_sent_this_round < lines_to_send && task->lines_sent < task->total_output_lines) {
            // Check if client is still connected
            if (!client_context_is_connected(task->client)) {
                task->cancelled = true;
                return;
            }
            
            // Send one line of output
            if (send_string(task->client, task->output_lines[task->lines_sent]) == -1) {
                task->cancelled = true;
                return;
            }
            
            // Update task state
            task->lines_sent++;
            task->iterations_completed++;
            task->remaining_time--;
            lines_sent_this_round++;
            
            // Simulate work by sleeping 1 second per iteration
            sleep(1);
        }
    } else if (task->type == TASK_PROGRAM_GENERIC) {
        // Generic program task: send formatted iteration messages
        for (int i = 0; i < slice; ++i) {
            // Check if client is still connected
            if (!client_context_is_connected(task->client)) {
                task->cancelled = true;
                break;
            }
            
            // Update task state
            task->iterations_completed++;
            task->remaining_time--;
            
            // Send iteration message to client
            if (send_formatted(task->client,
                "[%s] iteration %d/%d\n",
                task->label,
                task->iterations_completed,
                task->total_burst_time) == -1) {
                task->cancelled = true;
                break;
            }
            // Simulate work by sleeping 1 second per iteration
            sleep(1);
        }
    }
    
    // Increment round counter
    task->rounds_completed++;
}

// Execute a program and capture its output, sending it incrementally to client
// This function is currently unused but kept for potential future use
// Parameters:
//   program_path - Path to program executable
//   argv - Argument vector for program
//   max_iterations - Maximum number of output lines to read
//   client - Client to send output to
//   iterations_done - Output parameter: number of iterations actually completed
// Returns: 0 on success, -1 on failure
static int execute_program_with_capture(const char* program_path, char** argv, 
                                       int max_iterations, client_context_t* client, 
                                       int* iterations_done) {
    *iterations_done = 0;
    
    // Create pipe for capturing program output
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("[ERROR] pipe for program execution");
        return -1;
    }
    
    // Fork process to run program
    pid_t pid = fork();
    if (pid == -1) {
        perror("[ERROR] fork for program execution");
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    
    if (pid == 0) {
        // Child process: redirect stdout/stderr to pipe and exec program
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        execv(program_path, argv);
        fprintf(stderr, "Error: Failed to execute %s\n", program_path);
        exit(EXIT_FAILURE);
    }
    
    // Parent process: read output from pipe and send to client
    close(pipefd[1]);
    FILE* output_stream = fdopen(pipefd[0], "r");
    if (output_stream == NULL) {
        perror("[ERROR] fdopen");
        close(pipefd[0]);
        kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
        return -1;
    }
    
    // Read and forward output lines
    char line[BUFFER_SIZE];
    int lines_read = 0;
    
    while (lines_read < max_iterations && fgets(line, sizeof(line), output_stream) != NULL) {
        // Check if client is still connected
        if (!client_context_is_connected(client)) {
            fclose(output_stream);
            kill(pid, SIGTERM);
            waitpid(pid, NULL, 0);
            return -1;
        }
        
        // Send line to client
        if (send_string(client, line) == -1) {
            fclose(output_stream);
            kill(pid, SIGTERM);
            waitpid(pid, NULL, 0);
            return -1;
        }
        lines_read++;
    }
    
    *iterations_done = lines_read;
    
    // If we read max_iterations, terminate the program
    if (lines_read >= max_iterations) {
        kill(pid, SIGTERM);
    }
    
    // Clean up
    fclose(output_stream);
    int status;
    waitpid(pid, &status, 0);
    return 0;
}

// Update statistics when a task is enqueued
// Parameters:
//   type - Type of task being enqueued (currently unused but kept for future use)
static void update_stats_on_enqueue(task_type_t type) {
    (void)type;
    pthread_mutex_lock(&g_stats_mutex);
    g_stats.total_created++;
    pthread_mutex_unlock(&g_stats_mutex);
}

// Update statistics when a task completes
// Parameters:
//   type - Type of task that completed
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

// Utility function to remove trailing whitespace from a string
// Parameters:
//   str - String to trim (modified in place)
static void trim_trailing_whitespace(char* str) {
    size_t len = strlen(str);
    while (len > 0 && isspace((unsigned char)str[len - 1])) {
        str[len - 1] = '\0';
        len--;
    }
}

// Check if a string is blank (empty or contains only whitespace)
// Parameters:
//   str - String to check
// Returns: 1 if blank, 0 otherwise
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

// Send binary data to client (thread-safe)
// Handles partial sends and connection status checking
// Parameters:
//   client - Client context
//   data - Data to send
//   length - Length of data in bytes
// Returns: 0 on success, -1 on failure
static int send_buffer(client_context_t* client, const char* data, size_t length) {
    if (length == 0) {
        return 0;
    }
    
    // Get client file descriptor (thread-safe)
    int fd = -1;
    pthread_mutex_lock(&client->lock);
    if (client->connected) {
        fd = client->client_fd;
    }
    pthread_mutex_unlock(&client->lock);
    
    if (fd == -1) {
        return -1;
    }
    
    // Send data, handling partial sends
    size_t total = 0;
    while (total < length) {
        ssize_t sent = send(fd, data + total, length - total, 0);
        if (sent == -1) {
            if (errno == EINTR) {
                continue;  // Interrupted by signal, retry
            }
            perror("[ERROR] send");
            client_context_disconnect(client);
            return -1;
        }
        total += (size_t)sent;
    }
    return 0;
}

// Send a null-terminated string to client
// Parameters:
//   client - Client context
//   data - Null-terminated string to send
// Returns: 0 on success, -1 on failure
static int send_string(client_context_t* client, const char* data) {
    return send_buffer(client, data, strlen(data));
}

// Send formatted string to client (like printf)
// Parameters:
//   client - Client context
//   fmt - Format string
//   ... - Variable arguments for formatting
// Returns: 0 on success, -1 on failure
static int send_formatted(client_context_t* client, const char* fmt, ...) {
    char buffer[BUFFER_SIZE];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    return send_string(client, buffer);
}

// Increment reference count for client context (thread-safe)
// Used when a task holds a reference to a client
// Parameters:
//   client - Client context to increment reference for
static void client_context_ref(client_context_t* client) {
    pthread_mutex_lock(&client->lock);
    client->ref_count++;
    pthread_mutex_unlock(&client->lock);
}

// Decrement reference count for client context (thread-safe)
// Frees client context when reference count reaches zero
// Parameters:
//   client - Client context to decrement reference for
static void client_context_unref(client_context_t* client) {
    int should_free = 0;
    pthread_mutex_lock(&client->lock);
    client->ref_count--;
    if (client->ref_count == 0) {
        should_free = 1;
    }
    pthread_mutex_unlock(&client->lock);
    
    // Free client context if no more references
    if (should_free) {
        pthread_mutex_destroy(&client->lock);
        free(client);
    }
}

// Disconnect a client and close its socket (thread-safe)
// Parameters:
//   client - Client context to disconnect
static void client_context_disconnect(client_context_t* client) {
    int fd_to_close = -1;
    pthread_mutex_lock(&client->lock);
    if (client->connected) {
        client->connected = 0;
        fd_to_close = client->client_fd;
        client->client_fd = -1;
    }
    pthread_mutex_unlock(&client->lock);
    
    // Close socket outside of lock to avoid deadlock
    if (fd_to_close != -1) {
        shutdown(fd_to_close, SHUT_RDWR);
        close(fd_to_close);
    }
}

// Check if client is still connected (thread-safe)
// Parameters:
//   client - Client context to check
// Returns: 1 if connected, 0 if disconnected
static int client_context_is_connected(client_context_t* client) {
    pthread_mutex_lock(&client->lock);
    int connected = client->connected;
    pthread_mutex_unlock(&client->lock);
    return connected;
}

// Execute a shell command and capture its output (stdout and stderr)
// Uses shell_utils to parse and execute commands with pipeline support
// Parameters:
//   command - Shell command string to execute
//   success - Output parameter: set to 1 if command succeeded (exit code 0), 0 otherwise
// Returns: Allocated string containing command output (stdout + stderr), or NULL on error
//          Caller is responsible for freeing the returned string
char* execute_command_with_capture(const char* command, int* success) {
    int stdout_pipe[2];
    int stderr_pipe[2];
    
    // Create pipes for capturing stdout and stderr
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
    
    // Fork process to execute command
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
        // Child process: redirect stdout and stderr to pipes
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);
        
        // Parse and execute command using shell_utils
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
    
    // Parent process: read output from pipes
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);
    
    char stdout_buffer[BUFFER_SIZE];
    char stderr_buffer[BUFFER_SIZE];
    ssize_t stdout_bytes = 0;
    ssize_t stderr_bytes = 0;
    
    // Allocate output buffer (dynamically resized as needed)
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
    
    // Read stdout
    while ((stdout_bytes = read(stdout_pipe[0], stdout_buffer, BUFFER_SIZE - 1)) > 0) {
        // Resize buffer if needed
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
    
    // Read stderr
    while ((stderr_bytes = read(stderr_pipe[0], stderr_buffer, BUFFER_SIZE - 1)) > 0) {
        // Resize buffer if needed
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
    
    // Clean up pipes
    close(stdout_pipe[0]);
    close(stderr_pipe[0]);
    
    // Wait for child process to complete
    int status;
    waitpid(pid, &status, 0);
    
    // Determine success based on exit status
    if (WIFEXITED(status)) {
        int exit_status = WEXITSTATUS(status);
        *success = (exit_status == 0) ? 1 : 0;
    } else {
        *success = 0;
    }
    
    return output;
}