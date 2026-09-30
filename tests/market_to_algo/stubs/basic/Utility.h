#ifndef _UTILITY_H
#define _UTILITY_H

// Stub: only splitString is used by PairInfoManager::Init.
// Copied verbatim from the real quant_library/basic/Utility.h:60-68.
#include <string>
#include <vector>

using namespace std;

inline void splitString(const string& source, vector<string>& v, const string delimiters = " ") {
    string::size_type lastPos = source.find_first_not_of(delimiters, 0);
    string::size_type pos = source.find_first_of(delimiters, lastPos);
    while (string::npos != pos || string::npos != lastPos) {
        v.push_back(source.substr(lastPos, pos - lastPos));
        lastPos = source.find_first_not_of(delimiters, pos);
        pos = source.find_first_of(delimiters, lastPos);
    }
}

#endif
