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
#ifdef USE_JEMALLOC
  template<typename T>
  struct Aligned
  {
    
    static constexpr int alignment = std::exp2( std::floor(std::log2(sizeof(T))) );
    
    static inline void * _Acquire(size_t sz)
    {
      void * ptr = nullptr;
      ptr = mallocx(sz, MALLOCX_ALIGN(alignment));
      if (!ptr) throw std::bad_alloc();
      return ptr;
    }
    
    static void * operator new (size_t sz, T * ptr)
    {
      return ptr;
    }

    static void * operator new (size_t sz)
    {
      return _Acquire(sz);
    }

    static void * operator new[] (size_t sz)
    {
      return _Acquire(sz);
    }

    static void operator delete (void * ptr, size_t sz)
    {
      free(ptr);
    }
    
    static void operator delete[] (void * ptr, size_t sz)
    {
      free(ptr);
    }    
  };
#else
  template<typename T>
  struct Aligned {};
#endif
  
}
}

#endif
