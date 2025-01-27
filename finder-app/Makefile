# Compiler and flags
CROSS_COMPILE ?=
CFLAGS := -Wall -g
CC := $(CROSS_COMPILE)gcc

# Target
TARGET := writer

# Source file
SRC := writer.c
OBJ := $(SRC:.c=.o)

# Default build target
.PHONY: all
all: $(TARGET)

# Build the target
$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

# Compile the source file
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean target
.PHONY: clean
clean:
	rm -f $(TARGET) $(OBJ)

