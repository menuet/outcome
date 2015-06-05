/* future.hpp
Non-allocating constexpr future-promise
(C) 2015 Niall Douglas http://www.nedprod.com/
File Created: May 2015


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

#ifndef BOOST_SPINLOCK_FUTURE_HPP
#define BOOST_SPINLOCK_FUTURE_HPP

#include "spinlock.hpp"
#include <future>

// For some odd reason, VS2015 really hates to do much inlining unless forced
#ifdef _MSC_VER
# define BOOST_SPINLOCK_FUTURE_CONSTEXPR BOOST_SPINLOCK_FORCEINLINE
# define BOOST_SPINLOCK_FUTURE_CXX14_CONSTEXPR BOOST_SPINLOCK_FORCEINLINE
# define BOOST_SPINLOCK_FUTURE_MSVC_HELP BOOST_SPINLOCK_FORCEINLINE
#else
# define BOOST_SPINLOCK_FUTURE_CONSTEXPR BOOST_CONSTEXPR
# define BOOST_SPINLOCK_FUTURE_CXX14_CONSTEXPR BOOST_CXX14_CONSTEXPR
# define BOOST_SPINLOCK_FUTURE_MSVC_HELP 
#endif

BOOST_SPINLOCK_V1_NAMESPACE_BEGIN
namespace lightweight_futures {

template<typename R> class future;
template<typename R> class promise;
using std::exception_ptr;
using std::make_exception_ptr;
using std::error_code;
using std::rethrow_exception;
using std::future_error;
using std::future_errc;
using std::system_error;

namespace detail
{
  template<typename R> struct value_storage
  {
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4624)
#endif
    union
    {
      R value;
      error_code error;         // Often 16 bytes surprisingly
      exception_ptr exception;  // Typically 8 bytes
      future<R> *future_;       // Typically 8 bytes
    };
#ifdef _MSC_VER
#pragma warning(pop)
#endif
    enum class storage_type : unsigned char
    {
      empty,
      value,
      error,
      exception,
      future
    } type;
    BOOST_STATIC_CONSTEXPR bool is_nothrow_copy_constructible=std::is_nothrow_copy_constructible<R>::value && std::is_nothrow_copy_constructible<exception_ptr>::value && std::is_nothrow_copy_constructible<error_code>::value;
    BOOST_STATIC_CONSTEXPR bool is_nothrow_move_constructible=std::is_nothrow_move_constructible<R>::value && std::is_nothrow_move_constructible<exception_ptr>::value && std::is_nothrow_move_constructible<error_code>::value;
    BOOST_STATIC_CONSTEXPR bool is_nothrow_destructible=std::is_nothrow_destructible<R>::value && std::is_nothrow_destructible<exception_ptr>::value && std::is_nothrow_destructible<error_code>::value;
    
    BOOST_SPINLOCK_FUTURE_CONSTEXPR value_storage() noexcept : type(storage_type::empty) { }
    BOOST_SPINLOCK_FUTURE_CONSTEXPR value_storage(const R &v) noexcept(std::is_nothrow_copy_constructible<R>::value) : value(v), type(storage_type::value) { }
    BOOST_SPINLOCK_FUTURE_CONSTEXPR value_storage(const error_code &v) noexcept(std::is_nothrow_copy_constructible<error_code>::value) : error(v), type(storage_type::error) { }
    BOOST_SPINLOCK_FUTURE_CONSTEXPR value_storage(const exception_ptr &v) noexcept(std::is_nothrow_copy_constructible<exception_ptr>::value) : exception(v), type(storage_type::exception) { }
    BOOST_SPINLOCK_FUTURE_CONSTEXPR value_storage(R &&v) noexcept(std::is_nothrow_move_constructible<R>::value) : value(std::move(v)), type(storage_type::value) { }
    BOOST_SPINLOCK_FUTURE_CONSTEXPR value_storage(error_code &&v) noexcept(std::is_nothrow_move_constructible<error_code>::value) : error(std::move(v)), type(storage_type::error) { }
    BOOST_SPINLOCK_FUTURE_CONSTEXPR value_storage(exception_ptr &&v) noexcept(std::is_nothrow_move_constructible<exception_ptr>::value) : exception(std::move(v)), type(storage_type::exception) { }
    BOOST_SPINLOCK_FUTURE_CXX14_CONSTEXPR value_storage(const value_storage &o) noexcept(is_nothrow_copy_constructible) : type(o.type)
    {
      switch(type)
      {
        case storage_type::empty:
          break;
        case storage_type::value:
          new (&value) R(o.value);
          break;
        case storage_type::error:
          new (&error) error_code(o.error);
          break;
        case storage_type::exception:
          new (&exception) exception_ptr(o.exception);
          break;
        case storage_type::future:
          abort();
          break;
      }
    }
    BOOST_SPINLOCK_FUTURE_CXX14_CONSTEXPR value_storage(value_storage &&o) noexcept(is_nothrow_move_constructible) : type(o.type)
    {
      switch(type)
      {
        case storage_type::empty:
          break;
        case storage_type::value:
          new (&value) R(std::move(o.value));
          break;
        case storage_type::error:
          new (&error) error_code(std::move(o.error));
          break;
        case storage_type::exception:
          new (&exception) exception_ptr(std::move(o.exception));
          break;
        case storage_type::future:
          future_=o.future_;
          o.future_=nullptr;
          break;
      }
    }
    BOOST_SPINLOCK_FUTURE_CXX14_CONSTEXPR value_storage &operator=(const value_storage &o) noexcept(is_nothrow_copy_constructible)
    {
      clear();
      new (this) value_storage(o);
      return *this;
    }
    BOOST_SPINLOCK_FUTURE_CXX14_CONSTEXPR value_storage &operator=(value_storage &&o) noexcept(is_nothrow_move_constructible)
    {
      clear();
      new (this) value_storage(std::move(o));
      return *this;
    }
    BOOST_SPINLOCK_FUTURE_MSVC_HELP ~value_storage() noexcept(is_nothrow_destructible) { clear(); }
    BOOST_SPINLOCK_FUTURE_CXX14_CONSTEXPR void swap(storage_type &o) noexcept(is_nothrow_move_constructible)
    {
      switch(type)
      {
        case storage_type::empty:
          break;
        case storage_type::value:
          std::swap(value, o.value);
          break;
        case storage_type::error:
          std::swap(error, o.error);
          break;
        case storage_type::exception:
          std::swap(exception, o.exception);
          break;
        case storage_type::future:
          std::swap(future_, o.future_);
          break;
      }      
    }
    BOOST_SPINLOCK_FUTURE_CXX14_CONSTEXPR void clear() noexcept(is_nothrow_destructible)
    {
      switch(type)
      {
        case storage_type::empty:
          break;
        case storage_type::value:
          value.~R();
          type=storage_type::empty;
          break;
        case storage_type::error:
          error.~error_code();
          type=storage_type::empty;
          break;
        case storage_type::exception:
          exception.~exception_ptr();
          type=storage_type::empty;
          break;
        case storage_type::future:
          future_=nullptr;
          type=storage_type::empty;
          break;
      }
    }
    template<class U> BOOST_SPINLOCK_FUTURE_CXX14_CONSTEXPR void set_value(U &&v)
    {
      if(type!=storage_type::empty)
        throw future_error(future_errc::promise_already_satisfied);
      new (&value) R(std::forward<U>(v));
      type=storage_type::value;
    }
    void set_exception(exception_ptr e)
    {
      if(type!=storage_type::empty)
        throw future_error(future_errc::promise_already_satisfied);
      new (&exception) exception_ptr(std::move(e));
      type=storage_type::exception;
    }
    // Note to self: this can't be BOOST_SPINLOCK_FUTURE_CXX14_CONSTEXPR
    void set_error(error_code e)
    {
      if(type!=storage_type::empty)
        throw future_error(future_errc::promise_already_satisfied);
      new (&error) error_code(std::move(e));
      type=storage_type::error;
    }
    // Called by future to take ownership of storage from promise
    BOOST_SPINLOCK_FUTURE_CXX14_CONSTEXPR void set_future(future<R> *f) noexcept(is_nothrow_destructible)
    {
      // Always overwrites existing storage
      clear();
      future_=f;
      type=storage_type::future;
    }
  };
}

/*! \brief class monad
\brief Implements a lightweight simple monadic value transport with the same semantics as a future

This monad can hold empty, a type R, an error_code or an exception_ptr at a space cost of
max(20, sizeof(R)+4).

So long as you avoid the exception_ptr code paths, this implementation will be
ideally reduced to as few assembler instructions as possible by most recent compilers [1]
which can include exactly zero assembler instructions output. This monad is therefore
identical in terms of execution overhead to using the R type you specify directly - you
get the monadic functionality totally free of execution overhead where possible.

A similar thing applies to error_code which is a lightweight implementation on most
systems. An exception is on VS2015 as the lvalue reference to system_category appears
to be constructed via thread safe once routine called "Immortalize", so when you
construct an error_code on MSVC you'll force a memory synchronisation during the constructor
only. error_codes are very cheap to pass around though as they are but an integer and a lvalue ref,
though I see that on all compilers 16 bytes is always copied around instead of completely
eliding the copy.

exception_ptr is also pretty good on anything but MSVC, though never zero assembler
instructions. As soon as an exception_ptr \em could be created, you'll force out about twenty
instructions most of which won't be executed in practice. Unfortunately, MSVC churns out
about 2000 assembler instructions as soon as you might touch an exception_ptr, I've raised
this with Microsoft :(

[1]: GCC 5.1 does a perfect job, VS2015 does a good job, clang 3.7 not so great.
*/
template<typename R> class monad
{
public:
  //! \brief The type potentially held by the monad
  typedef R value_type;
  //! \brief The error code potentially held by the monad
  typedef error_code error_type;
  //! \brief The exception ptr potentially held by the monad
  typedef exception_ptr exception_type;
private:
  typedef detail::value_storage<value_type> value_storage_type;
  value_storage_type _storage;
protected:
  monad(value_storage_type &&s) : _storage(std::move(s)) { }
public:
  //! \brief This monad will never throw exceptions during copy construction
  BOOST_STATIC_CONSTEXPR bool is_nothrow_copy_constructible = value_storage_type::is_nothrow_copy_constructible;
  //! \brief This monad will never throw exceptions during move construction
  BOOST_STATIC_CONSTEXPR bool is_nothrow_move_constructible = value_storage_type::is_nothrow_move_constructible;
  //! \brief This monad will never throw exceptions during destruction
  BOOST_STATIC_CONSTEXPR bool is_nothrow_destructible = value_storage_type::is_nothrow_destructible;

  //! \brief Default constructor, initialises to empty
  monad() = default;
  //! \brief Implicit constructor from a value_type by copy
  BOOST_SPINLOCK_FUTURE_CONSTEXPR monad(const value_type &v) : _storage(v) { }
  //! \brief Implicit constructor from a value_type by move
  BOOST_SPINLOCK_FUTURE_CONSTEXPR monad(value_type &&v) : _storage(std::move(v)) { }
  //! \brief Implicit constructor from a error_type by copy
  BOOST_SPINLOCK_FUTURE_CONSTEXPR monad(const error_type &v) : _storage(v) { }
  //! \brief Implicit constructor from a error_type by move
  BOOST_SPINLOCK_FUTURE_CONSTEXPR monad(error_type &&v) : _storage(std::move(v)) { }
  //! \brief Implicit constructor from a exception_type by copy
  BOOST_SPINLOCK_FUTURE_CONSTEXPR monad(const exception_type &v) : _storage(v) { }
  //! \brief Implicit constructor from a exception_type by move
  BOOST_SPINLOCK_FUTURE_CONSTEXPR monad(exception_type &&v) : _storage(std::move(v)) { }
  //! \brief Move constructor
  monad(monad &&) = default;
  //! \brief Move assignment
  monad &operator=(monad &&) = default;
  //! \brief Copy constructor
  monad(const monad &v) = default;
  //! \brief Copy assignment
  monad &operator=(const monad &) = default;

  //! \brief True if monad contains a value_type
  BOOST_SPINLOCK_FUTURE_CONSTEXPR explicit operator bool() const noexcept { return has_value(); }
  //! \brief True if monad is not empty
  BOOST_SPINLOCK_FUTURE_CONSTEXPR bool is_ready() const noexcept
  {
    return _storage.type!=value_storage_type::storage_type::empty;
  }
  //! \brief True if monad contains a value_type
  BOOST_SPINLOCK_FUTURE_CONSTEXPR bool has_value() const noexcept
  {
    return _storage.type==value_storage_type::storage_type::value;
  }
  //! \brief True if monad contains an error_type
  BOOST_SPINLOCK_FUTURE_CONSTEXPR bool has_error() const noexcept
  {
    return _storage.type==value_storage_type::storage_type::error;
  }
  //! \brief True if monad contains an exception_type
  BOOST_SPINLOCK_FUTURE_CONSTEXPR bool has_exception() const noexcept
  {
    return _storage.type==value_storage_type::storage_type::exception || _storage.type==value_storage_type::storage_type::error;
  }

  //! \brief Swaps one monad for another
  BOOST_SPINLOCK_FUTURE_MSVC_HELP void swap(monad &o) noexcept(is_nothrow_move_constructible)
  {
    _storage.swap(o._storage);
  }
  //! \brief Destructs any state stored, resetting to empty
  BOOST_SPINLOCK_FUTURE_MSVC_HELP void clear() noexcept(is_nothrow_destructible)
  {
    _storage.clear();
  }

private:
  BOOST_SPINLOCK_FUTURE_MSVC_HELP void _get_value() const &&
  {
    if(!is_ready())
      throw future_error(future_errc::no_state);
    if(has_error() || has_exception())
    {
      if(has_error())
        throw system_error(_storage.error);
      if(has_exception())
        rethrow_exception(_storage.exception);
    }      
  }
public:
  //! \brief If contains a value_type, returns a lvalue reference to it, else throws an exception of future_error(no_state), system_error or the exception_ptr.
  BOOST_SPINLOCK_FUTURE_MSVC_HELP value_type &get() &
  {
      std::move(*this)._get_value();
      return _storage.value;
  }
  //| \overload
  BOOST_SPINLOCK_FUTURE_MSVC_HELP value_type &value() &
  {
      std::move(*this)._get_value();
      return _storage.value;
  }
  //! \brief If contains a value_type, returns a const lvalue reference to it, else throws an exception of future_error(no_state), system_error or the exception_ptr.
  BOOST_SPINLOCK_FUTURE_MSVC_HELP const value_type &get() const &
  {
      std::move(*this)._get_value();
      return _storage.value;
  }
  //! \overload
  BOOST_SPINLOCK_FUTURE_MSVC_HELP const value_type &value() const &
  {
      std::move(*this)._get_value();
      return _storage.value;
  }
  //! \brief If contains a value_type, returns a rvalue reference to it, else throws an exception of future_error(no_state), system_error or the exception_ptr.
  BOOST_SPINLOCK_FUTURE_MSVC_HELP value_type &&get() &&
  {
      std::move(*this)._get_value();
      return std::move(_storage.value);
  }
  //! \overload
  BOOST_SPINLOCK_FUTURE_MSVC_HELP value_type &&value() &&
  {
      std::move(*this)._get_value();
      return std::move(_storage.value);
  }
  //! \brief If contains a value_type, return that value type, else return the supplied value_type
  BOOST_SPINLOCK_FUTURE_CONSTEXPR value_type &get_or(value_type &v) & noexcept
  {
    return has_value() ? _storage.value : v;
  }
  //! \overload
  BOOST_SPINLOCK_FUTURE_CONSTEXPR value_type &value_or(value_type &v) & noexcept
  {
    return has_value() ? _storage.value : v;
  }
  //! \brief If contains a value_type, return that value type, else return the supplied value_type
  BOOST_SPINLOCK_FUTURE_CONSTEXPR const value_type &get_or(const value_type &v) const & noexcept
  {
    return has_value() ? _storage.value : v;
  }
  //! \overload
  BOOST_SPINLOCK_FUTURE_CONSTEXPR const value_type &value_or(const value_type &v) const & noexcept
  {
    return has_value() ? _storage.value : v;
  }
  //! \brief If contains a value_type, return that value type, else return the supplied value_type
  BOOST_SPINLOCK_FUTURE_CONSTEXPR value_type &&get_or(value_type &&v) && noexcept
  {
    return has_value() ? std::move(_storage.value) : std::move(v);
  }
  //! \overload
  BOOST_SPINLOCK_FUTURE_CONSTEXPR value_type &&value_or(value_type &&v) && noexcept
  {
    return has_value() ? std::move(_storage.value) : std::move(v);
  }
  //! \brief Disposes of any existing state, setting the monad to a copy of the value_type
  BOOST_SPINLOCK_FUTURE_MSVC_HELP void set_value(const value_type &v) { _storage.clear(); _storage.set_value(v); }
  //! \brief Disposes of any existing state, setting the monad to a move of the value_type
  BOOST_SPINLOCK_FUTURE_MSVC_HELP void set_value(value_type &&v) { _storage.clear(); _storage.set_value(std::move(v)); }
  
  //! \brief If contains an error_type, returns that error_type, else returns a null error_type. Can only throw the exception future_error(no_state) if empty.
  BOOST_SPINLOCK_FUTURE_MSVC_HELP error_type get_error() const
  {
    if(!is_ready())
      throw future_error(future_errc::no_state);
    if(!has_error())
      return error_type();
    return _storage.error;
  }
  //! \brief If contains an error_type, returns that error_type else returns the error_type supplied
  BOOST_SPINLOCK_FUTURE_MSVC_HELP error_type get_error_or(error_type e) const noexcept { return has_error() ? _storage.error : std::move(e); }
  //! \brief Disposes of any existing state, setting the monad to the error_type
  BOOST_SPINLOCK_FUTURE_MSVC_HELP void set_error(error_type v) { _storage.clear(); _storage.set_error(std::move(v)); }
  
  //! \brief If contains an exception_ptr, returns that exception_ptr. If contains an error_code, returns system_error(error_code). If contains a value_type, returns a null exception_ptr. Can only throw the exception future_error(no_state) if empty.
  BOOST_SPINLOCK_FUTURE_MSVC_HELP exception_type get_exception() const
  {
    if(!is_ready())
      throw future_error(future_errc::no_state);
    if(!has_error() && !has_exception())
      return exception_type();
    if(has_error())
      return make_exception_ptr(system_error(_storage.error));
    if(has_exception())
      return _storage.exception;
    return exception_type();
  }
  //! \brief If contains an exception_type, returns that exception_type else returns the exception_type supplied
  BOOST_SPINLOCK_FUTURE_MSVC_HELP exception_type get_exception_or(exception_type e) const noexcept { return has_exception() ? _storage.exception : std::move(e); }
  //! \brief Disposes of any existing state, setting the monad to the exception_type
  BOOST_SPINLOCK_FUTURE_MSVC_HELP void set_exception(exception_type v) { _storage.clear(); _storage.set_exception(std::move(v)); }
  //! \brief Disposes of any existing state, setting the monad to make_exception_ptr(forward<E>(e))
  template<typename E> BOOST_SPINLOCK_FUTURE_MSVC_HELP void set_exception(E &&e)
  {
    set_exception(make_exception_ptr(std::forward<E>(e)));
  }

  //! \brief If I am a monad<monad<...>>, return monad<...>
  // typename todo unwrap() const &;
  // typename todo unwrap() &&;

  //! \brief If I am a monad<monad<monad<monad<...>>>>, return monad<...>
  // typename todo unwrap_all();
  // typename todo unwrap_all() &&;

  //! \brief Return monad(F(*this)).unwrap()
  // TODO Only enable if F is of form F(monad<is_constructible<value_type>, c>)
  // template<class F> typename std::result_of<F(monad<value_type>)>::type then(F &&f);
  
  //! \brief If bool(*this), return monad(F(*this)).unwrap(), else return monad<result_of<F(*this)>>(error).unwrap()
  // template<class F> typename std::result_of<F(monad<value_type>)>::type bind(F &&f);
  
  //! \brief If bool(*this), return monad(F(*this)), else return monad<result_of<F(*this)>>(error)
  // template<class F> typename std::result_of<F(monad<value_type>)>::type map(F &&f);
};


