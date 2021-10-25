.DELETE_ON_ERROR:
BOMB_TIME_IN_DAYS?=75.0

CFLAGS:=$(CFLAGS)
CFLAGS+=-std=c++14
#CFLAGS+=-Wall -Wextra -Werror # xxxjack Werror gives av_init_packet deprecation warning which becomes error
CFLAGS+=-Wall -Wextra
CFLAGS+=-fvisibility=hidden -fvisibility-inlines-hidden
CFLAGS+=-DSIGNALS_SET_THREAD_NAME

BIN?=bin
SRC?=src

# always optimize
#CFLAGS+=-O3

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
SIGNALS_HAS_APPS?=1

CFLAGS+=-I$(SRC)

all: targets

PKGS:=

$(BIN)/config.mk: $(SRC)/../scripts/configure
	@echo "Configuring ..."
	@mkdir -p $(BIN)
	$(SRC)/../scripts/version.sh > $(BIN)/signals_version.h
	$(SRC)/../scripts/configure $(PKGS) > "$@"

ifneq ($(MAKECMDGOALS),clean)
include $(BIN)/config.mk
endif

TARGETS:=

define get-my-dir
$(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))
endef

#------------------------------------------------------------------------------

include $(SRC)/lib_utils/project.mk

LIB_PIPELINE_SRCS:=\
  $(SRC)/lib_pipeline/filter.cpp\
  $(SRC)/lib_pipeline/pipeline.cpp

include $(SRC)/lib_modules/project.mk

LIB_APPCOMMON_SRCS:=\
  $(SRC)/lib_appcommon/options.cpp \
  $(SRC)/lib_appcommon/timebomb.cpp 

include $(SRC)/lib_media/project.mk
include $(SRC)/plugins/project.mk

ifeq ($(SIGNALS_HAS_APPS), 1)
  include $(SRC)/apps/dashcastx/project.mk
  include $(SRC)/apps/ts2ip/project.mk
  include $(SRC)/apps/player/project.mk
  include $(SRC)/apps/mp42tsx/project.mk
  include $(SRC)/apps/monitor/project.mk
  include $(SRC)/apps/mcastdump/project.mk
  include $(SRC)/tests/project.mk
endif

ifeq ($(ENABLED_BOMB), 1)
  CFLAGS+=-DENABLE_BOMB -DBOMB_TIME_IN_DAYS=$(BOMB_TIME_IN_DAYS)
endif

#------------------------------------------------------------------------------

$(BIN)/$(SRC)/lib_utils/version.cpp.o: CFLAGS+=-I$(BIN)

#------------------------------------------------------------------------------
# Generic rules

targets: $(TARGETS)

$(BIN)/%.exe:
	@mkdir -p $(dir $@)
	$(CXX) -o "$@" $^ $(LDFLAGS)

$(BIN)/%.cpp.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX)  -c $(CFLAGS) "$<" -o "$@"
	@$(CXX) -c $(CFLAGS) "$<" -o "$@.deps" -MP -MM -MT "$@" # deps generation
	@$(CXX) -c $(CFLAGS) "$<" -E | wc -l > "$@.lines" # keep track of line count

clean:
	rm -rf $(BIN)

-include $(shell test -d $(BIN) && find $(BIN) -name "*.deps")
