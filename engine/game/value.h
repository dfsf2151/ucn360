#pragma once
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace ucn::game {

// A runtime expression value: the JS runtime keeps numbers and strings in the same slots and lets `+` concatenate.
// Only the operations the runtime's expression and comparison code use are here, with JS semantics.
struct Value {
    bool is_str = false;
    double num = 0;
    std::string str;

    Value() = default;
    Value(double d) : num(d) {}
    explicit Value(std::string s) : is_str(true), str(std::move(s)) {}

    // JS Number -> String for the values the program produces (integers and short decimals).
    static std::string number_string(double d) {
        if (std::isnan(d)) return "NaN";
        if (d == std::floor(d) && std::fabs(d) < 1e21) {
            char b[32];
            std::snprintf(b, sizeof b, "%.0f", d);
            return b;
        }
        char b[40];
        for (int p = 1; p <= 17; p++) {  // shortest representation that round-trips, as JS prints
            std::snprintf(b, sizeof b, "%.*g", p, d);
            if (std::strtod(b, nullptr) == d) break;
        }
        return b;
    }
    // JS String -> Number (Number(s)): empty/blank is 0, garbage is NaN.
    static double string_number(const std::string& s) {
        size_t a = s.find_first_not_of(" \t\r\n"), e = s.find_last_not_of(" \t\r\n");
        if (a == std::string::npos) return 0;
        std::string t = s.substr(a, e - a + 1);
        char* end = nullptr;
        double d = std::strtod(t.c_str(), &end);
        return end && *end == 0 ? d : NAN;
    }
    double as_num() const { return is_str ? string_number(str) : num; }
    std::string as_str() const { return is_str ? str : number_string(num); }

    Value& operator+=(const Value& o) {
        if (is_str || o.is_str) { str = as_str() + o.as_str(); is_str = true; }
        else num += o.num;
        return *this;
    }
    bool operator==(const Value& o) const {
        if (is_str && o.is_str) return str == o.str;
        return as_num() == o.as_num();
    }
    bool operator<(const Value& o) const {
        if (is_str && o.is_str) return str < o.str;
        return as_num() < o.as_num();
    }
};

} // namespace ucn::game
