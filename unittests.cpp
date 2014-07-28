/* unittests.cpp
Unit testing for memory transactions
(C) 2013-2014 Niall Douglas http://www.nedproductions.biz/


Boost Software License - Version 1.0 - August 17th, 2003

Permission is hereby granted, free of charge, to any person or organization
obtaining a copy of the software and accompanying documentation covered by
this license (the "Software") to use, reproduce, display, distribute,
execute, and transmit the Software, and to prepare derivative works of the
Software, and to permit third-parties to whom the Software is furnished to
do so, all subject to the following:

The copyright notices in the Software and this entire statement, including
the above license grant, this restriction and the following disclaimer,
must be included in all copies of the Software, in whole or in part, and
all derivative works of the Software, unless such copies or derivative
works are solely in the form of machine-executable object code generated by
a source language processor.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
*/

#include "spinlock.hpp"
#include "timing.h"

#include <stdio.h>
#include <unordered_map>
#include <vector>

#ifdef _MSC_VER
//#define BOOST_HAVE_SYSTEM_CONCURRENT_UNORDERED_MAP
#endif

#ifdef BOOST_HAVE_SYSTEM_CONCURRENT_UNORDERED_MAP
#include <concurrent_unordered_map.h>
#endif

#ifndef BOOST_MEMORY_TRANSACTIONS_DISABLE_CATCH
#define CATCH_CONFIG_RUNNER
#include "catch.hpp"
#endif

#include <mutex>

