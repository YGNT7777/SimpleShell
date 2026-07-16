CC = gcc
CFLAGS = -Wall -Wextra -g -O2 -Isrc
LIBS = -lreadline
TARGET = SimpleShell

SRCS = src/lexer.c src/parser.c src/executor.c src/completion.c src/main.c 
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
