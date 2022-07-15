CC = gcc
DEPS = $(shell pkg-config --cflags libcurl gstreamer-1.0 gstreamer-rtsp-server-1.0) -D_GNU_SOURCE
DEPS_LIBS = $(shell pkg-config --libs libcurl gstreamer-1.0 gstreamer-rtsp-server-1.0)
OPTS = -Wall -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations -Wunused #-Werror #-O2
GDB = -g -ggdb
OBJS = src/rtsp-auth-test.o

all: rtsp-auth-test

%.o: %.c
	$(CC) $(DEPS) -fPIC $(GDB) -c $< -o $@ $(OPTS)

rtsp-auth-test: $(OBJS)
	$(CC) $(GDB) -o rtsp-auth-test $(OBJS) $(DEPS_LIBS)

clean:
	rm -f rtsp-auth-test src/*.o
