
all: tsh.c tsh.h parse.c parse.h
	gcc $(TCFLAGS) *.c -o tsh
