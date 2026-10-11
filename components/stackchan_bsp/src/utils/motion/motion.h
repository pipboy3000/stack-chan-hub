/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "servo.h"
#include "../uitk/src/smooth_ui_toolkit.hpp"
#include <memory>
#include <mutex>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace stackchan::motion {

/**
 * @brief
 *
 */
class Motion {
public:
    ~Motion();

    /**
     * @brief
     *
     */
    void init(std::unique_ptr<Servo> yawServo, std::unique_ptr<Servo> pitchServo);

    /**
     * @brief
     *
     * @param angle
     * @param speed (0-1000)
     */
    void moveYaw(int angle, int speed = 500);
    inline void moveX(int angle, int speed = 500)
    {
        moveYaw(angle, speed);
    }

    /**
     * @brief
     *
     * @param angle
     * @param speed (0-1000)
     */
    void movePitch(int angle, int speed = 500);
    inline void moveY(int angle, int speed = 500)
    {
        movePitch(angle, speed);
    }

    /**
     * @brief
     *
     * @param yawAngle
     * @param pitchAngle
     * @param speed (0-1000)
     */
    void move(int yawAngle, int pitchAngle, int speed = 500);

    /**
     * @brief Move head to home position (0,0)
     *
     * @param speed (0-1000)
     */
    void goHome(int speed = 500);

    /**
     * @brief Stop head movement
     *
     */
    void stop();

    /**
     * @brief Rotate yaw servo with given velocity
     *
     * @param velocity (-1000, 1000)
     * Negative values for clockwise (CW),
     * positive values for counter-clockwise (CCW).
     */
    void rotateYaw(int velocity);
    inline void rotateX(int velocity)
    {
        rotateYaw(velocity);
    }

    /**
     * @brief Moves the head using normalized coordinates ranging from -1.0 to 1.0.
     *
     * This method maps a proportional input to the full physical range of the servos.
     * It is ideal for visual tracking (e.g., centering a face in a camera frame)
     * or joystick-based control.
     *
     * Mapping Logic:
     * - X-axis (Yaw): -1.0 (Max Left) <---> 1.0 (Max Right). 0.0 is center.
     * - Y-axis (Pitch): -1.0 (Max Down) <---> 1.0 (Max Up). 0.0 is the midpoint of the pitch range.
     *
     * @param x Normalized horizontal value [-1.0, 1.0].
     * @param y Normalized vertical value [-1.0, 1.0].
     * @param speed Movement speed from 0 to 1000.
     *
     * @note The actual angles are calculated based on the servo's `getAngleLimit()`.
     *       For example, if Pitch range is 0 to 900, y = -1.0 maps to 0 and y = 1.0 maps to 900.
     */
    void lookAtNormalized(float x, float y, int speed = 500);

    /**
     * @brief Directs the head to look at a target point in 3D Cartesian space.
     *
     * This method performs Inverse Kinematics (IK) to convert 3D coordinates
     * into Yaw and Pitch angles. It assumes the head rotation center is at (0,0,0).
     *
     * Coordinate System (Right-Handed):
     * - X-axis: Forward (positive is in front of the robot).
     * - Y-axis: Lateral (positive is to the left, negative is to the right).
     * - Z-axis: Vertical (positive is up, negative is down).
     *
     * @param x The forward distance from the rotation center (usually in mm).
     * @param y The lateral distance; positive values move the head left (usually in mm).
     * @param z The vertical distance; positive values move the head up (usually in mm).
     * @param speed The movement speed, ranging from 0 (slowest) to 1000 (fastest).
     *
     * @note If the target point is at (0,0,0), the behavior is undefined (mathematical singularity).
     */
    void lookAtPoint(float x, float y, float z, int speed = 500);

    bool isMoving();
    bool isYawMoving();
    inline bool isXMoving()
    {
        return isYawMoving();
    }
    bool isPitchMoving();
    inline bool isYMoving()
    {
        return isPitchMoving();
    }

    uitk_intl::Vector2i getCurrentAngles();
    int getCurrentYawAngle();
    inline int getCurrentXAngle()
    {
        return getCurrentYawAngle();
    }
    int getCurrentPitchAngle();
    inline int getCurrentYAngle()
    {
        return getCurrentPitchAngle();
    }

    void setTorqueEnabled(bool enabled);

    /**
     * @brief Set auto torque release enabled.
     * If enabled, torque will auto release on rest.
     *
     * @param enabled
     */
    void setAutoTorqueReleaseEnabled(bool enabled);

    /**
     * @brief Enables or disables automatic synchronization of the animation start point
     *        with the current physical angle.
     *
     * @param enabled
     *        - If true: Prevents sudden "jumps" when the servo is moved manually by
     *          external forces, but may cause stuttering during high-frequency updates
     *          as it resets the animation's velocity.
     *        - If false: Maintains animation momentum and velocity for smooth,
     *          continuous motion, but may cause a "snap" if the actual angle differs
     *          significantly from the internal state.
     */
    void setAutoAngleSyncEnabled(bool enabled);

    /**
     * @brief Set the current postion as home position (0,0)
     *
     */
    void setCurrentPostionAsHome();

private:
    void update();
    static void update_task(void* param);

    std::unique_ptr<Servo> _yaw_servo;
    std::unique_ptr<Servo> _pitch_servo;

    std::mutex _mutex;
    TaskHandle_t _task_handle{nullptr};
    std::atomic<bool> _running{false};

    static constexpr float _RAD_TO_DEG = 180.0f / M_PI;

    inline float to_degrees(float radians)
    {
        return radians * _RAD_TO_DEG;
    }
};

}  // namespace stackchan::motion
