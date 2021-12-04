c2c: c2c-busyloop.cpp
	g++ -g -fno-omit-frame-pointer -O3 $^ -lpthread -o $@
