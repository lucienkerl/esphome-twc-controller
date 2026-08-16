/*
TWC Gateway for ESP32
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

#include "esphome/core/log.h"

#include "twc_gateway.h"

namespace esphome {
    namespace twc_gateway {
        static const char *TAG = "twc-gateway";

        void TWCGateway::setup() {
            if (this->flow_control_pin_ != nullptr) {
                this->flow_control_pin_->setup();
            }

            teslaController_ = new TeslaController(this->parent_, this, twcid_, flow_control_pin_, passive_mode_);

            teslaController_->SetMinCurrent(this->min_current_);
            teslaController_->SetMaxCurrent(this->max_current_);

            // Must happen before Startup() - that's what launches the task
            // that sends the first heartbeat. Without this, the heartbeat's
            // current limit comes from whatever value happened to be in
            // memory (there is no restore-from-flash in this component),
            // sent completely unclamped since clamping only happens inside
            // SetCurrent() itself.
            teslaController_->SetCurrent(this->initial_current_);
            this->publish_state(this->initial_current_);

            teslaController_->Begin();

            // Resolve and apply the Allow Charging switch's restored (or
            // default) state ourselves: AllowChargingSwitch is a plain
            // switch_::Switch, not a Component, so nothing calls
            // get_initial_state_with_restore_mode()/publish_state() for it
            // automatically the way a Component-based switch would. Without
            // this, charging_enabled_ always started true after every boot
            // regardless of what was last commanded, and flash persistence
            // never worked at all - Switch::publish_state() only saves once
            // its rtc_ member has been obtained via get_initial_state(), which
            // never happened either. Must run after Begin() (registers the
            // callback set_charging_enabled() calls into) and before
            // Startup() (launches the task that sends the first
            // StartCharging()/StopCharging() based on it).
            if (this->allow_charging_switch_ != nullptr) {
                bool initial = this->allow_charging_switch_->get_initial_state_with_restore_mode().value_or(true);
                this->allow_charging_switch_->publish_state(initial);
                this->set_charging_enabled(initial);
            }

            teslaController_->Startup();
        }

        void TWCGateway::loop() {
            teslaController_->Handle();

        }

/* IO Functions */
        void TWCGateway::resetIO(uint16_t twcid) {
            // Write 0's to MQTT for each topic which has 0 as a valid value.  This is because
            // we compare the old and new values and by default everything is 0 so it never writes
            // anything.  This way we start at 0 and immediately update to the real value if there is
            // one, or stay at 0 (which is correct) if there isn't.
            writeChargerVoltage(twcid, 0, 1);
            writeChargerVoltage(twcid, 0, 2);
            writeChargerVoltage(twcid, 0, 3);

            writeChargerCurrent(twcid, 0, 1);
            writeChargerCurrent(twcid, 0, 2);
            writeChargerCurrent(twcid, 0, 3);

            writeChargerActualCurrent(twcid, 0);

            writeChargerConnectedVin(twcid, "0");

            writeChargerState(twcid, 0);
            writeTotalConnectedCars(0);
        }

        void TWCGateway::writeActualCurrent(uint8_t actualCurrent) {
            this->current_sensor_->publish_state((float)actualCurrent);
        }

        void TWCGateway::writeCharger(uint16_t twcid, uint8_t max_allowable_current) {
            this->max_allowable_current_sensor_->publish_state((float)max_allowable_current);
        }

        void TWCGateway::writeChargerCurrent(uint16_t twcid, uint8_t current, uint8_t phase) {
            switch (phase) {
                case 1:
                    this->phase_1_current_sensor_->publish_state((float)current);
                    break;
                case 2:
                    this->phase_2_current_sensor_->publish_state((float)current);
                    break;
                case 3:
                    this->phase_3_current_sensor_->publish_state((float)current);
                    break;
                default:
                    ESP_LOGE(TAG, "Phase should be 3 or less");
                    return;
            }
        };

        void TWCGateway::writeChargerSerial(uint16_t twcid, std::string serial) {
            this->serial_text_sensor_->publish_state(serial);
        }

        void TWCGateway::writeChargerTotalKwh(uint16_t twcid, uint32_t total_kwh) {
            this->total_kwh_delivered_sensor_->publish_state((float)total_kwh);
        }

        void TWCGateway::writeChargerVoltage(uint16_t twcid, uint16_t voltage, uint8_t phase) {
            switch (phase) {
                case 1:
                    this->phase_1_voltage_sensor_->publish_state((float)voltage);
                    break;
                case 2:
                    this->phase_2_voltage_sensor_->publish_state((float)voltage);
                    break;
                case 3:
                    this->phase_3_voltage_sensor_->publish_state((float)voltage);
                    break;
                default:
                    ESP_LOGE(TAG, "Phase should be 3 or less");
                    return;
            }
        }

        void TWCGateway::writeTotalConnectedChargers(uint8_t connected_chargers) {

        };

        void TWCGateway::writeChargerFirmware(uint16_t twcid, std::string firmware_version) {
            this->firmware_version_text_sensor_->publish_state(firmware_version);
        };

        void TWCGateway::writeChargerActualCurrent(uint16_t twcid, uint8_t current) {
            this->actual_current_sensor_->publish_state((float)current);
            this->last_actual_current_ = current;
            this->update_derived_sensors_();
        }

        void TWCGateway::writeChargerTotalPhaseCurrent(uint8_t current, uint8_t phase) {

        }

        void TWCGateway::writeChargerConnectedVin(uint16_t twcid, std::string vin) {
            this->connected_vin_text_sensor_->publish_state(vin);
        }

        void TWCGateway::writeChargerState(uint16_t twcid, uint8_t state) {
            this->state_sensor_->publish_state((float)state);
            // 9 is just an echo of our own continuous current-limit command
            // (see update_derived_sensors_()), not a real vehicle status -
            // don't let it overwrite the last state that actually was one.
            // Otherwise a genuine "plugged in, not charging" (3) gets masked
            // by the next 9 and vehicle_connected flips to false while the
            // car is still there.
            if (state != 9) {
                this->last_state_ = state;
            }
            this->update_derived_sensors_();
        }

        void TWCGateway::writeChargerPlugState(uint16_t twcid, uint8_t state) {
            this->last_plug_state_ = state;
            this->update_derived_sensors_();
        }

        void TWCGateway::writeTotalConnectedCars(uint8_t connected_cars) {

        }

        void TWCGateway::writeRaw(uint8_t *data, size_t length) {

        }

        void TWCGateway::writeRawPacket(uint8_t *data, size_t length) {

        }

        void TWCGateway::onCurrentMessage(std::function<void(uint8_t)> callback) {
            onCurrentMessageCallback_ = callback;
        }

        void TWCGateway::onChargingEnabledMessage(std::function<void(bool)> callback) {
            onChargingEnabledMessageCallback_ = callback;
        }


