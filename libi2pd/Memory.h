#ifndef MEMORY_H
#define MEMORY_H
#include <cstdint>

namespace i2p
{
    namespace util
    {
        
	    template<typename T, typename Fill_t>
	    void Fill(T & t, Fill_t value)
	    {
		    Fill_t * ptr = t;
		    std::size_t sz = sizeof(T);
		    while(sz--)
		    {
			    *ptr = value;
			    ptr += sizeof(Fill_t);
		    }
	    }
        
        template<typename T>
        void Zero(T & t)
        {
            Fill<T, uint8_t>(t, 0);
        }
    }
}

#endif
