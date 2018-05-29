.DELETE_ON_ERROR:

CFLAGS:=$(CFLAGS)
CFLAGS+=-std=c++14
CFLAGS+=-Wall
CFLAGS+=-Wextra
CFLAGS+=-Werror
CFLAGS+=-fvisibility=hidden -fvisibility-inlines-hidden
CFLAGS+=-D__STDC_CONSTANT_MACROS

BIN?=bin
SRC?=src

# always optimize
CFLAGS+=-O3

# default to: no debug info, full warnings
DEBUG?=2

ifeq ($(DEBUG), 1)
  CFLAGS+=-g3
  LDFLAGS+=-g
endif

ifeq ($(DEBUG), 0)
  # disable all warnings in release mode:
  # the code must always build, especially old versions with recent compilers
  CFLAGS+=-w -DNDEBUG
  LDFLAGS+=-Xlinker -s
endif

SIGNALS_HAS_X11?=1

CFLAGS+=-I$(SRC)

all: targets

PKGS:=\
  gpac\
  libavcodec\
  libavdevice\
  libavfilter\
  libavformat\
  libavutil\
  libcurl\
  libswresample\
  libswscale\
  libturbojpeg\
  x264\

$(BIN)/config.mk: $(SRC)/../scripts/configure
	@echo "Configuring ..."
	@mkdir -p $(BIN)
	$(SRC)/../scripts/configure $(PKGS) > "$@"

ifneq ($(MAKECMDGOALS),clean)
include $(BIN)/config.mk
endif

CFLAGS+=-Umain

TARGETS:=

define get-my-dir
$(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))
endef

include $(SRC)/lib_utils/project.mk
include $(SRC)/lib_media/project.mk

#------------------------------------------------------------------------------

LIB_PIPELINE_SRCS:=\
  $(SRC)/lib_pipeline/pipeline.cpp

#------------------------------------------------------------------------------

LIB_MODULES_SRCS:=\
  $(SRC)/lib_modules/core/connection.cpp\
  $(SRC)/lib_modules/core/data.cpp

#------------------------------------------------------------------------------

LIB_APPCOMMON_SRCS:=\
  $(SRC)/lib_appcommon/options.cpp \

#------------------------------------------------------------------------------

include $(SRC)/tests/project.mk

#------------------------------------------------------------------------------

include $(SRC)/apps/dashcastx/project.mk

#------------------------------------------------------------------------------

ifeq ($(SIGNALS_HAS_X11), 1)
include $(SRC)/apps/player/project.mk
endif

#------------------------------------------------------------------------------

include $(SRC)/apps/mp42tsx/project.mk

#------------------------------------------------------------------------------

TAG:=$(shell echo `git describe --tags --abbrev=0 2> /dev/null || echo "UNKNOWN"`)
VERSION:=$(shell echo `git describe --tags --long 2> /dev/null || echo "UNKNOWN"` | sed "s/^$(TAG)-//")
BRANCH:=$(shell git rev-parse --abbrev-ref HEAD 2> /dev/null || echo "UNKNOWN")

VER_NEW:=$(TAG)-$(BRANCH)-rev$(VERSION)

$(BIN)/src/lib_utils/version.cpp.o: CFLAGS+=-DVERSION="\"$(VER_NEW)\""

targets: $(TARGETS)

#------------------------------------------------------------------------------

$(BIN)/%.exe:
	@mkdir -p $(dir $@)
	$(CXX) -o "$@" $^ $(LDFLAGS)

$(BIN)/%.cpp.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX)  -c $(CFLAGS) "$<" -o "$@"
	@$(CXX) -c $(CFLAGS) "$<" -o "$@.deps" -MP -MM -MT "$@"

clean:
	rm -rf $(BIN)

#-------------------------------------------------------------------------------

-include $(shell test -d $(BIN) && find $(BIN) -name "*.deps")
