# IddSampleDriver Architecture Documentation

## Overview
This project implements an Indirect Display Driver (IDD) that enables Windows applications to stream graphics content to external displays over USB connections. The driver uses Microsoft's IddCx (Indirect Display Driver Class Extension) framework to create virtual display adapters and targets, capturing frames from DirectX and transmitting them via USB to external display devices.

## Components Architecture

### 1. Driver Entry Point
- **DriverEntry** - Standard WDM driver entry point, initializes WDF driver program
- **IddSampleDeviceAdd** - Device initialization callback, sets up WDF device and IddCx configuration
- **registry_config_base** - Loads configuration from Windows registry (debug level, etc.)

### 2. IddCx Integration Layer
- Implements the IddCx interface for Windows display integration
- **IddSampleAdapterInitFinished** - Called when adapter initialization completes
- **IddSampleAdapterCommitModes** - Handles mode commitment when display configurations change
- **IddSampleParseMonitorDescription** - Parses monitor descriptions (EDID)
- **IddSampleMonitorGetDefaultModes** - Generates default monitor modes
- **IddSampleMonitorQueryModes** - Queries supported target modes
- **IddSampleMonitorAssignSwapChain** - Assigns swap chain for frame processing
- **IddSampleMonitorUnassignSwapChain** - Unassigns swap chain

### 3. Display Management
- Virtual display target creation with hardcoded EDID
- Mode enumeration and setting supporting multiple resolutions
- Frame submission handling via swap chain mechanism
- EDID (Extended Display Identification Data) management with predefined modes

### 4. USB Communication Layer
- **idd_usbdisp_evt_device_prepareHardware** - Initializes USB device during hardware preparation
- **IddSampleDeviceReleaseHardware** - Releases USB resources during hardware release
- **IddSampleDeviceSurpriseRemoval** - Handles unexpected USB device removal
- Interface configuration and endpoint setup for bulk transfers
- USB device enumeration based on VID/PID matching
- Asynchronous message sending via URB (USB Request Block) mechanism

### 5. Frame Processing Pipeline
- **SwapChainProcessor class** - Core component managing the frame processing loop
- Receives frames from Windows display system via DXGI
- **Direct3DDevice class** - Manages D3D11 device, adapter and device context
- Encodes/compresses frame data using various formats (RGB565, RGB888, JPEG)
- Transmits frames via USB to external display device

## IDD (Indirect Display Driver) Workflow

### Initialization Phase
1. **Driver Loading**: DriverEntry → WdfDriverCreate → IddSampleDeviceAdd
2. **Device Creation**: WDF device creation with IddCx configuration
3. **Power On**: IddSampleDeviceD0Entry → InitAdapter (creates virtual display targets)

### Display Enumeration Phase
1. **Adapter Initialization**: IddSampleAdapterInitFinished → FinishInit → CreateMonitor
2. **Monitor Creation**: Creates virtual monitors with hardcoded EDID
3. **Mode Negotiation**: Mode queries via IddSampleMonitorQueryModes and IddSampleParseMonitorDescription

### Frame Streaming Phase
1. **Swap Chain Assignment**: IddSampleMonitorAssignSwapChain → AssignSwapChain → SwapChainProcessor
2. **Frame Capture Loop**: 
   - IddCxSwapChainReleaseAndAcquireBuffer captures rendered frames
   - D3D texture copying via enc_grab_surface function
   - Frame encoding using selected codec (RGB/JPEG)
   - USB transmission via asynchronous URB requests
3. **Frame Transmission**: Encoded frames sent via usb_send_msg_async

## USB Communication Flow

### Device Initialization
1. **Hardware Preparation**: EvtDevicePrepareHardware → USB device enumeration
2. **Interface Selection**: SelectInterfaces configures appropriate endpoints
3. **Configuration Parsing**: USB device info string parsed to configure display parameters

### Data Transmission
1. **URB Pool Management**: Pre-allocated URB items managed in a SLIST for efficient reuse
2. **Asynchronous Transfer**: Frames sent via usb_send_msg_async without blocking main thread
3. **Zero-Length Packet Handling**: Additional ZLP sent when frame size is multiple of endpoint size

### Error Recovery
1. **Connection Monitoring**: Regular checks for USB device connectivity
2. **Error Handling**: usb_error_recovery handles transmission failures
3. **Performance Tracking**: Frame timing and error statistics maintained

## Key Data Structures

### IndirectDeviceContext
- Stores device state and manages components
- Contains adapter handle, monitor object, and processing thread
- Maintains configuration parameters (resolution, encoding type, etc.)

### SwapChainProcessor
- Implements the frame processing thread
- Manages Direct3D device and USB communication
- Handles frame capture, encoding, and transmission
- Maintains performance statistics

### USB Context
- Tracks USB device connection state (connected/disconnected)
- Manages bulk read/write pipes
- Maintains URB list for asynchronous operations
- Stores device-specific configuration from USB string descriptors

## Threading Model

### Main Device Thread
- Handles WDF events and IddCx callbacks
- Manages device lifecycle (creation, power state changes)

### Swap Chain Processing Thread
- Runs in SwapChainProcessor::RunCore
- Uses multimedia thread characteristics for improved performance
- Handles frame acquisition and processing loop
- Implements frame rate limiting

### USB Completion Thread
- Handles asynchronous USB completion callbacks
- Processes completed URBs and returns them to pool
- Manages error recovery scenarios

## Performance Considerations

### Frame Rate Control
- Tools-based frame timing with adjustable target FPS
- Performance statistics collection for monitoring
- Adaptive behavior based on USB connection state

### Memory Management
- Pre-allocated URB pools for zero-allocation during frame processing
- Staging textures for CPU-accessible frame data
- Efficient buffer reuse patterns

### Power Management
- Proper handling of device power state transitions
- USB remote wake capability detection
- Surprise removal handling for robust disconnection