test: test.cpp
	g++ test.cpp -lgtest_main -lgtest -lpthread -o test

clean: ;
	rm test

