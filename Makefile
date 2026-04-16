# Makefile for ThunderSearch

CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c11
CFLAGS += $(shell pkg-config --cflags gtk4 gtk4-layer-shell-0 gio-2.0)
LDFLAGS = $(shell pkg-config --libs gtk4 gtk4-layer-shell-0 gio-2.0) -lm -lX11

TARGET = thundersearch
OBJ_DIR = obj

SOURCES = main.c \
          window.c \
          app_index.c \
          matcher.c \
          launcher.c \
          config.c \
          file_nav.c \
          animation.c \
          calc.c

OBJECTS = $(SOURCES:%.c=$(OBJ_DIR)/%.o)

# Default target
all: $(TARGET)

# Create obj directory if it doesn't exist
$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

# Link object files to create executable
$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TARGET) $(LDFLAGS)

# Compile source files to object files
$(OBJ_DIR)/%.o: %.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Clean build artifacts
clean:
	rm -rf $(OBJ_DIR) $(TARGET)

# Install binary + desktop file
install: $(TARGET)
	install -D -m 755 $(TARGET) $(DESTDIR)/usr/local/bin/$(TARGET)
	install -D -m 644 thundersearch.desktop \
		$(DESTDIR)/usr/share/applications/thundersearch.desktop

# Install autostart entry for the current user (no DESTDIR support)
install-autostart:
	mkdir -p $(HOME)/.config/autostart
	install -m 644 thundersearch.desktop $(HOME)/.config/autostart/thundersearch.desktop

# Uninstall
uninstall:
	rm -f $(DESTDIR)/usr/local/bin/$(TARGET)
	rm -f $(DESTDIR)/usr/share/applications/thundersearch.desktop
	rm -f $(HOME)/.config/autostart/thundersearch.desktop

# Run the program
run: $(TARGET)
	./$(TARGET)

.PHONY: all clean install install-autostart uninstall run
