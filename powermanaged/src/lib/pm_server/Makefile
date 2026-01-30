.PHONY: all

EXE	= $(TOP_DIR)/bin/pm_server.out
SRCS	= $(wildcard $(CURDIR)/*.c)
OBJS	= $(patsubst %.c,%.o, $(SRCS))
CFLAGS  = $(TOP_CFLAGS)
LDFLAGS = $(TOP_LDFLAGS)

all : $(EXE) $(SRC) $(OBJS)

include $(TOP_DIR)/Makefile.rule
