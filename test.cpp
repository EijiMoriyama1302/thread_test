//test.cpp
#include <gtest/gtest.h>
int sum(int a, int b){
        return a + b;
}
TEST(TestCase, sum){
        EXPECT_EQ(2,sum(1,1));
}

