CXX=g++
CXX_FLAGS=-c -g -std=c++20 -fno-exceptions

all: main.o MWLinkerMap.o
	$(CXX) -o test main.o MWLinkerMap.o

main.o: main.cpp
	$(CXX) $(CXX_FLAGS) main.cpp

MWLinkerMap.o: MWLinkerMap.cpp MWLinkerMap.h
	$(CXX) $(CXX_FLAGS) MWLinkerMap.cpp

clean:
	rm test main.o MWLinkerMap.o
