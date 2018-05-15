#ifndef LIBI2PD_I2NP_EXT_H
#define LIBI2PD_I2NP_EXT_H

#include "I2PEndian.h"

#include <cstring>
#include <string>
#include <set>

namespace i2p
{
  /** max size of an extension name */
  const size_t I2NP_EXT_DATA_NAME_SZ = 30;
  /** size of the extension id */
  const size_t I2NP_EXT_NUM_SZ = 2;
  
  const size_t I2NP_EXT_SZ = 32;
  
  /** the size of the number of extension supported */
  const size_t I2NP_EXT_NUM_EXT_SIZE = 1;


  /** extension name for loki network peer profiling */
  const char I2NPEXT_LokiProfile[] = "loki_profile";
  /** extension name for bi directional tunnels */
  const char I2NPEXT_BidiTunnel[] = "bidi_tunnel";

  /** enum for i2np extensions supported */
  enum I2NPExtension
  {
    ePeerProfileLookup,
    eBidiTunnel,
  };

  /**
   * i2np extension id type, must be nonzero
   */
  typedef uint16_t I2NPExtID_t;

  /**
   * a single extension info when I2NPExtension message ID is zero
   * format is:
   *  2 bytes big endian extension id
   *  30 bytes null padded extension name
   */
  struct I2NPExtInfo : public i2p::data::Tag<I2NP_EXT_SZ>
  {
    I2NPExtInfo() : i2p::data::Tag<I2NP_EXT_SZ>() {};

    /** construct from memory */
    I2NPExtInfo(const uint8_t * ptr) : i2p::data::Tag<I2NP_EXT_SZ> (ptr) {};
    
    /** construct with name and ID */
    I2NPExtInfo(const char * name, I2NPExtID_t id) : i2p::data::Tag<I2NP_EXT_SZ> ()
    {
      Zero();
      uint8_t * ptr = *this;
      htobe16buf(ptr, id);
      strncpy((char *) (ptr + I2NP_EXT_NUM_SZ), name, I2NP_EXT_DATA_NAME_SZ);
    }
    
    std::string GetName() const
    {
      const uint8_t * ptr = data();
      return std::string((const char*) (ptr + I2NP_EXT_NUM_SZ), I2NP_EXT_NUM_SZ);
    }
    
    I2NPExtID_t GetExtension () const
    {
      return bufbe16toh(data());
    }
    
  };

  // a list of supported i2np extensions
  typedef std::set<I2NPExtInfo> I2NPExtList;

  std::string as_string(const I2NPExtList & l);
  
}


#endif
