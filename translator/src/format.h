#pragma once

#if defined(__cpp_lib_format)
#include <format>
#if defined(__cpp_lib_print)
#include <print>
#endif
namespace translator
{
	using std::format;
#if defined(__cpp_lib_print)
	using std::print;
	using std::println;
#endif
}
#else
#define FMT_HEADER_ONLY
#include <fmt/format.h>
namespace translator
{
	using fmt::format;
	using fmt::print;
	using fmt::println;
}
#endif
