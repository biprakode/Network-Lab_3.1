CC      := gcc
CFLAGS  := -Wall -Wextra -std=c11 -g
UTILS_DIR := ../utils
LAB1_DIR := ../lab1
INCLUDES := -I. -I$(UTILS_DIR) -I$(LAB1_DIR)

# Common source files (shared by all binaries)
SRCS_COMMON := channel.c timer.c $(UTILS_DIR)/utils.c $(LAB1_DIR)/error.c $(LAB1_DIR)/scheme.c $(LAB1_DIR)/error_inject/bit_flip.c
OBJS_COMMON := $(SRCS_COMMON:.c=.o)

# Test targets
TEST_DIR  := tests
TEST_SRCS := $(wildcard $(TEST_DIR)/*_test.c)
TEST_BINS := $(TEST_SRCS:.c=)

.PHONY: all clean test channel_test timer_test arq_test

all: channel_test timer_test arq_test

channel_test: $(TEST_DIR)/channel_test

timer_test: $(TEST_DIR)/timer_test

arq_test: $(TEST_DIR)/arq_test

$(TEST_DIR)/channel_test: $(TEST_DIR)/channel_test.o $(OBJS_COMMON)
	$(CC) $(CFLAGS) -o $@ $^

$(TEST_DIR)/timer_test: $(TEST_DIR)/timer_test.o $(OBJS_COMMON)
	$(CC) $(CFLAGS) -o $@ $^

$(TEST_DIR)/arq_test: $(TEST_DIR)/arq_test.o arq/sender.o arq/receiver.o $(OBJS_COMMON)
	$(CC) $(CFLAGS) -pthread -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

test: channel_test timer_test arq_test
	./$(TEST_DIR)/channel_test
	./$(TEST_DIR)/timer_test
	./$(TEST_DIR)/arq_test

clean:
	rm -f *.o $(UTILS_DIR)/*.o $(LAB1_DIR)/*.o $(LAB1_DIR)/error_inject/*.o arq/*.o $(TEST_DIR)/*.o $(TEST_DIR)/channel_test $(TEST_DIR)/timer_test $(TEST_DIR)/arq_test
