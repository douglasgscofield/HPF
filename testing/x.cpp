#include <cstdio>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>

using namespace std;

// handy int-to-hex printer, from https://stackoverflow.com/questions/5100718/integer-to-hex-string-in-c
template< typename T >
std::string i2hp( T i )
{
  std::stringstream stream;
  stream << "0x"
         << std::setfill ('0') << std::setw(sizeof(T)*2)
         << std::hex << i;
  return stream.str();
}



uint64_t ww2W( uint32_t w1, uint32_t w2 )
{
    uint64_t ans = 0;
    cout << "w1=" << i2hp(w1) << " sizeof=" << sizeof(w1)*8
         << " w2=" << i2hp(w2) << " sizeof=" << sizeof(w2)*8
         << " ans=" << i2hp(ans) << " sizeof=" << sizeof(ans)*8
         << endl;
    ans = static_cast<uint64_t>(w1) << sizeof(w1)*8;
    cout << "w1=" << i2hp(w1) << " sizeof=" << sizeof(w1)*8
         << " w2=" << i2hp(w2) << " sizeof=" << sizeof(w2)*8
         << " ans=" << i2hp(ans) << " sizeof=" << sizeof(ans)*8
         << endl;
    ans |= w2;
    cout << "w1=" << i2hp(w1) << " sizeof=" << sizeof(w1)*8
         << " w2=" << i2hp(w2) << " sizeof=" << sizeof(w2)*8
         << " ans=" << i2hp(ans) << " sizeof=" << sizeof(ans)*8
         << endl;
    return ans;
}

int main() {
    uint32_t a = 0x12345678;
    uint32_t b = 0x22334455;
    uint64_t c = ww2W(a, b);
    cout << "a=" << i2hp(a)
         << "  b=" << i2hp(b)
         << "  c=" << i2hp(c)
         << endl;
}

