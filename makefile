all: clean test
	echo build

test: test.cpp
	g++ test.cpp my_api.cpp -g -lgtest_main -lgtest -lpthread -o test

clean: ;
	-rm test

