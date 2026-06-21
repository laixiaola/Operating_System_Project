webserver:
	gcc webserver.c pool.c -o webserver -pthread
clean:
	rm -f webserver webserver.log
	pkill webserver || true
