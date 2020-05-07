CFLAGS   := -Wall -Og -g3 $(shell sdl2-config --cflags)
LDLIBS   := $(shell sdl2-config --libs)

all: sdlarch
sdlarch: sdlarch.o glad.o
