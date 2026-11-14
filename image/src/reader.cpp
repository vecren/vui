module;
#include <cstddef>
#include <cstdlib>

namespace {
struct Budget {
    std::size_t used{}, limit{};
    bool exceeded{};
};
thread_local Budget decode_budget;
struct alignas(std::max_align_t) Allocation {
    std::size_t size;
};
auto budget_allocate(std::size_t size) -> void * {
    if (size > decode_budget.limit || sizeof(Allocation) > decode_budget.limit - size ||
        decode_budget.used > decode_budget.limit - size - sizeof(Allocation)) {
        decode_budget.exceeded = true;
        return nullptr;
    }
    auto block = static_cast<Allocation *>(std::malloc(size + sizeof(Allocation)));
    if (!block)
        return nullptr;
    block->size = size;
    decode_budget.used += size + sizeof(Allocation);
    return block + 1;
}
void budget_free(void *value) {
    if (!value)
        return;
    auto block = static_cast<Allocation *>(value) - 1;
    decode_budget.used -= block->size + sizeof(Allocation);
    std::free(block);
}
auto budget_resize(void *value, std::size_t size) -> void * {
    if (!value)
        return budget_allocate(size);
    auto previous = static_cast<Allocation *>(value) - 1;
    auto used = decode_budget.used - previous->size - sizeof(Allocation);
    if (size > decode_budget.limit || sizeof(Allocation) > decode_budget.limit - size ||
        used > decode_budget.limit - size - sizeof(Allocation)) {
        decode_budget.exceeded = true;
        return nullptr;
    }
    auto block = static_cast<Allocation *>(std::realloc(previous, size + sizeof(Allocation)));
    if (!block)
        return nullptr;
    block->size = size;
    decode_budget.used = used + size + sizeof(Allocation);
    return block + 1;
}
} // namespace
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_MALLOC(size) budget_allocate(size)
#define STBI_REALLOC_SIZED(value, old_size, size) budget_resize(value, size)
#define STBI_FREE(value) budget_free(value)
#include "../third_party/stb_image.h"

module vui.image;
import :reader;
using namespace rstd::prelude;

