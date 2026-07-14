CC = gcc
CFLAGS = -Wall -Wextra -g -O2 -Isrc
TARGET = SimpleShell

SRCS = src/lexer.c src/parser.c src/executor.c src/main.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
