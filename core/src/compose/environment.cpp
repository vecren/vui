module vui.compose;
import :environment;
using namespace rstd::prelude;

namespace vui::compose {
auto LocalBinding::same_value(const LocalBinding &other) const -> bool {
    return identity_ == other.identity_ &&
        (value_.get() == other.value_.get() || value_->equal(*other.value_));
}

auto CompositionEnvironment::resolve(const LocalBinding &fallback) const -> LocalBinding {
    if (data_) for (const auto &value : data_->values)
        if (value.identity_ == fallback.identity_) return value;
    return fallback;
}

auto CompositionEnvironment::extended(slice<LocalBinding> values) const -> ComposeResult<CompositionEnvironment> {
    if (values.is_empty()) return Ok(CompositionEnvironment(*this));
    if (values.len() > usize(4096)) return Err(ComposeError(ComposeStage::Limit));
    for (usize i{}; i < values.len(); ++i)
        for (usize j{}; j < i; ++j)
            if (values[i].identity_ == values[j].identity_) return Err(ComposeError(ComposeStage::Key));
    CompositionEnvironment result;
    result.data_ = rstd::rc::make_rc<LocalEnvironmentData>();
    if (data_) for (const auto &value : data_->values)
        result.data_.get()->values.push(LocalBinding(value));
    for (const auto &value : values) {
        bool replaced{};
        for (auto &previous : result.data_.get()->values)
            if (previous.identity_ == value.identity_) { previous = value; replaced = true; break; }
        if (!replaced) result.data_.get()->values.push(LocalBinding(value));
    }
    if (result.data_->values.len() > usize(4096)) return Err(ComposeError(ComposeStage::Limit));
    return Ok(rstd::move(result));
}

auto CompositionEnvironment::changed(slice<LocalRead> reads) const -> bool {
    for (const auto &read : reads)
        if (!resolve(read.fallback).same_value(read.value)) return true;
    return false;
}
}
