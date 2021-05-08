#pragma once

template<class L>
class AtScopeExit {
    L& ll;
public:
    AtScopeExit(L& action) : ll(action) {}
    ~AtScopeExit() { ll(); }
};

#define AUTO_TOKEN_PASTEx(x, y) x ## y
#define AUTO_TOKEN_PASTE(x, y) AUTO_TOKEN_PASTEx(x, y)

#define Auto_INTERNAL1(lname, aname, ...) \
    auto lname = [&]() { __VA_ARGS__; }; \
    AtScopeExit<decltype(lname)> aname(lname);

#define Auto_INTERNAL2(ctr, ...) \
    Auto_INTERNAL1(AUTO_TOKEN_PASTE(Auto_func_, ctr), \
        AUTO_TOKEN_PASTE(Auto_instance_, ctr), __VA_ARGS__)

#define Auto(...) \
    Auto_INTERNAL2(__COUNTER__, __VA_ARGS__)
