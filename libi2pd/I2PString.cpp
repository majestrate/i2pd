#include "I2PString.h"
#include "Log.h"
namespace i2p
{
namespace data
{
  void WriteString (const std::string& str, std::ostream& s)
	{
		uint8_t len = str.size ();
		s.write ((char *)&len, 1);
		s.write (str.c_str (), len);
	}

	size_t ReadString (char * str, size_t len, std::istream& s)
	{
		uint8_t l;
		s.read ((char *)&l, 1);
		if (l < len)
		{
			s.read (str, l);
			if (!s) l = 0; // failed, return empty string
			str[l] = 0;
		}
		else
		{
			LogPrint (eLogWarning, "String length ", (int)l, " exceeds buffer size ", len);
			s.seekg (l, std::ios::cur); // skip
			str[0] = 0;
		}
		return l+1;
	}
}
}