namespace detail
{
  template<typename R> struct lock_guard
  {
    promise<R> *_p;
    future<R>  *_f;
    lock_guard(const lock_guard &)=delete;
    lock_guard(lock_guard &&)=delete;
    BOOST_SPINLOCK_FUTURE_MSVC_HELP lock_guard(promise<R> *p) : _p(nullptr), _f(nullptr)
    {
      // constexpr fold
      if(!p->_need_locks)
      {
        _p=p;
        if(p->_storage.type==value_storage<R>::storage_type::future)
          _f=p->_storage.future_;
        return;
      }
      else for(;;)
      {
        p->_lock.lock();
        if(p->_storage.type==value_storage<R>::storage_type::future)
        {
          if(p->_storage.future_->_lock.try_lock())
          {
            _p=p;
            _f=p->_storage.future_;
            break;
          }
        }
        else
        {
          _p=p;
          break;
        }
        p->_lock.unlock();
      }
    }
    BOOST_SPINLOCK_FUTURE_MSVC_HELP lock_guard(future<R> *f) : _p(nullptr), _f(nullptr)
    {
      // constexpr fold
      if(!f->_need_locks)
      {
        _p=f->_promise;
        _f=f;
        return;
      }
      else for(;;)
      {
        f->_lock.lock();
        if(f->_promise)
        {
          if(f->_promise->_lock.try_lock())
          {
            _p=f->_promise;
            _f=f;
            break;
          }
        }
        else
        {
          _f=f;
          break;
        }
        f->_lock.unlock();
      }
    }
    BOOST_SPINLOCK_FUTURE_MSVC_HELP ~lock_guard()
    {
      unlock();
    }
    void unlock()
    {
      if(_p)
      {
        if(_p->_need_locks)
          _p->_lock.unlock();
        _p=nullptr;
      }
      if(_f)
      {
        if(_f->_need_locks)
          _f->_lock.unlock();
        _f=nullptr;
      }
    }
  };
}

