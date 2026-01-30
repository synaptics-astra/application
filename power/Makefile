all: power

power: power.c
		$(CC) $(E_CFLAG) $(E_LDFLAG) -o $(BUILD_DIR)/$@ $^

clean:
		@-rm $(BUILD_DIR)/power

install:
		cp $(BUILD_DIR)/power $(APP_DIR)

.PHONY: clean all install
