
# # the code below is makefile for phase 2
# # this is the old makefile

# # compiler and flags
# CC = gcc
# CFLAGS = -Wall -Wextra -std=c99 -g -pedantic
# LDFLAGS =

# # target executables
# TARGET_SHELL = myshell      # Phase 1 local shell
# TARGET_SERVER = server      # Phase 2 server
# TARGET_CLIENT = client      # Phase 2 client (will be implemented by Person B)

# # source files
# # Phase 1 shell sources
# SHELL_SOURCES = myshell.c shell_utils.c

# # Phase 2 server sources (reuses shell_utils.c from Phase 1)
# SERVER_SOURCES = server.c shell_utils.c

# # Phase 2 client sources (standalone, no Phase 1 dependency)
# CLIENT_SOURCES = client.c

# # header files
# HEADERS = shell_utils.h

# # Object files are automatically generated from source files
# SHELL_OBJECTS = $(SHELL_SOURCES:.c=.o)
# SERVER_OBJECTS = $(SERVER_SOURCES:.c=.o)
# CLIENT_OBJECTS = $(CLIENT_SOURCES:.c=.o)

# # Build everything by default (shell, server, and client)
# all: $(TARGET_SHELL) $(TARGET_SERVER) $(TARGET_CLIENT)

# # Note: All three executables are now built by default (Phase 1 shell, Phase 2 server, Phase 2 client)

# # build targets
# # Build the original Phase 1 myshell
# $(TARGET_SHELL): $(SHELL_OBJECTS)
# 	@echo "Linking $(TARGET_SHELL)..."
# 	$(CC) $(SHELL_OBJECTS) -o $(TARGET_SHELL) $(LDFLAGS)
# 	@echo "Build successful: $(TARGET_SHELL)"

# # Build the Phase 2 server
# $(TARGET_SERVER): $(SERVER_OBJECTS)
# 	@echo "Linking $(TARGET_SERVER)..."
# 	$(CC) $(SERVER_OBJECTS) -o $(TARGET_SERVER) $(LDFLAGS)
# 	@echo "Build successful: $(TARGET_SERVER)"

# # Build the Phase 2 client (to be implemented by Person B)
# $(TARGET_CLIENT): $(CLIENT_OBJECTS)
# 	@echo "Linking $(TARGET_CLIENT)..."
# 	$(CC) $(CLIENT_OBJECTS) -o $(TARGET_CLIENT) $(LDFLAGS)
# 	@echo "Build successful: $(TARGET_CLIENT)"

# # compilation rules
# %.o: %.c $(HEADERS)
# 	@echo "Compiling $<..."
# 	$(CC) $(CFLAGS) -c $< -o $@


# # individual build targets
# # Build only the server
# server: $(TARGET_SERVER)

# # Build only the client
# client: $(TARGET_CLIENT)

# # Build only the Phase 1 shell
# myshell: $(TARGET_SHELL)

# # utility targets
# # clean up all generated files (object files and executables)
# clean:
# 	@echo "Cleaning up..."
# 	rm -f $(SHELL_OBJECTS) $(SERVER_OBJECTS) $(CLIENT_OBJECTS)
# 	rm -f $(TARGET_SHELL) $(TARGET_SERVER) $(TARGET_CLIENT)
# 	rm -f *.txt *.log
# 	@echo "Clean complete!"

# # rebuild everything from scratch (clean then build)
# rebuild: clean all

# # testing targets
# # run server (starts server on port 8080)
# run-server: $(TARGET_SERVER)
# 	@echo "Starting server on port 8080..."
# 	@echo "To test: Use 'telnet localhost 8080' or 'nc localhost 8080' in another terminal"
# 	@echo "Or wait for Person B to implement the client"
# 	./$(TARGET_SERVER)

# # run client (to be used after Person B implements it)
# run-client: $(TARGET_CLIENT)
# 	@echo "Starting client..."
# 	./$(TARGET_CLIENT)

# # test Phase 1 shell
# test-shell: $(TARGET_SHELL)
# 	@echo "Running Phase 1 shell..."
# 	./$(TARGET_SHELL)

# # help target
# help:
# 	@echo "Available targets:"
# 	@echo "  all          - Build myshell and server (default)"
# 	@echo "  server       - Build only the Phase 2 server"
# 	@echo "  client       - Build only the Phase 2 client"
# 	@echo "  myshell      - Build only the Phase 1 shell"
# 	@echo "  clean        - Remove all generated files"
# 	@echo "  rebuild      - Clean and build from scratch"
# 	@echo "  run-server   - Build and run the server"
# 	@echo "  run-client   - Build and run the client"
# 	@echo "  test-shell   - Build and run Phase 1 shell"
# 	@echo "  help         - Show this help message"

