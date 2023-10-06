CC=gcc
SRC=server.c
OBJ=server
ZIP=project1.zip

all: $(OBJ)

proxy: $(SRC)
	$(CC) $< -o $@

clean:
	rm -rf $(OBJ) $(ZIP)

zip: $(ZIP)

$(ZIP): Makefile $(SRC)
	zip $@ $^

.PHONY: all clean zip
