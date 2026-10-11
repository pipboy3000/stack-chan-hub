/**
 * @file vector.hpp
 * @author Forairaaaaa
 * @brief
 * @version 0.1
 * @date 2025-08-15
 *
 * @copyright Copyright (c) 2025
 *
 */
// ref: https://github.com/godotengine/godot/blob/master/core/math/vector2.h
#pragma once
#include "base.hpp"
#include <cmath>

namespace uitk_intl {

/**
 * @brief
 *
 */
template <typename T>
struct Vector2Base {
    union {
        struct {
            T x;
            T y;
        };

        struct {
            T width;
            T height;
        };

        T coord[2] = {0};
    };

    Vector2Base() : x(0), y(0) {}
    Vector2Base(T x, T y) : x(x), y(y) {}
    Vector2Base(const Vector2Base& other) : x(other.x), y(other.y) {}

    Vector2Base& operator=(const Vector2Base& other)
    {
        if (this != &other) {
            x = other.x;
            y = other.y;
        }
        return *this;
    }

    void set(const T& new_x, const T& new_y)
    {
        x = new_x;
        y = new_y;
    }

    Vector2Base clamp(const Vector2Base& p_min, const Vector2Base& p_max)
    {
        return Vector2Base(uitk_intl::clamp(x, p_min.x, p_max.x),
                           uitk_intl::clamp(y, p_min.y, p_max.y));
    }
};

/**
 * @brief A 2D vector using floating-point coordinates.
 *
 */
struct Vector2 : public Vector2Base<float> {
    Vector2() : Vector2Base<float>() {}
    Vector2(float x, float y) : Vector2Base<float>(x, y) {}
    Vector2(const Vector2Base<float>& other) : Vector2Base<float>(other) {}

    // 从整数向量转换
    Vector2(const Vector2Base<int>& other)
        : Vector2Base<float>(static_cast<float>(other.x), static_cast<float>(other.y))
    {
    }

    // 显式转换操作符
    explicit operator Vector2Base<int>() const
    {
        return Vector2Base<int>(static_cast<int>(x), static_cast<int>(y));
    }

    // Add
    Vector2 operator+(const Vector2& other) const
    {
        return Vector2(x + other.x, y + other.y);
    }

    // Subtract
    Vector2 operator-(const Vector2& other) const
    {
        return Vector2(x - other.x, y - other.y);
    }

    // Multiply by scalar
    Vector2 operator*(float scalar) const
    {
        return Vector2(x * scalar, y * scalar);
    }

    // += operator
    Vector2& operator+=(const Vector2& other)
    {
        x += other.x;
        y += other.y;
        return *this;
    }

    // -= operator
    Vector2& operator-=(const Vector2& other)
    {
        x -= other.x;
        y -= other.y;
        return *this;
    }

    // Divide by scalar
    Vector2 operator/(float scalar) const
    {
        return Vector2(x / scalar, y / scalar);
    }

    float length() const
    {
        return std::sqrt(x * x + y * y);
    }

    Vector2 normalized() const
    {
        float len = length();
        if (len == 0.0f) {
            return Vector2(0.0f, 0.0f);
        }
        return Vector2(x / len, y / len);
    }
};

/**
 * @brief A 2D vector using integer coordinates.
 *
 */
struct Vector2i : public Vector2Base<int> {
    Vector2i() : Vector2Base<int>() {}
    Vector2i(int x, int y) : Vector2Base<int>(x, y) {}
    Vector2i(const Vector2Base<int>& other) : Vector2Base<int>(other) {}

    // 从浮点向量转换
    Vector2i(const Vector2Base<float>& other) : Vector2Base<int>(static_cast<int>(other.x), static_cast<int>(other.y))
    {
    }

    // 显式转换操作符
    explicit operator Vector2Base<float>() const
    {
        return Vector2Base<float>(static_cast<float>(x), static_cast<float>(y));
    }
};

} // namespace smooth_ui_toolkit
