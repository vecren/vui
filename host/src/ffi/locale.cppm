module;
#include <locale.h>
#include <wchar.h>

namespace vui_locale_constants {
inline constexpr auto ctype_mask = LC_CTYPE_MASK;
}

export module vui.host:ffi.locale;

export namespace vui::host::ffi::locale {
using ::locale_t;
using ::mbstate_t;
using ::newlocale;
using ::freelocale;
using ::uselocale;
using ::mbrtowc;
using ::vui_locale_constants::ctype_mask;
}
