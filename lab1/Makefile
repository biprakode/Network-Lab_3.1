CC      := gcc
CFLAGS  := -Wall -Wextra -std=c11 -g
UTILS_DIR := ../utils
INCLUDES  := -I. -I$(UTILS_DIR)

# Object files shared by sender, receiver, AND every test binary.
SRCS_COMMON := error.c scheme.c $(UTILS_DIR)/utils.c
OBJS_COMMON := $(SRCS_COMMON:.c=.o)

TEST_DIR  := tests
TEST_SRCS := $(wildcard $(TEST_DIR)/*_test.c)
TEST_BINS := $(TEST_SRCS:.c=)

.PHONY: all clean test run-receiver run-sender

all: sender receiver

sender: sender.o $(OBJS_COMMON)
	$(CC) $(CFLAGS) -o $@ $^

receiver: receiver.o $(OBJS_COMMON)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

# ---- tests ----
$(TEST_DIR)/%_test: $(TEST_DIR)/%_test.c $(OBJS_COMMON)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^

test: $(TEST_BINS)
	@for t in $(TEST_BINS); do \
		echo "== $$t =="; \
		./$$t || exit 1; \
	done

# ---- convenience targets for the two-terminal manual test ----
# Run these AS-IS in two separate terminals — see instructions below.
run-receiver: receiver
	./receiver output.txt checksum

run-sender: sender
	./sender data.txt checksum

clean:
	rm -f *.o $(UTILS_DIR)/*.o sender receiver $(TEST_BINS) $(TEST_DIR)/*.o output.txt