template<typename R> class promise
{
  friend class future<R>;
  friend struct detail::lock_guard<R>;
public:
  typedef R value_type;
  typedef exception_ptr exception_type;
  typedef error_code error_type;
private:
  typedef detail::value_storage<value_type> value_storage_type;
  value_storage_type _storage;
  bool _need_locks;                 // Used to inhibit unnecessary atomic use, thus enabling constexpr collapse
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4624)
#endif
  union { spinlock<bool> _lock; };  // Delay construction
#ifdef _MSC_VER
#pragma warning(pop)
#endif
public:
  //! \brief EXTENSION: constexpr capable constructor
  BOOST_SPINLOCK_FUTURE_CONSTEXPR promise() : _need_locks(false)
  {
    static_assert(std::is_move_constructible<value_type>::value, "Type must be move constructible to be used in a lightweight promise");    
  }
  // template<class Allocator> promise(allocator_arg_t, Allocator a); // cannot support
  BOOST_SPINLOCK_FUTURE_CXX14_CONSTEXPR promise(promise &&o) noexcept(std::is_nothrow_move_constructible<value_storage_type>::value) : _need_locks(o._need_locks)
  {
    if(_need_locks) new (&_lock) spinlock<bool>();
    detail::lock_guard<value_type> h(&o);
    _storage=std::move(o._storage);
    if(h._f)
      h._f->_promise=this;
  }
  promise &operator=(promise &&o) noexcept(std::is_nothrow_move_constructible<value_storage_type>::value)
  {
    // TODO FIXME: Only safe if both of these are noexcept
    this->~promise();
    new (this) promise(std::move(o));
    return *this;
  }
  promise(const promise &)=delete;
  promise &operator=(const promise &)=delete;
  BOOST_SPINLOCK_FUTURE_MSVC_HELP ~promise() noexcept(std::is_nothrow_destructible<value_storage_type>::value)
  {
    detail::lock_guard<value_type> h(this);
    if(h._f)
    {
      if(!h._f->is_ready())
        h._f->set_exception(make_exception_ptr(future_error(future_errc::broken_promise)));
      h._f->_promise=nullptr;
    }
    // Destroy myself before locks exit
    _storage.clear();
  }
  
  void swap(promise &o) noexcept(std::is_nothrow_move_constructible<value_storage_type>::value)
  {
    detail::lock_guard<value_type> h1(this), h2(&o);
    _storage.swap(o._storage);
    if(h1._f)
      h1._f->_promise=&o;
    if(h2._f)
      h2._f->_promise=this;
  }
  
  BOOST_SPINLOCK_FUTURE_MSVC_HELP future<value_type> get_future()
  {
    // If no value stored yet, I need locks on from now on
    if(!_need_locks && _storage.type==value_storage_type::storage_type::empty)
    {
      _need_locks=true;
      new (&_lock) spinlock<bool>();
    }
    detail::lock_guard<value_type> h(this);
    if(h._f)
      throw future_error(future_errc::future_already_retrieved);
    future<value_type> ret(this);
    h.unlock();
    return std::move(ret);
  }
  //! \brief EXTENSION: Does this promise have a future?
  bool has_future() const noexcept
  {
    //detail::lock_guard<value_type> h(this);
    return _storage.type==value_storage_type::storage_type::future;
  }
  
  BOOST_SPINLOCK_FUTURE_MSVC_HELP void set_value(const value_type &v) noexcept(std::is_nothrow_copy_constructible<value_type>::value)
  {
    detail::lock_guard<value_type> h(this);
    if(h._f)
      h._f->set_value(v);
    else
      _storage.set_value(v);
  }
  BOOST_SPINLOCK_FUTURE_MSVC_HELP void set_value(value_type &&v) noexcept(std::is_nothrow_move_constructible<value_type>::value)
  {
    detail::lock_guard<value_type> h(this);
    if(h._f)
      h._f->set_value(std::move(v));
    else
      _storage.set_value(std::move(v));
  }
  //! \brief EXTENSION: Set an error code (doesn't allocate)
  void set_error(error_type e) noexcept(std::is_nothrow_copy_constructible<error_type>::value)
  {
    detail::lock_guard<value_type> h(this);
    if(h._f)
      h._f->set_error(e);
    else
      _storage.set_error(e);
  }
  void set_exception(exception_type e) noexcept(std::is_nothrow_copy_constructible<exception_type>::value)
  {
    detail::lock_guard<value_type> h(this);
    if(h._f)
      h._f->set_exception(e);
    else
      _storage.set_exception(e);
  }
  template<typename E> void set_exception(E &&e)
  {
    set_exception(make_exception_ptr(std::forward<E>(e)));
  }
  
  // Not supported right now
  // void set_value_at_thread_exit(R v);
  // void set_exception_at_thread_exit(R v);

  //! \brief Call F when the future signals, consuming the future. Only one of these may be set.
  // template<class F> typename std::result_of<F(future<value_type>)>::type then(F &&f);

  //! \brief Call F when the future signals, not consuming the future.
  // template<class F> typename std::result_of<F(future<const value_type &>)>::type then(F &&f);
};

