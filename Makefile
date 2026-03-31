CC ?= cc
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    BREW_PREFIX := $(shell brew --prefix 2>/dev/null)
    CFLAGS ?= -O2 -Wall -Wextra -std=c11 -I$(BREW_PREFIX)/include
    LDFLAGS ?= -L$(BREW_PREFIX)/lib -Wl,-rpath,$(BREW_PREFIX)/lib
    LIBS := -lglfw -lGLEW -framework OpenGL -lm
    VULKAN_LIBS := -lglfw -lvulkan -lm
else
    CFLAGS ?= -O2 -Wall -Wextra -std=c11
    LDFLAGS ?=
    LIBS := $(shell pkg-config --libs glfw3 glew opengl) -lm
    VULKAN_LIBS := $(shell pkg-config --libs glfw3 vulkan) -lm
endif
GLSLC ?= glslc

TARGET := solar_system_modern
SRC := solar_system_modern.c
VULKAN_TARGET := solar_system_vulkan
VULKAN_SRC := solar_system_vulkan.c
VULKAN_SHADERS := \
	shaders/vulkan_planet.vert.spv \
	shaders/vulkan_planet.frag.spv \
	shaders/vulkan_orbit.vert.spv \
	shaders/vulkan_orbit.frag.spv \
	shaders/vulkan_ring.vert.spv \
	shaders/vulkan_ring.frag.spv \
	shaders/vulkan_star.vert.spv \
	shaders/vulkan_star.frag.spv

.PHONY: all clean

all: $(TARGET) $(VULKAN_TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) $(LIBS)

$(VULKAN_TARGET): $(VULKAN_SRC) $(VULKAN_SHADERS)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) $(VULKAN_LIBS)

shaders/%.spv: shaders/%
	$(GLSLC) $< -o $@

clean:
	rm -f $(TARGET) $(VULKAN_TARGET) $(VULKAN_SHADERS)
