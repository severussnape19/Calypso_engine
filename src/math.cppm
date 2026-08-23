module;

#include <cmath>
#include <concepts>
#include <cassert>
#include <iomanip>
#include <ios>
#include <ostream>
#include <experimental/simd>

export module math;

import types;

export template <std::floating_point T = f32>
struct Vec2 {
public:
    constexpr Vec2() noexcept : x(static_cast<T>(0)), y(static_cast<T>(0)) {}

    template <typename U> requires std::convertible_to<U, T>
    constexpr Vec2(U x, U y) noexcept
        : x(static_cast<T>(x)),
          y(static_cast<T>(y)) {}

    constexpr auto perp_dot(Vec2 const& rhs) const noexcept -> T {
        return x * rhs.y - y * rhs.x;
    }
public:
    T x;
    T y;
};

export template <std::floating_point T>
constexpr auto deg_to_rad(T degrees) noexcept -> T {
    return degrees * (std::numbers::pi_v<T> / static_cast<T>(180));
}

export template <std::floating_point T = f32>
struct Vec3 {
public:
    constexpr Vec3() noexcept
        : x(static_cast<T>(0))
        , y(static_cast<T>(0))
        , z(static_cast<T>(0)) {}

    constexpr Vec3(T scalar) noexcept
        : x(scalar), y(scalar), z(scalar) {}

    constexpr Vec3(T vx, T vy = 0, T vz = 0) noexcept
        : x(vx), y(vy), z(vz) {}

    template <typename U> requires std::convertible_to<U, T>
    constexpr Vec3(Vec2<U> const& xy, T vz = 0) noexcept
        : x(static_cast<T>(xy.x))
        , y(static_cast<T>(xy.y))
        , z(vz) {}

    constexpr auto operator/(T scalar) const noexcept -> Vec3 {
        assert(scalar != static_cast<T>(0));
        return Vec3(x / scalar, y / scalar, z / scalar);
    }

    constexpr auto operator*(T scalar) const noexcept -> Vec3 {
        return Vec3(x * scalar, y * scalar, z * scalar);
    }

    [[nodiscard]] constexpr auto cross(Vec3 const& rhs) const noexcept -> Vec3 {
        return Vec3(
            y * rhs.z - z * rhs.y,
            z * rhs.x - x * rhs.z,
            x * rhs.y - y * rhs.x
        );
    }

    [[nodiscard]] constexpr auto dot(Vec3 const& rhs) const noexcept -> T {
        return x * rhs.x + y * rhs.y + z * rhs.z;
    }

    [[nodiscard]] constexpr auto length_sq() const noexcept -> T {
        return x*x + y*y + z*z;
    }

    [[nodiscard]] constexpr auto length() const noexcept -> T {
        auto length_squared = this->length_sq();
        assert(length_squared != static_cast<T>(0));
        return std::sqrt(length_squared);
    }

     [[nodiscard]] constexpr auto normalized() const noexcept -> Vec3 {
        auto len = this->length();
        assert(len != static_cast<T>(0));

        T inv_len = static_cast<T>(1) / len;
        return *this * inv_len;
    }

public:
    T x;
    T y;
    T z;
};

export template <std::floating_point T = f32>
constexpr auto operator-(Vec3<T> const& a, Vec3<T> const& b) noexcept -> Vec3<T> {
    return Vec3(a.x - b.x, a.y - b.y, a.z - b.z);
}

export template <std::floating_point T = f32>
constexpr auto operator+(Vec3<T> const& a, Vec3<T> const& b) noexcept -> Vec3<T> {
    return Vec3(a.x + b.x, a.y + b.y, a.z + b.z);
}

