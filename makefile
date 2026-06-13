test: test.cpp
	g++ test.cpp -g -lgtest_main -lgtest -lpthread -o test

clean: ;
	rm test