# # phony targets
# .PHONY: all clean rebuild server client myshell run-server run-client test-shell help

# # precious files
# # prevent make from deleting intermediate object files
# .PRECIOUS: %.o



# the code below is makefile for phase 3
# this is the new makefile

# compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g -pedantic
LDFLAGS = -pthread

# target executables
TARGET_SHELL = myshell      # Phase 1 local shell
TARGET_SERVER = server      # Phase 4 scheduled server
TARGET_CLIENT = client      # Remote client
TARGET_DEMO = demo          # Standalone workload generator

# source files
SHELL_SOURCES = myshell.c shell_utils.c
SERVER_SOURCES = server.c shell_utils.c
CLIENT_SOURCES = client.c
DEMO_SOURCES = demo.c

# header files
HEADERS = shell_utils.h

# Object files
SHELL_OBJECTS = $(SHELL_SOURCES:.c=.o)
SERVER_OBJECTS = $(SERVER_SOURCES:.c=.o)
CLIENT_OBJECTS = $(CLIENT_SOURCES:.c=.o)
DEMO_OBJECTS = $(DEMO_SOURCES:.c=.o)

# Build everything by default
all: $(TARGET_SHELL) $(TARGET_SERVER) $(TARGET_CLIENT) $(TARGET_DEMO)

# build targets
$(TARGET_SHELL): $(SHELL_OBJECTS)
	@echo "Linking $(TARGET_SHELL)..."
	$(CC) $(SHELL_OBJECTS) -o $(TARGET_SHELL)
	@echo "Build successful: $(TARGET_SHELL)"

$(TARGET_SERVER): $(SERVER_OBJECTS)
	@echo "Linking $(TARGET_SERVER)..."
	$(CC) $(SERVER_OBJECTS) -o $(TARGET_SERVER) $(LDFLAGS)
	@echo "Build successful: $(TARGET_SERVER)"

$(TARGET_CLIENT): $(CLIENT_OBJECTS)
	@echo "Linking $(TARGET_CLIENT)..."
	$(CC) $(CLIENT_OBJECTS) -o $(TARGET_CLIENT)
	@echo "Build successful: $(TARGET_CLIENT)"

$(TARGET_DEMO): $(DEMO_OBJECTS)
	@echo "Linking $(TARGET_DEMO)..."
	$(CC) $(DEMO_OBJECTS) -o $(TARGET_DEMO)
	@echo "Build successful: $(TARGET_DEMO)"

# compilation rules
%.o: %.c $(HEADERS)
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

# individual build targets
server: $(TARGET_SERVER)
client: $(TARGET_CLIENT)
myshell: $(TARGET_SHELL)
demo: $(TARGET_DEMO)

# utility targets
clean:
	@echo "Cleaning up..."
	rm -f $(SHELL_OBJECTS) $(SERVER_OBJECTS) $(CLIENT_OBJECTS) $(DEMO_OBJECTS)
	rm -f $(TARGET_SHELL) $(TARGET_SERVER) $(TARGET_CLIENT) $(TARGET_DEMO)
	rm -f *.txt *.log
	@echo "Clean complete!"

rebuild: clean all

# testing targets
run-server: $(TARGET_SERVER)
	@echo "Starting multithreaded server on port 8080..."
	@echo "Multiple clients can now connect simultaneously"
	./$(TARGET_SERVER)

run-client: $(TARGET_CLIENT)
	@echo "Starting client..."
	./$(TARGET_CLIENT)

test-shell: $(TARGET_SHELL)
	@echo "Running Phase 1 shell..."
	./$(TARGET_SHELL)

help:
	@echo "Available targets:"
	@echo "  all          - Build myshell, server, client, and demo (default)"
	@echo "  server       - Build only the scheduled server"
	@echo "  client       - Build only the client"
	@echo "  myshell      - Build only the Phase 1 shell"
	@echo "  demo         - Build the standalone workload generator"
	@echo "  clean        - Remove all generated files"
	@echo "  rebuild      - Clean and build from scratch"
	@echo "  run-server   - Build and run the multithreaded server"
	@echo "  run-client   - Build and run the client"
	@echo "  test-shell   - Build and run Phase 1 shell"
	@echo "  help         - Show this help message"

.PHONY: all clean rebuild server client myshell run-server run-client test-shell help
.PRECIOUS: %.o