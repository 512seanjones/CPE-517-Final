snoop: main.cpp
	g++ -g -O2 -pthread -std=c++11 $^ -o $@

.PHONY: clean
clean:
	rm -rf *.o *~ snoop
