all:
	if [ ! -e ./bin ]; then mkdir ./bin; fi
	gcc -o ./bin/test cdecl.c test.c

clean:
	if [ -e ./bin ]; then rm -rf ./bin/*; fi

