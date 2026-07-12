#ifndef AURORA_SENSOR_FRAMEWORK_HPP
#define AURORA_SENSOR_FRAMEWORK_HPP

#include "config/autoconf.h"

#ifdef CONFIG_BOARD_MIBAND8

#include <stdint.h>

// ========================================================
// 传感器数据类型与标准载荷抽象 (Xiaomi Mi Band 8 专用)
// ========================================================
enum class SensorType : uint8_t {
    HEART_RATE,
    ACCELEROMETER,
    STEP_COUNTER
};

struct SensorData {
    SensorType type;
    uint32_t   timestamp;
    union {
        struct {
            int32_t x;
            int32_t y;
            int32_t z;
        } accel;
        uint32_t bpm;   // 心率 (次/分钟)
        uint32_t steps; // 步数
    } payload;
};

// ========================================================
// 标准化 Sensor Driver 接口
// ========================================================
class SensorDriver {
public:
    virtual ~SensorDriver() = default;
    virtual bool init() = 0;                                 // 初始化硬件
    virtual bool read(SensorData* out_data) = 0;             // 读取单次采样数据
    virtual void set_sample_rate(uint16_t hz) = 0;           // 设置采样率
    virtual void power_up() = 0;                             // 退出休眠，开启供电
    virtual void power_down() = 0;                           // 进入休眠，切断供电
};

// ========================================================
// 1. GH3026 PPG 光电心率传感器驱动
// ========================================================
class HeartRateSensor : public SensorDriver {
private:
    uint16_t sample_rate_;
    bool     is_powered_on_;
    uint32_t simulated_bpm_;

public:
    HeartRateSensor() : sample_rate_(25), is_powered_on_(false), simulated_bpm_(75) {} // 默认 25Hz 采样率

    bool init() override {
        // 实际实现需调用 I2C 写寄存器开启绿光/红光/红外通道
        power_up();
        return true;
    }

    void set_sample_rate(uint16_t hz) override {
        sample_rate_ = hz;
    }

    void power_up() override {
        is_powered_on_ = true;
    }

    void power_down() override {
        is_powered_on_ = false;
    }

    bool read(SensorData* out_data) override {
        if (!is_powered_on_ || !out_data) return false;
        
        // 模拟 I2C 读取与底层算法处理
        out_data->type = SensorType::HEART_RATE;
        out_data->payload.bpm = simulated_bpm_; 
        return true;
    }
};

// ========================================================
// 2. BHI260AP 6轴加速度计与计步器驱动
// ========================================================
class AccelerometerSensor : public SensorDriver {
private:
    uint16_t sample_rate_;
    bool     is_powered_on_;
    uint32_t current_steps_;

    // 步数检测核心：三态机算法
    enum class StepState { STABLE, RISING, FALLING };
    StepState step_state_;
    int32_t   last_accel_mag_;

    // 简单的整数平方根近似，用于计算三轴向量模长
    int32_t approx_sqrt(int32_t val) {
        if (val <= 0) return 0;
        int32_t res = 0, bit = 1 << 30;
        while (bit > val) bit >>= 2;
        while (bit != 0) {
            if (val >= res + bit) {
                val -= res + bit;
                res = (res >> 1) + bit;
            } else {
                res >>= 1;
            }
            bit >>= 2;
        }
        return res;
    }

public:
    AccelerometerSensor() : 
        sample_rate_(25), is_powered_on_(false), // 默认 25Hz 采样率
        current_steps_(0), step_state_(StepState::STABLE), last_accel_mag_(1000) {}

    bool init() override {
        power_up();
        return true;
    }

    void set_sample_rate(uint16_t hz) override { sample_rate_ = hz; }
    void power_up() override   { is_powered_on_ = true; }
    void power_down() override { is_powered_on_ = false; }

    bool read(SensorData* out_data) override {
        if (!is_powered_on_ || !out_data) return false;

        // 占位：实际需通过 I2C 读取 BHI260AP FIFO 中的 X/Y/Z 原始数据
        int32_t ax = 0, ay = 0, az = 1000; 

        // 计算合加速度模长 (单位 mg)
        int32_t magnitude = approx_sqrt(ax * ax + ay * ay + az * az);

        // ========================================================
        // STABLE -> RISING -> FALLING 峰值检测计步算法
        // ========================================================
        const int32_t STEP_THRESHOLD_HIGH = 1200; // 抬腿加速度阈值
        const int32_t STEP_THRESHOLD_LOW  = 800;  // 落脚加速度阈值

        switch (step_state_) {
            case StepState::STABLE:
                if (magnitude > STEP_THRESHOLD_HIGH) {
                    step_state_ = StepState::RISING; // 识别到抬腿峰值
                }
                break;
            case StepState::RISING:
                if (magnitude < STEP_THRESHOLD_LOW) {
                    step_state_ = StepState::FALLING; // 识别到落脚谷值
                }
                break;
            case StepState::FALLING:
                if (magnitude >= 900 && magnitude <= 1100) { // 回归 1g 重力平稳态
                    current_steps_++; // 完整循环，计步加一
                    step_state_ = StepState::STABLE; // 重置状态
                }
                break;
        }

        last_accel_mag_ = magnitude;

        out_data->type = SensorType::ACCELEROMETER;
        out_data->payload.accel.x = ax;
        out_data->payload.accel.y = ay;
        out_data->payload.accel.z = az;
        
        return true;
    }

