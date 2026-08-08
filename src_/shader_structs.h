#ifndef SHADER_STRUCTS_H
#ifndef __SLANG__
    #include <cstdint>

    template <typename T> struct Vec4;
    template <typename T> struct Mat4;

    using float4   = Vec4<float>;
    using float4x4 = Mat4<float>;
    using uint     = std::uint32_t;
#endif
#endif