// TODO: promise<void>, promise<R&> specialisations
// TODO: future<void>, future<R&> specialisations

/*! \class future
\brief Lightweight next generation future with N4399 Concurrency TS extensions

http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2015/n4399.html
*/
template<typename R> class future : protected monad<R>
{
  typedef monad<R> monad_type;
  friend class promise<R>;
  friend struct detail::lock_guard<R>;
public:
  typedef typename monad_type::value_type value_type;
  typedef typename monad_type::exception_type exception_type;
  typedef typename monad_type::error_type error_type;
  BOOST_STATIC_CONSTEXPR bool is_consuming=true;
  typedef promise<value_type> promise_type;
private:
  bool _need_locks;                 // Used to inhibit unnecessary atomic use, thus enabling constexpr collapse
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4624)
#endif
  union { spinlock<bool> _lock; };  // Delay construction
#ifdef _MSC_VER
#pragma warning(pop)
#endif
  promise_type *_promise;
protected:
  // Called by promise::get_future(), so currently thread safe
  BOOST_SPINLOCK_FUTURE_CXX14_CONSTEXPR future(promise_type *p) : monad_type(std::move(p->_storage)), _need_locks(p->_need_locks), _promise(p)
  {
    if(_need_locks) new (&_lock) spinlock<bool>();
    p->_storage.set_future(this);
  }