namespace boost { namespace spinlock {
  /* \class concurrent_unordered_map
  \brief Provides an unordered_map which is thread safe and wait free to use and whose find, insert/emplace and erase functions are usually wait free.
   
  */
  template<class Key, class T, class Hash=std::hash<Key>, class Pred=std::equal_to<Key>, class Alloc=std::allocator<std::pair<const Key, T>>> class concurrent_unordered_map
  {
  public:
    typedef Key key_type;
    typedef T mapped_type;
    typedef std::pair<const key_type, mapped_type> value_type;
    typedef Hash hasher;
    typedef Pred key_equal;
    typedef Alloc allocator_type;
    
    typedef value_type& reference;
    typedef const value_type& const_reference;
    typedef value_type* pointer;
    typedef const value_type *const_pointer;
    typedef std::size_t size_type;
    typedef std::ptrdiff_t difference_type;
  private:
    spinlock<bool> _rehash_lock;
    struct non_zero_hasher
    {
      hasher _hasher;
      size_t operator()(const key_type &k) const BOOST_NOEXCEPT
      {
        size_t ret=_hasher(k);
        return ret==0 ? (size_t) -1 : ret;
      }
    } _hasher;
    key_equal _key_equal;
    struct item_type
    {
      std::pair<key_type, mapped_type> p;
      size_t hash;  // bit 0 always 1 if this item is in use
      item_type() : hash(0) { }
      item_type(value_type &&_p, size_t _hash) BOOST_NOEXCEPT : p(std::move(_p)), hash(_hash) { }
      item_type(item_type &&o) BOOST_NOEXCEPT : p(std::move(o.p)), hash(o.hash) { o.hash=0; }
    };
    typedef typename allocator_type::template rebind<item_type>::other item_type_allocator_type;
    struct bucket_type
    {
      spinlock<unsigned char> lock;  // = 2 if you need to reload the bucket list
      atomic<unsigned> count; // count is used items in there
      std::vector<item_type, item_type_allocator_type> items;
      //char pad[64-sizeof(spinlock<unsigned char>)-sizeof(atomic<unsigned>)-sizeof(std::vector<item_type, item_type_allocator_type>)];
      bucket_type() : count(0), items(0) { }
      bucket_type(bucket_type &&) BOOST_NOEXCEPT : count(0) { }
    };
    std::vector<bucket_type> _buckets;
    typename std::vector<bucket_type>::iterator _get_bucket(size_t k) BOOST_NOEXCEPT
    {
      //static_assert(sizeof(bucket_type)==64, "bucket_type is not 64 bytes long!");
      //k ^= k + 0x9e3779b9 + (k<<6) + (k>>2); // really need to avoid sequential keys tapping the same cache line
      //k ^= k + 0x9e3779b9; // really need to avoid sequential keys tapping the same cache line
      size_type i=k % _buckets.size();
      return _buckets.begin()+i;
    }
  public:
    class iterator : public std::iterator<std::forward_iterator_tag, value_type, difference_type, pointer, reference>
    {
      concurrent_unordered_map *_parent;
      void *_bucket_data; // used for sanity check that he hasn't rehashed
      typename std::vector<bucket_type>::iterator _itb;
      size_t _offset, _pending_incr; // used to avoid erase() doing a costly increment unless necessary
      friend class concurrent_unordered_map;
      iterator(concurrent_unordered_map *parent) : _parent(parent), _bucket_data(parent->_buckets.data()), _itb(parent->_buckets.begin()), _offset((size_t) -1), _pending_incr(1) { }
      iterator(concurrent_unordered_map *parent, std::nullptr_t) : _parent(parent), _bucket_data(parent->_buckets.data()), _itb(parent->_buckets.end()), _offset((size_t) -1), _pending_incr(0) { }
      void catch_up()
      {
        while(_pending_incr && _itb!=_parent->_buckets.end())
        {
          assert(_bucket_data==_parent->_buckets.data());
          if(_bucket_data!=_parent->_buckets.data())
            abort(); // stale iterator
          bucket_type &b=*_itb;
          BOOST_BEGIN_TRANSACT_LOCK_ONLY_IF_NOT(b.lock, 2)
          {
            for(_offset++; _offset<b.items.size(); _offset++)
              if(b.items[_offset].hash)
                if(!(--_pending_incr)) break;
            if(_pending_incr)
            {
              while(_offset>=b.items.size() && _itb!=_parent->_buckets.end())
              {
                ++_itb;
                _offset=(size_t)-1;
              }
            }
          }
          BOOST_END_TRANSACT_LOCK(b.lock)
        }
      }
    public:
      iterator() : _parent(nullptr), _bucket_data(nullptr), _offset((size_t) -1), _pending_incr(0) { }
      bool operator!=(const iterator &o) const BOOST_NOEXCEPT { return _itb!=o._itb || _offset!=o._offset || _pending_incr|=o._pending_incr; }
      bool operator==(const iterator &o) const BOOST_NOEXCEPT { return _itb==o._itb && _offset==o._offset && _pending_incr==o._pending_incr; }
      iterator &operator++()
      {
        if(_itb==_parent->_buckets.end())
          return *this;
        ++_pending_incr;
        return *this;
      }
      iterator operator++(int) { iterator t(*this); operator++(); return t; }
      value_type &operator*() { catch_up(); assert(_itb!=_parent->_buckets.end()); if(_itb==_parent->_buckets.end()) abort(); return _itb->items[_offset]->p.get(); }
    };
  public:
    // local_iterator
    // const_local_iterator
    concurrent_unordered_map() : _buckets(13) { }
    concurrent_unordered_map(size_t n) : _buckets(n>0 ? n : 1) { }
    ~concurrent_unordered_map() { clear(); }
    concurrent_unordered_map(const concurrent_unordered_map &);
    concurrent_unordered_map(concurrent_unordered_map &&);
    concurrent_unordered_map &operator=(const concurrent_unordered_map &);
    concurrent_unordered_map &operator=(concurrent_unordered_map &&);
    //bool empty() const BOOST_NOEXCEPT;
    //size_type size() const BOOST_NOEXCEPT;
    //iterator begin() BOOST_NOEXCEPT
    //const_iterator begin() const BOOST_NOEXCEPT
    iterator end() BOOST_NOEXCEPT
    {
      return iterator(this, nullptr);
    }
    //const_iterator end() const BOOST_NOEXCEPT
    iterator find(const key_type &k)
    {
      iterator ret=end();
      size_t h=_hasher(k);
      bool done=false;
      do
      {
        auto itb=_get_bucket(h);
        bucket_type &b=*itb;
        size_t offset=0;
        if(b.count.load(memory_order_acquire))
        {
          // Should run completely concurrently with other finds
          BOOST_BEGIN_TRANSACT_LOCK_ONLY_IF_NOT(b.lock, 2)
          {
            for(;;)
            {
              for(; offset<b.items.size() && b.items[offset].hash!=h; offset++);
              if(offset==b.items.size())
                break;
              if(_key_equal(k, b.items[offset].p.first))
              {
                ret._itb=itb;
                ret._offset=offset;
                done=true;
                break;
              }
              else offset++;
            }
            done=true;
          }
          BOOST_END_TRANSACT_LOCK(b.lock)
        }
      } while(!done);
      return ret;
    }
    //const_iterator find(const keytype &k) const;
    template<class P> std::pair<iterator, bool> insert(P &&v)
    {
      std::pair<iterator, bool> ret(end(), true);
      size_t h=_hasher(v.first);
      bool done=false;
      do
      {
        auto itb=_get_bucket(h);
        bucket_type &b=*itb;
        size_t emptyidx=(size_t) -1;
        // First search for equivalents and empties. Start from the top to avoid cache line sharing with find
        if(b.count.load(memory_order_acquire))
        {
          BOOST_BEGIN_TRANSACT_LOCK_ONLY_IF_NOT(b.lock, 2)
          for(size_t offset=b.items.size()-1;;)
          {
            for(; offset!=(size_t)-1 && b.items[offset].hash!=h; offset--)
              if(emptyidx==(size_t) -1 && !b.items[offset].hash)
                emptyidx=offset;
            if(offset==(size_t)-1)
              break;
            if(_key_equal(v.first, b.items[offset].p.first))
            {
              ret.first._itb=itb;
              ret.first._offset=offset;
              ret.second=false;
              done=true;
              break;
            }
            else offset--;
          }
          BOOST_END_TRANSACT_LOCK(b.lock)
        }
        else if(!b.items.empty())
          emptyidx=0;
        
        if(!done)
        {
          // If the lock is other state we need to reload bucket list
          if(!b.lock.lock(2))
            continue;
          std::lock_guard<decltype(b.lock)> g(b.lock, std::adopt_lock); // Will abort all concurrency
          // If we earlier found an empty use that
          if(emptyidx!=(size_t) -1)
          {
            if(emptyidx<b.items.size() && !b.items[emptyidx].hash)
            {
              ret.first._itb=itb;
              ret.first._offset=emptyidx;
              b.items[emptyidx].p=std::forward<P>(v);
              b.items[emptyidx].hash=h;
              b.count.fetch_add(1, memory_order_acquire);
              done=true;
            }
          }
          else
          {
            if(b.items.size()==b.items.capacity())
            {
              size_t newcapacity=b.items.capacity()*2;
              b.items.reserve(newcapacity ? newcapacity : 1);
            }
            ret.first._itb=itb;
            ret.first._offset=b.items.size();
            b.items.push_back(item_type(std::forward<P>(v), h));
            b.count.fetch_add(1, memory_order_acquire);
            done=true;
          }
        }
      } while(!done);
      return ret;
    }
    iterator erase(/*const_*/iterator it)
    {
      iterator ret(end());
      //assert(it!=end());
      if(it==ret) return ret;
      bucket_type &b=*it._itb;
      std::pair<const key_type, mapped_type> former;
      {
        // If the lock is other state we need to reload bucket list
        if(!b.lock.lock(2))
          abort();
        std::lock_guard<decltype(b.lock)> g(b.lock, std::adopt_lock); // Will abort all concurrency
        if(it._offset<b.items.size() && b.items[it._offset].hash)
        {
          former=std::move(b.items[it._offset].p); // Move into former, hopefully avoiding a free()
          b.items[it._offset].hash=0;
          if(it._offset==b.items.size()-1)
          {
            // Shrink table to minimum
            while(!b.items.empty() && !b.items.back().hash)
              b.items.pop_back(); // Will abort all concurrency
          }
          b.count.fetch_sub(1, memory_order_acquire);
          ret=it;
          ++ret;
        }
      }
      return ret;
    }
    size_type erase(const key_type &k)
    {
      size_type ret=0;
      size_t h=_hasher(k);
      bool done=false;
      std::pair<key_type, mapped_type> former;
      do
      {
        auto itb=_get_bucket(h);
        bucket_type &b=*itb;
        size_t offset=0;
        if(b.count.load(memory_order_acquire))
        {
          // If the lock is other state we need to reload bucket list
          if(!b.lock.lock(2))
            continue;
          std::lock_guard<decltype(b.lock)> g(b.lock, std::adopt_lock); // Will abort all concurrency
          {
            for(;;)
            {
              for(; offset<b.items.size() && b.items[offset].hash!=h; offset++);
              if(offset==b.items.size())
                break;
              if(_key_equal(k, b.items[offset].p.first))
              {
                former=std::move(b.items[offset].p); // Move into former, hopefully avoiding a free()
                b.items[offset].hash=0;
                if(offset==b.items.size()-1)
                {
                  // Shrink table to minimum
                  while(!b.items.empty() && !b.items.back().hash)
                    b.items.pop_back(); // Will abort all concurrency
                }
                b.count.fetch_sub(1, memory_order_acquire);
                ++ret;
                done=true;
                break;
              }
              else offset++;
            }
            done=true;
          }
        }
      } while(!done);
      return ret;
    }
    void clear() BOOST_NOEXCEPT
    {
      bool done;
      do
      {
        done=true;
        for(auto &b : _buckets)
        {
          // If the lock is other state we need to reload bucket list
          if(!b.lock.lock(2))
          {
            done=false;
            break;
          }
          std::lock_guard<decltype(b.lock)> g(b.lock, std::adopt_lock); // Will abort all concurrency
          b.items.clear();
        }
      } while(!done);
    }
    void reserve(size_type n)
    {
      _buckets.resize(n);
    }
    void dump_buckets(std::ostream &s) const
    {
      for(size_t n=0; n<_buckets.size(); n++)
      {
        s << "Bucket " << n << ": size=" << _buckets[n].items.size() << " count=" << _buckets[n].count << std::endl;
      }
    }
  };
} }

