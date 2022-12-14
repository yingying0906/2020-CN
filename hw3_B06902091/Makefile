all: agent server receiver

agent: agent.c
	gcc -o agent agent.c
server: server.cpp
	g++ -o server server.cpp `pkg-config opencv --cflags --libs`

receiver: receiver.cpp
	g++ -o receiver receiver.cpp `pkg-config opencv --cflags --libs`

clean:
	rm server receiver agent
