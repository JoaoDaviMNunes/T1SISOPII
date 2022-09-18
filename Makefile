client:
	g++ --std=c++11 -pthread client.cpp -o client

server:
	g++ --std=c++11 -pthread server.cpp -o server