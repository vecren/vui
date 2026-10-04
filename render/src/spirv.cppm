export module vui.render.spirv;

export namespace vui::gpu::shaders {
template <decltype(sizeof(0)) N> struct Code {
    unsigned words[N];
};

template <decltype(sizeof(0)) N>
consteval auto decode(const unsigned char (&bytes)[N]) {
    static_assert(sizeof(unsigned) == 4);
    static_assert(N >= 20 && N % 4 == 0);
    Code<N / 4> result{};
    for (decltype(N) i = 0; i < N / 4; ++i) {
        result.words[i] = static_cast<unsigned>(bytes[i * 4]) |
                          (static_cast<unsigned>(bytes[i * 4 + 1]) << 8) |
                          (static_cast<unsigned>(bytes[i * 4 + 2]) << 16) |
                          (static_cast<unsigned>(bytes[i * 4 + 3]) << 24);
    }
    return result;
}
}
