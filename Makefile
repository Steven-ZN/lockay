.PHONY: all clean test install

CC       := gcc
CFLAGS   := -Wall -Wextra -Werror -std=c11 -O2 -g -D_GNU_SOURCE
LDFLAGS  :=

SRCDIR   := src
BUILDDIR := build
TESTDIR  := tests

# Common objects
COMMON   := filebuf sha256 lockdb validate apply cmdlock

COMMON_OBJS := $(addprefix $(BUILDDIR)/, $(addsuffix .o, $(COMMON)))

# lockay binary (TUI + CLI + cmdlock)
LOCKAY_OBJS := $(COMMON_OBJS) $(BUILDDIR)/tui.o $(BUILDDIR)/main.o

# Test binary
TEST_OBJS := $(COMMON_OBJS) $(BUILDDIR)/test_runner.o

all: $(BUILDDIR)/lockay $(BUILDDIR)/test_runner

$(BUILDDIR)/lockay: $(LOCKAY_OBJS)
	@mkdir -p $(BUILDDIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILDDIR)/test_runner: $(TEST_OBJS)
	@mkdir -p $(BUILDDIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(BUILDDIR)
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

$(BUILDDIR)/test_runner.o: $(TESTDIR)/test_runner.c
	@mkdir -p $(BUILDDIR)
	$(CC) $(CFLAGS) -I$(SRCDIR) -c -o $@ $<

-include $(BUILDDIR)/*.d

test: $(BUILDDIR)/test_runner
	@$(BUILDDIR)/test_runner

clean:
	rm -rf $(BUILDDIR)

install: $(BUILDDIR)/lockay
	install -m 755 $(BUILDDIR)/lockay /usr/local/bin/lockay
