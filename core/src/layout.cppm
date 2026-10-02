export module vui:layout;
import :types;
using namespace rstd::prelude;

export namespace vui {
enum class Alignment { Start, Center, End, Stretch };
inline auto valid_alignment(Alignment value) -> bool {
    return value == Alignment::Start || value == Alignment::Center || value == Alignment::End ||
           value == Alignment::Stretch;
}
struct LayoutConstraints {
    Option<Scalar> max_width, max_height;
    Scalar min_width{}, min_height{};
    friend auto operator==(const LayoutConstraints &, const LayoutConstraints &) -> bool = default;
    auto valid() const -> bool {
        return Size{min_width, min_height}.valid() &&
               (max_width.is_none() || (finite(*max_width) && *max_width >= min_width)) &&
               (max_height.is_none() || (finite(*max_height) && *max_height >= min_height));
    }
};
struct LayoutStyle {
    Option<Scalar> width, height;
    Scalar gap{}, padding{};
    Scalar min_width{}, min_height{};
    Option<Scalar> max_width, max_height;
    Alignment horizontal{Alignment::Start}, vertical{Alignment::Start};
    friend auto operator==(const LayoutStyle &, const LayoutStyle &) -> bool = default;
    auto valid() const -> bool {
        return (width.is_none() || (finite(*width) && *width >= 0)) &&
               (height.is_none() || (finite(*height) && *height >= 0)) && finite(gap) &&
               finite(padding) && gap >= 0 && padding >= 0 &&
               LayoutConstraints{max_width, max_height, min_width, min_height}.valid() &&
               valid_alignment(horizontal) && valid_alignment(vertical);
    }
};
enum class LayoutIssue { ConstraintConflict, Strategy, MissingChild, InvalidChild, Limit };
struct LayoutDiagnostic {
    UiId node;
    LayoutIssue issue;
};
} // namespace vui
