/**
 * @file ease.cpp
 * @author Forairaaaaa
 * @brief
 * @version 0.1
 * @date 2025-01-06
 *
 * @copyright Copyright (c) 2025
 *
 */
#include "ease.hpp"
#include <cmath>

using namespace uitk_intl;

float ease::ease_in_out_quad(float t)
{
    return t < 0.5 ? 2 * t * t : 1 - std::pow(-2 * t + 2, 2) / 2;
};
