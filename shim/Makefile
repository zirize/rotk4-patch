CC = i686-w64-mingw32-gcc
CFLAGS = -O2 -Wall -Wextra -std=c99
LDFLAGS = -shared -static-libgcc -Wl,--enable-stdcall-fixup -s

SRCS = ddraw.c cdaudio.c movie.c mp3.c palette.c scale.c util.c winpos.c

ddraw.dll: $(SRCS) shim.h ddraw.def minimp3/minimp3.h minimp3/minimp3_ex.h
	$(CC) $(CFLAGS) -o $@ $(SRCS) ddraw.def $(LDFLAGS) -luser32 -lwinmm -lavifil32

clean:
	rm -f ddraw.dll

.PHONY: clean
