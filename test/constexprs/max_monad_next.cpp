#include "../../include/boost/outcome/future.hpp"

using namespace boost::outcome;
extern outcome<int> unknown();
extern BOOST_OUTCOME_NOINLINE outcome<int> test1()
{
  return unknown().next([](outcome<int> m) { return m.get()*3; });
}
extern BOOST_OUTCOME_NOINLINE void test2()
{
}

int main(void)
{
  outcome<int> m(test1());
  test2();
  return 0;
}