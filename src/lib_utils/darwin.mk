LDFLAGS+=-ldl
LDFLAGS+=-L/usr/local/opt/libjpeg-turbo/lib

LIB_UTILS_SRCS+=\
  $(MYDIR)/os_darwin.cpp
