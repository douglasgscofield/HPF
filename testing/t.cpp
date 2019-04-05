#include <iostream>
#include <sstream>
#include <cstring>
#include <cstdint>
#include <vector>

using namespace std;

struct I;
typedef struct I I;
vector<I> i;
struct I {
    int64_t _index;
    int64_t d;
    I(int64_t dd)
        : d(dd)
    {
        _index = i.size();
    };
    string out() const {
        stringstream ss;
        ss << "_index=" << _index << "  d=" << d;
        return ss.str();
    }
};


int main()
{
    string s = "123.456";
    //string t = "65537";
    string t = "abc";
    long x, y;
    /*
    cout << s.substr(0, 3) << endl;
    x = atol(s.substr(0, 3).c_str());
    cout << s.substr(4, 3) << endl;
    y = atol(s.substr(4, 3).c_str());
    cout << x + y << endl;
    */
    if (auto j = atol(t.c_str())) { 
        cout << "j=:" << j << ":" << endl;
    }

    i.emplace_back(3);
    i.emplace_back(17);
    i.emplace_back(-21);
    for (auto c : i) cerr << c.out() << endl;
}
