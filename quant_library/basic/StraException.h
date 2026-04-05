#ifndef _STRA_EXCEPTION_H
#define _STRA_EXCEPTION_H


#include <exception>
#include <cstring>

using namespace std;

class StraException : public exception {
public:
    StraException(const char* m, int d = 0) : exception() {
        strncpy(msg, m, 1024);
        code = d;
    }

    const char* what() const noexcept override {
        return msg;
    }

    const int errorCode() const noexcept {
        return code;
    }

private:
    char msg[1024];
    int code{0};
};

#endif