public:
  //! \brief EXTENSION: constexpr capable constructor
  BOOST_SPINLOCK_FUTURE_CONSTEXPR future() : _need_locks(false), _promise(nullptr)
  {
    static_assert(std::is_move_constructible<value_type>::value, "Type must be move constructible to be used in a lightweight future");    
  }
  BOOST_SPINLOCK_FUTURE_CXX14_CONSTEXPR future(future &&o) noexcept(std::is_nothrow_move_constructible<monad_type>::value) : _need_locks(o._need_locks), _promise(nullptr)
  {
    if(_need_locks) new (&_lock) spinlock<bool>();
    detail::lock_guard<value_type> h(&o);
    new(this) monad_type(std::move(o));
    if(o._promise)
    {
      _promise=o._promise;
      o._promise=nullptr;
      if(h._p)
        h._p->_storage.future_=this;
    }
  }
  future &operator=(future &&o) noexcept(std::is_nothrow_move_constructible<monad_type>::value)
  {
    // TODO FIXME: Only safe if both of these are noexcept
    this->~future();
    new (this) future(std::move(o));
    return *this;
  }
  future(const future &)=delete;
  future &operator=(const future &)=delete;
  // MSVC needs the destructor force inlined to do the right thing for some reason
  BOOST_SPINLOCK_FUTURE_MSVC_HELP ~future() noexcept(std::is_nothrow_destructible<monad_type>::value)
  {
    detail::lock_guard<value_type> h(this);
    if(h._p)
      h._p->_storage.clear();
    // Destroy myself before locks exit
    monad_type::clear();
  }
  
  void swap(future &o) noexcept(std::is_nothrow_move_constructible<monad_type>::value)
  {
    detail::lock_guard<value_type> h1(this), h2(&o);
    monad_type::swap(o._storage);
    if(h1._p)
      h1._p->_storage.future_=&o;
    if(h2._p)
      h2._p->_storage.future_=this;
  }
  
  // shared_future<value_type> share();  // TODO
  
  BOOST_SPINLOCK_FUTURE_MSVC_HELP value_type get()
  {
    wait();
    detail::lock_guard<value_type> h(this);
    value_type ret(std::move(*this).value());
    monad_type::clear();
    if(h._p)
      h._p->_storage.clear();
    if(h._f)
      h._f->_promise=nullptr;
    return std::move(ret);
  }
  // value_type get_or(const value_type &);  // TODO
  // value_type get_or(value_type &&);  // TODO
  error_type get_error()
  {
    wait();
    detail::lock_guard<value_type> h(this);
    error_code e(monad_type::error());
    if (!e)
      return std::move(e);
    monad_type::clear();
    if (h._p)
      h._p->_storage.clear();
    if (h._f)
      h._f->_promise = nullptr;
    return std::move(e);
  }
  exception_type get_exception()
  {
    wait();
    detail::lock_guard<value_type> h(this);
    exception_ptr e(monad_type::get_exception());
    if(!e)
      return std::move(e);
    monad_type::clear();
    if(h._p)
      h._p->_storage.clear();
    if(h._f)
      h._f->_promise=nullptr;
    return std::move(e);
  }
  // Compatibility with Boost.Thread
  exception_type get_exception_ptr() { return get_exception(); }
  
  bool valid() const noexcept
  {
    return !!_promise;
  }
  using monad_type::is_ready;
  using monad_type::has_value;
  using monad_type::has_error;
  using monad_type::has_exception;
  
  void wait() const
  {
    if(!valid())
      throw future_error(future_errc::no_state);
    // TODO Actually sleep
    while(!monad_type::is_ready())
    {
    }
  }
  // template<class R, class P> future_status wait_for(const std::chrono::duration<R, P> &rel_time) const;  // TODO
  // template<class C, class D> future_status wait_until(const std::chrono::time_point<C, D> &abs_time) const;  // TODO
  
  // TODO Where F would return a future<future<...>>, we unwrap to a single future<R>
  // template<class F> typename std::result_of<F(future)>::type then(F &&f);
};

