#ifndef LIBI2PD_I2PSTRING_H
#define LIBI2PD_I2PSTRING_H

#include <iostream>

namespace i2p
{
namespace data
{
  void WriteString (const std::string& str, std::ostream& s);
  size_t ReadString (char* str, size_t len, std::istream& s);
}
}

#endif