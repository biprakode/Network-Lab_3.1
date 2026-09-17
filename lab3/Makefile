CC      := gcc
CFLAGS  := -Wall -Wextra -std=c11 -g
UTILS_DIR := ../utils
LAB1_DIR  := ../lab1
INCLUDES  := -I. -I$(UTILS_DIR) -I$(LAB1_DIR)

# Common source files shared by every binary in this lab
SRCS_COMMON := channel.c $(UTILS_DIR)/utils.c $(LAB1_DIR)/error.c $(LAB1_DIR)/scheme.c
OBJS_COMMON := $(SRCS_COMMON:.c=.o)

TEST_DIR := tests

.PHONY: all clean test channel_test

all: channel_test

channel_test: $(TEST_DIR)/channel_test

$(TEST_DIR)/channel_test: $(TEST_DIR)/channel_test.o $(OBJS_COMMON)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

test: channel_test
	./$(TEST_DIR)/channel_test

clean:
	rm -f *.o $(UTILS_DIR)/*.o $(LAB1_DIR)/*.o $(TEST_DIR)/*.o $(TEST_DIR)/channel_test
