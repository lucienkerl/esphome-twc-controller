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

#include "twc_switch.h"
#include "twc_gateway.h"

namespace esphome {
    namespace twc_gateway {

        void AllowChargingSwitch::write_state(bool state) {
            this->parent_->set_charging_enabled(state);
            this->publish_state(state);
        }

    }
}
