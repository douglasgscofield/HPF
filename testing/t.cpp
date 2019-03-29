#include <iostream>
#include <cstring>
#include <cstdint>

using namespace std;

int main()
{
    string s = "123.456";
    long x, y;
    cout << s.substr(0, 3) << endl;
    x = atol(s.substr(0, 3).c_str());
    cout << s.substr(4, 3) << endl;
    y = atol(s.substr(4, 3).c_str());
    cout << x + y << endl;
}
