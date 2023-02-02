CFLAGS = -g3 -Wall -Wextra -Wconversion -Wcast-qual -Wcast-align -g
CFLAGS += -Winline -Wfloat-equal -Wnested-externs
CFLAGS += -pedantic -std=gnu99
CFLAGS += -std=gnu99 -D_GNU_SOURCE

PROMPT = -DPROMPT
CC = gcc
CP = /bin/cp
EXECS = 33sh 33noprompt

.PHONY: all clean

all: $(EXECS)

33sh: sh.c jobs.c
	$(CC) $(CFLAGS) -DPROMPT $^ -o 33sh

33noprompt:  sh.c jobs.c
	$(CC) $(CFLAGS) $^ -o 33noprompt

clean:
	rm -f $(EXECS)