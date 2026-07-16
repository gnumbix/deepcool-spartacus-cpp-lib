# libspartacus — DeepCool SPARTACUS 360 / 420 C++17 client
#
# Primary build (cmake is not required):
#   make            build the static library and the examples
#   make test       build and run the golden-vector tests (no hardware needed)
#   make check      alias for `make test`
#   make examples   build the example programs
#   make install    install headers, libspartacus.a and spartacus.pc under $(PREFIX)
#                   (default /usr/local)
#   make clean      remove the build directory
#
# Image support (showRgb / showJpegFile / clear) is on by default; build the core-only
# library (no bundled JPEG encoder) with:  make WITH_IMAGE=0

CXX      ?= c++
AR       ?= ar
PREFIX   ?= /usr/local

CXXSTD   := -std=c++17
WARN     := -Wall -Wextra
OPT      ?= -O2
INCLUDES := -Iinclude -Ithird_party

WITH_IMAGE ?= 1

# Library version — parsed from the single source of truth in Version.hpp.
VERSION := $(shell sed -n 's/^\#define SPARTACUS_VERSION_STRING "\(.*\)".*/\1/p' \
             include/spartacus/Version.hpp)

LIBUSB_CFLAGS := $(shell pkg-config --cflags libusb-1.0 2>/dev/null)
LIBUSB_LIBS   := $(shell pkg-config --libs libusb-1.0 2>/dev/null)

CXXFLAGS ?= $(CXXSTD) $(WARN) $(OPT)
CPPFLAGS := $(INCLUDES) $(LIBUSB_CFLAGS)
DEPFLAGS := -MMD -MP

BUILD := build
LIB   := $(BUILD)/libspartacus.a
PC    := $(BUILD)/spartacus.pc

LIB_SRCS := src/Protocol.cpp src/Display.cpp src/Linker.cpp

# Examples that only use the core API (no image helpers) build in any configuration.
EXAMPLES := brightness orientation native_telemetry \
            linker_read_speeds linker_set_speeds linker_argb linker_motherboard_sync

ifeq ($(WITH_IMAGE),1)
LIB_SRCS += src/Image.cpp
CPPFLAGS += -DSPARTACUS_WITH_IMAGE
# These examples call showRgb()/clear(), which require image support.
EXAMPLES += image clear
endif

LIB_OBJS := $(patsubst src/%.cpp,$(BUILD)/%.o,$(LIB_SRCS))
EXAMPLE_BINS := $(addprefix $(BUILD)/examples/,$(EXAMPLES))

.PHONY: all lib examples test check clean install
all: lib examples

lib: $(LIB)

# Create build directories (order-only prerequisites).
$(BUILD) $(BUILD)/examples:
	mkdir -p $@

$(LIB): $(LIB_OBJS)
	$(AR) rcs $@ $^

$(BUILD)/%.o: src/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(DEPFLAGS) -c $< -o $@

examples: $(EXAMPLE_BINS)

$(BUILD)/examples/%: examples/%.cpp $(LIB) | $(BUILD)/examples
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $< $(LIB) $(LIBUSB_LIBS) -o $@

# Golden-vector tests: pure builders only — no libusb, no image, no hardware.
test: $(BUILD)/test_vectors
	./$(BUILD)/test_vectors

check: test

$(BUILD)/test_vectors: tests/test_vectors.cpp src/Protocol.cpp include/spartacus/Protocol.hpp | $(BUILD)
	$(CXX) $(CXXSTD) $(WARN) $(OPT) -Iinclude tests/test_vectors.cpp src/Protocol.cpp -o $@

# pkg-config metadata, filled in from packaging/spartacus.pc.in.
$(PC): packaging/spartacus.pc.in include/spartacus/Version.hpp | $(BUILD)
	sed -e 's|@PREFIX@|$(PREFIX)|g' \
	    -e 's|@LIBDIR@|$(PREFIX)/lib|g' \
	    -e 's|@INCLUDEDIR@|$(PREFIX)/include|g' \
	    -e 's|@VERSION@|$(VERSION)|g' $< > $@

install: lib $(PC)
	mkdir -p $(DESTDIR)$(PREFIX)/include/spartacus $(DESTDIR)$(PREFIX)/lib/pkgconfig
	cp include/spartacus/*.hpp $(DESTDIR)$(PREFIX)/include/spartacus/
	cp $(LIB) $(DESTDIR)$(PREFIX)/lib/
	cp $(PC) $(DESTDIR)$(PREFIX)/lib/pkgconfig/

clean:
	rm -rf $(BUILD)

# Compiler-generated header dependencies (rebuild objects when headers change).
-include $(LIB_OBJS:.o=.d)
