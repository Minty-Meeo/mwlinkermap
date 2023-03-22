CXX=clang++ -stdlib=libc++ -fexperimental-library
CXX_FLAGS=-c -g -O3 -std=c++20 -fno-exceptions -Wall -Wextra -Weverything -pedantic -Wno-c++98-compat -Wno-c++98-compat-pedantic -Wno-global-constructors -Wno-exit-time-destructors -Wno-switch-enum -Wno-weak-vtables -Wno-padded

all: main.o MWLinkerMap.o
	$(CXX) -o test main.o MWLinkerMap.o

main.o: main.cpp MWLinkerMap.h
	$(CXX) $(CXX_FLAGS) main.cpp

MWLinkerMap.o: MWLinkerMap.cpp MWLinkerMap.h
	$(CXX) $(CXX_FLAGS) MWLinkerMap.cpp

clean:
	rm test main.o MWLinkerMap.o
