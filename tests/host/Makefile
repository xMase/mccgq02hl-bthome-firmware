# Copyright (C) 2026 xMase
# SPDX-License-Identifier: GPL-3.0-only
# Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.

# Host unit tests. Run in the distrobox (gcc, make, mbedtls-devel):
#   distrobox enter efr32 -- make -C tests/host test
# The PSA Crypto API comes from Fedora's mbedTLS 3.6: the SDK's copy needs Silicon Labs drivers.
ROOT := $(abspath ../..)
B := build
PYTHON ?= $(ROOT)/.tmp/venv/bin/python
CFLAGS := -std=c11 -Wall -Wextra -Werror -O1 -I$(ROOT)/firmware
TESTS := $(patsubst %.c,$(B)/%,$(wildcard test_*.c))

.PHONY: test clean
test: $(TESTS)
	@for t in $(TESTS); do ./$$t > $$t.out || { echo "FAIL $$t"; exit 1; }; echo "PASS $$t"; done
	$(PYTHON) -I check_bthome.py $(B)/test_bthome.out

# Each test_<name>.c tests firmware/<name>.c.
$(B)/test_%: test_%.c $(ROOT)/firmware/%.c check.h
	@mkdir -p $(B)
	$(CC) $(CFLAGS) -o $@ $< $(ROOT)/firmware/$*.c -lmbedcrypto

# test_app.c runs firmware/app.c on the fake SDK (sdk/, fake_sdk.c) with the pure modules and the real
# PSA crypto; hw.c is replaced by the fakes. This rule wins over the pattern rule above.
APP_SRC := $(filter-out %/hw.c,$(wildcard $(ROOT)/firmware/*.c))
$(B)/test_app: test_app.c fake_sdk.c $(wildcard sdk/*.h) $(APP_SRC) check.h
	@mkdir -p $(B)
	$(CC) $(CFLAGS) -Isdk -o $@ test_app.c fake_sdk.c $(APP_SRC) -lmbedcrypto

clean:
	rm -rf $(B)
