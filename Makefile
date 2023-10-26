CC=gcc
CFLAGS=-Wall -Wextra -Werror -Wpedantic
SRC=server.c
OBJ=server
ZIP=project1.zip

all: $(OBJ)

proxy: $(SRC)
	$(CC) $(CLAGS) $< -o $@

clean:
	rm -rf $(OBJ) $(ZIP)

zip: $(ZIP)

$(ZIP): Makefile $(SRC)
	zip $@ $^

.PHONY: all clean zip
