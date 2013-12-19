
SRC:=server.c client.c

.PHONY: clean

all: $(patsubst %.c, out/%, $(SRC))

out/%: out $(SRC)
	$(CC) $(patsubst out/%, %.c, $@) -o $@ -g

out:
	mkdir -p out

clean:
	rm -f out/*