export template <std::floating_point T = f32>
constexpr auto dot(Vec3<T> const& a, Vec3<T> const& b) noexcept -> T {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

export template <std::floating_point T = f32>
auto operator<<(std::ostream& os, Vec3<T> const& v) -> std::ostream& {
    std::ios_base::fmtflags old_flags = os.flags();
    os << std::fixed << std::setprecision(4);
    os << "[ " << v.x << ", " << v.y << ", " << v.z << " ]";
    os.flags(old_flags);
    return os;
}

export template <std::floating_point T = f32>
struct alignas(16) Vec4 { // vulkan specifies that 4d vectors and matrices need to be 16 byte aligned
public:
    constexpr Vec4() noexcept
        : x(static_cast<T>(0))
        , y(static_cast<T>(0))
        , z(static_cast<T>(0))
        , w(static_cast<T>(0)) {}

    // 4th co-ordinate is homogeneous. w = 1 indicates that it is a point. w = 0 indicates its a direction.
    template <typename U> requires std::convertible_to<U, T>
    constexpr Vec4(Vec3<U> const& v, T w = 0) noexcept
        : x(static_cast<T>(v.x))
        , y(static_cast<T>(v.y))
        , z(static_cast<T>(v.z))
        , w(w) {}

    constexpr Vec4(T scalar) noexcept
        : x(scalar)
        , y(scalar)
        , z(scalar)
        , w(scalar) {}

    // 4th co-ordinate is homogeneous. w = 1 indicates that it is a point. w = 0 indicates its a direction.
    template <typename U> requires std::convertible_to<U, T>
    constexpr Vec4(U x = 0, U y = 0, U z = 0, U w = 0) noexcept
        : x(static_cast<T>(x))
        , y(static_cast<T>(y))
        , z(static_cast<T>(z))
        , w(static_cast<T>(w)) {}

    constexpr auto operator*(T scalar) const noexcept -> Vec4 {
        return Vec4(x*scalar, y*scalar, z*scalar, w*scalar);
    }

    constexpr auto operator/(T scalar) const noexcept -> Vec4 {
        assert(scalar != static_cast<T>(0));
        T inv_scalar = static_cast<T>(1) / scalar;
        return Vec4(x*inv_scalar, y*inv_scalar, z*inv_scalar, w*inv_scalar);
    }

    constexpr auto operator/=(T scalar) noexcept -> Vec4& {
        assert(scalar != static_cast<T>(0));
        T inv_scalar = static_cast<T>(1) / scalar;
        *this = Vec4(x*inv_scalar, y*inv_scalar, z*inv_scalar, w*inv_scalar);
        return *this;
    }

    [[nodiscard]] constexpr auto cross(Vec4<T> const& rhs) const noexcept -> Vec4 {
        return Vec4(
            y * rhs.z - z * rhs.y,
            z * rhs.x - x * rhs.z,
            x * rhs.y - y * rhs.x,
            static_cast<T>(0) // 4D cross doesnt exist we need it cuz homogeneous co-ords.
        );
    }

    [[nodiscard]] constexpr auto dot(Vec4<T> const& rhs) const noexcept -> T {
        return x*rhs.x + y*rhs.y + z*rhs.z + w*rhs.w;
    }

    [[nodiscard]] constexpr auto length_sq() const noexcept -> T {
        return x*x + y*y + z*z + w*w;
    }

    [[nodiscard]] constexpr auto length() const noexcept -> T {
        auto length_squared = this->length_sq();
        assert(length_squared != static_cast<T>(0));
        return std::sqrt(length_squared);
    }

    [[nodiscard]] constexpr auto normalized() const noexcept {
        auto len = this->length();
        assert(len != static_cast<T>(0));

        T inv_len = static_cast<T>(1) / len;
        return *this * inv_len;
    }
public:
    T x, y, z, w;
};

export template <std::floating_point T = f32>
constexpr auto operator-(Vec4<T> const& a, Vec4<T> const& b) noexcept -> Vec4<T> {
    return Vec4(a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w);
}

export template <std::floating_point T = f32>
constexpr auto operator+(Vec4<T> const& a, Vec4<T> const& b) noexcept -> Vec4<T> {
    return Vec4(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w);
}

export template <std::floating_point T = f32>
auto operator<<(std::ostream& os, Vec4<T> const& v) -> std::ostream& {
    std::ios_base::fmtflags old_flags = os.flags();
    os << std::fixed << std::setprecision(4);
    os << "[ " << v.x << ", " << v.y << ", " << v.z << ", " << v.w << " ]";
    os.flags(old_flags);
    return os;
}

export template <std::floating_point T = f32>
struct alignas(16) Mat4 {
/*
> data is stored in column major order. follow the fkn order.
*/
public:
    constexpr Mat4() noexcept = default;

    template <typename U> requires std::convertible_to<U, T>
    constexpr Mat4(
        Vec4<U> const& x,
        Vec4<U> const& y,
        Vec4<U> const& z,
        Vec4<U> const& w
    ) noexcept {
        data[0]  = static_cast<T>(x.x);
        data[1]  = static_cast<T>(x.y);
        data[2]  = static_cast<T>(x.z);
        data[3]  = static_cast<T>(x.w);

        data[4]  = static_cast<T>(y.x);
        data[5]  = static_cast<T>(y.y);
        data[6]  = static_cast<T>(y.z);
        data[7]  = static_cast<T>(y.w);

        data[8]  = static_cast<T>(z.x);
        data[9]  = static_cast<T>(z.y);
        data[10] = static_cast<T>(z.z);
        data[11] = static_cast<T>(z.w);

        data[12] = static_cast<T>(w.x);
        data[13] = static_cast<T>(w.y);
        data[14] = static_cast<T>(w.z);
        data[15] = static_cast<T>(w.w);
    }

    template <typename U> requires std::convertible_to<U, T>
    constexpr Mat4(
        Vec4<U> const& x,
        Vec4<U> const& y,
        Vec4<U> const& z
    ) noexcept {
        data[0]  = static_cast<T>(x.x);
        data[1]  = static_cast<T>(x.y);
        data[2]  = static_cast<T>(x.z);
        data[3]  = static_cast<T>(x.w);

        data[4]  = static_cast<T>(y.x);
        data[5]  = static_cast<T>(y.y);
        data[6]  = static_cast<T>(y.z);
        data[7]  = static_cast<T>(y.w);

        data[8]  = static_cast<T>(z.x);
        data[9]  = static_cast<T>(z.y);
        data[10] = static_cast<T>(z.z);
        data[11] = static_cast<T>(z.w);

        data[12] = static_cast<T>(0);
        data[13] = static_cast<T>(0);
        data[14] = static_cast<T>(0);
        data[15] = static_cast<T>(1);
    }

    // data is stored in column major order. follow the fkn order.
    [[nodiscard]] constexpr static auto projection_matrix() noexcept -> Mat4 {
        Mat4 m{};
        m.data[0]  = static_cast<T>(1);
        m.data[5]  =  static_cast<T>(1);
        m.data[10] =  static_cast<T>(1);
        m.data[11] = static_cast<T>(-1); // idx is 14 if you followed row major
        return m;
    }

    [[nodiscard]] constexpr static auto identity_matrix() noexcept -> Mat4 {
        Mat4 m{};
        m.data[0]  = static_cast<T>(1);
        m.data[5]  = static_cast<T>(1);
        m.data[10] = static_cast<T>(1);
        m.data[15] = static_cast<T>(1);
        return m;
    }

    [[nodiscard]] constexpr static auto translation_matrix(
        T tx, T ty, T tz
    ) noexcept -> Mat4 {
        Mat4 m{};
        m.data[12] = tx;
        m.data[13] = ty;
        m.data[14] = tz;

        m.data[0]  = static_cast<T>(1);
        m.data[5]  = static_cast<T>(1);
        m.data[10] = static_cast<T>(1);
        m.data[15] = static_cast<T>(1);

        return m;
    }

    [[nodiscard]] constexpr static auto rotation_x(T angle) noexcept -> Mat4 {
        Mat4 m{};
        T sin_theta = std::sin(angle);
        T cos_theta = std::cos(angle);
        m.data[0]   = static_cast<T>(1);
        m.data[5]   = cos_theta;
        m.data[6]   = sin_theta;
        m.data[9]   = -sin_theta;
        m.data[10]  = cos_theta;
        m.data[15]  = static_cast<T>(1);
        return m;
    }

    [[nodiscard]] constexpr static auto rotation_y(T angle) noexcept -> Mat4 {
        Mat4 m{};
        T sin_theta = std::sin(angle);
        T cos_theta = std::cos(angle);
        m.data[0]   = cos_theta;
        m.data[2]   = -sin_theta;
        m.data[5]   = static_cast<T>(1);
        m.data[8]   = sin_theta;
        m.data[10]  = cos_theta;
        m.data[15]  = static_cast <T>(1);
        return m;
    }

    [[nodiscard]] constexpr static auto rotation_z(T angle) noexcept -> Mat4 {
        Mat4 m{};
        T sin_theta = std::sin(angle);
        T cos_theta = std::cos(angle);
        m.data[0]   = cos_theta;
        m.data[1]   = sin_theta;
        m.data[4]   = -sin_theta;
        m.data[5]   = cos_theta;
        m.data[10]  = static_cast <T>(1);
        m.data[15]  = static_cast <T>(1);
        return m;
    }

    [[nodiscard]] constexpr static auto scale(T sx, T sy, T sz) noexcept -> Mat4 {
        Mat4 m{};
        m.data[0]  = sx;
        m.data[5]  = sy;
        m.data[10] = sz;
        m.data[15] = static_cast<T>(1);
        return m;
    }

    [[nodiscard]] constexpr static auto perspective(T fov_y_radians, T aspect, T near, T far) noexcept -> Mat4 {
        T f = static_cast<T>(1) / std::tan(fov_y_radians / static_cast<T>(2));
        Mat4 m{};
        m.data[0]  = f / aspect;
        m.data[5]  = -f;
        m.data[10] = far / (near - far);
        m.data[11] = -static_cast<T>(1);
        m.data[14] = (far * near) / (near - far);
        return m;
    }
    // data is stored in column major order. follow the fkn order.
    constexpr auto operator*(Mat4<T> const& rhs) const noexcept -> Mat4 {
        Mat4<T> m{};
        for (usize col{}; col < 4; ++col) {
            for (usize row{}; row < 4; ++row) {
                m.data[col * 4 + row] =
                    data[0 * 4 + row] * rhs.data[col * 4 + 0] +
                    data[1 * 4 + row] * rhs.data[col * 4 + 1] +
                    data[2 * 4 + row] * rhs.data[col * 4 + 2] +
                    data[3 * 4 + row] * rhs.data[col * 4 + 3];
            }
        }
        return m;
    }

    // data is stored in column major order. follow the fkn order.
    constexpr auto operator*(Vec4<T> const& rhs) const noexcept -> Vec4<T> {
        Vec4<T> v{};
        v.x = data[0] * rhs.x + data[4] * rhs.y + data[8] * rhs.z + data[12] * rhs.w;
        v.y = data[1] * rhs.x + data[5] * rhs.y + data[9] * rhs.z + data[13] * rhs.w;
        v.z = data[2] * rhs.x + data[6] * rhs.y + data[10] * rhs.z + data[14] * rhs.w;
        v.w = data[3] * rhs.x + data[7] * rhs.y + data[11] * rhs.z + data[15] * rhs.w;
        return v;
    }
public:
    std::array<T, 16> data{};
};

export template <std::floating_point T = f32>
constexpr auto operator<<(std::ostream& os, Mat4<T> const& m) -> std::ostream& {
    std::ios_base::fmtflags old_flags = os.flags();

    os << std::fixed << std::setprecision(4);

    os << "Mat4([\n";
    for (usize row{}; row < 4; ++row) {
        os << "  ";
        for (usize col{}; col < 4; ++col) {
            os << std::setw(10) << m.data[col * 4 + row];
            if (col < 3) {
                os << ", ";
            }
        }
        os << '\n';
    }
    os << ")]";

    os.flags(old_flags);
    return os;
}

export using Vec2f = Vec2<f32>;
export using Vec3f = Vec3<f32>;
export using Vec4f = Vec4<f32>;
export using Mat4f = Mat4<f32>;

export using Color = Vec3<f32>;
export using Point = Vec3<f32>;
