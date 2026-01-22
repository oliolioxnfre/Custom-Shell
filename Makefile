TARGETS = mysh greet
CC		= gcc
CFLAGS  = -g -std=c99 -Wall -Wvla -Werror -fsanitize=address,undefined

all: $(TARGETS)

mysh: mysh.c
	$(CC) $(CFLAGS) -o mysh mysh.c

greet: greet.c
	$(CC) $(CFLAGS) -o greet greet.c

clean:
	rm -rf $(TARGETS) *.o *.a *.dylib *.dSYM