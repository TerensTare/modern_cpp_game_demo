
#pragma once

#include <string_view>

inline std::string_view trim_func_name(char const *func_name) noexcept
{
#ifndef _MSC_VER
#warning \
    "Short function names only work on MSVC for now, on other compilers it will return the full string, untrimmed.\n" \
    "If you know how std::source_location reports function_name() on other compilers, please submit a PR."

    return func_name;
#endif

#define start_str "__cdecl "

    auto start = strstr(func_name, start_str);
    start += std::size(start_str) - 1; // - 1 because of the '\0'

    auto end = strchr(start, '(');

    return std::string_view{start, end};

#undef start_str
}