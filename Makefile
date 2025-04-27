CC = gcc
CFLAGS =  -std=gnu99 -Wall -Wextra -Werror -pedantic

OBJ = proc_sync

all: $(OBJ)
#######################################################

%.o: %.c
	$(CC) $(CFLAGS) -c $^ -o $@

$(OBJ): $(OBJ).o
	$(CC) $^ -o $@ -lrt -pthread

#######################################################

clean:
	rm $(OBJ) $(OBJ).o