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

.PHONY: all clean test run-receiver run-sender eval perf

all: sender receiver

# ---- error injection module evaluation ----
ERROR_INJECT_DIR := error_inject
ERROR_INJECT_SRCS := $(ERROR_INJECT_DIR)/eval_main.c $(ERROR_INJECT_DIR)/bit_flip.c $(ERROR_INJECT_DIR)/targeted_inject.c
ERROR_INJECT_OBJS := $(ERROR_INJECT_SRCS:.c=.o)

eval: $(ERROR_INJECT_OBJS) $(OBJS_COMMON)
	$(CC) $(CFLAGS) -o $@ $^ -lm

# ---- performance analysis module ----
PERF_ANALYSIS_DIR := perf_analysis
PERF_ANALYSIS_SRCS := $(PERF_ANALYSIS_DIR)/perf_main.c $(PERF_ANALYSIS_DIR)/perf_analyzer.c
PERF_ANALYSIS_OBJS := $(PERF_ANALYSIS_SRCS:.c=.o)

perf: $(PERF_ANALYSIS_OBJS) $(OBJS_COMMON)
	$(CC) $(CFLAGS) -o $@ $^

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
	rm -f *.o $(UTILS_DIR)/*.o sender receiver eval perf $(TEST_BINS) $(TEST_DIR)/*.o $(ERROR_INJECT_OBJS) $(PERF_ANALYSIS_OBJS) output.txt results.csv perf_results.csv