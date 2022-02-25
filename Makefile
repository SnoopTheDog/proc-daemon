CC=clang
CFLAGS = -std=c99
LDFLAGS = -Weverything -g 
LDLIBS = -lprocps

SRC_DIR = ./src
BUILD_DIR = ./build
BIN = $(BUILD_DIR)/proclog-daemon
SERVICE = ./proclog-daemon.service

# Output directory for binary
EXE_OUT_PATH = /usr/bin/

# Directory with systemd unit files
SYSTEMD_UNIT_DIR = /usr/lib/systemd/system

# Default directory for log file
DAEMON_LOG_DIR = /var/log/proclog-daemon

# Default directory for PID file
DAEMON_PID_DIR = /run/proclog-daemon

all: install

install: $(BIN) | $(DAEMON_LOG_DIR) $(DAEMON_PID_DIR)
	cp $(BIN) $(EXE_OUT_PATH)
	cp $(SERVICE) $(SYSTEMD_UNIT_DIR)
	systemctl daemon-reload

$(BIN): ./src/main.c
	$(CC) $(CFLAGS) ./src/main.c $(LDLIBS) $(LDFLAGS) -o $(BIN)

$(DAEMON_LOG_DIR):
	mkdir -p $(DAEMON_LOG_DIR)

$(DAEMON_PID_DIR):
	mkdir -p $(DAEMON_PID_DIR)