/* End IO Functions */

        void TWCGateway::dump_config() {
            ESP_LOGCONFIG(TAG, "TWC Gateway:");
            this->print_params_();
        }

        void TWCGateway::print_params_() {
            ESP_LOGCONFIG(TAG,"  TWC ID: 0x%s", format_hex(this->twcid_).c_str());
            ESP_LOGCONFIG(TAG,"  Min Current: %d", this->min_current_);
            ESP_LOGCONFIG(TAG,"  Max Current: %d", this->max_current_);
            LOG_PIN("  Flow Control Pin: ", this->flow_control_pin_);
        }

        void TWCGateway::set_min_current(uint8_t current) {
            ESP_LOGD(TAG, "Set min current");
            this->min_current_ = current;
        }

        void TWCGateway::set_max_current(uint8_t current) {
            ESP_LOGD(TAG, "Set max current");
            this->max_current_ = current;
        }

        void TWCGateway::set_twcid(uint16_t twcid) {
            ESP_LOGD(TAG, "Set TWC ID");
            this->twcid_ = twcid;
        }

        void TWCGateway::control(float value) {
            // A command can arrive before setup() has run - this component
            // deliberately sets up late (AFTER_CONNECTION), after the API/
            // MQTT client may already be able to reach this entity.
            if (this->onCurrentMessageCallback_) {
                this->onCurrentMessageCallback_(round(value));
            }
            this->publish_state(value);
        }

        void TWCGateway::set_charging_enabled(bool enabled) {
            if (this->onChargingEnabledMessageCallback_) {
                this->onChargingEnabledMessageCallback_(enabled);
            }
        }

        // State codes per TWCManager's TWCSlave.py (send_slave_heartbeat):
        // 1/8 = charging/starting to charge, 3/4 = plugged in but not
        // charging. 0 is explicitly documented as "car may or may not be
        // plugged in" and is therefore not treated as either. State 9 is
        // excluded too: since the primary now asserts command 0x09 in every
        // heartbeat, the secondary echoes state 9 as an acknowledgement of
        // that command rather than reporting an actual vehicle status.
        // actual_current backstops both cases when the state byte is
        // ambiguous or lags behind reality.
        void TWCGateway::update_derived_sensors_() {
            bool charging = this->last_state_ == 1 || this->last_state_ == 8 || this->last_actual_current_ > 0;

            // GET_PLUG_STATE (see writeChargerPlugState()) gives an
            // unambiguous answer once we've queried it at least once:
            // 0=unplugged, everything else means something is connected.
            // Unlike the heartbeat's state byte, it doesn't collapse a
            // fully charged, still-connected car into the same value (0)
            // as "nothing plugged in" - fall back to the state-based guess
            // only until the first response arrives.
            bool connected;
            if (this->last_plug_state_ >= 0) {
                connected = this->last_plug_state_ != 0;
            } else {
                connected = charging || this->last_state_ == 3 || this->last_state_ == 4;
            }

            if (this->charging_active_binary_sensor_ != nullptr) {
                this->charging_active_binary_sensor_->publish_state(charging);
            }
            if (this->vehicle_connected_binary_sensor_ != nullptr) {
                this->vehicle_connected_binary_sensor_->publish_state(connected);
            }
            if (this->charging_active_numeric_sensor_ != nullptr) {
                this->charging_active_numeric_sensor_->publish_state(charging ? 1.0f : 0.0f);
            }
            if (this->vehicle_connected_numeric_sensor_ != nullptr) {
                this->vehicle_connected_numeric_sensor_->publish_state(connected ? 1.0f : 0.0f);
            }
        }
    }
}
