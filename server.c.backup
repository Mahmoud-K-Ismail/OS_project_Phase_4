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
#include <sys/select.h>

#define PORT 8080
#define BUFFER_SIZE 4096
#define BACKLOG 16
#define MAX_TASKS 512
#define FIRST_ROUND_QUANTUM 3
#define NEXT_ROUND_QUANTUM 7
#define MAX_LABEL_LEN 64
#define MAX_SIMULATED_BURST 120


// ANSI Color codes
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_CYAN    "\033[36m"

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
    char** output_lines;
    int total_output_lines;
    int lines_sent;
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

// Timeline tracking
static int g_cumulative_time = 0;
static pthread_mutex_t g_timeline_mutex = PTHREAD_MUTEX_INITIALIZER;
static char g_timeline_buffer[4096] = {0};

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

static int get_cumulative_time(void) {
    return g_cumulative_time;
}

static void add_execution_time(int seconds) {
    pthread_mutex_lock(&g_timeline_mutex);
    g_cumulative_time += seconds;
    pthread_mutex_unlock(&g_timeline_mutex);
}

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

static void print_timeline(void) {
    pthread_mutex_lock(&g_timeline_mutex);
    if (strlen(g_timeline_buffer) > 0) {
        printf(COLOR_BLUE "Gantt Chart: %s" COLOR_RESET "\n", g_timeline_buffer);
        fflush(stdout);
    }
    pthread_mutex_unlock(&g_timeline_mutex);
}

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
    
    printf("---------------------------\n");
    printf("| Hello, Server Started |\n");
    printf("---------------------------\n");
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
        client->ref_count = 1;
        
        pthread_mutex_lock(&g_id_mutex);
        g_next_client_id++;
        client->client_id = g_next_client_id;
        pthread_mutex_unlock(&g_id_mutex);
        
        printf("[%d]<<< client connected\n", client->client_id);
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
        
        printf("[%d]>>> %s\n", client->client_id, buffer);
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
    task->output_lines = NULL;
    task->total_output_lines = 0;
    task->lines_sent = 0;
    
    bool parsed_program = false;
    if (try_parse_demo_request(command, task, error_buf, error_size)) {
        parsed_program = true;
    } else if (try_parse_program_keyword(command, task, error_buf, error_size)) {
        parsed_program = true;
    }
    
    if (!parsed_program) {
        task->type = TASK_SHELL_COMMAND;
        snprintf(task->label, sizeof(task->label), "shell");
        task->total_burst_time = -1;
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
    
    if (strcmp(keyword, "demo") != 0 && strcmp(keyword, "./demo") != 0) {
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
    pthread_mutex_unlock(&g_queue.mutex);
    
    printf(COLOR_BLUE "(%d)---- created (%d)" COLOR_RESET "\n", task->client->client_id, task->total_burst_time);

    fflush(stdout);
    return 0;
}

static scheduler_task_t* select_next_task(int last_task_id) {
    scheduler_task_t* best = NULL;
    int best_index = -1;
    
    for (size_t i = 0; i < g_queue.count; ++i) {
        scheduler_task_t* candidate = g_queue.tasks[i];
        if (candidate->type == TASK_SHELL_COMMAND) {
            if (best == NULL || candidate->arrival_seq < best->arrival_seq) {
                best = candidate;
                best_index = (int)i;
            }
        }
    }
    
    if (best == NULL) {
        for (size_t i = 0; i < g_queue.count; ++i) {
            scheduler_task_t* candidate = g_queue.tasks[i];
            if (best == NULL || candidate->remaining_time < best->remaining_time ||
                (candidate->remaining_time == best->remaining_time && candidate->arrival_seq < best->arrival_seq)) {
                best = candidate;
                best_index = (int)i;
            }
        }
    }
    
    if (best != NULL && best->task_id == last_task_id && g_queue.count > 1) {
        scheduler_task_t* second = NULL;
        int second_index = -1;
        
        for (size_t i = 0; i < g_queue.count; ++i) {
            scheduler_task_t* candidate = g_queue.tasks[i];
            if (candidate->task_id == last_task_id) {
                continue;
            }
            if (candidate->type == TASK_SHELL_COMMAND) {
                if (second == NULL || candidate->arrival_seq < second->arrival_seq) {
                    second = candidate;
                    second_index = (int)i;
                }
            }
        }
        
        if (second == NULL) {
            for (size_t i = 0; i < g_queue.count; ++i) {
                scheduler_task_t* candidate = g_queue.tasks[i];
                if (candidate->task_id == last_task_id) {
                    continue;
                }
                if (second == NULL || candidate->remaining_time < second->remaining_time ||
                    (candidate->remaining_time == second->remaining_time && candidate->arrival_seq < second->arrival_seq)) {
                    second = candidate;
                    second_index = (int)i;
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
    
    if (task->output_lines) {
        for (int i = 0; i < task->total_output_lines; i++) {
            free(task->output_lines[i]);
        }
        free(task->output_lines);
    }
    free(task);
}

static void remove_client_tasks(client_context_t* client) {
    pthread_mutex_lock(&g_queue.mutex);
    size_t write_index = 0;
    for (size_t i = 0; i < g_queue.count; ++i) {
        scheduler_task_t* task = g_queue.tasks[i];
        if (task->client == client) {
            destroy_task(task);
            continue;
        }
        g_queue.tasks[write_index++] = task;
    }
    g_queue.count = write_index;
    pthread_mutex_unlock(&g_queue.mutex);
}

static void* scheduler_main(void* arg) {
    (void)arg;
    int last_task_id = -1;
    
    while (g_scheduler_running) {
        pthread_mutex_lock(&g_queue.mutex);
        
        while (g_queue.count == 0 && g_scheduler_running) {
            pthread_cond_wait(&g_queue.cond, &g_queue.mutex);
        }
        
        if (!g_scheduler_running) {
            pthread_mutex_unlock(&g_queue.mutex);
            break;
        }
        
        scheduler_task_t* task = select_next_task(last_task_id);
        pthread_mutex_unlock(&g_queue.mutex);
        
        if (task == NULL) {
            continue;
        }
        
        if (!client_context_is_connected(task->client)) {
            destroy_task(task);
            continue;
        }
        
        int start_value = (task->type == TASK_SHELL_COMMAND) ? -1 : task->remaining_time;
        printf(COLOR_GREEN "(%d)---- started (%d)" COLOR_RESET "\n", task->client->client_id, start_value);
        fflush(stdout);
        
        // Track how much time will be used in this execution
        int time_before = task->remaining_time;
        
        if (task->type == TASK_SHELL_COMMAND) {
            run_shell_task(task);
            task->remaining_time = 0;
        } else {
            run_program_task(task);
        }
        
        // For program tasks, add execution time to timeline
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
        
        
        if (task->remaining_time > 0 && !task->cancelled) {
            printf(COLOR_YELLOW "(%d)---- waiting (%d)" COLOR_RESET "\n", task->client->client_id, task->remaining_time);
        } else if (!task->cancelled) {
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
            
            if (task->type != TASK_SHELL_COMMAND) {
                send_string(task->client, "<<END>>\n");
            }
        }

        fflush(stdout);
        
        last_task_id = task->task_id;
        
        if (task->remaining_time > 0 && !task->cancelled) {
            pthread_mutex_lock(&g_queue.mutex);
            task->arrival_seq = ++g_queue.next_arrival_seq;
            g_queue.tasks[g_queue.count++] = task;
            pthread_cond_broadcast(&g_queue.cond);
            pthread_mutex_unlock(&g_queue.mutex);
            continue;
        }
        
        update_stats_on_completion(task->type);
        
        pthread_mutex_lock(&g_queue.mutex);
        int pending = g_queue.count;
        pthread_mutex_unlock(&g_queue.mutex);
        
        if (pending == 0 && task->type != TASK_SHELL_COMMAND) {
            print_timeline();
        }
        
        destroy_task(task);
    }
    return NULL;
}

static void run_shell_task(scheduler_task_t* task) {
    int success = 0;
    char* output = execute_command_with_capture(task->command, &success);
    if (output == NULL) {
        send_string(task->client, "Error: Server failed to execute command.\n");
        return;
    }
    
    size_t output_len = strlen(output);
    if (output_len == 0) {
        send_string(task->client, "\n");
        printf("[%d]<<< 1 bytes sent\n", task->client->client_id);
    } else {
        send_string(task->client, output);
        printf("[%d]<<< %zu bytes sent\n", task->client->client_id, output_len);
    }
    fflush(stdout);
    free(output);
}

static int buffer_demo_output(scheduler_task_t* task) {
    if (task->output_lines != NULL) {
        return 0;
    }
    
    char burst_str[32];
    snprintf(burst_str, sizeof(burst_str), "%d", task->total_burst_time);
    
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("[ERROR] pipe for demo buffering");
        return -1;
    }
    
    pid_t pid = fork();
    if (pid == -1) {
        perror("[ERROR] fork for demo buffering");
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        execl("./demo", "demo", burst_str, NULL);
        fprintf(stderr, "Error: Failed to execute ./demo\n");
        exit(EXIT_FAILURE);
    }
    
    close(pipefd[1]);
    FILE* stream = fdopen(pipefd[0], "r");
    if (stream == NULL) {
        perror("[ERROR] fdopen");
        close(pipefd[0]);
        kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
        return -1;
    }
    
    int capacity = task->total_burst_time + 10;
    task->output_lines = malloc(capacity * sizeof(char*));
    if (task->output_lines == NULL) {
        fclose(stream);
        kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
        return -1;
    }
    
    char line[BUFFER_SIZE];
    int line_count = 0;
    while (fgets(line, sizeof(line), stream) != NULL && line_count < capacity) {
        task->output_lines[line_count] = malloc(strlen(line) + 1);
        if (task->output_lines[line_count] == NULL) {
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
    
    task->total_output_lines = line_count;
    task->lines_sent = 0;
    
    fclose(stream);
    waitpid(pid, NULL, 0);
    return 0;
}

static void run_program_task(scheduler_task_t* task) {
    int quantum = (task->rounds_completed == 0) ? FIRST_ROUND_QUANTUM : NEXT_ROUND_QUANTUM;
    int slice = (task->remaining_time < quantum) ? task->remaining_time : quantum;
    
    printf(COLOR_GREEN "(%d)---- running (%d)" COLOR_RESET "\n", task->client->client_id, slice);
    fflush(stdout);
    
    if (task->type == TASK_PROGRAM_DEMO) {
        if (task->output_lines == NULL) {
            if (buffer_demo_output(task) != 0) {
                task->cancelled = true;
                send_string(task->client, "Error: Failed to execute demo\n");
                return;
            }
        }
        
        int lines_to_send = slice;
        int lines_sent_this_round = 0;
        
        while (lines_sent_this_round < lines_to_send && task->lines_sent < task->total_output_lines) {
            if (!client_context_is_connected(task->client)) {
                task->cancelled = true;
                return;
            }
            
            if (send_string(task->client, task->output_lines[task->lines_sent]) == -1) {
                task->cancelled = true;
                return;
            }
            
            task->lines_sent++;
            task->iterations_completed++;
            task->remaining_time--;
            lines_sent_this_round++;
            
            sleep(1);
        }
    } else if (task->type == TASK_PROGRAM_GENERIC) {
        for (int i = 0; i < slice; ++i) {
            if (!client_context_is_connected(task->client)) {
                task->cancelled = true;
                break;
            }
            
            task->iterations_completed++;
            task->remaining_time--;
            
            if (send_formatted(task->client,
                "[%s] iteration %d/%d\n",
                task->label,
                task->iterations_completed,
                task->total_burst_time) == -1) {
                task->cancelled = true;
                break;
            }
            sleep(1);
        }
    }
    
    task->rounds_completed++;
}

static int execute_program_with_capture(const char* program_path, char** argv, 
                                       int max_iterations, client_context_t* client, 
                                       int* iterations_done) {
    *iterations_done = 0;
    
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("[ERROR] pipe for program execution");
        return -1;
    }
    
    pid_t pid = fork();
    if (pid == -1) {
        perror("[ERROR] fork for program execution");
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        execv(program_path, argv);
        fprintf(stderr, "Error: Failed to execute %s\n", program_path);
        exit(EXIT_FAILURE);
    }
    
    close(pipefd[1]);
    FILE* output_stream = fdopen(pipefd[0], "r");
    if (output_stream == NULL) {
        perror("[ERROR] fdopen");
        close(pipefd[0]);
        kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
        return -1;
    }
    
    char line[BUFFER_SIZE];
    int lines_read = 0;
    
    while (lines_read < max_iterations && fgets(line, sizeof(line), output_stream) != NULL) {
        if (!client_context_is_connected(client)) {
            fclose(output_stream);
            kill(pid, SIGTERM);
            waitpid(pid, NULL, 0);
            return -1;
        }
        
        if (send_string(client, line) == -1) {
            fclose(output_stream);
            kill(pid, SIGTERM);
            waitpid(pid, NULL, 0);
            return -1;
        }
        lines_read++;
    }
    
    *iterations_done = lines_read;
    
    if (lines_read >= max_iterations) {
        kill(pid, SIGTERM);
    }
    
    fclose(output_stream);
    int status;
    waitpid(pid, &status, 0);
    return 0;
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