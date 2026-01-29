CC = cc
CFLAGS = -Wall -Wextra -g -I. -I../src

# Musashi sources (pre-built objects in musashi/)
MUSASHI_DIR = musashi
MUSASHI_OBJS = $(MUSASHI_DIR)/m68kcpu.o $(MUSASHI_DIR)/m68kops.o $(MUSASHI_DIR)/softfloat/softfloat.o

# Compiler library
COMPILER_SRCS = compiler.c emitters.c branches.c flags.c interop.c cb_prefix.c reg_loads.c alu.c stack.c instructions.c timing.c
COMPILER_OBJS = compiler.o emitters.o branches.o flags.o interop.o cb_prefix.o reg_loads.o alu.o stack.o instructions.o timing.o

# Test binary
TEST_BIN = tests/test_compiler
TEST_SRCS = tests/test_compiler.c tests/test_exec_loads.c tests/test_exec_alu.c \
            tests/test_exec_branches.c tests/test_exec_cb.c tests/test_exec_stack.c \
            tests/test_exec_timing.c

.PHONY: all test clean

all: $(TEST_BIN)

$(TEST_BIN): $(TEST_SRCS) $(COMPILER_OBJS) $(MUSASHI_OBJS)
	$(CC) $(CFLAGS) -I$(MUSASHI_DIR) -o $@ $(TEST_SRCS) $(COMPILER_OBJS) $(MUSASHI_OBJS) -lm

compiler.o: compiler.c compiler.h
	$(CC) $(CFLAGS) -c -o $@ $<

test: $(TEST_BIN)
	./$(TEST_BIN)

clean:
	rm -f $(COMPILER_OBJS) $(TEST_BIN)
