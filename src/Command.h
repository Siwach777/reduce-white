#pragma once

#include <cmath>
#include <string_view>
#ifdef REDUCE_WHITE_HAS_FLOAT_FROM_CHARS
#include <charconv>
#include <system_error>
#else
#include <QLocale>
#include <QString>
#endif

inline std::string_view trimCommand(std::string_view value) {
    constexpr std::string_view whitespace = " \t\r\n";
    const auto first = value.find_first_not_of(whitespace);
    if (first == std::string_view::npos) return {};
    return value.substr(first, value.find_last_not_of(whitespace) - first + 1);
}

inline bool parseLevel(std::string_view text, double &value, bool isStep = false) {
    if (text.empty()) return false;
#ifdef REDUCE_WHITE_HAS_FLOAT_FROM_CHARS
    if (text.front() == '+') {
        text.remove_prefix(1);
        if (!text.empty() && text.front() == '-') return false;
    }
    if (text.empty()) return false;
    double parsed = 0.0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc() || result.ptr != text.data() + text.size() ||
        !std::isfinite(parsed)) {
        return false;
    }
#else
    // Older Apple libc++ versions have no floating-point from_chars overload.
    // Qt's C locale keeps the wire format independent of the user's locale.
    if (trimCommand(text) != text || text.size() > 255) return false;
    static const QLocale locale = [] {
        QLocale result = QLocale::c();
        result.setNumberOptions(QLocale::RejectGroupSeparator);
        return result;
    }();
    bool valid = false;
    const double parsed = locale.toDouble(QString::fromLatin1(text.data(), static_cast<int>(text.size())), &valid);
    if (!valid || !std::isfinite(parsed)) return false;
#endif
    if (parsed < 0.0 || parsed > 1.0 || (isStep && parsed == 0.0)) return false;
    value = parsed;
    return true;
}
