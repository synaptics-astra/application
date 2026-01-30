.PHONY: all clean

all:init test_send_notice test_listener

TOP_DIR=$(CURDIR)

TOP_CFLAGS = $(E_CFLAG)
TOP_CFLAGS += -Wall -D_PM_DEBUG_ON_ -I $(TOP_DIR)/inc

TOP_LDFLAGS = $(E_LDFLAG)
TOP_LDFLAGS := -L$(CONFIG_SYNA_SDK_OUT_TARGET_PATH)/build_sysroot/lib -lpthread -lrt -L$(TOP_DIR)/lib

clean:
	find $(TOP_DIR) -name \*.o |xargs rm -f
	find $(TOP_DIR) -name \*.d |xargs rm -f
	find $(TOP_DIR) -name \*.so |xargs rm -f
	find $(TOP_DIR) -name \*.out |xargs rm -f

init:
	## TOP_DIR $(TOP_DIR)
	## CC $(CC)
	## TOP_CFLAGS $(TOP_CFLAGS)
	## TOP_LDFLAGS $(TOP_LDFLAGS)

pm_client:
	@$(MAKE) -C $(TOP_DIR)/src/lib/pm_client TOP_DIR=$(TOP_DIR) CC="$(CC)" TOP_CFLAGS="$(TOP_CFLAGS)" TOP_LDFLAGS="$(TOP_LDFLAGS)"

pm_server:
	@$(MAKE) -C $(TOP_DIR)/src/lib/pm_server TOP_DIR=$(TOP_DIR) CC="$(CC)" TOP_CFLAGS="$(TOP_CFLAGS)" TOP_LDFLAGS="$(TOP_LDFLAGS)"

test_send_notice:pm_client pm_server
	@$(MAKE) -C $(TOP_DIR)/src/test/test_send_notice TOP_DIR=$(TOP_DIR) CC="$(CC)" TOP_CFLAGS="$(TOP_CFLAGS)" TOP_LDFLAGS="$(TOP_LDFLAGS)"

test_listener:pm_client pm_server
	@$(MAKE) -C $(TOP_DIR)/src/test/test_listener TOP_DIR=$(TOP_DIR) CC="$(CC)" TOP_CFLAGS="$(TOP_CFLAGS)" TOP_LDFLAGS="$(TOP_LDFLAGS)"
