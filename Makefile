CC ?= cc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Wpedantic

.PHONY: all clean preview

all: fire

fire: fire.c fireplace_frames.h
	$(CC) $(CPPFLAGS) $(CFLAGS) fire.c $(LDFLAGS) -o $@

preview: fire
	python3 tools/generate_readme_gif.py ./fire assets/fireplace.gif

clean:
	rm -f fire
