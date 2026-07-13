#ifndef AURORA_APPS_NFC_SERVICE_HPP
#define AURORA_APPS_NFC_SERVICE_HPP

#include "../../drivers/nfc/nfc_controller.hpp"
#include "../../apps/notification_center.hpp"
#include <string.h> // For memcmp

namespace aurora {
namespace apps {

enum class CardType {
    TRANSIT,
    DOOR_KEY
};

struct VirtualCard {
    CardType type;
    uint8_t aid[16];     // Application Identifier (AID)
    uint32_t aid_length;
    bool is_active;
    int32_t balance_cents; // Used for transit card
};

class NfcService : public nfc::ApduHandler {
private:
    VirtualCard transit_card_;
    VirtualCard door_key_;

    // Helpers
    bool is_select_apdu(const nfc::ApduRequest& req, const VirtualCard& card) {
        // Minimum SELECT APDU length is 5 (CLA INS P1 P2 Lc) + AID
        if (req.length < 5 + card.aid_length) return false;
        if (req.data[0] != 0x00 || req.data[1] != 0xA4 || req.data[2] != 0x04 || req.data[3] != 0x00) return false;
        return memcmp(&req.data[5], card.aid, card.aid_length) == 0;
    }

public:
    NfcService() {
        // Setup Transit Card AID (Example: 1PAY.SYS.DDF01 or similar)
        transit_card_.type = CardType::TRANSIT;
        transit_card_.aid_length = 7;
        const uint8_t transit_aid[] = {0xA0, 0x00, 0x00, 0x00, 0x03, 0x10, 0x10};
        for (int i = 0; i < 7; ++i) transit_card_.aid[i] = transit_aid[i];
        transit_card_.is_active = true;
        transit_card_.balance_cents = 1500; // $15.00

        // Setup Door Key AID
        door_key_.type = CardType::DOOR_KEY;
        door_key_.aid_length = 5;
        const uint8_t door_aid[] = {0xF0, 0x00, 0x00, 0x00, 0x01};
        for (int i = 0; i < 5; ++i) door_key_.aid[i] = door_aid[i];
        door_key_.is_active = true;
        door_key_.balance_cents = 0;
    }

    void init() {
        nfc::NfcController::instance().init();
        nfc::NfcController::instance().register_apdu_handler(this);
    }

    bool handle_apdu(const nfc::ApduRequest& req, nfc::ApduResponse& resp) override {
        // Simple routing based on SELECT APDU
        if (is_select_apdu(req, transit_card_) && transit_card_.is_active) {
            // Process Transit Card transaction
            transit_card_.balance_cents -= 250; // Deduct $2.50
            if (transit_card_.balance_cents < 0) transit_card_.balance_cents = 0;

            // Trigger Notification
            Notification notification;
            notification.priority = NotificationPriority::HIGH;
            // Simple string format
            const char* msg = "Transit Swipe\n-$2.50";
            for(int i = 0; i < 31 && msg[i] != '\0'; ++i) {
                notification.message[i] = msg[i];
            }
            notification.message[31] = '\0';
            NotificationCenter::instance().publish(notification);

            // Respond Success (90 00)
            resp.length = 2;
            resp.data[0] = 0x90;
            resp.data[1] = 0x00;
            return true;
        } else if (is_select_apdu(req, door_key_) && door_key_.is_active) {
            // Process Door Key
            Notification notification;
            notification.priority = NotificationPriority::NORMAL;
            const char* msg = "Door Unlocked";
            for(int i = 0; i < 31 && msg[i] != '\0'; ++i) {
                notification.message[i] = msg[i];
            }
            notification.message[31] = '\0';
            NotificationCenter::instance().publish(notification);

            // Respond Success (90 00)
            resp.length = 2;
            resp.data[0] = 0x90;
            resp.data[1] = 0x00;
            return true;
        }

        // SW_FILE_NOT_FOUND (6A 82)
        resp.length = 2;
        resp.data[0] = 0x6A;
        resp.data[1] = 0x82;
        return true;
    }
};

} // namespace apps
} // namespace aurora

#endif // AURORA_APPS_NFC_SERVICE_HPP