using namespace std;

TEST_CASE("spinlock/works", "Tests that the spinlock works as intended")
{
  boost::spinlock::spinlock<bool> lock;
  REQUIRE(lock.try_lock());
  REQUIRE(!lock.try_lock());
  lock.unlock();
  
  lock_guard<decltype(lock)> h(lock);
  REQUIRE(!lock.try_lock());
}

TEST_CASE("spinlock/works_threaded", "Tests that the spinlock works as intended under threads")
{
  boost::spinlock::spinlock<bool> lock;
  boost::spinlock::atomic<size_t> gate(0);
#pragma omp parallel
  {
    ++gate;
  }
  size_t threads=gate;
  for(size_t i=0; i<1000; i++)
  {
    gate.store(threads);
    size_t locked=0;
#pragma omp parallel for reduction(+:locked)
    for(int n=0; n<threads; n++)
    {
      --gate;
      while(gate);
      locked+=lock.try_lock();
    }
    REQUIRE(locked==1);
    lock.unlock();
  }
}

TEST_CASE("spinlock/works_transacted", "Tests that the spinlock works as intended under transactions")
{
  boost::spinlock::spinlock<bool> lock;
  boost::spinlock::atomic<size_t> gate(0);
  size_t locked=0;
#pragma omp parallel
  {
    ++gate;
  }
  size_t threads=gate;
#pragma omp parallel for
  for(int i=0; i<1000*threads; i++)
  {
    BOOST_BEGIN_TRANSACT_LOCK(lock)
    {
      ++locked;
    }
    BOOST_END_TRANSACT_LOCK(lock)
  }
  REQUIRE(locked==1000*threads);
}

