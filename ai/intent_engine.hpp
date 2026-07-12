#ifndef AURORA_INTENT_ENGINE_HPP
#define AURORA_INTENT_ENGINE_HPP

#include "../drivers/sensor/sensor_framework.hpp"
#include "../kernel/app_lifecycle.hpp"

class IntentEngine {
public:
    // 基于传感器规则的决策引擎
    static void process_sensors(AppControlBlock& fitness_app) {
        uint32_t steps = SensorManager::instance().get_accel_sensor().get_steps();

        // 简易意图逻辑：如果步数增加导致变化超过某个阈值，判定为“运动开始”
        // 或者是每 20 步唤醒一次
        if (steps > 0 && (steps % 20 == 0)) { 
            if (fitness_app.state != AppState::FOREGROUND) {
                fitness_app.transition_to(AppState::FOREGROUND);
                
                int fd = open("/dev/uart0", 0);
                if (fd >= 0) {
                    write(fd, "\r\n🤖 [Intent Engine] High activity detected! Promoting Fitness App to FOREGROUND.\r\n", 83);
                    close(fd);
                }
            }
        } else {
            // 否则降级到后台
            if (fitness_app.state == AppState::FOREGROUND && (steps % 20 != 0)) {
                fitness_app.transition_to(AppState::BACKGROUND);
                int fd = open("/dev/uart0", 0);
                if (fd >= 0) {
                    write(fd, "\r\n🤖 [Intent Engine] Activity reduced. Demoting Fitness App to BACKGROUND.\r\n", 76);
                    close(fd);
                }
            }
        }
    }
};

#endif
