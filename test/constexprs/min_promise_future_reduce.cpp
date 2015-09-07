#define BOOST_OUTCOME_FUTURE_ENABLE_CONSTEXPR_LOCK_FOLDING

#include "../../include/boost/outcome/future.hpp"

extern BOOST_OUTCOME_NOINLINE int test1()
{
  using namespace boost::outcome::lightweight_futures;
  promise<int> p;
  p.set_value(5);
  future<int> f(p.get_future());
  return f.get();
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