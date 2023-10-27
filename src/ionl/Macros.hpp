#pragma once

#define STRINGIFY_IMPL(text) #text
#define STRINGIFY(text) STRINGIFY_IMPL(text)

#define CONCAT_IMPL(a, b) a##b
#define CONCAT(a, b) CONCAT_IMPL(a, b)
#define CONCAT_3(a, b, c) CONCAT(a, CONCAT(b, c))
#define CONCAT_4(a, b, c, d) CONCAT(CONCAT(a, b), CONCAT(c, d))

#define UNIQUE_NAME(prefix) CONCAT(prefix, __COUNTER__)
#define UNIQUE_NAME_LINE(prefix) CONCAT(prefix, __LINE__)
#define DISCARD UNIQUE_NAME(_discard)

#define UNUSED(x) (void)x;

#define PRINTF_STRING_VIEW(s) (int)s.size(), s.data()

#if defined(_MSC_VER)
#	define UNREACHABLE __assume(0)
#elif defined(__GNUC__) || defined(__clang__)
#	define UNREACHABLE __builtin_unreachable()
#else
#	define UNREACHABLE
#endif

#if _WIN32
#	define PLATFORM_PATH_STR "%ls"
#else
#	define PLATFORM_PATH_STR "%s"
#endif
