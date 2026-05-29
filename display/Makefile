PKG_CONFIG ?= pkg-config
BUILD_DIR ?= .

all: gamma_set

gamma_set: gamma_set.c
	$(CC) $(E_CFLAG) $(CFLAGS) $(shell $(PKG_CONFIG) --cflags libdrm) -o $(BUILD_DIR)/$@ $^ $(E_LDFLAG) $(LDFLAGS) $(shell $(PKG_CONFIG) --libs libdrm) -lm

clean:
	@-rm -f $(BUILD_DIR)/gamma_set

install:
	cp $(BUILD_DIR)/gamma_set $(APP_DIR)

.PHONY: clean all install
