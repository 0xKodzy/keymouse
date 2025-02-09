CC = gcc
CFLAGS = -Wall -g $(shell pkg-config --cflags x11 freetype2 xft)
LDFLAGS = $(shell pkg-config --libs x11 xrandr freetype2 xft) -lXtst -lXrender
TARGET = overlay
SRC = main.c
OBJ = $(SRC:.c=.o)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJ)