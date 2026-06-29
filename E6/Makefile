lru:
	gcc -DLRUA webserver.c pool.c Hash.c Cache.c LRU.c -o webserver -pthread
lfu:
	gcc -DLFUA webserver.c pool.c Hash.c Cache.c LFU.c -o webserver -pthread
clean:
	rm -f webserver ./htmls/webserver.log webserver.log
	pkill webserver || true
