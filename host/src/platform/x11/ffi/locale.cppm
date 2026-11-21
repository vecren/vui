module;
#include <locale.h>
#include <wchar.h>

inline constexpr auto alias_LC_CTYPE_MASK = LC_CTYPE_MASK;

#undef LC_CTYPE_MASK

module vui.host:platform.x11.ffi.locale;
namespace ffi {
using ::locale_t;
using ::mbstate_t;
using ::newlocale;
using ::freelocale;
using ::uselocale;
using ::mbrtowc;

inline constexpr auto LC_CTYPE_MASK = alias_LC_CTYPE_MASK;
}
