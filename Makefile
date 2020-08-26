CC=gcc
test:
	$(CC) -O2 libuio.c -o test.out -lpthread
	sudo ./test.out

.PHONY: clean
clean:
	rm -vrf *.out *.o