module vui.image:file;
import :reader;
using namespace rstd::prelude;

namespace vui {
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
