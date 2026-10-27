-include config.mk

CC ?= cc
LLVM_CONFIG ?= llvm-config
PKG_CONFIG ?= pkg-config
CFLAGS ?= -O2 -g
WARNINGS := -std=c11 -Wall -Wextra -Wpedantic -Werror
LLVM_CFLAGS = $(shell $(LLVM_CONFIG) --cflags)
LLVM_LIBS = $(shell $(LLVM_CONFIG) --link-shared --ldflags --libs core executionengine interpreter --system-libs)
LLVM_RPATH = -Wl,-rpath,$(shell $(LLVM_CONFIG) --libdir)
SDL_CFLAGS = $(shell $(PKG_CONFIG) --cflags sdl2)
SDL_LIBS = $(shell $(PKG_CONFIG) --libs sdl2)
OPT = $(shell $(LLVM_CONFIG) --bindir)/opt

.PHONY: all run ir check clean
all: build/irgen

build:
	mkdir -p $@

build/irgen: main.c irgen.c irgen.h sim.c sim.h app.h Makefile | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $(LLVM_CFLAGS) $(SDL_CFLAGS) main.c irgen.c sim.c $(LDFLAGS) $(LLVM_RPATH) $(LLVM_LIBS) $(SDL_LIBS) -o $@

build/reference: tests/reference.c app.c app.h sim.c sim.h Makefile | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $(SDL_CFLAGS) -I. tests/reference.c app.c sim.c $(LDFLAGS) $(SDL_LIBS) -o $@

build/control: tests/control.c irgen.c irgen.h app.h sim.h Makefile | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $(LLVM_CFLAGS) -UNDEBUG -I. tests/control.c irgen.c $(LDFLAGS) $(LLVM_RPATH) $(LLVM_LIBS) -o $@

run: build/irgen
	./build/irgen

ir: build/irgen
	./build/irgen --emit build/app.ll --no-run

check: build/irgen build/reference build/control ir
	$(OPT) -passes=verify -disable-output build/app.ll
	sh tests/check.sh
	./build/control

clean:
	rm -rf build
