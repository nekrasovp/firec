CC ?= cc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Wpedantic
TEST_BINARIES := tests/test_temperature tests/test_ui

.PHONY: all clean preview test

all: fire

fire: fire.c temperature.c temperature.h ui.c ui.h fireplace_frames.h
	$(CC) $(CPPFLAGS) $(CFLAGS) fire.c temperature.c ui.c $(LDFLAGS) -o $@

preview: fire
	python3 tools/generate_readme_gif.py ./fire assets/fireplace.gif

tests/test_temperature: tests/test_temperature.c temperature.c temperature.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -I. tests/test_temperature.c temperature.c $(LDFLAGS) -o $@

tests/test_ui: tests/test_ui.c ui.c ui.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -I. tests/test_ui.c ui.c $(LDFLAGS) -o $@

test: $(TEST_BINARIES)
	./tests/test_temperature
	./tests/test_ui

clean:
	rm -f fire $(TEST_BINARIES)
