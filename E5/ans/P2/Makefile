webserver:
	gcc webserver.c pool.c -o webserver -pthread
clean:
	rm -f webserver ./htmls/webserver.log webserver.log
	pkill webserver || true
