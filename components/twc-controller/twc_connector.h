/*
TWC Manager for ESP32
Copyright (C) 2023 Jarl Nicolson
This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 3
of the License, or (at your option) any later version.
This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#pragma once

#include <cstring>
#include <stdint.h>

#include "esphome/core/log.h"

namespace esphome {
    namespace twc_controller {
        // Encapsulate some information specific to a single wall connector.  This is to allow multiple connectors to be added
        class TeslaConnector {
            public:
                TeslaConnector(uint16_t twcid, uint8_t max_charge_rate);
                void SetVin(uint8_t* upperVin);
                uint8_t* GetVin();
                void SetActualCurrent(uint8_t current);
                uint8_t GetActualCurrent();
                uint8_t GetPhaseCurrent(uint8_t phase);
                
                uint16_t twcid;
                uint8_t state = 0;
                uint8_t firmware_version[4] = {0};

                uint8_t serial_number[12] = {0};  // 11 chars + NUL

                uint32_t total_kwh = 0;
                uint16_t phase1_voltage = 0;
                uint16_t phase2_voltage = 0;
                uint16_t phase3_voltage = 0;

                uint8_t phase1_current = 0;
                uint8_t phase2_current = 0;
                uint8_t phase3_current = 0;

                uint8_t max_allowable_current;

                // -1 = never sent, otherwise last StartCharging(1)/StopCharging(0)
                // sent to this connector. Tracked per-connector so a freshly
                // linked secondary gets the current allow/deny state asserted
                // once, and it isn't resent every loop iteration.
                int8_t charging_enabled_last_sent = -1;

                // The vehicle's real reported state, excluding state 9 (an
                // echo of our own current-limit command). See the re-arm
                // logic in DecodeSecondaryHeartbeat().
                uint8_t last_vehicle_state = 0;

                // -1 = never queried yet. Otherwise the response to
                // GET_PLUG_STATE: 0=unplugged, 1=charging, 2=unknown,
                // 3=plugged in but not charging. Unlike the heartbeat's
                // state byte, this is unambiguous about "unplugged" - it
                // doesn't collapse into the same value (0) that a fully
                // charged, still-connected car settles into.
                int8_t plug_state = -1;
            private:
                uint8_t vin_[18];
                uint8_t actual_current_ = 0;

        };
    }
}