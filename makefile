all: vblkdev csefsck

vblkdev: vblkdev.c
	gcc -g -Wall vblkdev.c `pkg-config fuse --cflags --libs` -o vblkdev
csefsck: csefsck.c
	gcc -g -o csefsck -Wall -std=c99 csefsck.c

