CFLAGS = -Wall -Wall -Wextra -pedantic -std=c11
CC = gcc
LDFLAGS = -I/usr/include/freetype2 -lXrandr -lX11 -lXft -lc

main:
	$(CC) $(CFLAGS) -o tinybar tinybar.c $(LDFLAGS)

clean:
	rm tinybar
