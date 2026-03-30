CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
LDFLAGS ?=
LIBS := $(shell pkg-config --libs glfw3 glew opengl) -lm

TARGET := solar_system_modern
SRC := solar_system_modern.c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) $(LIBS)

clean:
	rm -f $(TARGET)
