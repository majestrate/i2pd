#ifndef LIBI2PD_MEMORY_H_
#define LIBI2PD_MEMORY_H_

#ifdef USE_JEMALLOC
#include <jemalloc/jemalloc.h>
#endif

namespace i2p
{
namespace util
{
  template<typename Alloc, typename T>
  struct _Allocator
  {
    
    static T * _Acquire(size_t sz)
    {
      T * ptr = nullptr;
      ptr = static_cast<T*>(Alloc()(sz));
      if (!ptr) throw std::bad_alloc();
      return ptr;
    }

    static void _Release(void * ptr)
    {
      free(ptr);
    }
    
    void * operator new (size_t sz, T * & ptr)
    {
      ptr = _Acquire(sz);
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
      _Release(ptr);
    }

    void operator delete[] (void * ptr)
    {
      _Release(ptr);
    }
    
  };

#ifndef USE_JEMALLOC
  static inline void * mallocx(size_t sz, int flags)
  {
    (void) flags;
    return std::malloc(sz);
  }
#define MALLOCX_ALIGN(x) x
#endif

  template<typename T>
  constexpr int align()
  {
    return std::exp2( 1 + std::floor(std::log2(sizeof(T))) );
  }

  template<typename T>
  struct xmalloc
  {
    void * operator ()(size_t sz)
    {
      return mallocx(sz, MALLOCX_ALIGN(align<T>()));
    }
  };
  
  template<typename T>
  struct Aligned : public _Allocator<xmalloc<T>, T>
  {
  };
}
}

#endif
