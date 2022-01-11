#include "roode.h"

#include "esphome/core/log.h"
namespace esphome {
namespace roode {
void Roode::dump_config() {
  ESP_LOGCONFIG(TAG, "dump config:");
  LOG_I2C_DEVICE(this);

  LOG_UPDATE_INTERVAL(this);
}
void Roode::setup() {
  ESP_LOGI(SETUP, "Booting Roode %s", VERSION);
  if (version_sensor != nullptr) {
    version_sensor->publish_state(VERSION);
  }
  Wire.begin();
  Wire.setClock(400000);

  // Initialize the sensor, give the special I2C_address to the Begin function
  // Set a different I2C address
  // This address is stored as long as the sensor is powered. To revert this
  // change you can unplug and replug the power to the sensor
  distanceSensor.SetI2CAddress(VL53L1X_ULD_I2C_ADDRESS);

  sensor_status = distanceSensor.Begin(VL53L1X_ULD_I2C_ADDRESS);
  if (sensor_status != VL53L1_ERROR_NONE) {
    // If the sensor could not be initialized print out the error code. -7 is
    // timeout
    ESP_LOGE(SETUP, "Could not initialize the sensor, error code: %d", sensor_status);
    this->mark_failed();
    return;
  }
  if (sensor_offset_calibration_ != -1) {
    ESP_LOGI(CALIBRATION, "Setting sensor offset calibration to %d", sensor_offset_calibration_);
    sensor_status = distanceSensor.SetOffsetInMm(sensor_offset_calibration_);
    if (sensor_status != VL53L1_ERROR_NONE) {
      ESP_LOGE(SETUP, "Could not set sensor offset calibration, error code: %d", sensor_status);
      this->mark_failed();
      return;
    }
  }
  if (sensor_xtalk_calibration_ != -1) {
    ESP_LOGI(CALIBRATION, "Setting sensor xtalk calibration to %d", sensor_xtalk_calibration_);
    sensor_status = distanceSensor.SetXTalk(sensor_xtalk_calibration_);
    if (sensor_status != VL53L1_ERROR_NONE) {
      ESP_LOGE(SETUP, "Could not set sensor offset calibration, error code: %d", sensor_status);
      this->mark_failed();
      return;
    }
  }
  ESP_LOGI(SETUP, "Using sampling with sampling size: %d", samples);
  ESP_LOGI(SETUP, "Creating entry and exit zones.");
  createEntryAndExitZone();

  calibrateZones(distanceSensor);
}

void Roode::update() {
  if (distance_entry != nullptr) {
    distance_entry->publish_state(entry->getDistance());
  }
  if (distance_exit != nullptr) {
    distance_exit->publish_state(exit->getDistance());
  }
}

void Roode::loop() {
  // unsigned long start = micros();
  getAlternatingZoneDistances();
  // uint16_t samplingDistance = sampling(this->current_zone);
  doPathTracking(this->current_zone);
  handleSensorStatus();
  this->current_zone = this->current_zone == this->entry ? this->exit : this->entry;
  // ESP_LOGI("Experimental", "Entry zone: %d, exit zone: %d",
  // entry->getDistance(Roode::distanceSensor, Roode::sensor_status),
  // exit->getDistance(Roode::distanceSensor, Roode::sensor_status)); unsigned
  // long end = micros(); unsigned long delta = end - start; ESP_LOGI("Roode
  // loop", "loop took %lu microseconds", delta);
}

bool Roode::handleSensorStatus() {
  ESP_LOGV(TAG, "Sensor status: %d, Last sensor status: %d", sensor_status, last_sensor_status);
  bool check_status = false;
  if (last_sensor_status != sensor_status && sensor_status == VL53L1_ERROR_NONE) {
    if (status_sensor != nullptr) {
      status_sensor->publish_state(sensor_status);
    }
    check_status = true;
  }
  if (sensor_status < 28 && sensor_status != VL53L1_ERROR_NONE) {
    ESP_LOGE(TAG, "Ranging failed with an error. status: %d", sensor_status);
    status_sensor->publish_state(sensor_status);
    check_status = false;
  }

  last_sensor_status = sensor_status;
  sensor_status = VL53L1_ERROR_NONE;
  return check_status;
}

void Roode::createEntryAndExitZone() {
  if (!entry->roi->center) {
    entry->roi->center = orientation_ == Parallel ? 167 : 195;
  }
  if (!exit->roi->center) {
    exit->roi->center = orientation_ == Parallel ? 231 : 60;
  }
}

VL53L1_Error Roode::getAlternatingZoneDistances() {
  sensor_status += this->current_zone->readDistance(distanceSensor);
  App.feed_wdt();
  return sensor_status;
}

void Roode::doPathTracking(Zone *zone) {
  static int PathTrack[] = {0, 0, 0, 0};
  static int PathTrackFillingSize = 1;  // init this to 1 as we start from state
                                        // where nobody is any of the zones
  static int LeftPreviousStatus = NOBODY;
  static int RightPreviousStatus = NOBODY;
  int CurrentZoneStatus = NOBODY;
  int AllZonesCurrentStatus = 0;
  int AnEventHasOccured = 0;

  uint16_t Distances[2][samples];
  uint16_t MinDistance;
  uint8_t i;
  uint8_t zoneId = zone == this->entry ? 0 : 1;
  auto zoneName = zone == this->entry ? "entry" : "exit ";
  if (DistancesTableSize[zoneId] < samples) {
    ESP_LOGD(TAG, "Distances[%d][DistancesTableSize[zone]] = %d", zoneId, zone->getDistance());
    Distances[zoneId][DistancesTableSize[zoneId]] = zone->getDistance();
    DistancesTableSize[zoneId]++;
  } else {
    for (i = 1; i < samples; i++)
      Distances[zoneId][i - 1] = Distances[zoneId][i];
    Distances[zoneId][samples - 1] = zone->getDistance();
    ESP_LOGD(SETUP, "Distance[%s] = %d", zoneName, Distances[zoneId][samples - 1]);
  }
  // pick up the min distance
  MinDistance = Distances[zoneId][0];
  if (DistancesTableSize[zoneId] >= 2) {
    for (i = 1; i < DistancesTableSize[zoneId]; i++) {
      if (Distances[zoneId][i] < MinDistance)
        MinDistance = Distances[zoneId][i];
    }
  }

  // PathTrack algorithm
  if (MinDistance < zone->threshold->max && MinDistance > zone->threshold->min) {
    // Someone is in the sensing area
    CurrentZoneStatus = SOMEONE;
    if (presence_sensor != nullptr) {
      presence_sensor->publish_state(true);
    }
  }

  // left zone
  if (zone == (this->invert_direction_ ? this->exit : this->entry)) {
    if (CurrentZoneStatus != LeftPreviousStatus) {
      // event in left zone has occured
      AnEventHasOccured = 1;

      if (CurrentZoneStatus == SOMEONE) {
        AllZonesCurrentStatus += 1;
      }
      // need to check right zone as well ...
      if (RightPreviousStatus == SOMEONE) {
        // event in right zone has occured
        AllZonesCurrentStatus += 2;
      }
      // remember for next time
      LeftPreviousStatus = CurrentZoneStatus;
    }
  }
  // right zone
  else {
    if (CurrentZoneStatus != RightPreviousStatus) {
      // event in right zone has occured
      AnEventHasOccured = 1;
      if (CurrentZoneStatus == SOMEONE) {
        AllZonesCurrentStatus += 2;
      }
      // need to check left zone as well ...
      if (LeftPreviousStatus == SOMEONE) {
        // event in left zone has occured
        AllZonesCurrentStatus += 1;
      }
      // remember for next time
      RightPreviousStatus = CurrentZoneStatus;
    }
  }

  // if an event has occured
  if (AnEventHasOccured) {
    delay(1);
    ESP_LOGD(TAG, "Event has occured, AllZonesCurrentStatus: %d", AllZonesCurrentStatus);
    if (PathTrackFillingSize < 4) {
      PathTrackFillingSize++;
    }

    // if nobody anywhere lets check if an exit or entry has happened
    if ((LeftPreviousStatus == NOBODY) && (RightPreviousStatus == NOBODY)) {
      delay(1);
      ESP_LOGD(TAG, "Nobody anywhere, AllZonesCurrentStatus: %d", AllZonesCurrentStatus);
      // check exit or entry only if PathTrackFillingSize is 4 (for example 0 1
      // 3 2) and last event is 0 (nobobdy anywhere)
      if (PathTrackFillingSize == 4) {
        // check exit or entry. no need to check PathTrack[0] == 0 , it is
        // always the case

        if ((PathTrack[1] == 1) && (PathTrack[2] == 3) && (PathTrack[3] == 2)) {
          // This an exit
          ESP_LOGI("Roode pathTracking", "Exit detected.");
          DistancesTableSize[0] = 0;
          DistancesTableSize[1] = 0;
          this->updateCounter(-1);
          if (entry_exit_event_sensor != nullptr) {
            entry_exit_event_sensor->publish_state("Exit");
          }
        } else if ((PathTrack[1] == 2) && (PathTrack[2] == 3) && (PathTrack[3] == 1)) {
          // This an entry
          ESP_LOGI("Roode pathTracking", "Entry detected.");
          this->updateCounter(1);
          if (entry_exit_event_sensor != nullptr) {
            entry_exit_event_sensor->publish_state("Entry");
          }
          DistancesTableSize[0] = 0;
          DistancesTableSize[1] = 0;
        } else {
          // reset the table filling size also in case of unexpected path
          DistancesTableSize[0] = 0;
          DistancesTableSize[1] = 0;
        }
      }

      PathTrackFillingSize = 1;
    } else {
      // update PathTrack
      // example of PathTrack update
      // 0
      // 0 1
      // 0 1 3
      // 0 1 3 1
      // 0 1 3 3
      // 0 1 3 2 ==> if next is 0 : check if exit
      PathTrack[PathTrackFillingSize - 1] = AllZonesCurrentStatus;
    }
  }
  if (presence_sensor != nullptr) {
    if (CurrentZoneStatus == NOBODY && LeftPreviousStatus == NOBODY && RightPreviousStatus == NOBODY) {
      // nobody is in the sensing area
      presence_sensor->publish_state(false);
    }
  }
}
void Roode::updateCounter(int delta) {
  if (this->people_counter == nullptr) {
    return;
  }
  auto next = this->people_counter->state + (float) delta;
  ESP_LOGI(TAG, "Updating people count: %d", (int) next);
  this->people_counter->set(next);
}
void Roode::recalibration() { calibrateZones(distanceSensor); }

void Roode::setRangingMode(const RangingConfig *mode) {
  time_budget_in_ms = mode->timing_budget;
  delay_between_measurements = mode->delay_between_measurements;

  sensor_status = distanceSensor.SetDistanceMode(mode->mode);
  if (sensor_status != VL53L1_ERROR_NONE) {
    ESP_LOGE(SETUP, "Could not set distance mode.  mode: %d", mode->mode);
  }
  sensor_status = distanceSensor.SetTimingBudgetInMs(mode->timing_budget);
  if (sensor_status != VL53L1_ERROR_NONE) {
    ESP_LOGE(SETUP, "Could not set timing budget.  timing_budget: %d ms", mode->timing_budget);
  }
  sensor_status = distanceSensor.SetInterMeasurementInMs(mode->delay_between_measurements);
  if (sensor_status != VL53L1_ERROR_NONE) {
    ESP_LOGE(SETUP, "Could not set measurement delay.  %d ms", mode->delay_between_measurements);
  }

  ESP_LOGI(SETUP, "Set ranging mode. timing_budget: %d, delay: %d, distance_mode: %d", mode->timing_budget,
           mode->delay_between_measurements, mode->mode);
}

const RangingConfig *Roode::determineRangingMode(uint16_t average_entry_zone_distance,
                                                 uint16_t average_exit_zone_distance) {
  if (this->timing_budget.has_value()) {
    auto time_budget = this->timing_budget.value();
    if (this->distance_mode.has_value()) {
      EDistanceMode &mode = this->distance_mode.value();
      return Ranging::Custom(time_budget, mode);
    }
    return Ranging::Custom(time_budget);
  }

  uint16_t min = average_entry_zone_distance < average_exit_zone_distance ? average_entry_zone_distance
                                                                          : average_exit_zone_distance;
  uint16_t max = average_entry_zone_distance > average_exit_zone_distance ? average_entry_zone_distance
                                                                          : average_exit_zone_distance;
  if (min <= short_distance_threshold) {
    return Ranging::Short;
  }
  if (max > short_distance_threshold && min <= medium_distance_threshold) {
    return Ranging::Medium;
  }
  if (max > medium_distance_threshold && min <= medium_long_distance_threshold) {
    return Ranging::MediumLong;
  }
  if (max > medium_long_distance_threshold && min <= long_distance_threshold) {
    return Ranging::Long;
  }
  return Ranging::Longest;
}

void Roode::calibrateZones(VL53L1X_ULD distanceSensor) {
  ESP_LOGI(SETUP, "Calibrating sensor zone");
  calibrateDistance();

  entry->roi_calibration(distanceSensor, entry->threshold->idle, exit->threshold->idle, orientation_);
  entry->calibrateThreshold(distanceSensor, number_attempts);
  exit->roi_calibration(distanceSensor, entry->threshold->idle, exit->threshold->idle, orientation_);
  exit->calibrateThreshold(distanceSensor, number_attempts);

  publishSensorConfiguration(entry, exit, true);
  App.feed_wdt();
  publishSensorConfiguration(entry, exit, false);
}

void Roode::calibrateDistance() {
  setRangingMode(Ranging::Medium);

  entry->calibrateThreshold(distanceSensor, number_attempts);
  exit->calibrateThreshold(distanceSensor, number_attempts);

  auto *mode = determineRangingMode(entry->threshold->idle, exit->threshold->idle);
  setRangingMode(mode);
}

void Roode::publishSensorConfiguration(Zone *entry, Zone *exit, bool isMax) {
  if (isMax) {
    if (max_threshold_entry_sensor != nullptr) {
      max_threshold_entry_sensor->publish_state(entry->threshold->max);
    }

    if (max_threshold_exit_sensor != nullptr) {
      max_threshold_exit_sensor->publish_state(exit->threshold->max);
    }
  } else {
    if (min_threshold_entry_sensor != nullptr) {
      min_threshold_entry_sensor->publish_state(entry->threshold->min);
    }
    if (min_threshold_exit_sensor != nullptr) {
      min_threshold_exit_sensor->publish_state(exit->threshold->min);
    }
  }

  if (entry_roi_height_sensor != nullptr) {
    entry_roi_height_sensor->publish_state(entry->roi->height);
  }
  if (entry_roi_width_sensor != nullptr) {
    entry_roi_width_sensor->publish_state(entry->roi->width);
  }

  if (exit_roi_height_sensor != nullptr) {
    exit_roi_height_sensor->publish_state(exit->roi->height);
  }
  if (exit_roi_width_sensor != nullptr) {
    exit_roi_width_sensor->publish_state(exit->roi->width);
  }
}
}  // namespace roode
}  // namespace esphome