# Smart Women Safety Wearable Using ESP32-CAM

## Overview

An IoT-based wearable safety device designed to enhance women's safety through autonomous emergency detection and real-time evidence capture.

The system automatically sends emergency alerts containing images, GPS location, and timestamps to trusted contacts using Telegram.

## Features

- SOS Button Emergency Trigger
- Loud Sound Distress Detection
- Tamper Detection
- Real-Time Image Capture
- GPS Location Tracking
- RTC Time Stamping
- Telegram Alert Notifications
- Cloud-Based Evidence Storage

## Hardware Components

- ESP32-CAM
- NEO-6M GPS Module
- DS3231 RTC Module
- SW-420 Vibration Sensor
- Microphone Sensor
- SOS Push Button
- Rechargeable Battery

## Working

1. Continuously monitors sensors.
2. Detects:
   - SOS Emergency
   - Loud Sound Distress
   - Tamper Alert
3. Captures image using ESP32-CAM.
4. Retrieves GPS coordinates and timestamp.
5. Sends alert via Telegram Bot.

## Technologies Used

- Embedded C
- Arduino IDE
- ESP32-CAM
- IoT
- GPS
- Telegram Bot API

## Applications

- Women's Safety
- Elderly Monitoring
- Lone Worker Protection
- Emergency Documentation

## Authors

Nandana S  
Department of Electrical and Electronics Engineering  
Amrita Vishwa Vidyapeetham
