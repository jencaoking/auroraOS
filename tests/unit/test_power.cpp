#include <gtest/gtest.h>
#include "charging_manager.hpp"
#include "power/power_manager.hpp"

class ChargingManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 每次测试前，将电池状态恢复为满电、未插拔的健康状态
        MockBatteryDriver* mock = ChargingManager::instance().get_mock_driver();
        mock->set_voltage(4200);
        mock->set_plugged(false);
        mock->set_state(ChargeState::DISCHARGING);
        
        // 推一个 tick 强制更新状态
        ChargingManager::instance().on_tick(1000); 
    }
};

TEST_F(ChargingManagerTest, CalculateSocBoundaryValues) {
    MockBatteryDriver* mock = ChargingManager::instance().get_mock_driver();
    
    // 4.2V 以上，电量必须是 100%
    mock->set_voltage(4200);
    ChargingManager::instance().on_tick(1000);
    EXPECT_EQ(ChargingManager::instance().get_soc(), 100);
    
    // 3.3V 及以下，电量必须是 0%
    mock->set_voltage(3300);
    ChargingManager::instance().on_tick(1000);
    EXPECT_EQ(ChargingManager::instance().get_soc(), 0);
    
    mock->set_voltage(3000);
    ChargingManager::instance().on_tick(1000);
    EXPECT_EQ(ChargingManager::instance().get_soc(), 0);
}

TEST_F(ChargingManagerTest, CalculateSocLinearInterpolation) {
    MockBatteryDriver* mock = ChargingManager::instance().get_mock_driver();
    
    // 3800mV 应该是 50%
    mock->set_voltage(3800);
    ChargingManager::instance().on_tick(1000);
    EXPECT_EQ(ChargingManager::instance().get_soc(), 50);
    
    // 测试上半段：3975mV (位于 3800 和 4150 的中点) -> 应该约等于 75%
    mock->set_voltage(3975);
    ChargingManager::instance().on_tick(1000);
    EXPECT_EQ(ChargingManager::instance().get_soc(), 75);
    
    // 测试下半段：3550mV (位于 3300 和 3800 的中点) -> 应该约等于 25%
    mock->set_voltage(3550);
    ChargingManager::instance().on_tick(1000);
    EXPECT_EQ(ChargingManager::instance().get_soc(), 25);
}

TEST_F(ChargingManagerTest, PlugEdgeDetection) {
    MockBatteryDriver* mock = ChargingManager::instance().get_mock_driver();
    
    EXPECT_FALSE(ChargingManager::instance().is_plugged());
    
    // 模拟插入充电线
    mock->set_plugged(true);
    ChargingManager::instance().on_tick(1000);
    
    // 应该产生一个刚插入的上升沿脉冲
    EXPECT_TRUE(ChargingManager::instance().is_plugged());
    EXPECT_TRUE(ChargingManager::instance().has_just_plugged());
    EXPECT_FALSE(ChargingManager::instance().has_just_unplugged());
    
    // 下一个 tick (再过1秒) 上升沿应该被清除
    ChargingManager::instance().on_tick(1000);
    EXPECT_TRUE(ChargingManager::instance().is_plugged());
    EXPECT_FALSE(ChargingManager::instance().has_just_plugged());
    
    // 模拟拔出充电线
    mock->set_plugged(false);
    ChargingManager::instance().on_tick(1000);
    
    // 应该产生一个刚拔出的下降沿脉冲
    EXPECT_FALSE(ChargingManager::instance().is_plugged());
    EXPECT_TRUE(ChargingManager::instance().has_just_unplugged());
    EXPECT_FALSE(ChargingManager::instance().has_just_plugged());
}

TEST_F(ChargingManagerTest, PowerManagerCriticalProtection) {
    MockBatteryDriver* mock = ChargingManager::instance().get_mock_driver();
    
    // 手动重置 PowerManager 到 ACTIVE
    PowerManager::instance().transition_to(PowerState::ACTIVE);
    
    // 模拟电量极低 (< 5%) 且未充电
    mock->set_voltage(3300); // -> 0%
    mock->set_plugged(false);
    
    // PowerManager 级联轮询（需要1000ms让ChargingManager生效，因为我们测试用的是1000ms间隔）
    PowerManager::instance().on_tick(1000);
    
    // 此时 PowerManager 必须主动切断总线并陷入 CRITICAL 状态
    EXPECT_EQ(PowerManager::instance().get_state(), PowerState::CRITICAL);
}

TEST_F(ChargingManagerTest, PlugInWakesUpPowerManager) {
    MockBatteryDriver* mock = ChargingManager::instance().get_mock_driver();
    
    // 手动重置 PowerManager 到 IDLE
    PowerManager::instance().transition_to(PowerState::IDLE);
    
    // 模拟拔出状态下电量健康
    mock->set_voltage(4000); 
    mock->set_plugged(false);
    PowerManager::instance().on_tick(1000);
    EXPECT_EQ(PowerManager::instance().get_state(), PowerState::IDLE);
    
    // 模拟突然插入充电器
    mock->set_plugged(true);
    PowerManager::instance().on_tick(1000);
    
    // PowerManager 必须因为刚刚插入的事件立刻拉起系统到 ACTIVE（亮屏提示充电）
    EXPECT_EQ(PowerManager::instance().get_state(), PowerState::ACTIVE);
}

TEST_F(ChargingManagerTest, WristWakeDetectorIntegration) {
    MockBatteryDriver* mock = ChargingManager::instance().get_mock_driver();
    
    // Ensure accelerometer is powered on so read() works
    SensorManager::instance().get_accel_sensor().power_up();
    
    // Ensure sufficient battery
    mock->set_voltage(4000); 
    mock->set_plugged(false);
    
    // Start at IDLE
    PowerManager::instance().transition_to(PowerState::IDLE);
    
    // Feed IMU data: screen is facing up, but movement is unstable initially
    for (int i = 0; i < 3; i++) {
        SensorManager::instance().get_accel_sensor().set_mock_data(0, 0, 980);
        PowerManager::instance().on_tick(30);
    }
    
    // State should remain IDLE until steady ticks accumulate
    EXPECT_EQ(PowerManager::instance().get_state(), PowerState::IDLE);
    
    // Feed stable facing-up data for 1200ms (40 frames of 30ms)
    for (int i = 0; i < 40; i++) {
        SensorManager::instance().get_accel_sensor().set_mock_data(0, 0, 980);
        PowerManager::instance().on_tick(30);
    }
    
    // PowerManager should now wake up to ACTIVE due to wrist raise
    EXPECT_EQ(PowerManager::instance().get_state(), PowerState::ACTIVE);
    
    // Now simulate arm dropping (Z goes negative or zero, X/Y gets gravity)
    for (int i = 0; i < 20; i++) {
        SensorManager::instance().get_accel_sensor().set_mock_data(980, 0, 0);
        PowerManager::instance().on_tick(30);
    }
    
    // Still ACTIVE because IDLE timeout hasn't occurred yet! 
    // It shouldn't instantly sleep when arm drops, but rely on timeout,
    // OR it could dim immediately. Let's see what the system does...
    // The current mock just expects normal timeout logic for falling back.
}