static double CalculatePerformance(bool use_transact)
{
  boost::spinlock::spinlock<bool> lock;
  boost::spinlock::atomic<size_t> gate(0);
  struct
  {
    size_t value;
    char padding[64-sizeof(size_t)];
  } count[64];
  memset(&count, 0, sizeof(count));
  usCount start, end;
#pragma omp parallel
  {
    ++gate;
  }
  size_t threads=gate;
  //printf("There are %u threads in this CPU\n", (unsigned) threads);
  start=GetUsCount();
#pragma omp parallel for
  for(int thread=0; thread<threads; thread++)
  {
    --gate;
    while(gate);
    for(size_t n=0; n<10000000; n++)
    {
      if(use_transact)
      {
        BOOST_BEGIN_TRANSACT_LOCK(lock)
        {
          ++count[thread].value;
        }
        BOOST_END_TRANSACT_LOCK(lock)
      }
      else
      {
        std::lock_guard<decltype(lock)> g(lock);
        ++count[thread].value;      
      }
    }
  }
  end=GetUsCount();
  size_t increments=0;
  for(size_t thread=0; thread<threads; thread++)
  {
    REQUIRE(count[thread].value == 10000000);
    increments+=count[thread].value;
  }
  return increments/((end-start)/1000000000000.0);
}

