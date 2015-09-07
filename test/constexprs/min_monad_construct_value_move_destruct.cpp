#include "../../include/boost/outcome/future.hpp"

extern BOOST_OUTCOME_NOINLINE int test1()
{
  using namespace boost::outcome;
  outcome<int> m1(5);
  outcome<int> m2(std::move(m1));
  return std::move(m2).get();
}
extern BOOST_OUTCOME_NOINLINE void test2()
{
}

int main(void)
{
  int ret=0;
  if(5!=test1()) ret=1;
  test2();
  return ret;
}