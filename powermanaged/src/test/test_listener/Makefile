.PHONY: all

EXE	= $(TOP_DIR)/bin/test_listener.out
SRCS	= $(wildcard $(CURDIR)/*.c)
OBJS	= $(patsubst %.c,%.o, $(SRCS))
CFLAGS  = $(TOP_CFLAGS)
LDFLAGS = $(TOP_LDFLAGS)
LDFLAGS += -lpm_client

all : $(EXE) $(SRC) $(OBJS)

include $(TOP_DIR)/Makefile.rule
