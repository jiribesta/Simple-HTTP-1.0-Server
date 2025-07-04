# Source files
SRCS = $(wildcard src/*.c)

# Executable name
TARGET = http_server

# Build target
all: $(TARGET)

$(TARGET): $(SRCS)
	gcc -o $@ $^

# Clean up
clean:
	rm -f $(TARGET)

.PHONY: all clean