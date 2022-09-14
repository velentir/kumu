#!/usr/bin/make -f

# Standard make paths
prefix?=/usr/local
exec_prefix?=$(prefix)
bindir?=$(exec_prefix)/bin
binpath?=$(bindir)/kumu

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
CFLAGS+=-std=$(C_STD)
CFLAGS+=-Wall -Wextra -Werror -pedantic
CFLAGS+=-Wno-gnu-statement-expression -Wno-gnu-zero-variadic-macro-arguments
LLVMCOVFLAGS=-fprofile-instr-generate -fcoverage-mapping
LLVMCOVLDFLAGS=-fprofile-instr-generate
LLVM_PROFRAW_FILE=build/debug/kumu.profraw
LLVM_PROFDATA_FILE=build/debug/kumu.profdata
LLVM_COV?=xcrun llvm-cov
LLVM_PROFDATA?=xcrun llvm-profdata

build:;@mkdir $@
build/debug:|build;@mkdir $@
build/release:|build;@mkdir $@
out:;@mkdir $@

build/debug/%.o: kumu/%.c | build/debug
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LLVMCOVFLAGS) -g -c $< -o $@

build/release/%.o: kumu/%.c | build/release
	$(CC) $(CFLAGS) $(CPPFLAGS) -Oz -c $< -o $@

out/kumu: build/release/kumu.o build/release/kumain.o build/release/main.o | out
	$(CC) $(LDFLAGS) $(LDLIBS) $^ -o $@

out/test: build/debug/kumu.o build/debug/kutest.o build/debug/testmain.o | out
	$(CC) $(LDFLAGS) $(LDLIBS) $(LLVMCOVLDFLAGS) $^ -o $@

$(call usage,install,Install the kumu REPL CLI ($(binpath)).)
all:: install
.PHONY: install
install: out/kumu
	cp -f $< $(binpath)

$(call usage,uninstall,Uninstall the kumu REPL CLI ($(binpath)).)
.PHONY: uninstall
uninstall:
	rm -f $(binpath)

$(call usage,kumu,Build the REPL CLI (out/kumu).)
all:: kumu
.PHONY: kumu
kumu: out/kumu

$(call usage,run,Runs the REPL CLI.)
.PHONY: run
run: out/kumu
	$<

$(call usage,test,Build and run the unit tests (out/test).)
all:: test
.PHONY: test
test: out/test
	LLVM_PROFILE_FILE=$(LLVM_PROFRAW_FILE) $<

$(call usage,cov,Get test coverage of kumu core (excludes tests and main).)
all:: cov
.PHONY: cov
cov: test
	$(LLVM_PROFDATA) merge -sparse $(LLVM_PROFRAW_FILE) -o $(LLVM_PROFDATA_FILE)
	$(LLVM_COV) show -show-branches=count -show-expansions -show-line-counts-or-regions -format=html -output-dir=out/cov -instr-profile=$(LLVM_PROFDATA_FILE) build/debug/kumu.o
	$(LLVM_COV) report -instr-profile=$(LLVM_PROFDATA_FILE) build/debug/kumu.o
	@echo Coverage details: out/cov/index.html

$(call usage,clean,Prints this usage message.)
.PHONY: clean
clean:
	$(RM) -r build/*
	$(RM) -r out/*

.PHONY: env
env:
	@echo bindir: $(bindir)

help::;@echo ""
