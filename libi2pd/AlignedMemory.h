#ifndef LIBI2PD_MEMORY_H_
#define LIBI2PD_MEMORY_H_

#ifdef USE_JEMALLOC
#include <jemalloc/jemalloc.h>
#endif

#include <cmath>

namespace i2p
{
namespace util
{

#ifndef USE_JEMALLOC
  static inline void * mallocx(size_t sz, int flags)
  {
    (void) flags;
    return std::malloc(sz);
  }
#define MALLOCX_ALIGN(x) x
#endif
  
  template<typename T>
  struct _Allocator
  {
    static constexpr int align()
    {
      return std::exp2( 1 + std::floor(std::log2(sizeof(T))) );
    }
    
    static void * _Acquire(size_t sz)
    {
      void * ptr = nullptr;
      ptr = mallocx(sz, MALLOCX_ALIGN(align()));
      if (!ptr) throw std::bad_alloc();
      return ptr;
    }
    
    void * operator new (size_t sz, T * & ptr)
    {
      ptr = static_cast<T*>( _Acquire(sz));
      return ptr;
    }

    void * operator new (size_t sz)
    {
      return _Acquire(sz);
    }

    void * operator new[] (size_t sz)
    {
      return _Acquire(sz);
    }

    void operator delete (void * ptr)
    {
      free(ptr);
    }

    void operator delete[] (void * ptr)
    {
      free(ptr);
    }
    
  };

  template<typename T>
  struct Aligned : public _Allocator<T>
  {
  };
}
}

#endif
