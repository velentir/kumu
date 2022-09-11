#!/usr/bin/make -f

# Declare this first, so that it is the default target.
.PHONY: help
help::

# Remove the default compiler from Make, while still allowing for a custom compiler to be specified by the environment.
ifeq ($(origin CC),default)
CC=
endif
ifndef CC
CC=clang
endif # CC
C_STD?=c11
CFLAGS+=-Wall -Werror -std=$(C_STD)
# TODO: add "-Wextra -pedantic"

out:
	@mkdir $@

out/%.o: kumu/%.c | out
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $< -o $@

out/kumu: out/kumu.o out/kumain.o out/main.o
	$(CC) $(LDFLAGS) $(LDLIBS) $^ -o $@

out/test: out/kumu.o out/kutest.o out/testmain.o
	$(CC) $(LDFLAGS) $(LDLIBS) $^ -o $@

.PHONY: all
all::

.PHONY: kumu
kumu: out/kumu
all:: kumu

.PHONY: run
run: out/kumu
	$<

.PHONY: test
test: out/test
	$<
all:: test

.PHONY: clean
clean:
	$(RM) -r out/*

help::
	@echo "Usage: make target..."
	@echo ""
	@echo "Targets:"
	@echo "  kumu       Build the REPL CLI (out/kumu)."
	@echo "  run        Runs the REPL CLI."
	@echo "  test       Build and run the unit tests (out/test)."
	@echo "  clean      Cleans all build artifacts."
	@echo "  all        Build all of the output targets (out/*)."
	@echo "  help       Prints this usage message."
