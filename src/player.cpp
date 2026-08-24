#include "lineup/player.hpp"

#include <cmath>
#include <charconv>
#include <string_view>

namespace lineup {

const char* to_string(Position p) {
    switch (p) {
        case Position::QB: return "QB";
        case Position::RB: return "RB";
        case Position::WR: return "WR";
        case Position::TE: return "TE";
        case Position::DST: return "DST";
    }
    return "??";
}

bool parse_position(std::string_view text, Position& out) {
    if (text == "QB") { out = Position::QB; return true; }
    if (text == "RB") { out = Position::RB; return true; }
    if (text == "WR") { out = Position::WR; return true; }
    if (text == "TE") { out = Position::TE; return true; }
    if (text == "DST" || text == "DEF") { out = Position::DST; return true; }
    return false;
}

bool parse_projection(std::string_view text, int& centipoints_out) {
    if (text.empty()) return false;

    // from_chars for floating point is not available in every standard library
    // shipping today, so parse the fixed-point value directly. This also avoids
    // binary rounding: "18.4" becomes exactly 1840, never 1839.
    bool negative = false;
    std::size_t i = 0;
    if (text[i] == '+' || text[i] == '-') {
        negative = (text[i] == '-');
        ++i;
    }
    if (i >= text.size()) return false;

    long long whole = 0;
    bool saw_digit = false;
    for (; i < text.size() && text[i] != '.'; ++i) {
        if (text[i] < '0' || text[i] > '9') return false;
        whole = whole * 10 + (text[i] - '0');
        saw_digit = true;
        if (whole > 100000000LL) return false;
    }

    int fraction = 0;
    if (i < text.size() && text[i] == '.') {
        ++i;
        int digits = 0;
        int third = 0;
        for (; i < text.size(); ++i) {
            if (text[i] < '0' || text[i] > '9') return false;
            saw_digit = true;
            if (digits < 2) {
                fraction = fraction * 10 + (text[i] - '0');
            } else if (digits == 2) {
                third = text[i] - '0';
            }
            ++digits;
        }
        while (digits < 2) { fraction *= 10; ++digits; }
        if (third >= 5) ++fraction;  // round half away from zero
    }

    if (!saw_digit) return false;

    long long total = whole * 100 + fraction;
    centipoints_out = static_cast<int>(negative ? -total : total);
    return true;
}

}  // namespace lineup
