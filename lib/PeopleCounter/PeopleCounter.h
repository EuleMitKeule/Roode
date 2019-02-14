#ifndef PEOPLECOUNTER_H
#define PEOPLECOUNTER_H

#pragma once

#include <Config.h>
#include <Transmitter.h>
#include <SendCounter.h>
int room_sensor_value;     //analog value store for the room sensor
int corridor_sensor_value; //analog value store for the corridor sensor

template <typename T, typename G>
void peoplecounting(T ROOM_SENSOR, T CORRIDOR_SENSOR, G transmitter)
{
    int starttime = millis();
    int endtime = starttime;
    int inout = -1;                        //if inout== 0 -> out; if inout == 1 --> in; This var describes the direction
    while ((endtime - starttime) <= LTIME) // perform the loop for 10 seconds
    {
#ifdef USE_MQTT
        if (!client.connected())
        { // MQTT connection
            transmitter.reconnect();
        }
        client.loop();
        yield();
#endif
        inout = -1;

#ifdef MY_DEBUG
        // Serial.print("ROOM_SENSOR:");
        // Serial.println(room_sensor_value);
        // Serial.print("ROOM_SENSOR THRESHOLD: ");
        // Serial.println(ROOM_SENSOR.getThreshold());
        // Serial.print("CORRIDOR_SENSOR:");
        // Serial.println(corridor_sensor_value);
        // Serial.print("CORRIDOR_SENSOR THRESHOLD: ");
        // Serial.println(CORRIDOR_SENSOR.getThreshold());

#endif

        // room_sensor_value = ROOM_SENSOR.readData();
        // corridor_sensor_value = CORRIDOR_SENSOR.readData();

        if (ROOM_SENSOR.readData() < ROOM_SENSOR.getThreshold() && CORRIDOR_SENSOR.readData() > CORRIDOR_SENSOR.getThreshold() && inout != 1)
        {
            int startR = millis();
            int endR = startR;
            while ((endR - startR) <= MTIME)
            {
#ifdef USE_MQTT
                yield();
#endif
                // room_sensor_value = ROOM_SENSOR.readData();
                // corridor_sensor_value = CORRIDOR_SENSOR.readData();
                if (CORRIDOR_SENSOR.readData() < CORRIDOR_SENSOR.getThreshold() && ROOM_SENSOR.readData() < ROOM_SENSOR.getThreshold())
                {
#ifdef MY_DEBUG
                    Serial.print("In Loop ROOM_SENSOR: ");
                    Serial.println(room_sensor_value);
                    Serial.print("In Loop CORRIDOR_SENSOR: ");
                    Serial.println(corridor_sensor_value);
                    Serial.print("Delay Time: ");
                    Serial.println(MTIME - (endR - startR));
#endif
                    while (ROOM_SENSOR.readData() < ROOM_SENSOR.getThreshold() || CORRIDOR_SENSOR.readData() < CORRIDOR_SENSOR.getThreshold())
                    {
#ifdef USE_MQTT
                        yield();
#endif
                        // room_sensor_value = ROOM_SENSOR.readData();
                        // corridor_sensor_value = CORRIDOR_SENSOR.readData();
                        if (CORRIDOR_SENSOR.readData() < CORRIDOR_SENSOR.getThreshold() && ROOM_SENSOR.readData() > ROOM_SENSOR.getThreshold())
                        {
                            inout = 0;
                            sendCounter(inout, transmitter);
                            break;
                        }
                    }

                    if (inout == 0)
                    {
                        corridor_sensor_value = 0;
                        endR = millis();
                        starttime = millis();
                        endtime = starttime;
                        break;
                    }
                }
                else
                {
                    endR = millis();
                }
            }
        }
        else
        {
            endtime = millis();
        }

        if (CORRIDOR_SENSOR.readData() < CORRIDOR_SENSOR.getThreshold() && ROOM_SENSOR.readData() > ROOM_SENSOR.getThreshold() && inout != 0)
        {
            int startC = millis();
            int endC = startC;
            while ((endC - startC) <= MTIME)
            {
#ifdef USE_MQTT
                yield();
#endif
                // room_sensor_value = ROOM_SENSOR.readData();
                // corridor_sensor_value = CORRIDOR_SENSOR.readData();
                if (ROOM_SENSOR.readData() < ROOM_SENSOR.getThreshold() && CORRIDOR_SENSOR.readData() < CORRIDOR_SENSOR.getThreshold())
                {
#ifdef MY_DEBUG
                    Serial.print("In Loop CORRIDOR_SENSOR: ");
                    Serial.println(corridor_sensor_value);
                    Serial.print("In Loop ROOM_SENSOR: ");
                    Serial.println(room_sensor_value);
                    Serial.print("Delay Time: ");
                    Serial.println(MTIME - (endC - startC));
#endif
                    while (ROOM_SENSOR.readData() < ROOM_SENSOR.getThreshold() || CORRIDOR_SENSOR.readData() < CORRIDOR_SENSOR.getThreshold())
                    {
#ifdef USE_MQTT
                        yield();
#endif
                        // room_sensor_value = ROOM_SENSOR.readData();
                        // corridor_sensor_value = CORRIDOR_SENSOR.readData();
                        if (ROOM_SENSOR.readData() < ROOM_SENSOR.getThreshold() && CORRIDOR_SENSOR.readData() > CORRIDOR_SENSOR.getThreshold())
                        {
                            inout = 1;
                            sendCounter(inout, transmitter);
                            break;
                        }
                    }

                    if (inout == 1)
                    {
                        room_sensor_value = 0;
                        endC = millis();
                        starttime = millis();
                        endtime = starttime;
                        break;
                    }
                }
                else
                {
                    endC = millis();
                }
            }
        }
        else
        {
            endtime = millis();
        }
    }
}
#endif