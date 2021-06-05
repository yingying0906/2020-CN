all: server client

server: server.cpp
	g++ -o server server.cpp `pkg-config opencv --cflags --libs`

client: client.cpp
	g++ -o client client.cpp `pkg-config opencv --cflags --libs`

clean:
	rm server client