namespace vui {
namespace {
auto tag(slice<unsigned char> bytes, usize offset, const char *value, unsigned count) -> bool {
    if (offset.to_primitive() > bytes.len().to_primitive() ||
        count > bytes.len().to_primitive() - offset.to_primitive())
        return false;
    for (unsigned i{}; i < count; ++i)
        if (bytes[offset + usize(i)] != static_cast<unsigned char>(value[i]))
            return false;
    return true;
}
auto metadata(slice<unsigned char> bytes, ImageReadOptions options)
    -> Result<bool, ImageReadError> {
    bool srgb{}, other_color{};
    if (tag(bytes, usize(), "\x89PNG\r\n\x1a\n", 8)) {
        usize cursor(8);
        bool ended{};
        while (cursor < bytes.len()) {
            if (bytes.len() - cursor < usize(12))
                return Err(ImageReadError{ImageReadStage::InvalidInput});
            Serial length{};
            for (unsigned i{}; i < 4; ++i)
                length = (length << 8) | bytes[cursor + usize(i)];
            if (length > bytes.len().to_primitive() - cursor.to_primitive() - 12)
                return Err(ImageReadError{ImageReadStage::InvalidInput});
            auto kind = cursor + usize(4);
            if (tag(bytes, kind, "iCCP", 4) || tag(bytes, kind, "acTL", 4))
                return Err(ImageReadError{ImageReadStage::Unsupported});
            if (tag(bytes, kind, "gAMA", 4) || tag(bytes, kind, "cHRM", 4))
                other_color = true;
            if (tag(bytes, kind, "sRGB", 4)) {
                if (length != 1 || bytes[cursor + usize(8)] > 3)
                    return Err(ImageReadError{ImageReadStage::InvalidInput});
                srgb = true;
            }
            if (tag(bytes, kind, "IEND", 4)) {
                ended = length == 0 && cursor.to_primitive() + 12 == bytes.len().to_primitive();
                break;
            }
            cursor += usize(length + 12);
        }
        if (!ended)
            return Err(ImageReadError{ImageReadStage::InvalidInput});
        if (other_color && !srgb)
            return Err(ImageReadError{ImageReadStage::Unsupported});
    } else if (bytes.len() >= usize(2) && bytes[usize()] == 255 && bytes[usize(1)] == 216) {
        if (bytes.len() < usize(4) || bytes[bytes.len() - usize(2)] != 255 ||
            bytes[bytes.len() - usize(1)] != 217)
            return Err(ImageReadError{ImageReadStage::InvalidInput});
        usize cursor(2);
        while (cursor < bytes.len()) {
            if (bytes[cursor++] != 255)
                return Err(ImageReadError{ImageReadStage::InvalidInput});
            while (cursor < bytes.len() && bytes[cursor] == 255)
                ++cursor;
            if (cursor >= bytes.len())
                return Err(ImageReadError{ImageReadStage::InvalidInput});
            auto marker = bytes[cursor++];
            if (marker == 218 || marker == 217)
                break;
            if (marker >= 208 && marker <= 215)
                continue;
            if (bytes.len() - cursor < usize(2))
                return Err(ImageReadError{ImageReadStage::InvalidInput});
            unsigned length = (unsigned(bytes[cursor]) << 8) | bytes[cursor + usize(1)];
            if (length < 2 || length > (bytes.len() - cursor).to_primitive())
                return Err(ImageReadError{ImageReadStage::InvalidInput});
            if (marker == 226 && tag(bytes, cursor + usize(2), "ICC_PROFILE", 11))
                return Err(ImageReadError{ImageReadStage::Unsupported});
            cursor += usize(length);
        }
    } else
        return Err(ImageReadError{ImageReadStage::Unsupported});
    if (!srgb && options.unmarked_color != UnmarkedImageColor::AssumeSrgb)
        return Err(ImageReadError{ImageReadStage::Color});
    return Ok(srgb);
}
} // namespace
auto ImageReader::read(slice<unsigned char> bytes) const -> Result<ImageData, ImageReadError> {
    const auto &options = options_;
    if (!finite(options.density) || options.density <= 0 || bytes.is_empty())
        return Err(ImageReadError{ImageReadStage::InvalidInput});
    if (bytes.len().to_primitive() > options.max_encoded_bytes ||
        bytes.len().to_primitive() > 0x7fffffff || !options.max_working_bytes)
        return Err(ImageReadError{ImageReadStage::Limit});
    auto color = metadata(bytes, options);
    if (color.is_err())
        return Err(rstd::move(color).unwrap_err());
    struct BudgetGuard {
        Budget previous;
        explicit BudgetGuard(Serial limit) : previous(decode_budget) {
            decode_budget = {0, limit, false};
        }
        ~BudgetGuard() { decode_budget = previous; }
    } budget(options.max_working_bytes);
    int width{}, height{}, channels{};
    auto length = static_cast<int>(bytes.len().to_primitive());
    if (!stbi_info_from_memory(bytes.as_ptr(), length, &width, &height, &channels))
        return Err(ImageReadError{decode_budget.exceeded ? ImageReadStage::Limit
                                                         : ImageReadStage::Decode});
    if (bytes[usize()] == 255 && channels != 1 && channels != 3)
        return Err(ImageReadError{ImageReadStage::Unsupported});
    auto dimensions = ImageData::validate_dimensions(width, height, options.density);
    if (dimensions.is_err())
        return Err(ImageReadError{ImageReadStage::Pixels, {}, Some(dimensions.unwrap_err())});
    Serial output_size = Serial(width) * height * 4;
    if (output_size > options.max_decoded_bytes)
        return Err(ImageReadError{ImageReadStage::Limit});
    auto pixels = stbi_load_from_memory(bytes.as_ptr(), length, &width, &height, &channels, 4);
    if (!pixels)
        return Err(ImageReadError{decode_budget.exceeded ? ImageReadStage::Limit
                                                         : ImageReadStage::Decode});
    struct Pixels {
        unsigned char *value;
        ~Pixels() { stbi_image_free(value); }
    } owned{pixels};
    if (output_size > options.max_working_bytes - decode_budget.used)
        return Err(ImageReadError{ImageReadStage::Limit});
    auto image = ImageData::rgba8_srgb(
        width, height, slice<unsigned char>::from_raw_parts(pixels, usize(output_size)),
        options.density);
    if (image.is_err())
        return Err(ImageReadError{ImageReadStage::Pixels, {}, Some(image.unwrap_err())});
    return Ok(rstd::move(image).unwrap());
}

auto ImageReader::read(ref<rstd::path::Path> path) const -> Result<ImageData, ImageReadError> {
    auto opened = rstd::fs::File::open(path);
    if (opened.is_err())
        return Err(ImageReadError{ImageReadStage::Io, Some(rstd::move(opened).unwrap_err())});
    auto file = rstd::move(opened).unwrap();
    Vec<unsigned char> bytes;
    rstd::byte chunk[4096]{};
    while (true) {
        auto read = file.read(
            rstd::as_u8_slice_mut(mut_ref<rstd::byte[]>::from_raw_parts(chunk, usize(4096))));
        if (read.is_err())
            return Err(ImageReadError{ImageReadStage::Io, Some(rstd::move(read).unwrap_err())});
        auto count = read.unwrap();
        if (count == usize())
            break;
        if (bytes.len().to_primitive() > options_.max_encoded_bytes ||
            count.to_primitive() > options_.max_encoded_bytes - bytes.len().to_primitive())
            return Err(ImageReadError{ImageReadStage::Limit});
        bytes.extend_from_slice(reinterpret_cast<const unsigned char *>(chunk), count);
    }
    return this->read(bytes.as_slice());
}
} // namespace vui
