.PHONY: all

LIBNAME	= $(TOP_DIR)/lib/libpm_client.so
SRCS	= $(wildcard $(CURDIR)/*.c)
OBJS	= $(patsubst %.c,%.o, $(SRCS))
CFLAGS  = $(TOP_CFLAGS) -fPIC
LDFLAGS = $(TOP_LDFLAGS) -ldl

all:$(LIBNAME) $(SRC) $(OBJS)

include $(TOP_DIR)/Makefile.rule