TEST_CASE("performance/spinlock", "Tests the performance of spinlocks")
{
  printf("\n=== Spinlock performance ===\n");
  printf("1. Achieved %lf transactions per second\n", CalculatePerformance(false));
  printf("2. Achieved %lf transactions per second\n", CalculatePerformance(false));
  printf("3. Achieved %lf transactions per second\n", CalculatePerformance(false));
}

TEST_CASE("performance/transaction", "Tests the performance of spinlock transactions")
{
  printf("\n=== Transacted spinlock performance ===\n");
  printf("1. Achieved %lf transactions per second\n", CalculatePerformance(true));
  printf("2. Achieved %lf transactions per second\n", CalculatePerformance(true));
  printf("3. Achieved %lf transactions per second\n", CalculatePerformance(true));
}

static double CalculateMallocPerformance(size_t size, bool use_transact)
{
  boost::spinlock::spinlock<bool> lock;
  boost::spinlock::atomic<size_t> gate(0);
  usCount start, end;
#pragma omp parallel
  {
    ++gate;
  }
  size_t threads=gate;
  //printf("There are %u threads in this CPU\n", (unsigned) threads);
  start=GetUsCount();
#pragma omp parallel for
  for(int n=0; n<10000000*threads; n++)
  {
    void *p;
    if(use_transact)
    {
      BOOST_BEGIN_TRANSACT_LOCK(lock)
      {
        p=malloc(size);
      }
      BOOST_END_TRANSACT_LOCK(lock)
    }
    else
    {
      std::lock_guard<decltype(lock)> g(lock);
      p=malloc(size);
    }
    if(use_transact)
    {
      BOOST_BEGIN_TRANSACT_LOCK(lock)
      {
        free(p);
      }
      BOOST_END_TRANSACT_LOCK(lock)
    }
    else
    {
      std::lock_guard<decltype(lock)> g(lock);
      free(p);
    }
  }
  end=GetUsCount();
  REQUIRE(true);
//  printf("size=%u\n", (unsigned) map.size());
  return threads*10000000/((end-start)/1000000000000.0);
}

TEST_CASE("performance/malloc/transact/small", "Tests the transact performance of multiple threads using small memory allocations")
{
  printf("\n=== Small malloc transact performance ===\n");
  printf("1. Achieved %lf transactions per second\n", CalculateMallocPerformance(16, 1));
  printf("2. Achieved %lf transactions per second\n", CalculateMallocPerformance(16, 1));
  printf("3. Achieved %lf transactions per second\n", CalculateMallocPerformance(16, 1));
}

TEST_CASE("performance/malloc/transact/large", "Tests the transact performance of multiple threads using large memory allocations")
{
  printf("\n=== Large malloc transact performance ===\n");
  printf("1. Achieved %lf transactions per second\n", CalculateMallocPerformance(65536, 1));
  printf("2. Achieved %lf transactions per second\n", CalculateMallocPerformance(65536, 1));
  printf("3. Achieved %lf transactions per second\n", CalculateMallocPerformance(65536, 1));
}

