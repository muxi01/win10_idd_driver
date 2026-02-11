# IddSampleDriver Software Architecture

## Overview

This project implements an Indirect Display Driver (IDD) that enables Windows applications to stream graphics content to external displays over USB connections. The driver uses Microsoft's IddCx (Indirect Display Driver Class Extension) framework to create virtual display adapters and targets.

## Components Architecture

### 1. Driver Entry Point
- `DriverEntry` - Standard WDM driver entry point
- `IddSampleDeviceAdd` - Device initialization callback

### 2. IddCx Integration Layer
- Implements the IddCx interface for Windows display integration
- Handles enumeration of display targets
- Manages connection between Windows display subsystem and USB output

### 3. Display Management
- Virtual display target creation
- Mode enumeration and setting
- Frame submission handling
- EDID (Extended Display Identification Data) management

### 4. USB Communication Layer
- USB device discovery and enumeration
- Interface configuration and endpoint setup
- Bulk transfer handling for video data
- USB power state management

### 5. Frame Processing Pipeline
- Receives frames from Windows display system
- Encodes/compresses frame data if necessary
- Transmits frames via USB to external display device

## IDD (Indirect Display Driver) Workflow