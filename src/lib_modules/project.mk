MYDIR=$(call get-my-dir)

LIB_MODULES_SRCS:=\
  $(SRC)/lib_modules/core/allocator.cpp\
  $(SRC)/lib_modules/core/connection.cpp\
  $(SRC)/lib_modules/core/data.cpp\
  $(SRC)/lib_modules/utils/helper.cpp\
  $(SRC)/lib_modules/utils/factory.cpp\
  $(SRC)/lib_modules/utils/loader.cpp\

CFLAGS+=-fPIC

$(BIN)/%.smd: CFLAGS+=$(shell test -z "$(PKGS)" || $(SRC)/../scripts/pkg-config $(PKGS) --cflags)
$(BIN)/%.smd: LDFLAGS+=$(shell test -z "$(PKGS)" || $(SRC)/../scripts/pkg-config $(PKGS) --libs)

$(BIN)/%.smd: $(LIB_MODULES_SRCS:%=$(BIN)/%.o) $(LIB_UTILS_SRCS:%=$(BIN)/%.o)
	$(CXX) $(CFLAGS) -pthread -shared -o "$@" $^ $(LDFLAGS)

