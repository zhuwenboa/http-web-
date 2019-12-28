web_serve: http_main.o http_conn.o
	g++ -o web_serve http_main.o -lpthread http_conn.o
http_main.o: http_main.cpp   http_conn.h threadpool.h
	g++ -c http_main.cpp
http_conn.o: http_conn.cpp http_conn.h
	g++ -c http_conn.cpp 
