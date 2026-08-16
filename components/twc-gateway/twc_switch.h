/*
TWC Gateway for ESP32
Copyright (C) 2026 Jarl Nicolson
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

#include "esphome/components/switch/switch.h"

namespace esphome {
    namespace twc_gateway {

        class TWCGateway;

        // Sends the protocol's dedicated START_CHARGING/STOP_CHARGING commands
        // (0xFCB1/0xFCB2) instead of relying on the heartbeat's current-limit
        // field. Experimental: unlike the current limit, these commands are
        // unverified to reliably start/stop an active charging session.
        class AllowChargingSwitch : public switch_::Switch {
            public:
                void set_parent(TWCGateway *parent) { this->parent_ = parent; }

            protected:
                void write_state(bool state) override;

                TWCGateway *parent_{nullptr};
        };

    }
}