static double CalculateUnorderedMapPerformance(size_t reserve, bool use_transact, int type)
{
  boost::spinlock::spinlock<bool> lock;
  boost::spinlock::atomic<size_t> gate(0);
  std::unordered_map<int, int> map;
  usCount start, end;
  if(reserve)
  {
    map.reserve(reserve);
    for(int n=0; n<reserve/2; n++)
      map.insert(std::make_pair(-n, n));
  }
#pragma omp parallel
  {
    ++gate;
  }
  size_t threads=gate;
  //printf("There are %u threads in this CPU\n", (unsigned) threads);
  start=GetUsCount();
#pragma omp parallel for
  for(int thread=0; thread<threads; thread++)
  for(int n=0; n<10000000; n++)
  {
    if(2==type)
    {
      // One thread always writes with lock, remaining threads read with transact
      bool amMaster=(thread==0);
      if(amMaster)
      {
        bool doInsert=((n/threads) & 1)!=0;
        std::lock_guard<decltype(lock)> g(lock);
        if(doInsert)
          map.insert(std::make_pair(n, n));
        else if(!map.empty())
          map.erase(map.begin());
      }
      else
      {
        if(use_transact)
        {
          BOOST_BEGIN_TRANSACT_LOCK(lock)
          {
            map.find(n-1);
          }
          BOOST_END_TRANSACT_LOCK(lock)
        }
        else
        {
          std::lock_guard<decltype(lock)> g(lock);
          map.find(n-1);
        }
      }
    }
    else if(1==type)
    {
      if(use_transact)
      {
        int v=-(int)(n % (reserve/2));
        if(v)
        {
          BOOST_BEGIN_TRANSACT_LOCK(lock)
          auto it=map.find(v);
          if(it==map.end()) std::cout << v;
          BOOST_END_TRANSACT_LOCK(lock)
        }
      }
      else
      {
        int v=-(int)(n % (reserve/2));
        if(v)
        {
          std::lock_guard<decltype(lock)> g(lock);
          auto it=map.find(v);
          if(it==map.end()) std::cout << v;
        }
      }
    }    
    else
    {
      if(use_transact)
      {
        size_t v=n*10+thread;
        BOOST_BEGIN_TRANSACT_LOCK(lock)
        {
          if((n & 255)<128)
            map.insert(std::make_pair(v, n));
          else if(!map.empty())
            map.erase(map.find(v-128));
        }
        BOOST_END_TRANSACT_LOCK(lock)
      }
      else
      {
        size_t v=n*10+thread;
        std::lock_guard<decltype(lock)> g(lock);
        if((n & 255)<128)
          map.insert(std::make_pair(v, n));
        else if(!map.empty())
        {
          auto it=map.find(v-1280);
          if(it!=map.end())
            map.erase(it);
        }
      }
    }
//    if(!(n % 1000000))
//      std::cout << "Items now " << map.size() << std::endl;
  }
  end=GetUsCount();
  REQUIRE(true);
//  printf("size=%u\n", (unsigned) map.size());
  return threads*10000000/((end-start)/1000000000000.0);
}

TEST_CASE("performance/unordered_map/small/write", "Tests the performance of multiple threads writing a small unordered_map")
{
  printf("\n=== Small unordered_map spinlock write performance ===\n");
  printf("1. Achieved %lf transactions per second\n", CalculateUnorderedMapPerformance(0, false, false));
  printf("2. Achieved %lf transactions per second\n", CalculateUnorderedMapPerformance(0, false, false));
  printf("3. Achieved %lf transactions per second\n", CalculateUnorderedMapPerformance(0, false, false));
}

TEST_CASE("performance/unordered_map/large/write", "Tests the performance of multiple threads writing a large unordered_map")
{
  printf("\n=== Large unordered_map spinlock write performance ===\n");
  printf("1. Achieved %lf transactions per second\n", CalculateUnorderedMapPerformance(10000, false, false));
  printf("2. Achieved %lf transactions per second\n", CalculateUnorderedMapPerformance(10000, false, false));
  printf("3. Achieved %lf transactions per second\n", CalculateUnorderedMapPerformance(10000, false, false));
}

TEST_CASE("performance/unordered_map/large/read", "Tests the performance of multiple threads reading a large unordered_map")
{
  printf("\n=== Large unordered_map spinlock read performance ===\n");
  printf("1. Achieved %lf transactions per second\n", CalculateUnorderedMapPerformance(10000, false, 1));
  printf("2. Achieved %lf transactions per second\n", CalculateUnorderedMapPerformance(10000, false, 1));
  printf("3. Achieved %lf transactions per second\n", CalculateUnorderedMapPerformance(10000, false, 1));
}

