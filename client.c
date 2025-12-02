// client.c -- Phase 4 Client Implementation
// Client connects to the remote shell server via TCP socket,
// presents a shell prompt to the user, sends commands to server,
// and receives and displays output from server

#define _POSIX_C_SOURCE 200809L
// Standard C library headers
#include <stdbool.h>     // bool type
#include <stdio.h>       // printf(), fprintf(), fgets(), etc.
#include <stdlib.h>      // EXIT_SUCCESS, EXIT_FAILURE, atoi()
#include <string.h>      // strncmp(), strlen(), strstr(), memset()
#include <unistd.h>      // close()
// Socket programming headers
#include <sys/socket.h>  // socket(), connect(), send(), recv()
#include <netinet/in.h>  // struct sockaddr_in
#include <arpa/inet.h>   // inet_pton(), htons()
// System headers
#include <errno.h>       // errno, EAGAIN, EWOULDBLOCK for error handling
#include <fcntl.h>       // File control operations (unused but may be needed)
#include <sys/time.h>    // struct timeval for socket timeouts

// Client configuration constants (must match server protocol)
#define PORT 8081                 // Server port number (must match server)
#define BUFFER_SIZE 4096          // Buffer size for send/receive operations (must match server)
#define SERVER_IP "127.0.0.1"     // Default server IP address (localhost)

// Function prototypes
// Socket management functions
int create_and_connect_socket(const char* server_ip, int port);
// User interface and command handling
void run_client_loop(int socket_fd);
int is_empty_or_whitespace(const char* str);
// Error handling functions
void print_connection_error(const char* server_ip, int port);
void print_connection_lost_error(void);

// Client entry point
// Parses command line arguments, connects to server, and runs main client loop
// Usage: ./client [server_ip] [port]
// Parameters:
//   argc - Number of command line arguments
//   argv - Command line arguments
// Returns: EXIT_SUCCESS on normal exit, EXIT_FAILURE on connection error
int main(int argc, char* argv[]) {
    // Parse command line arguments for server IP and port (optional)
    const char* server_ip = SERVER_IP;  // Default to localhost
    int port = PORT;                      // Default to 8081
    
    // Allow user to specify server IP and port as command line arguments
    // Usage: ./client [server_ip] [port]
    if (argc >= 2) {
        server_ip = argv[1];  // Use provided IP
    }
    if (argc >= 3) {
        port = atoi(argv[2]);  // Use provided port
        // Validate port number (must be in valid range)
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "Error: Invalid port number. Using default port %d\n", PORT);
            port = PORT;
        }
    }
    
    // Create socket and connect to server
    int socket_fd = create_and_connect_socket(server_ip, port);
    if (socket_fd == -1) {
        // Connection failed - error message already printed
        return EXIT_FAILURE;
    }
    
    // Print connection message matching assignment format
    printf("Connected to a server\n");
    
    // Connection successful - enter main client loop
    // Presents prompt, reads commands, sends to server, displays results
    run_client_loop(socket_fd);
    
    // Cleanup - close socket connection
    close(socket_fd);
    return EXIT_SUCCESS;
}

// Create a TCP socket and connect to the specified server
// Parameters:
//   server_ip - IP address of server as string (e.g., "127.0.0.1")
//   port - Port number to connect to
// Returns: Socket file descriptor on success, -1 on failure
int create_and_connect_socket(const char* server_ip, int port) {
    int socket_fd;
    struct sockaddr_in server_addr;
    
    // Create TCP socket (AF_INET = IPv4, SOCK_STREAM = TCP)
    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd == -1) {
        perror("Error: Socket creation failed");
        return -1;
    }
    
    // Configure server address structure
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;           // IPv4 protocol
    server_addr.sin_port = htons(port);          // Convert port to network byte order
    
    // Convert IP address from string to binary form
    // inet_pton converts "127.0.0.1" to binary representation
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "Error: Invalid server IP address: %s\n", server_ip);
        close(socket_fd);
        return -1;
    }
    
    // Connect to server
    // This blocks until connection is established or fails
    if (connect(socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        // Connection failed - server might not be running
        print_connection_error(server_ip, port);
        close(socket_fd);
        return -1;
    }
    
    // Connection successful
    return socket_fd;
}

