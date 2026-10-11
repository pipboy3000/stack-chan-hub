/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "motion.h"
#include <cmath>
#include <esp_log.h>

using namespace uitk_intl;
using namespace stackchan::motion;

static const char* TAG = "Motion";

Motion::~Motion()
{
    _running = false;
}

void Motion::init(std::unique_ptr<Servo> yawServo, std::unique_ptr<Servo> pitchServo)
{
    std::lock_guard<std::mutex> lock(_mutex);

    _yaw_servo   = std::move(yawServo);
    _pitch_servo = std::move(pitchServo);
    _yaw_servo->init();
    _pitch_servo->init();

    if (!_running) {
        _running = true;
        // USB host and HID tasks run at priorities 10 and 9; motion must yield.
        xTaskCreate(update_task, "motion_task", 4096, this, 3, &_task_handle);
        ESP_LOGI(TAG, "Motion task started");
    }
}

void Motion::update_task(void* param)
{
    auto self = static_cast<Motion*>(param);
    while (self->_running) {
        vTaskDelay(pdMS_TO_TICKS(20));
        self->update();
    }
    self->_task_handle = nullptr;
    vTaskDelete(NULL);
}

void Motion::update()
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (_yaw_servo) {
        _yaw_servo->update();
    }
    if (_pitch_servo) {
        _pitch_servo->update();
    }
}

void Motion::moveYaw(int angle, int speed)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _yaw_servo->moveWithSpeed(angle, speed);
}

void Motion::movePitch(int angle, int speed)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _pitch_servo->moveWithSpeed(angle, speed);
}

void Motion::move(int yawAngle, int pitchAngle, int speed)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _yaw_servo->moveWithSpeed(yawAngle, speed);
    _pitch_servo->moveWithSpeed(pitchAngle, speed);
}

void Motion::goHome(int speed)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _yaw_servo->moveWithSpeed(0, speed);
    _pitch_servo->moveWithSpeed(0, speed);
}

void Motion::stop()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _yaw_servo->move(_yaw_servo->getCurrentAngle());
    _pitch_servo->move(_pitch_servo->getCurrentAngle());
}

void Motion::rotateYaw(int velocity)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _yaw_servo->rotate(velocity);
}

void Motion::lookAtNormalized(float x, float y, int speed)
{
    std::lock_guard<std::mutex> lock(_mutex);

    int yaw_angle   = uitk_intl::map_range(x, -1.0f, 1.0f, (float)_yaw_servo->getAngleLimit().x,
                                           (float)_yaw_servo->getAngleLimit().y);
    int pitch_angle = uitk_intl::map_range(y, -1.0f, 1.0f, (float)_pitch_servo->getAngleLimit().x,
                                           (float)_pitch_servo->getAngleLimit().y);

    _yaw_servo->moveWithSpeed(yaw_angle, speed);
    _pitch_servo->moveWithSpeed(pitch_angle, speed);
}

void Motion::lookAtPoint(float x, float y, float z, int speed)
{
    // Yaw: 绕 Z 轴旋转。使用 atan2(y, x)
    float yaw_rad = std::atan2(y, x);

    // Pitch: 俯仰。使用 atan2(z, sqrt(x*x + y*y))
    float ground_dist = std::sqrt(x * x + y * y);
    float pitch_rad   = std::atan2(z, ground_dist);

    // 将弧度转换为你的舵机单位 (-1280~1280 等)
    int yaw_angle   = static_cast<int>(to_degrees(yaw_rad) * 10);
    int pitch_angle = static_cast<int>(to_degrees(pitch_rad) * 10);

    std::lock_guard<std::mutex> lock(_mutex);
    _yaw_servo->moveWithSpeed(yaw_angle, speed);
    _pitch_servo->moveWithSpeed(pitch_angle, speed);
}

bool Motion::isMoving()
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _yaw_servo->isMoving() || _pitch_servo->isMoving();
}

bool Motion::isYawMoving()
{
    return _yaw_servo->isMoving();
}

bool Motion::isPitchMoving()
{
    return _pitch_servo->isMoving();
}

int Motion::getCurrentYawAngle()
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _yaw_servo->getCurrentAngle();
}

int Motion::getCurrentPitchAngle()
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _pitch_servo->getCurrentAngle();
}

uitk_intl::Vector2i Motion::getCurrentAngles()
{
    std::lock_guard<std::mutex> lock(_mutex);
    return uitk_intl::Vector2i(_yaw_servo->getCurrentAngle(), _pitch_servo->getCurrentAngle());
}

void Motion::setTorqueEnabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _yaw_servo->setTorqueEnabled(enabled);
    _pitch_servo->setTorqueEnabled(enabled);
}

void Motion::setAutoTorqueReleaseEnabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _yaw_servo->setAutoTorqueReleaseEnabled(enabled);
    _pitch_servo->setAutoTorqueReleaseEnabled(enabled);
}

void Motion::setAutoAngleSyncEnabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _yaw_servo->setAutoAngleSyncEnabled(enabled);
    _pitch_servo->setAutoAngleSyncEnabled(enabled);
}

void Motion::setCurrentPostionAsHome()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _yaw_servo->setCurrentAngleAsZero();
    _pitch_servo->setCurrentAngleAsZero();
}
