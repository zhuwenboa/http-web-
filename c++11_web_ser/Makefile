object = http_main.o http_conn.o AsyncLog.o heap_time.o

web_serve: $(object)
	g++ -o web_serve $(object) -lpthread

http_main.o: http_conn.h threadpool.h AsyncLog.h
http_conn.o: http_conn.h AsyncLog.h Mutexlock.h
AsyncLog.o: AsyncLog.h Mutexlock.h
heap_time.o: heap_time.h 

.PHONY: clean #.PHONY 表明clean是个伪目标文件
clean: 
	- rm *.o
