#ifndef CORE_LIB_COMMON_H
#define CORE_LIB_COMMON_H

#include <cstdint>

#ifdef __GNUC__
#define CORE_LIB_ALIGN_16(x) x __attribute__((aligned(16)))
#else
#define CORE_LIB_ALIGN_16(x) __declspec(align(16)) x
#endif

#define VARIADIC_TEMPLATE

namespace Slang
{
	typedef int64_t Int64;
	typedef unsigned short Word;
#ifdef _M_X64
	typedef int64_t PtrInt;
#else
	typedef int PtrInt;
#endif

	template <typename T>
	inline T&& _Move(T & obj)
	{
		return static_cast<T&&>(obj);
	}

	template <typename T>
	inline void Swap(T & v0, T & v1)
	{
		T tmp = _Move(v0);
		v0 = _Move(v1);
		v1 = _Move(tmp);
	}

#ifdef _MSC_VER
#define SLANG_RETURN_NEVER __declspec(noreturn)
#else
#efine SLANG_RETURN_NEVER /* empty */
#endif

    SLANG_RETURN_NEVER void signalUnexpectedError(char const* message);
}

#define SLANG_UNEXPECTED(reason) \
    Slang::signalUnexpectedError("unexpected: " reason)

#define SLANG_UNIMPLEMENTED_X(what) \
    Slang::signalUnexpectedError("unimplemented: " what)

#define SLANG_UNREACHABLE(msg) \
    Slang::signalUnexpectedError("unreachable code executed: " msg)

#ifdef _DEBUG
#define SLANG_EXPECT(VALUE, MSG) if(VALUE) {} else Slang::signalUnexpectedError("assertion failed: '" MSG "'")
#define SLANG_ASSERT(VALUE) SLANG_EXPECT(VALUE, #VALUE)
#else
#define SLANG_EXPECT(VALUE, MSG) do {} while(0)
#define SLANG_ASSERT(VALUE) do {} while(0)
#endif

#define SLANG_RELEASE_ASSERT(VALUE) if(VALUE) {} else Slang::signalUnexpectedError("assertion failed")
#define SLANG_RELEASE_EXPECT(VALUE, WHAT) if(VALUE) {} else SLANG_UNEXPECTED(WHAT)


#endif
