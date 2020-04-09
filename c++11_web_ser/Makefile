web_serve: http_main.o http_conn.o AsyncLog.o heap_time.o
	g++ -o web_serve http_main.o http_conn.o AsyncLog.o heap_time.o -lpthread
http_main.o: http_main.cpp http_conn.h threadpool.h AsyncLog.h
	g++ -c http_main.cpp
http_conn.o: http_conn.cpp http_conn.h AsyncLog.h Mutexlock.h
	g++ -c http_conn.cpp 
AsyncLog.o: AsyncLog.cpp AsyncLog.h Mutexlock.h
	g++ -c AsyncLog.cpp
heap_time.o: heap_time.cpp heap_time.h 
	g++ -c heap_time.cpp

clean: 
	rm http_conn.o http_main.o AsyncLog.o heap_time.o