template<typename R> future<typename std::decay<R>::type> make_ready_future(R &&v)
{
  return future<typename std::decay<R>::type>(std::forward<R>(v));
}
template<typename R> future<R> make_errored_future(std::error_code v)
{
  return future<R>(v);
}
template<typename R> future<R> make_exceptional_future(std::exception_ptr v)
{
  return future<R>(v);
}

// TODO
// template<class InputIterator> ? when_all(InputIterator first, InputIterator last);
// template<class... Futures> ? when_all(Futures &&... futures);
// template<class Sequence> struct when_any_result;
// template<class InputIterator> ? when_any(InputIterator first, InputIterator last);
// template<class... Futures> ? when_any(Futures &&... futures);

// TODO packaged_task

#ifdef _MSC_VER
#undef value
#undef exception
#undef error
#undef future_
#endif

}
BOOST_SPINLOCK_V1_NAMESPACE_END

namespace std
{
  template<typename R> void swap(BOOST_SPINLOCK_V1_NAMESPACE::lightweight_futures::promise<R> &a, BOOST_SPINLOCK_V1_NAMESPACE::lightweight_futures::promise<R> &b)
  {
    a.swap(b);
  }
  template<typename R> void swap(BOOST_SPINLOCK_V1_NAMESPACE::lightweight_futures::future<R> &a, BOOST_SPINLOCK_V1_NAMESPACE::lightweight_futures::future<R> &b)
  {
    a.swap(b);
  }
}

#endif
