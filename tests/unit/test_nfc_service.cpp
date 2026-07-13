#include <gtest/gtest.h>
#include "../../apps/nfc/nfc_service.hpp"
#include "../../apps/notification_center.hpp"
#include "../../drivers/nfc/nfc_controller.hpp"

using namespace aurora;
using namespace aurora::apps;
using namespace aurora::nfc;

class NfcServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Reset Notification Center state
        NotificationCenter::instance().clear();
        
        // Initialize NFC Controller and Service
        service_.init();
    }

    void TearDown() override {
        NotificationCenter::instance().clear();
    }

    NfcService service_;
};

TEST_F(NfcServiceTest, TransitCardDeductsBalanceAndNotifies) {
    // SELECT APDU for Transit Card: 00 A4 04 00 07 A0 00 00 00 03 10 10
    const uint8_t select_transit[] = {
        0x00, 0xA4, 0x04, 0x00, 0x07, 
        0xA0, 0x00, 0x00, 0x00, 0x03, 0x10, 0x10
    };

    ApduResponse response;
    
    // Simulate field on
    NfcController::instance().simulate_field_on(NfcTagType::TYPE_A);
    
    // Send APDU
    bool handled = NfcController::instance().simulate_incoming_apdu(select_transit, sizeof(select_transit), response);
    
    EXPECT_TRUE(handled);
    EXPECT_EQ(response.length, 2);
    EXPECT_EQ(response.data[0], 0x90);
    EXPECT_EQ(response.data[1], 0x00);

    // Check notification
    Notification received_notif;
    bool has_notif = NotificationCenter::instance().pop(received_notif);
    EXPECT_TRUE(has_notif);
    EXPECT_EQ(received_notif.priority, NotificationPriority::HIGH);
    EXPECT_STREQ(received_notif.message, "Transit Swipe\n-$2.50");
}

TEST_F(NfcServiceTest, DoorKeyNotifiesUnlock) {
    // SELECT APDU for Door Key: 00 A4 04 00 05 F0 00 00 00 01
    const uint8_t select_door[] = {
        0x00, 0xA4, 0x04, 0x00, 0x05, 
        0xF0, 0x00, 0x00, 0x00, 0x01
    };

    ApduResponse response;
    
    NfcController::instance().simulate_field_on(NfcTagType::TYPE_A);
    bool handled = NfcController::instance().simulate_incoming_apdu(select_door, sizeof(select_door), response);
    
    EXPECT_TRUE(handled);
    EXPECT_EQ(response.length, 2);
    EXPECT_EQ(response.data[0], 0x90);
    EXPECT_EQ(response.data[1], 0x00);

    // Check notification
    Notification received_notif;
    bool has_notif = NotificationCenter::instance().pop(received_notif);
    EXPECT_TRUE(has_notif);
    EXPECT_EQ(received_notif.priority, NotificationPriority::NORMAL);
    EXPECT_STREQ(received_notif.message, "Door Unlocked");
}

TEST_F(NfcServiceTest, UnknownCardReturnsFileNotFound) {
    // SELECT APDU with unknown AID
    const uint8_t select_unknown[] = {
        0x00, 0xA4, 0x04, 0x00, 0x05, 
        0xAA, 0xBB, 0xCC, 0xDD, 0xEE
    };

    ApduResponse response;
    
    NfcController::instance().simulate_field_on(NfcTagType::TYPE_A);
    bool handled = NfcController::instance().simulate_incoming_apdu(select_unknown, sizeof(select_unknown), response);
    
    EXPECT_TRUE(handled);
    EXPECT_EQ(response.length, 2);
    EXPECT_EQ(response.data[0], 0x6A);
    EXPECT_EQ(response.data[1], 0x82); // SW_FILE_NOT_FOUND
}
