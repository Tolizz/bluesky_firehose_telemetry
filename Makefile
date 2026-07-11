# Ορισμός του Compiler και των Flags
CC = gcc
CFLAGS = -Wall -Wextra -O2 -g
LDFLAGS = -lpthread -lwebsockets -lcjson

# Πηγαία αρχεία (ΜΟΝΟ το main.c πλέον)
SOURCES = main.c
OBJECTS = $(SOURCES:.c=.o)

# Το όνομα του τελικού εκτελέσιμου
TARGET = bsky_monitor

# Βασικός κανόνας
all: $(TARGET)

# Κανόνας δημιουργίας του εκτελέσιμου
$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Κανόνας μεταγλώττισης των .c σε .o
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Καθαρισμός των παραγόμενων αρχείων
clean:
	rm -f $(OBJECTS) $(TARGET)
