CC ?= cc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Wpedantic
TEST_BINARIES := tests/test_temperature tests/test_ui
VERSION ?= dev
PLATFORM ?= local
DIST_DIR ?= dist

.PHONY: all clean dist preview preview-omz test test-omz

all: fire

fire: fire.c temperature.c temperature.h ui.c ui.h fireplace_frames.h
	$(CC) $(CPPFLAGS) $(CFLAGS) fire.c temperature.c ui.c $(LDFLAGS) -o $@

preview: fire
	python3 tools/generate_readme_gif.py ./fire assets/fireplace.gif

preview-omz: fire
	FIREC_BIN="$(CURDIR)/fire" python3 tools/generate_readme_gif.py \
		/usr/bin/zsh assets/omz-fireplace.gif \
		--argument=contrib/oh-my-zsh/fireplace.zsh --seconds=4

dist: fire
	python3 tools/package_release.py \
		--binary ./fire \
		--version "$(VERSION)" \
		--platform "$(PLATFORM)" \
		--output-dir "$(DIST_DIR)"

tests/test_temperature: tests/test_temperature.c temperature.c temperature.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -I. tests/test_temperature.c temperature.c $(LDFLAGS) -o $@

tests/test_ui: tests/test_ui.c ui.c ui.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -I. tests/test_ui.c ui.c $(LDFLAGS) -o $@

test: fire $(TEST_BINARIES)
	./tests/test_temperature
	./tests/test_ui
	python3 tests/test_shellsaver.py ./fire
	python3 tests/test_package_release.py

test-omz: fire
	zsh -n contrib/oh-my-zsh/fireplace.zsh tests/test_omz_fireplace.zsh
	zsh tests/test_omz_fireplace.zsh

clean:
	rm -f fire $(TEST_BINARIES)
