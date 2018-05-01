#include "Poly1305.h"
#include "CPU.h"


namespace i2p
{
namespace crypto
{

	namespace poly1305
	{
		
	}
		
	Poly1305::Poly1305(const uint8_t * key)
	{
	}

	void Poly1305::Update(const uint8_t * buf, size_t sz)
	{
		
	}

	bool Poly1305VeirfyHMAC(const uint8_t * hmac, const uint8_t * key, const uint8_t * buf, std::size_t sz)
	{
	    uint8_t digest[POLY1305_DIGEST_SIZE];
    
  		Poly1305 poly(key);
  		poly.Update(buf, sz);
   		poly.Finish(digest);
    
    	return memcmp(digest, hmac, POLY1305_DIGEST_SIZE) == 0;
  	}
}
}
