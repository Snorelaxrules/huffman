CC      = cc
CFLAGS  = -std=c11 -Wall -Wextra -O2
LDFLAGS =

BIN     = huff
SRCS    = main.c huffman.c priorityqueue.c bitwriter.c
OBJS    = $(SRCS:.c=.o)
DEPS    = huffman.h priority_queue.h bitwriter.h

.PHONY: all debug check clean

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS)

%.o: %.c $(DEPS)
	$(CC) $(CFLAGS) -c -o $@ $<

# Rebuild from scratch with sanitizers; run the binary normally to use them.
debug: CFLAGS += -g -O0 -fsanitize=address,undefined
debug: clean $(BIN)

# Smoke test: every source file should survive a round trip byte for byte.
check: $(BIN)
	@for f in $(SRCS) $(DEPS) Makefile; do \
		./$(BIN) c $$f /tmp/huff-check.huf >/dev/null && \
		./$(BIN) d /tmp/huff-check.huf /tmp/huff-check.out && \
		cmp -s $$f /tmp/huff-check.out && echo "ok   $$f" || { echo "FAIL $$f"; exit 1; }; \
	done
	@rm -f /tmp/huff-check.huf /tmp/huff-check.out

clean:
	rm -f $(BIN) $(OBJS)
