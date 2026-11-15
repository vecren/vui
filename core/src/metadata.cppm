export module vui:metadata;
import rstd;
using namespace rstd::prelude;

export namespace vui::detail {
class FramePayload {
    struct Value {
        virtual ~Value()=default;
        virtual auto type() const -> rstd::any::TypeId=0;
    };
    template<class T> struct Stored : Value {
        T value;
        explicit Stored(T next):value(rstd::move(next)){}
        auto type() const -> rstd::any::TypeId override {return rstd::any::TypeId::of<T>();}
    };
    Box<Value> value_;
    explicit FramePayload(Box<Value> value):value_(rstd::move(value)){}
  public:
    FramePayload(FramePayload &&)=default;
    auto operator=(FramePayload &&) -> FramePayload & =default;
    template<class T> static auto make(T value) -> FramePayload {
        auto stored=Box<Stored<T>>::make(rstd::move(value));
        return FramePayload(Box<Value>::from_raw(rstd::move(stored).into_raw().template cast<Value>()));
    }
    template<class T> auto get() const -> const T * {
        if(value_->type()!=rstd::any::TypeId::of<T>())return nullptr;
        return &static_cast<const Stored<T> &>(*value_).value;
    }
};
class FrameMetadata {
    struct Data {
        FramePayload value;
        bool shared{};
        Data(FramePayload next,bool concurrent):value(rstd::move(next)),shared(concurrent){}
    };
    rstd::sync::Arc<Data> data_;
    explicit FrameMetadata(rstd::sync::Arc<Data> data):data_(rstd::move(data)){}
  public:
    auto clone() const -> FrameMetadata {return FrameMetadata(data_.clone());}
    auto operator==(const FrameMetadata &other) const -> bool {return &*data_==&*other.data_;}
    template<class T> static auto make(T value,bool concurrent_read) -> FrameMetadata {return FrameMetadata(rstd::sync::Arc<Data>::make(FramePayload::make(rstd::move(value)),concurrent_read));}
    template<class T> auto get() const -> const T * {return data_->value.get<T>();}
    auto concurrent_read() const -> bool {return data_->shared;}
};
}