// Main client loop
// Presents shell prompt, reads user commands, sends to server, displays output
// Continues until user types "exit" or connection is lost
// Handles asynchronous command execution by tracking pending commands
// Parameters:
//   socket_fd - Socket file descriptor for server connection
void run_client_loop(int socket_fd) {
    char command[BUFFER_SIZE];    // Buffer for user input
    char response[BUFFER_SIZE];   // Buffer for server response
    ssize_t bytes_received;
    int pending_commands = 0;     // Track how many commands we're waiting for completion
    int pending_shell_commands = 0;  // Track how many shell commands are pending (don't send <<END>>)
    
    // main command loop - runs indefinitely until exit or disconnect
    while (1) {
        // Only prompt for input if we don't have pending commands
        // If we have pending commands, continue receiving their output
        if (pending_commands == 0) {
            // display shell prompt - CHANGED TO >>> to match assignment
            printf(">>> ");
            fflush(stdout);
            
            // read command from user input
            if (fgets(command, sizeof(command), stdin) == NULL) {
                const char* exit_cmd = "exit\n";
                send(socket_fd, exit_cmd, strlen(exit_cmd), 0);
                break;
            }
            
            // check if command is empty or only whitespace
            if (is_empty_or_whitespace(command)) {
                continue;
            }
            
            // send command to server
            ssize_t bytes_sent = send(socket_fd, command, strlen(command), 0);
            if (bytes_sent == -1) {
                perror("Error: Failed to send command to server");
                print_connection_lost_error();
                break;
            }
            
            // check if user wants to exit
            if (strncmp(command, "exit", 4) == 0) {
                recv(socket_fd, response, sizeof(response) - 1, 0);
                break;
            }
            
            // Determine if this is a program command (demo/program) or shell command
            bool is_program_command = (strncmp(command, "./demo", 6) == 0 || 
                                       strncmp(command, "demo", 4) == 0 ||
                                       strncmp(command, "program", 7) == 0);
            
            // Increment pending commands counter
            pending_commands++;
            if (!is_program_command) {
                pending_shell_commands++;  // Track shell commands separately
            }
            
            // Set up timeout for receiving data based on command type
            struct timeval tv;
            if (is_program_command) {
                // Demo/program commands: 30 seconds timeout (handles scheduling delays)
                tv.tv_sec = 30;
                tv.tv_usec = 0;
            } else {
                // Shell commands: 300ms timeout (should respond quickly)
                tv.tv_sec = 0;
                tv.tv_usec = 300000;
            }
            setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        }
        
        // Receive and display output until we get <<END>> for all pending commands
        // This handles the case where multiple commands are sent in quick succession
        while (pending_commands > 0) {
            memset(response, 0, sizeof(response));
            bytes_received = recv(socket_fd, response, sizeof(response) - 1, 0);
            
            if (bytes_received > 0) {
                // Got more data
                response[bytes_received] = '\0';
                
                // Check for end-of-task marker
                char* marker_pos = strstr(response, "<<END>>");
                if (marker_pos != NULL) {
                    // Remove the marker from output
                    *marker_pos = '\0';  // Truncate at marker
                    if (strlen(response) > 0) {
                        printf("%s", response);
                        fflush(stdout);  // Flush immediately
                    }
                    // One command completed (program command)
                    pending_commands--;
                } else {
                    // Normal output, display it
                    printf("%s", response);
                    fflush(stdout);  // Flush immediately to ensure output is displayed
                }
            } else if (bytes_received == 0) {
                // Server closed connection
                print_connection_lost_error();
                return;
            } else if (bytes_received == -1) {
                // Error occurred during receive
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // Timeout - no more data for now
                    // For shell commands, if we got a timeout after receiving output,
                    // assume the command is complete (shell commands don't send <<END>>)
                    if (pending_shell_commands > 0) {
                        // Shell command completed (timeout after output means done)
                        pending_shell_commands--;
                        pending_commands--;
                    } else if (pending_commands > 0) {
                        // Program command still pending - reset timeout and continue
                        struct timeval tv;
                        tv.tv_sec = 30;
                        tv.tv_usec = 0;
                        setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
                    }
                    break;
                } else {
                    // Real error (not just a timeout)
                    perror("Error: Failed to receive response from server");
                    print_connection_lost_error();
                    return;
                }
            }
        }
        
        // Clear timeout when no pending commands
        if (pending_commands == 0) {
            pending_shell_commands = 0;  // Reset shell command counter
            struct timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 0;
            setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        }
    }
}

// Utility function to check if string is empty or contains only whitespace
// Parameters:
//   str - String to check
// Returns: 1 if empty/whitespace only, 0 otherwise
int is_empty_or_whitespace(const char* str) {
    if (str == NULL || str[0] == '\0') {
        return 1;  // NULL or empty string
    }
    // Check if string contains only whitespace characters
    for (int i = 0; str[i] != '\0'; i++) {
        // If we find a non-whitespace character, string is not empty
        if (str[i] != ' ' && str[i] != '\t' && str[i] != '\n' && str[i] != '\r') {
            return 0;  // Found non-whitespace character
        }
    }
    return 1;  // Only whitespace found
}

// Error handling functions
// Print user-friendly error messages

// Print connection error when unable to connect to server
// Parameters:
//   server_ip - IP address that connection was attempted to
//   port - Port number that connection was attempted to
void print_connection_error(const char* server_ip, int port) {
    fprintf(stderr, "Error: Cannot connect to server at %s:%d\n", server_ip, port);
    fprintf(stderr, "Please ensure the server is running and the IP/port are correct.\n");
    fprintf(stderr, "To start the server: ./server\n");
}

// Print error when connection to server is lost during operation
void print_connection_lost_error(void) {
    fprintf(stderr, "\nError: Connection to server lost.\n");
    fprintf(stderr, "The server may have crashed or the network connection was interrupted.\n");
}