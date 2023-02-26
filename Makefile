CXX=g++
CXX_FLAGS=-c -g -O3 -std=c++20 -fno-exceptions

all: main.o MWLinkerMap.o MWLinkerMap2.o
	$(CXX) -o test main.o MWLinkerMap.o MWLinkerMap2.o

main.o: main.cpp
	$(CXX) $(CXX_FLAGS) main.cpp

MWLinkerMap.o: MWLinkerMap.cpp MWLinkerMap.h
	$(CXX) $(CXX_FLAGS) MWLinkerMap.cpp

MWLinkerMap2.o: MWLinkerMap2.cpp MWLinkerMap2.h
	$(CXX) $(CXX_FLAGS) MWLinkerMap2.cpp

clean:
	rm test main.o MWLinkerMap.o MWLinkerMap2.o
