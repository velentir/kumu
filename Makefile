#!/usr/bin/make -f

# Declare the help target first, so that it is the default target.
usage=help::;@echo "  $(1)		$(2)"
.PHONY: help
help::
	@echo "Usage: make target..."
	@echo ""
	@echo "Targets:"
$(call usage,help,Prints this usage message.)

.PHONY: all
all::

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

out:;@mkdir $@

out/%.o: kumu/%.c | out
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $< -o $@

out/kumu: out/kumu.o out/kumain.o out/main.o
	$(CC) $(LDFLAGS) $(LDLIBS) $^ -o $@

out/test: out/kumu.o out/kutest.o out/testmain.o
	$(CC) $(LDFLAGS) $(LDLIBS) $^ -o $@

$(call usage,kumu,Build the REPL CLI (out/kumu).)
all:: kumu
.PHONY: kumu
kumu: out/kumu

$(call usage,run,Runs the REPL CLI.)
.PHONY: run
run: out/kumu
	@$<

$(call usage,test,Build and run the unit tests (out/test).)
all:: test
.PHONY: test
test: out/test
	@$<

$(call usage,clean,Prints this usage message.)
.PHONY: clean
clean:
	$(RM) -r out/*

help::;@echo ""