    uint32_t get_steps() const { return current_steps_; }
};

// ========================================================
// 统一传感器管理器与环形缓冲区
// ========================================================
class SensorManager {
private:
    static constexpr int RING_BUFFER_SIZE = 64; // 定义环形缓冲区大小
    SensorData ring_buffer_[RING_BUFFER_SIZE];
    uint32_t   head_;
    uint32_t   tail_;

    HeartRateSensor     hr_sensor_;
    AccelerometerSensor accel_sensor_;

    SensorManager() : head_(0), tail_(0) {}

public:
    static SensorManager& instance() {
        static SensorManager manager;
        return manager;
    }

    void init_all() {
        hr_sensor_.init();
        accel_sensor_.init();
    }

    // 后台高速采样线程调用，将数据推入环形缓冲区
    void fetch_and_buffer(uint32_t current_tick) {
        SensorData data;
        
        if (hr_sensor_.read(&data)) {
            data.timestamp = current_tick;
            ring_buffer_[head_] = data;
            head_ = (head_ + 1) % RING_BUFFER_SIZE;
            if (head_ == tail_) tail_ = (tail_ + 1) % RING_BUFFER_SIZE; // 覆盖旧数据
        }

        if (accel_sensor_.read(&data)) {
            data.timestamp = current_tick;
            ring_buffer_[head_] = data;
            head_ = (head_ + 1) % RING_BUFFER_SIZE;
            if (head_ == tail_) tail_ = (tail_ + 1) % RING_BUFFER_SIZE; // 覆盖旧数据
        }
    }

    // 供前端 UI 或健康算法提取最新的一批数据进行批量处理
    bool pop_data(SensorData* out_data) {
        if (head_ == tail_) return false; // 缓冲区空
        *out_data = ring_buffer_[tail_];
        tail_ = (tail_ + 1) % RING_BUFFER_SIZE;
        return true;
    }

    HeartRateSensor& get_hr_sensor() { return hr_sensor_; }
    AccelerometerSensor& get_accel_sensor() { return accel_sensor_; }
};

#else // !CONFIG_BOARD_MIBAND8

#include "device.hpp"

// ========================================================
// QEMU TI LM3S6965-QB 专用模拟传感器框架
// ========================================================

// 传感器通道类型 (借鉴 Zephyr sensor_channel)
enum class SensorChannel {
    ACCEL_X,
    ACCEL_Y,
    ACCEL_Z,
    HEART_RATE,
    STEP_COUNT,
    BATTERY_LEVEL
};

// 统一的传感器数据值结构
struct SensorValue {
    int32_t val1; // 整数部分
    int32_t val2; // 小数部分 (百万分之一)
};

// 传感器基类
class SensorDevice : public Device {
public:
    SensorDevice(const char* name) : Device(name, DeviceType::Char) {}

    // 核心 API：触发硬件进行一次最新采样
    virtual int sample_fetch() = 0;
    
    // 核心 API：获取指定通道的缓存数据
    virtual int channel_get(SensorChannel chan, SensorValue* val) = 0;
};

// ========================================================
// 模拟心率与计步传感器 (如 HRS3300)
// ========================================================
class HealthSensor : public SensorDevice {
private:
    int32_t current_hr_;
    int32_t current_steps_;

public:
    HealthSensor(const char* name) : SensorDevice(name), current_hr_(75), current_steps_(1000) {}

    int sample_fetch() override {
        // 物理硬件：通过 I2C 发送采样指令并读取 FIFO
        // 这里在仿真中引入一些随机波动
        current_hr_ = 70 + (current_steps_ % 15);
        current_steps_ += 2;
        return 0;
    }

    int channel_get(SensorChannel chan, SensorValue* val) override {
        if (chan == SensorChannel::HEART_RATE) {
            val->val1 = current_hr_;
            val->val2 = 0;
            return 0;
        } else if (chan == SensorChannel::STEP_COUNT) {
            val->val1 = current_steps_;
            val->val2 = 0;
            return 0;
        }
        return -1;
    }
};

#endif // CONFIG_BOARD_MIBAND8

#endif // AURORA_SENSOR_FRAMEWORK_HPP