/*TEST_CASE("performance/unordered_map/transact/small", "Tests the transact performance of multiple threads using a small unordered_map")
{
  printf("\n=== Small unordered_map transact performance ===\n");
  printf("1. Achieved %lf transactions per second\n", CalculateUnorderedMapPerformance(0, true, false));
#ifndef BOOST_HAVE_TRANSACTIONAL_MEMORY_COMPILER
  printf("2. Achieved %lf transactions per second\n", CalculateUnorderedMapPerformance(0, true, false));
  printf("3. Achieved %lf transactions per second\n", CalculateUnorderedMapPerformance(0, true, false));
#endif
}

TEST_CASE("performance/unordered_map/transact/large", "Tests the transact performance of multiple threads using a large unordered_map")
{
  printf("\n=== Large unordered_map transact performance ===\n");
  printf("1. Achieved %lf transactions per second\n", CalculateUnorderedMapPerformance(10000, true, false));
#ifndef BOOST_HAVE_TRANSACTIONAL_MEMORY_COMPILER
  printf("2. Achieved %lf transactions per second\n", CalculateUnorderedMapPerformance(10000, true, false));
  printf("3. Achieved %lf transactions per second\n", CalculateUnorderedMapPerformance(10000, true, false));
#endif
}*/

static double CalculateConcurrentUnorderedMapPerformance(size_t reserve, int type)
{
  boost::spinlock::atomic<size_t> gate(0);
#ifdef BOOST_HAVE_SYSTEM_CONCURRENT_UNORDERED_MAP
  concurrency::concurrent_unordered_map<int, int> map;
#else
  boost::spinlock::concurrent_unordered_map<int, int> map;
#endif
  usCount start, end;
  if(reserve)
  {
#ifndef BOOST_HAVE_SYSTEM_CONCURRENT_UNORDERED_MAP
    map.reserve(reserve);
#endif
    for(int n=0; n<reserve/2; n++)
      map.insert(std::make_pair(-n, n));
  }
#pragma omp parallel
  {
    ++gate;
  }
  size_t threads=gate;
  printf("There are %u threads in this CPU\n", (unsigned) threads);
  start=GetUsCount();
#pragma omp parallel for
  for(int thread=0; thread<threads; thread++)
  for(int n=0; n<10000000; n++)
  {
#if 0
    if(readwrites)
    {
      // One thread always writes with lock, remaining threads read with transact
      bool amMaster=(thread==0);
      if(amMaster)
      {
        bool doInsert=((n/threads) & 1)!=0;
        if(doInsert)
          map.insert(std::make_pair(n, n));
        else if(!map.empty())
          map.erase(map.begin());
      }
      else
      {
        map.find(n-1);
      }
    }
    else
#endif
    if(0==type)
    {
      size_t v=n*10+thread;
      if((n & 255)<128)
        map.insert(std::make_pair(v, n));
      else
      {
#ifdef BOOST_HAVE_SYSTEM_CONCURRENT_UNORDERED_MAP
        map.unsafe_erase(v-1280);
#else
        map.erase(v-1280);
#endif
      }
    }
    else if(1==type)
    {
      int v=-(int)(n % (reserve/2));
      if(v)
      {
        auto it=map.find(v);
        if(it==map.end()) std::cout << v;
      }
    }
//    if(!(n % 1000000))
//      std::cout << "Items now " << map.size() << std::endl;
  }
  end=GetUsCount();
  //map.dump_buckets(std::cout);
  REQUIRE(true);
//  printf("size=%u\n", (unsigned) map.size());
  return threads*10000000/((end-start)/1000000000000.0);
}

