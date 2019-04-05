#include <iostream>
#include <sstream>
#include <cstring>
#include <cstdint>
#include <vector>
#include <iomanip>
#include <fstream>
#include <cctype>
#include <limits>
#include <algorithm>

using namespace std;


int main()
{
    int64_t p = 100, data_line = 0;
    for (auto i = 0; i < 1013; ++i) {
        ++data_line;
        if ((data_line - 1) % p)
            continue;
        cout << data_line << endl;
    }
}
