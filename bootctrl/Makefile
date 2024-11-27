all: bootctrl

bootctrl: bootctrl.c
		$(CC) $(E_CFLAG) $(E_LDFLAG) -o $(BUILD_DIR)/$@ $^

clean:
		@-rm $(BUILD_DIR)/bootctrl

install:
		cp $(BUILD_DIR)/bootctrl $(APP_DIR)

.PHONY: clean all install