TEST_CASE("performance/concurrent_unordered_map/small", "Tests the performance of multiple threads writing a small concurrent_unordered_map")
{
  printf("\n=== Small concurrent_unordered_map write performance ===\n");
  printf("1. Achieved %lf transactions per second\n", CalculateConcurrentUnorderedMapPerformance(0, false));
  printf("2. Achieved %lf transactions per second\n", CalculateConcurrentUnorderedMapPerformance(0, false));
  printf("3. Achieved %lf transactions per second\n", CalculateConcurrentUnorderedMapPerformance(0, false));
#ifdef BOOST_USING_INTEL_TSX
  if(boost::spinlock::intel_stuff::have_intel_tsx_support())
  {
    printf("\nForcing Intel TSX support off ...\n");
    boost::spinlock::intel_stuff::have_intel_tsx_support_result=1;
    printf("1. Achieved %lf transactions per second\n", CalculateConcurrentUnorderedMapPerformance(0, false));
    printf("2. Achieved %lf transactions per second\n", CalculateConcurrentUnorderedMapPerformance(0, false));
    printf("3. Achieved %lf transactions per second\n", CalculateConcurrentUnorderedMapPerformance(0, false));
    boost::spinlock::intel_stuff::have_intel_tsx_support_result=0;
  }
#endif
}

TEST_CASE("performance/concurrent_unordered_map/large/write", "Tests the performance of multiple threads writing a large concurrent_unordered_map")
{
  printf("\n=== Large concurrent_unordered_map write performance ===\n");
  printf("1. Achieved %lf transactions per second\n", CalculateConcurrentUnorderedMapPerformance(10000, false));
  printf("2. Achieved %lf transactions per second\n", CalculateConcurrentUnorderedMapPerformance(10000, false));
  printf("3. Achieved %lf transactions per second\n", CalculateConcurrentUnorderedMapPerformance(10000, false));
#ifdef BOOST_USING_INTEL_TSX
  if(boost::spinlock::intel_stuff::have_intel_tsx_support())
  { 
    printf("\nForcing Intel TSX support off ...\n");
    boost::spinlock::intel_stuff::have_intel_tsx_support_result=1;
    printf("1. Achieved %lf transactions per second\n", CalculateConcurrentUnorderedMapPerformance(10000, false));
    printf("2. Achieved %lf transactions per second\n", CalculateConcurrentUnorderedMapPerformance(10000, false));
    printf("3. Achieved %lf transactions per second\n", CalculateConcurrentUnorderedMapPerformance(10000, false));
    boost::spinlock::intel_stuff::have_intel_tsx_support_result=0;
  } 
#endif
}

TEST_CASE("performance/concurrent_unordered_map/large/read", "Tests the performance of multiple threads reading a large concurrent_unordered_map")
{
  printf("\n=== Large concurrent_unordered_map read performance ===\n");
  printf("1. Achieved %lf transactions per second\n", CalculateConcurrentUnorderedMapPerformance(10000, 1));
  printf("2. Achieved %lf transactions per second\n", CalculateConcurrentUnorderedMapPerformance(10000, 1));
  printf("3. Achieved %lf transactions per second\n", CalculateConcurrentUnorderedMapPerformance(10000, 1));
#ifdef BOOST_USING_INTEL_TSX
  if(boost::spinlock::intel_stuff::have_intel_tsx_support())
  { 
    printf("\nForcing Intel TSX support off ...\n");
    boost::spinlock::intel_stuff::have_intel_tsx_support_result=1;
    printf("1. Achieved %lf transactions per second\n", CalculateConcurrentUnorderedMapPerformance(10000, 1));
    printf("2. Achieved %lf transactions per second\n", CalculateConcurrentUnorderedMapPerformance(10000, 1));
    printf("3. Achieved %lf transactions per second\n", CalculateConcurrentUnorderedMapPerformance(10000, 1));
    boost::spinlock::intel_stuff::have_intel_tsx_support_result=0;
  } 
#endif
}


#ifndef BOOST_MEMORY_TRANSACTIONS_DISABLE_CATCH
int main(int argc, char *argv[])
{
#ifdef _OPENMP
  printf("These unit tests have been compiled with parallel support. I will use as many threads as CPU cores.\n");
#else
  printf("These unit tests have not been compiled with parallel support and will execute only those which are sequential.\n");
#endif
#ifdef BOOST_HAVE_TRANSACTIONAL_MEMORY_COMPILER
  printf("These unit tests have been compiled using a transactional compiler. I will use __transaction_relaxed.\n");
#else
  printf("These unit tests have not been compiled using a transactional compiler.\n");
#endif
  int result=Catch::Session().run(argc, argv);
  return result;
}
#endif
