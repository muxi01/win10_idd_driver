/*++

Copyright (c) Microsoft Corporation

Abstract:

    This module contains a sample implementation of an indirect display driver. See the included README.md file and the
    various TODO blocks throughout this file and all accompanying files for information on building a production driver.

    MSDN documentation on indirect displays can be found at https://msdn.microsoft.com/en-us/library/windows/hardware/mt761968(v=vs.85).aspx.

Environment:

    User Mode, UMDF

--*/

#include "Driver.h"
#include "Driver.tmh"
#include "usb_driver.h"
#include "image_encoder.h"
#include "tools.h"
#include <stdarg.h>

using namespace std;
using namespace Microsoft::IndirectDisp;
using namespace Microsoft::WRL;

extern "C" DRIVER_INITIALIZE DriverEntry;

LONG debug_level = LOG_LEVEL_INFO;

VOID registry_config_base(void);
int enc_grab_surface(std::shared_ptr<Direct3DDevice> m_Device,ComPtr<IDXGIResource> AcquiredBuffer, uint8_t *fb_buf,D3D11_TEXTURE2D_DESC *);

EVT_WDF_DRIVER_DEVICE_ADD IddSampleDeviceAdd;
EVT_WDF_DEVICE_D0_ENTRY IddSampleDeviceD0Entry;
EVT_WDF_DEVICE_PREPARE_HARDWARE IddSampleDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE IddSampleDeviceReleaseHardware;
EVT_WDF_DEVICE_SURPRISE_REMOVAL IddSampleDeviceSurpriseRemoval;

EVT_IDD_CX_ADAPTER_INIT_FINISHED IddSampleAdapterInitFinished;
EVT_IDD_CX_ADAPTER_COMMIT_MODES IddSampleAdapterCommitModes;

EVT_IDD_CX_PARSE_MONITOR_DESCRIPTION IddSampleParseMonitorDescription;
EVT_IDD_CX_MONITOR_GET_DEFAULT_DESCRIPTION_MODES IddSampleMonitorGetDefaultModes;
EVT_IDD_CX_MONITOR_QUERY_TARGET_MODES IddSampleMonitorQueryModes;

EVT_IDD_CX_MONITOR_ASSIGN_SWAPCHAIN IddSampleMonitorAssignSwapChain;
EVT_IDD_CX_MONITOR_UNASSIGN_SWAPCHAIN IddSampleMonitorUnassignSwapChain;

// This macro creates the methods for accessing an IndirectDeviceContextWrapper as a context for a WDF object
WDF_DECLARE_CONTEXT_TYPE(IndirectDeviceContextWrapper);

extern "C" BOOL WINAPI DllMain(
    _In_ HINSTANCE hInstance,
    _In_ UINT dwReason,
    _In_opt_ LPVOID lpReserved)
{
    UNREFERENCED_PARAMETER(hInstance);
    UNREFERENCED_PARAMETER(lpReserved);
    UNREFERENCED_PARAMETER(dwReason);

    return TRUE;
}

_Use_decl_annotations_
extern "C" NTSTATUS DriverEntry(
    PDRIVER_OBJECT  pDriverObject,
    PUNICODE_STRING pRegistryPath
)
{
    WDF_DRIVER_CONFIG Config;
    NTSTATUS Status;

    WDF_OBJECT_ATTRIBUTES Attributes;
    WDF_OBJECT_ATTRIBUTES_INIT(&Attributes);

    registry_config_base();
    LOGI("IDD USB Graphic Driver v1.0\n");

    WDF_DRIVER_CONFIG_INIT(&Config,
        IddSampleDeviceAdd
    );

    Status = WdfDriverCreate(pDriverObject, pRegistryPath, &Attributes, &Config, WDF_NO_HANDLE);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    return Status;
}

_Use_decl_annotations_
NTSTATUS IddSampleDeviceAdd(WDFDRIVER Driver, PWDFDEVICE_INIT pDeviceInit)
{
    NTSTATUS Status = STATUS_SUCCESS;
    WDF_PNPPOWER_EVENT_CALLBACKS PnpPowerCallbacks;

    UNREFERENCED_PARAMETER(Driver);

    // Register for power callbacks - in this sample only power-on is needed
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&PnpPowerCallbacks);
    PnpPowerCallbacks.EvtDeviceD0Entry = IddSampleDeviceD0Entry;
    PnpPowerCallbacks.EvtDevicePrepareHardware = IddSampleDevicePrepareHardware;
    PnpPowerCallbacks.EvtDeviceReleaseHardware = IddSampleDeviceReleaseHardware;
    PnpPowerCallbacks.EvtDeviceSurpriseRemoval = IddSampleDeviceSurpriseRemoval;
    WdfDeviceInitSetPnpPowerEventCallbacks(pDeviceInit, &PnpPowerCallbacks);

    IDD_CX_CLIENT_CONFIG IddConfig;
    IDD_CX_CLIENT_CONFIG_INIT(&IddConfig);

    IddConfig.EvtIddCxAdapterInitFinished = IddSampleAdapterInitFinished;

    IddConfig.EvtIddCxParseMonitorDescription = IddSampleParseMonitorDescription;
    IddConfig.EvtIddCxMonitorGetDefaultDescriptionModes = IddSampleMonitorGetDefaultModes;
    IddConfig.EvtIddCxMonitorQueryTargetModes = IddSampleMonitorQueryModes;
    IddConfig.EvtIddCxAdapterCommitModes = IddSampleAdapterCommitModes;
    IddConfig.EvtIddCxMonitorAssignSwapChain = IddSampleMonitorAssignSwapChain;
    IddConfig.EvtIddCxMonitorUnassignSwapChain = IddSampleMonitorUnassignSwapChain;

    Status = IddCxDeviceInitConfig(pDeviceInit, &IddConfig);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    WDF_OBJECT_ATTRIBUTES Attr;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attr, IndirectDeviceContextWrapper);
    Attr.EvtCleanupCallback = [](WDFOBJECT Object)
    {
        // Automatically cleanup the context when the WDF object is about to be deleted
        auto* pContext = WdfObjectGet_IndirectDeviceContextWrapper(Object);
        if (pContext)
        {
            pContext->Cleanup();
        }
    };

    WDFDEVICE Device = nullptr;
    Status = WdfDeviceCreate(&pDeviceInit, &Attr, &Device);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = IddCxDeviceInitialize(Device);

    // Create a new device context object and attach it to the WDF device object
    auto* pContext = WdfObjectGet_IndirectDeviceContextWrapper(Device);
    pContext->pContext = new IndirectDeviceContext(Device);

    // Initialize USB device context fields
    pContext->UsbDevice = NULL;
    pContext->BulkReadPipe = NULL;
    pContext->BulkWritePipe = NULL;
    pContext->purb_list = NULL;

    // Set default values
    pContext->display_config.w = 1920;
    pContext->display_config.h = 1080;
    pContext->display_config.enc = IMAGE_TYPE_RGB888;
    pContext->display_config.quality = 5;
    pContext->display_config.fps = 60;
    pContext->display_config.blimit = 1920 * 1080 * 4;
    pContext->display_config.dbg_mode = 0;

    // Initialize error recovery and performance tracking
    pContext->usb_state = USB_STATE_DISCONNECTED;
    tools_perf_stats_init(&pContext->perf_stats);

    return Status;
}

_Use_decl_annotations_
NTSTATUS IddSampleDeviceD0Entry(WDFDEVICE Device, WDF_POWER_DEVICE_STATE PreviousState)
{
    UNREFERENCED_PARAMETER(PreviousState);

    // This function is called by WDF to start the device in the fully-on power state.

    auto* pContext = WdfObjectGet_IndirectDeviceContextWrapper(Device);
    pContext->pContext->InitAdapter();

    return STATUS_SUCCESS;
}

#pragma region Direct3DDevice

Direct3DDevice::Direct3DDevice(LUID AdapterLuid) : AdapterLuid(AdapterLuid)
{

}

Direct3DDevice::Direct3DDevice()
    : AdapterLuid{}, DxgiFactory{}, Adapter{}, Device{}, DeviceContext{}
{

}

HRESULT Direct3DDevice::Init()
{
    // The DXGI factory could be cached, but if a new render adapter appears on the system, a new factory needs to be
    // created. If caching is desired, check DxgiFactory->IsCurrent() each time and recreate the factory if !IsCurrent.
    HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&DxgiFactory));
    if (FAILED(hr))
    {
        return hr;
    }

    // Find the specified render adapter
    hr = DxgiFactory->EnumAdapterByLuid(AdapterLuid, IID_PPV_ARGS(&Adapter));
    if (FAILED(hr))
    {
        return hr;
    }

    // Create a D3D device using the render adapter. BGRA support is required by the WHQL test suite.
    hr = D3D11CreateDevice(Adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &Device, nullptr, &DeviceContext);
    if (FAILED(hr))
    {
        // If creating the D3D device failed, it's possible the render GPU was lost (e.g. detachable GPU) or else the
        // system is in a transient state.
        return hr;
    }

    return S_OK;
}

#pragma endregion

#pragma region Helper Functions

void IndirectDeviceContextWrapper::Cleanup()
{
    delete pContext;
    pContext = nullptr;
}

void SwapChainProcessor::decision_runtime_encoder(WDFDEVICE Device)
{
    auto* pDeviceContext = WdfObjectGet_IndirectDeviceContextWrapper(Device);

    LOGI("Creating encoder: type=%d, quality=%d\n", pDeviceContext->display_config.enc, pDeviceContext->display_config.quality);

    switch (pDeviceContext->display_config.enc) {
        case IMAGE_TYPE_RGB565:
            encoder = image_encoder_create_rgb565();
            LOGI("Created RGB565 encoder\n");
            break;

        case IMAGE_TYPE_RGB888:
            encoder = image_encoder_create_rgb888();
            LOGI("Created RGB888 encoder\n");
            break;

        case IMAGE_TYPE_JPG:
            encoder = image_encoder_create_jpeg(pDeviceContext->display_config.quality);
            LOGI("Created JPEG encoder with quality=%d\n", pDeviceContext->display_config.quality);
            break;

        default:
            encoder = image_encoder_create_rgb888();
            LOGW("Unknown encoder type=%d, using RGB888 as default\n", pDeviceContext->display_config.enc);
            break;
    }
}

int enc_grab_surface(std::shared_ptr<Direct3DDevice> m_Device, ComPtr<IDXGIResource> AcquiredBuffer, uint8_t* fb_buf, D3D11_TEXTURE2D_DESC* pframeDescriptor)
{
    HRESULT hr;
    ID3D11Texture2D* pAcquiredImage = NULL;
    ID3D11Texture2D* pStagingTexture = NULL;
    IDXGISurface* pStagingSurface = NULL;
    int result = -1;

    hr = AcquiredBuffer->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&pAcquiredImage));
    if (FAILED(hr)) {
        LOGE("Failed to query ID3D11Texture2D: 0x%x\n", hr);
        goto cleanup;
    }

    D3D11_TEXTURE2D_DESC srcDesc;
    pAcquiredImage->GetDesc(&srcDesc);

    D3D11_TEXTURE2D_DESC stagingDesc = srcDesc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.BindFlags = 0;
    stagingDesc.MiscFlags = 0;
    stagingDesc.MipLevels = 1;
    stagingDesc.ArraySize = 1;
    stagingDesc.SampleDesc.Count = 1;

    *pframeDescriptor = stagingDesc;

    hr = m_Device->Device->CreateTexture2D(&stagingDesc, NULL, &pStagingTexture);
    if (FAILED(hr)) {
        LOGE("Failed to create staging texture: 0x%x\n", hr);
        goto cleanup;
    }

    m_Device->DeviceContext->CopyResource(pStagingTexture, pAcquiredImage);

    hr = pStagingTexture->QueryInterface(__uuidof(IDXGISurface), reinterpret_cast<void**>(&pStagingSurface));
    if (FAILED(hr)) {
        LOGE("Failed to query IDXGISurface: 0x%x\n", hr);
        goto cleanup;
    }

    DXGI_MAPPED_RECT mappedRect;
    hr = pStagingSurface->Map(&mappedRect, DXGI_MAP_READ);
    if (FAILED(hr)) {
        LOGE("Failed to map staging surface: 0x%x\n", hr);
        goto cleanup;
    }

    const int expected_pitch = stagingDesc.Width * 4;
    if (mappedRect.Pitch == expected_pitch) {
        memcpy(fb_buf, mappedRect.pBits, stagingDesc.Width * stagingDesc.Height * 4);
    } else {
        for (UINT i = 0; i < stagingDesc.Height; i++) {
            memcpy(&fb_buf[expected_pitch * i],&mappedRect.pBits[mappedRect.Pitch * i],expected_pitch);
        }
    }

    pStagingSurface->Unmap();
    result = 0;

cleanup:
    if (pStagingSurface) pStagingSurface->Release();
    if (pStagingTexture) pStagingTexture->Release();
    if (pAcquiredImage) pAcquiredImage->Release();

    return result;
}

VOID registry_config_base(void)
{
    HKEY hKey = NULL;
    const LPCTSTR REGISTRY_PATH = TEXT("SYSTEM\\CurrentControlSet\\Services\\IddSampleDriver\\Parameters");

    LONG result = RegOpenKeyEx(
        HKEY_LOCAL_MACHINE,
        REGISTRY_PATH,
        0,
        KEY_QUERY_VALUE | KEY_READ,
        &hKey);

    if (result == ERROR_SUCCESS) {
        DWORD dwType = REG_DWORD;
        DWORD dwValue = 0;
        DWORD dwSize = sizeof(dwValue);

        result = RegQueryValueEx(
            hKey,
            TEXT("debug_level"),
            NULL,
            &dwType,
            (LPBYTE)&dwValue,
            &dwSize);

        if (result == ERROR_SUCCESS) {
            debug_level = (LONG)dwValue;
            LOGI("Loaded debug_level from registry: %d\n", debug_level);
        } else {
            LOGI("debug_level not found in registry, using default\n");
        }

        RegCloseKey(hKey);
    } else {
        LOGI("Could not open registry key: %s\n", REGISTRY_PATH);
    }

    LOGI("Registry config loaded, debug_level=%d\n", debug_level);
}

#pragma endregion


#pragma region SwapChainProcessor

SwapChainProcessor::SwapChainProcessor(IDDCX_SWAPCHAIN hSwapChain, std::shared_ptr<Direct3DDevice> Device, WDFDEVICE WdfDevice, HANDLE NewFrameEvent)
    : m_hSwapChain(hSwapChain), m_Device(Device), mp_WdfDevice(WdfDevice), m_hAvailableBufferEvent(NewFrameEvent),
      encoder(nullptr), urb_list{}, max_out_pkg_size(0), fb_buf{}
{
    auto* pContext = WdfObjectGet_IndirectDeviceContextWrapper(WdfDevice);

    m_hTerminateEvent.Attach(CreateEvent(nullptr, FALSE, FALSE, nullptr));

    usb_transf_init(&urb_list);
    pContext->purb_list = &urb_list;
    LOGI("create SwapChainProcessor");

    // Immediately create and run the swap-chain processing thread, passing 'this' as the thread parameter
    m_hThread.Attach(CreateThread(nullptr, 0, RunThread, this, 0, nullptr));
}

SwapChainProcessor::~SwapChainProcessor()
{
    // Alert the swap-chain processing thread to terminate
    auto* pContext = WdfObjectGet_IndirectDeviceContextWrapper(this->mp_WdfDevice);
    SetEvent(m_hTerminateEvent.Get());
    pContext->purb_list = NULL;
    LOGI("destroy SwapChainProcessor");

    // Clean up encoder
    if (encoder != NULL) {
        image_encoder_destroy(encoder);
        encoder = NULL;
    }

    usb_transf_exit(&urb_list);

    if (m_hThread.Get()) {
        // Wait for the thread to terminate
        WaitForSingleObject(m_hThread.Get(), INFINITE);
    }
}

DWORD CALLBACK SwapChainProcessor::RunThread(LPVOID Argument)
{
    reinterpret_cast<SwapChainProcessor*>(Argument)->Run();
    return 0;
}

void SwapChainProcessor::Run()
{
    // For improved performance, make use of the Multimedia Class Scheduler Service, which will intelligently
    // prioritize this thread for improved throughput in high CPU-load scenarios.
    DWORD AvTask = 0;

    HANDLE AvTaskHandle = AvSetMmThreadCharacteristicsW(L"Distribution", &AvTask);

    decision_runtime_encoder(mp_WdfDevice);
    RunCore();

    // Always delete the swap-chain object when swap-chain processing loop terminates in order to kick the system to
    // provide a new swap-chain if necessary.
    WdfObjectDelete((WDFOBJECT)m_hSwapChain);
    m_hSwapChain = nullptr;

    AvRevertMmThreadCharacteristics(AvTaskHandle);
}

void SwapChainProcessor::RunCore()
{
    ComPtr<IDXGIDevice> DxgiDevice;
    HRESULT hr = m_Device->Device.As(&DxgiDevice);
    auto* pContext = WdfObjectGet_IndirectDeviceContextWrapper(mp_WdfDevice);

    if (FAILED(hr)) {
        LOGE("Failed to get DXGI device: 0x%x\n", hr);
        return;
    }

    IDARG_IN_SWAPCHAINSETDEVICE SetDevice = {};
    SetDevice.pDevice = DxgiDevice.Get();

    hr = IddCxSwapChainSetDevice(m_hSwapChain, &SetDevice);
    if (FAILED(hr)) {
        LOGE("Failed to set swap-chain device: 0x%x\n", hr);
        return;
    }

    // Set USB state to connected
    usb_device_connect(mp_WdfDevice);

    LOGI("SwapChainProcessor started, FPS target: %d\n", pContext->display_config.fps);

    // Print performance stats every 100 frames
    const int stats_print_interval = 100;

    for (;;) {
        ComPtr<IDXGIResource> AcquiredBuffer;
        IDARG_OUT_RELEASEANDACQUIREBUFFER Buffer = {};
        hr = IddCxSwapChainReleaseAndAcquireBuffer(m_hSwapChain, &Buffer);

        if (hr == E_PENDING) {
            HANDLE WaitHandles[] = {
                m_hAvailableBufferEvent,
                m_hTerminateEvent.Get()
            };

            DWORD WaitResult = WaitForMultipleObjects(ARRAYSIZE(WaitHandles), WaitHandles, FALSE, 16);

            if (WaitResult == WAIT_OBJECT_0 || WaitResult == WAIT_TIMEOUT) {
                continue;
            }
            else if (WaitResult == WAIT_OBJECT_0 + 1) {
                LOGI("SwapChainProcessor termination requested\n");
                break;
            }
            else {
                hr = HRESULT_FROM_WIN32(WaitResult);
                LOGE("WaitForMultipleObjects failed: 0x%x\n", hr);
                break;
            }
        }
        else if (SUCCEEDED(hr)) {
            AcquiredBuffer.Attach(Buffer.MetaData.pSurface);

            if (pContext->display_config.dbg_mode) {
                LOGD("Frame dropped: dbg_mode=%d\n", pContext->display_config.dbg_mode);
                pContext->perf_stats.dropped_frames++;
                goto next_frame;
            }

            // Check USB state
            usb_connection_state_t state;
            usb_check_connection_state(mp_WdfDevice, &state);

            if (state != USB_STATE_CONNECTED) {
                LOGW("USB not connected (state=%d), frame dropped\n", state);
                pContext->perf_stats.dropped_frames++;
                goto next_frame;
            }

            PSLIST_ENTRY pentry = InterlockedPopEntrySList(&urb_list);
            urb_item_t* purb = (urb_item_t*)pentry;

            if (purb != NULL) {
                int64_t grab_start = tools_get_time_us();

                D3D11_TEXTURE2D_DESC frameDescriptor;
                enc_grab_surface(m_Device, AcquiredBuffer, fb_buf, &frameDescriptor);

                int64_t grab_end = tools_get_time_us();

                const int payload_offset = sizeof(image_frame_header_t);
                int total_bytes = encoder->encode(
                    encoder,
                    &purb->urb_msg[payload_offset],
                    fb_buf,
                    0, 0,
                    frameDescriptor.Width - 1,
                    frameDescriptor.Height - 1,
                    frameDescriptor.Width,
                    pContext->display_config.blimit);

                int64_t encode_end = tools_get_time_us();

                encoder->encode_header(encoder, purb->urb_msg, 0, 0, frameDescriptor.Width - 1, frameDescriptor.Height - 1, total_bytes);
                total_bytes += sizeof(image_frame_header_t);

                LOGD("Sending URB id:%d, size:%d bytes\n", purb->id, total_bytes);
                NTSTATUS ret = usb_send_msg_async(purb, pContext->BulkWritePipe, purb->Request, purb->urb_msg, total_bytes);

                if (total_bytes % pContext->max_out_pkg_size == 0) {
                    PSLIST_ENTRY zlp_entry = InterlockedPopEntrySList(&urb_list);
                    urb_item_t* zlp_urb = (urb_item_t*)zlp_entry;

                    if (zlp_urb != NULL) {
                        image_setup_frame_header(zlp_urb->urb_msg, 0xff, 0, 0);
                        usb_send_msg_async(zlp_urb, pContext->BulkWritePipe, zlp_urb->Request, zlp_urb->urb_msg, sizeof(image_frame_header_t));
                        LOGD("Sent ZLP for URB id:%d (ep_size:%d)\n", zlp_urb->id, pContext->max_out_pkg_size);
                    } else {
                        LOGW("No URB available for ZLP\n");
                    }
                }

                int64_t send_end = tools_get_time_us();
                const int64_t grab_time = grab_end - grab_start;
                const int64_t encode_time = encode_end - grab_end;
                const int64_t send_time = send_end - encode_end;
                const int64_t total_time = send_end - grab_start;

                LOGM("[Frame] id:%d size:%d grab:%lldus encode:%lldus send:%lldus total:%lldus\n",
                     purb->id, total_bytes, grab_time, encode_time, send_time, total_time);

                // Update performance statistics
                tools_perf_stats_update(&pContext->perf_stats, total_bytes,
                                      grab_time, encode_time, send_time,
                                      NT_SUCCESS(ret));

                // Print stats periodically
                if (pContext->perf_stats.total_frames % stats_print_interval == 0) {
                    tools_perf_stats_print(&pContext->perf_stats);
                }

                // Error recovery
                if (!NT_SUCCESS(ret)) {
                    LOGW("USB send failed with status 0x%x, attempting recovery\n", ret);
                    NTSTATUS recovery_status = usb_error_recovery(mp_WdfDevice, ret);
                    if (!NT_SUCCESS(recovery_status)) {
                        LOGE("USB error recovery failed: 0x%x\n", recovery_status);
                    }
                }

                tools_sample_tick(pContext->display_config.fps);
            } else {
                LOGW("No URB available, frame dropped\n");
                pContext->perf_stats.dropped_frames++;
            }

        next_frame:
            AcquiredBuffer.Reset();
            hr = IddCxSwapChainFinishedProcessingFrame(m_hSwapChain);
            if (FAILED(hr)) {
                LOGE("Failed to finish processing frame: 0x%x\n", hr);
                break;
            }
        }
        else {
            LOGE("Swap-chain abandoned, exiting loop: 0x%x\n", hr);
            break;
        }
    }

    // Print final statistics
    tools_perf_stats_print(&pContext->perf_stats);

    LOGI("SwapChainProcessor exiting\n");
}

#pragma endregion




#pragma region IndirectDeviceContext

const UINT64 MHZ = 1000000;
const UINT64 KHZ = 1000;

constexpr DISPLAYCONFIG_VIDEO_SIGNAL_INFO dispinfo(UINT32 h, UINT32 v) {
    const UINT32 clock_rate = 60 * (v + 4) * (v + 4) + 1000;
    return {
      clock_rate,                                      // pixel clock rate [Hz]
    { clock_rate, v + 4 },                         // fractional horizontal refresh rate [Hz]
    { clock_rate, (v + 4) * (v + 4) },          // fractional vertical refresh rate [Hz]
    { h, v },                                    // (horizontal, vertical) active pixel resolution
    { h + 4, v + 4 },                         // (horizontal, vertical) total pixel resolution
    { { 255, 0 }},                                   // video standard and vsync divider
    DISPLAYCONFIG_SCANLINE_ORDERING_PROGRESSIVE
    };
}
// A list of modes exposed by the sample monitor EDID - FOR SAMPLE PURPOSES ONLY
const DISPLAYCONFIG_VIDEO_SIGNAL_INFO IndirectDeviceContext::s_KnownMonitorModes[] =
{
    // 640 x 480 @ 60Hz
    {
          25249 * KHZ,                                   // pixel clock rate [Hz]
        { 25249 * KHZ, 640 + 160 },                      // fractional horizontal refresh rate [Hz]
        { 25249 * KHZ, (640 + 160) * (480 + 46) },       // fractional vertical refresh rate [Hz]
        { 640, 480 },                                    // (horizontal, vertical) active pixel resolution
        { 640 + 160, 480 + 46 },                         // (horizontal, vertical) blanking pixel resolution
        { { 255, 0 } },                                  // video standard and vsync divider
        DISPLAYCONFIG_SCANLINE_ORDERING_PROGRESSIVE
    },
    // 800 x 480 @ 60Hz
    {
          29500 * KHZ,                                   // pixel clock rate [Hz] (29.5 MHz)
        { 29500 * KHZ, 800 + 216 },                      // fractional horizontal refresh rate [Hz]
        { 29500 * KHZ, (800 + 216) * (480 + 23) },       // fractional vertical refresh rate [Hz]
        { 800, 480 },                                    // (horizontal, vertical) active pixel resolution
        { 800 + 216, 480 + 23 },                         // (horizontal, vertical) total pixel resolution
        { { 255, 0 } },                                  // video standard and vsync divider
        DISPLAYCONFIG_SCANLINE_ORDERING_PROGRESSIVE
    },
    // 800 x 600 @ 60Hz
    {
          40 * MHZ,                                      // pixel clock rate [Hz]
        { 40 * MHZ, 800 + 256 },                         // fractional horizontal refresh rate [Hz]
        { 40 * MHZ, (800 + 256) * (600 + 28) },          // fractional vertical refresh rate [Hz]
        { 800, 600 },                                    // (horizontal, vertical) active pixel resolution
        { 800 + 256, 600 + 28 },                         // (horizontal, vertical) total pixel resolution
        { { 255, 0 }},                                   // video standard and vsync divider
        DISPLAYCONFIG_SCANLINE_ORDERING_PROGRESSIVE
    },
    // 1920 x 1280 @ 60Hz
{
      40 * MHZ,                                      // pixel clock rate [Hz]
    { 40 * MHZ, 800 + 256 },                         // fractional horizontal refresh rate [Hz]
    { 40 * MHZ, (800 + 256) * (600 + 28) },          // fractional vertical refresh rate [Hz]
    { 1920, 1280 },                                    // (horizontal, vertical) active pixel resolution
    { 1920 + 256, 1280 + 28 },                         // (horizontal, vertical) total pixel resolution
    { { 255, 0 }},                                   // video standard and vsync divider
    DISPLAYCONFIG_SCANLINE_ORDERING_PROGRESSIVE
},
dispinfo(1920, 1200),
dispinfo(1920, 1440),
dispinfo(2560, 1440),
dispinfo(2560, 1600),
dispinfo(2880, 1620),
dispinfo(2880, 1800),
dispinfo(3008, 1692),
dispinfo(3200, 1800),
dispinfo(3200, 2400),
dispinfo(3840, 2160),
dispinfo(3840, 2400),
dispinfo(4096, 2304),
dispinfo(4096, 2560),
dispinfo(5120, 2880),
dispinfo(6016, 3384),
dispinfo(7680, 4320),
};

// This is a sample monitor EDID - FOR SAMPLE PURPOSES ONLY
const BYTE IndirectDeviceContext::s_KnownMonitorEdid[] =
{
  /*  0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,0x79,0x5E,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xA6,0x01,0x03,0x80,0x28,
    0x1E,0x78,0x0A,0xEE,0x91,0xA3,0x54,0x4C,0x99,0x26,0x0F,0x50,0x54,0x20,0x00,0x00,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0xA0,0x0F,0x20,0x00,0x31,0x58,0x1C,0x20,0x28,0x80,0x14,0x00,
    0x90,0x2C,0x11,0x00,0x00,0x1E,0x00,0x00,0x00,0x10,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x10,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x10,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x6E */

    0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x31, 0xD8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x05, 0x16, 0x01, 0x03, 0x6D, 0x32, 0x1C, 0x78, 0xEA, 0x5E, 0xC0, 0xA4, 0x59, 0x4A, 0x98, 0x25,
    0x20, 0x50, 0x54, 0x00, 0x00, 0x00, 0xD1, 0xC0, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x3A, 0x80, 0x18, 0x71, 0x38, 0x2D, 0x40, 0x58, 0x2C,
    0x45, 0x00, 0xF4, 0x19, 0x11, 0x00, 0x00, 0x1E, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x4C, 0x69, 0x6E,
    0x75, 0x78, 0x20, 0x23, 0x30, 0x0A, 0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0xFD, 0x00, 0x3B,
    0x3D, 0x42, 0x44, 0x0F, 0x00, 0x0A, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0xFC,
    0x00, 0x4C, 0x69, 0x6E, 0x75, 0x78, 0x20, 0x46, 0x48, 0x44, 0x0A, 0x20, 0x20, 0x20, 0x00, 0x05
    
};

IndirectDeviceContext::IndirectDeviceContext(_In_ WDFDEVICE WdfDevice) :
    m_WdfDevice(WdfDevice), m_Adapter(nullptr), m_Monitor(nullptr)
{
}

IndirectDeviceContext::~IndirectDeviceContext()
{
    m_ProcessingThread.reset();
}

#define NUM_VIRTUAL_DISPLAYS 1

void IndirectDeviceContext::InitAdapter()
{
    // ==============================
    // TODO: Update the below diagnostic information in accordance with the target hardware. The strings and version
    // numbers are used for telemetry and may be displayed to the user in some situations.
    //
    // This is also where static per-adapter capabilities are determined.
    // ==============================
    
    IDDCX_ADAPTER_CAPS AdapterCaps = {};
    AdapterCaps.Size = sizeof(AdapterCaps);
    
    // Declare basic feature support for the adapter (required)
    AdapterCaps.MaxMonitorsSupported = NUM_VIRTUAL_DISPLAYS;
    AdapterCaps.EndPointDiagnostics.Size = sizeof(AdapterCaps.EndPointDiagnostics);
    AdapterCaps.EndPointDiagnostics.GammaSupport = IDDCX_FEATURE_IMPLEMENTATION_NONE;
    AdapterCaps.EndPointDiagnostics.TransmissionType = IDDCX_TRANSMISSION_TYPE_WIRED_OTHER;
    
    // Declare your device strings for telemetry (required)
    AdapterCaps.EndPointDiagnostics.pEndPointFriendlyName = L"IddSample Device";
    AdapterCaps.EndPointDiagnostics.pEndPointManufacturerName = L"Microsoft";
    AdapterCaps.EndPointDiagnostics.pEndPointModelName = L"IddSample Model";

    // Declare your hardware and firmware versions (required)
    IDDCX_ENDPOINT_VERSION Version = {};
    Version.Size = sizeof(Version);
    Version.MajorVer = 1;
    AdapterCaps.EndPointDiagnostics.pFirmwareVersion = &Version;
    AdapterCaps.EndPointDiagnostics.pHardwareVersion = &Version;

    // Initialize a WDF context that can store a pointer to the device context object
    WDF_OBJECT_ATTRIBUTES Attr;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attr, IndirectDeviceContextWrapper);

    IDARG_IN_ADAPTER_INIT AdapterInit = {};
    AdapterInit.WdfDevice = m_WdfDevice;
    AdapterInit.pCaps = &AdapterCaps;
    AdapterInit.ObjectAttributes = &Attr;

    // Start the initialization of the adapter, which will trigger the AdapterFinishInit callback later
    IDARG_OUT_ADAPTER_INIT AdapterInitOut;
    NTSTATUS Status = IddCxAdapterInitAsync(&AdapterInit, &AdapterInitOut);

    if (NT_SUCCESS(Status))
    {
        // Store a reference to the WDF adapter handle
        m_Adapter = AdapterInitOut.AdapterObject;

        // Store the device context object into the WDF object context
        auto* pContext = WdfObjectGet_IndirectDeviceContextWrapper(AdapterInitOut.AdapterObject);
        pContext->pContext = this;
    }
}

void IndirectDeviceContext::FinishInit()
{
    for (unsigned int i = 0; i < NUM_VIRTUAL_DISPLAYS; i++) {
        CreateMonitor(i);
    }
} 

void IndirectDeviceContext::CreateMonitor(unsigned int index) {
    // ==============================
    // TODO: In a real driver, the EDID should be retrieved dynamically from a connected physical monitor. The EDID
    // provided here is purely for demonstration, as it describes only 640x480 @ 60 Hz and 800x600 @ 60 Hz. Monitor
    // manufacturers are required to correctly fill in physical monitor attributes in order to allow the OS to optimize
    // settings like viewing distance and scale factor. Manufacturers should also use a unique serial number every
    // single device to ensure the OS can tell the monitors apart.
    // ==============================

    WDF_OBJECT_ATTRIBUTES Attr;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attr, IndirectDeviceContextWrapper);

    IDDCX_MONITOR_INFO MonitorInfo = {};
    MonitorInfo.Size = sizeof(MonitorInfo);
    MonitorInfo.MonitorType = DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HDMI;
    MonitorInfo.ConnectorIndex = index;
    MonitorInfo.MonitorDescription.Size = sizeof(MonitorInfo.MonitorDescription);
    MonitorInfo.MonitorDescription.Type = IDDCX_MONITOR_DESCRIPTION_TYPE_EDID;
    MonitorInfo.MonitorDescription.DataSize = sizeof(s_KnownMonitorEdid);
    MonitorInfo.MonitorDescription.pData = const_cast<BYTE*>(s_KnownMonitorEdid);

    // ==============================
    // TODO: The monitor's container ID should be distinct from "this" device's container ID if the monitor is not
    // permanently attached to the display adapter device object. The container ID is typically made unique for each
    // monitor and can be used to associate the monitor with other devices, like audio or input devices. In this
    // sample we generate a random container ID GUID, but it's best practice to choose a stable container ID for a
    // unique monitor or to use "this" device's container ID for a permanent/integrated monitor.
    // ==============================

    // Create a container ID
    CoCreateGuid(&MonitorInfo.MonitorContainerId);

    IDARG_IN_MONITORCREATE MonitorCreate = {};
    MonitorCreate.ObjectAttributes = &Attr;
    MonitorCreate.pMonitorInfo = &MonitorInfo;

    // Create a monitor object with the specified monitor descriptor
    IDARG_OUT_MONITORCREATE MonitorCreateOut;
    NTSTATUS Status = IddCxMonitorCreate(m_Adapter, &MonitorCreate, &MonitorCreateOut);
    if (NT_SUCCESS(Status))
    {
        m_Monitor = MonitorCreateOut.MonitorObject;

        // Associate the monitor with this device context
        auto* pContext = WdfObjectGet_IndirectDeviceContextWrapper(MonitorCreateOut.MonitorObject);
        pContext->pContext = this;

        // Tell the OS that the monitor has been plugged in
        IDARG_OUT_MONITORARRIVAL ArrivalOut;
        Status = IddCxMonitorArrival(m_Monitor, &ArrivalOut);
    }
}

void IndirectDeviceContext::AssignSwapChain(IDDCX_SWAPCHAIN SwapChain, LUID RenderAdapter, HANDLE NewFrameEvent)
{
    m_ProcessingThread.reset();

    auto Device = make_shared<Direct3DDevice>(RenderAdapter);
    if (FAILED(Device->Init()))
    {
        // It's important to delete the swap-chain if D3D initialization fails, so that the OS knows to generate a new
        // swap-chain and try again.
        WdfObjectDelete(SwapChain);
    }
    else
    {
        // Create a new swap-chain processing thread
        m_ProcessingThread.reset(new SwapChainProcessor(SwapChain, Device, m_WdfDevice, NewFrameEvent));
    }
}

void IndirectDeviceContext::UnassignSwapChain()
{
    // Stop processing the last swap-chain
    m_ProcessingThread.reset();
}

#pragma endregion


#pragma region DDI Callbacks

_Use_decl_annotations_
NTSTATUS IddSampleAdapterInitFinished(IDDCX_ADAPTER AdapterObject, const IDARG_IN_ADAPTER_INIT_FINISHED* pInArgs)
{
    // This is called when the OS has finished setting up the adapter for use by the IddCx driver. It's now possible
    // to report attached monitors.

    auto* pContext = WdfObjectGet_IndirectDeviceContextWrapper(AdapterObject);
    if (NT_SUCCESS(pInArgs->AdapterInitStatus))
    {
        pContext->pContext->FinishInit();
    }

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS IddSampleAdapterCommitModes(IDDCX_ADAPTER AdapterObject, const IDARG_IN_COMMITMODES* pInArgs)
{
    UNREFERENCED_PARAMETER(AdapterObject);
    UNREFERENCED_PARAMETER(pInArgs);

    // For the sample, do nothing when modes are picked - the swap-chain is taken care of by IddCx

    // ==============================
    // TODO: In a real driver, this function would be used to reconfigure the device to commit the new modes. Loop
    // through pInArgs->pPaths and look for IDDCX_PATH_FLAGS_ACTIVE. Any path not active is inactive (e.g. the monitor
    // should be turned off).
    // ==============================

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS IddSampleParseMonitorDescription(const IDARG_IN_PARSEMONITORDESCRIPTION* pInArgs, IDARG_OUT_PARSEMONITORDESCRIPTION* pOutArgs)
{
    // ==============================
    // TODO: In a real driver, this function would be called to generate monitor modes for an EDID by parsing it. In
    // this sample driver, we hard-code the EDID, so this function can generate known modes.
    // ==============================

    pOutArgs->MonitorModeBufferOutputCount = ARRAYSIZE(IndirectDeviceContext::s_KnownMonitorModes);

    if (pInArgs->MonitorModeBufferInputCount < ARRAYSIZE(IndirectDeviceContext::s_KnownMonitorModes))
    {
        // Return success if there was no buffer, since the caller was only asking for a count of modes
        return (pInArgs->MonitorModeBufferInputCount > 0) ? STATUS_BUFFER_TOO_SMALL : STATUS_SUCCESS;
    }
    else
    {
        // Copy the known modes to the output buffer
        for (DWORD ModeIndex = 0; ModeIndex < ARRAYSIZE(IndirectDeviceContext::s_KnownMonitorModes); ModeIndex++)
        {
            pInArgs->pMonitorModes[ModeIndex].Size = sizeof(IDDCX_MONITOR_MODE);
            pInArgs->pMonitorModes[ModeIndex].Origin = IDDCX_MONITOR_MODE_ORIGIN_MONITORDESCRIPTOR;
            pInArgs->pMonitorModes[ModeIndex].MonitorVideoSignalInfo = IndirectDeviceContext::s_KnownMonitorModes[ModeIndex];
        }

        // Set the preferred mode as represented in the EDID
        // Mode 1: 800 x 480 @ 60Hz (Index 0 is 640x480, Index 1 is 800x480)
        pOutArgs->PreferredMonitorModeIdx = 1;

        return STATUS_SUCCESS;
    }
}

_Use_decl_annotations_
NTSTATUS IddSampleMonitorGetDefaultModes(IDDCX_MONITOR MonitorObject, const IDARG_IN_GETDEFAULTDESCRIPTIONMODES* pInArgs, IDARG_OUT_GETDEFAULTDESCRIPTIONMODES* pOutArgs)
{
    UNREFERENCED_PARAMETER(MonitorObject);
    UNREFERENCED_PARAMETER(pInArgs);
    UNREFERENCED_PARAMETER(pOutArgs);

    // Should never be called since we create a single monitor with a known EDID in this sample driver.

    // ==============================
    // TODO: In a real driver, this function would be called to generate monitor modes for a monitor with no EDID.
    // Drivers should report modes that are guaranteed to be supported by the transport protocol and by nearly all
    // monitors (such 640x480, 800x600, or 1024x768). If the driver has access to monitor modes from a descriptor other
    // than an EDID, those modes would also be reported here.
    // ==============================

    return STATUS_NOT_IMPLEMENTED;
}

/// <summary>
/// Creates a target mode from the fundamental mode attributes.
/// </summary>
void CreateTargetMode(DISPLAYCONFIG_VIDEO_SIGNAL_INFO& Mode, UINT Width, UINT Height, UINT VSync)
{
    Mode.totalSize.cx = Mode.activeSize.cx = Width;
    Mode.totalSize.cy = Mode.activeSize.cy = Height;
    Mode.AdditionalSignalInfo.vSyncFreqDivider = 1;
    Mode.AdditionalSignalInfo.videoStandard = 255;
    Mode.vSyncFreq.Numerator = VSync;
    Mode.vSyncFreq.Denominator = Mode.hSyncFreq.Denominator = 1;
    Mode.hSyncFreq.Numerator = VSync * Height;
    Mode.scanLineOrdering = DISPLAYCONFIG_SCANLINE_ORDERING_PROGRESSIVE;
    Mode.pixelRate = VSync * Width * Height;
}

void CreateTargetMode(IDDCX_TARGET_MODE& Mode, UINT Width, UINT Height, UINT VSync)
{
    Mode.Size = sizeof(Mode);
    CreateTargetMode(Mode.TargetVideoSignalInfo.targetVideoSignalInfo, Width, Height, VSync);
}

_Use_decl_annotations_
NTSTATUS IddSampleMonitorQueryModes(IDDCX_MONITOR MonitorObject, const IDARG_IN_QUERYTARGETMODES* pInArgs, IDARG_OUT_QUERYTARGETMODES* pOutArgs)
{
    UNREFERENCED_PARAMETER(MonitorObject);

    vector<IDDCX_TARGET_MODE> TargetModes(34);

    // Create a set of modes supported for frame processing and scan-out. These are typically not based on the
    // monitor's descriptor and instead are based on the static processing capability of the device. The OS will
    // report the available set of modes for a given output as the intersection of monitor modes with target modes.


    CreateTargetMode(TargetModes[0], 7680, 4320, 60);
    CreateTargetMode(TargetModes[1], 6016, 3384, 60);
    CreateTargetMode(TargetModes[2], 5120, 2880, 60);
    CreateTargetMode(TargetModes[3], 4096, 2560, 60);
    CreateTargetMode(TargetModes[4], 4096, 2304, 60);
    CreateTargetMode(TargetModes[5], 3840, 2400, 60);
    CreateTargetMode(TargetModes[6], 3840, 2160, 60);
    CreateTargetMode(TargetModes[7], 3200, 2400, 60);
    CreateTargetMode(TargetModes[8], 3200, 1800, 60);
    CreateTargetMode(TargetModes[9], 3008, 1692, 60);
    CreateTargetMode(TargetModes[10], 2880, 1800, 60);
    CreateTargetMode(TargetModes[11], 2880, 1620, 60);
    CreateTargetMode(TargetModes[12], 2560, 1600, 60);
    CreateTargetMode(TargetModes[13], 2560, 1440, 60);
    CreateTargetMode(TargetModes[14], 1920, 1440, 60);
    CreateTargetMode(TargetModes[15], 1920, 1200, 60);

    CreateTargetMode(TargetModes[16], 1920, 1080, 60);
    CreateTargetMode(TargetModes[17], 1600, 1024, 60);
    CreateTargetMode(TargetModes[18], 1680, 1050, 60);
    CreateTargetMode(TargetModes[19], 1600, 900, 60);
    CreateTargetMode(TargetModes[20], 1440, 900, 60);
    CreateTargetMode(TargetModes[21], 1400, 1050, 60);
    CreateTargetMode(TargetModes[22], 1366, 768, 60);
    CreateTargetMode(TargetModes[23], 1360, 768, 60);
    CreateTargetMode(TargetModes[24], 1280, 1024, 60);
    CreateTargetMode(TargetModes[25], 1280, 960, 60);
    CreateTargetMode(TargetModes[26], 1280, 800, 60);
    CreateTargetMode(TargetModes[27], 1280, 768, 60);
    CreateTargetMode(TargetModes[28], 1280, 720, 60);
    CreateTargetMode(TargetModes[29], 1280, 600, 60);
    CreateTargetMode(TargetModes[30], 1152, 864, 60);
    CreateTargetMode(TargetModes[31], 1024, 768, 60);
    CreateTargetMode(TargetModes[32], 800, 600, 60);
    CreateTargetMode(TargetModes[33], 640, 480, 60);

    pOutArgs->TargetModeBufferOutputCount = (UINT)TargetModes.size();

    if (pInArgs->TargetModeBufferInputCount >= TargetModes.size())
    {
        copy(TargetModes.begin(), TargetModes.end(), pInArgs->pTargetModes);
    }

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS IddSampleMonitorAssignSwapChain(IDDCX_MONITOR MonitorObject, const IDARG_IN_SETSWAPCHAIN* pInArgs)
{
    auto* pContext = WdfObjectGet_IndirectDeviceContextWrapper(MonitorObject);
    pContext->pContext->AssignSwapChain(pInArgs->hSwapChain, pInArgs->RenderAdapterLuid, pInArgs->hNextSurfaceAvailable);
    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS IddSampleMonitorUnassignSwapChain(IDDCX_MONITOR MonitorObject)
{
    auto* pContext = WdfObjectGet_IndirectDeviceContextWrapper(MonitorObject);
    pContext->pContext->UnassignSwapChain();
    return STATUS_SUCCESS;
}

#pragma endregion

#pragma region USB Device PrepareHardware

NTSTATUS
idd_usbdisp_evt_device_prepareHardware(
	WDFDEVICE Device,
	WDFCMRESLIST ResourceList,
	WDFCMRESLIST ResourceListTranslated
	)
{
	NTSTATUS                            status;
	auto* pDeviceContext = WdfObjectGet_IndirectDeviceContextWrapper(Device);
	WDF_USB_DEVICE_INFORMATION          deviceInfo;
	ULONG                               waitWakeEnable;

	UNREFERENCED_PARAMETER(ResourceList);
	UNREFERENCED_PARAMETER(ResourceListTranslated);
	waitWakeEnable = FALSE;
	PAGED_CODE();

	LOGI("--> EvtDevicePrepareHardware\n");

	if (pDeviceContext->UsbDevice == NULL) {
#if UMDF_VERSION_MINOR >= 25
		WDF_USB_DEVICE_CREATE_CONFIG createParams;

		WDF_USB_DEVICE_CREATE_CONFIG_INIT(&createParams,
			USBD_CLIENT_CONTRACT_VERSION_602);

		status = WdfUsbTargetDeviceCreateWithParameters(
			Device,
			&createParams,
			WDF_NO_OBJECT_ATTRIBUTES,
			&pDeviceContext->UsbDevice);
#else
		status = WdfUsbTargetDeviceCreate(Device,
			WDF_NO_OBJECT_ATTRIBUTES,
			&pDeviceContext->UsbDevice);
#endif

		if (!NT_SUCCESS(status)) {
			LOGE("WdfUsbTargetDeviceCreate failed with Status code %x\n", status);
			return status;
		}
	}

	WDF_USB_DEVICE_INFORMATION_INIT(&deviceInfo);

	status = WdfUsbTargetDeviceRetrieveInformation(
		pDeviceContext->UsbDevice,
		&deviceInfo);
	if (NT_SUCCESS(status)) {
		waitWakeEnable = deviceInfo.Traits &
			WDF_USB_DEVICE_TRAIT_REMOTE_WAKE_CAPABLE;

		pDeviceContext->UsbDeviceTraits = deviceInfo.Traits;
	}
	else {
		pDeviceContext->UsbDeviceTraits = 0;
	}

	status = SelectInterfaces(Device);
	if (!NT_SUCCESS(status)) {
		LOGI("SelectInterfaces failed 0x%x\n", status);
		return status;
	}

	get_usb_dev_string_info(Device, pDeviceContext->tchar_udisp_devinfo);
	WideCharToMultiByte(CP_ACP,0,pDeviceContext->tchar_udisp_devinfo,-1,pDeviceContext->udisp_dev_info.cstr,UDISP_CONFIG_STR_LEN,NULL,NULL);

	LOGI("<-- EvtDevicePrepareHardware\n");

	// Update USB connection state
	pDeviceContext->usb_state = USB_STATE_CONNECTED;

	// Parse USB device info to configure IDD parameters
	if (strlen(pDeviceContext->udisp_dev_info.cstr) > 3) {
		usb_dev_config_t config;
		tools_parse_usb_dev_info(pDeviceContext->udisp_dev_info.cstr, &config);

		// Apply parsed configuration
		if (config.width != 0 && config.height != 0) {
			pDeviceContext->display_config.w = config.width;
			pDeviceContext->display_config.h = config.height;
			LOGI("Applied USB config: width=%d, height=%d\n", config.width, config.height);
		}

		pDeviceContext->display_config.enc = config.enc_type;
		pDeviceContext->display_config.quality = config.quality;
		pDeviceContext->display_config.fps = config.fps;
		pDeviceContext->display_config.blimit = config.blimit;

		LOGI("USB device configuration applied:\n");
		LOGI("  Width: %d\n", pDeviceContext->display_config.w);
		LOGI("  Height: %d\n", pDeviceContext->display_config.h);
		LOGI("  Encoder: %d (0=RGB565, 1=RGB888, 3=JPEG)\n", pDeviceContext->display_config.enc);
		LOGI("  Quality: %d\n", pDeviceContext->display_config.quality);
		LOGI("  FPS: %d\n", pDeviceContext->display_config.fps);
		LOGI("  Buffer limit: %d bytes (%.2f MB)\n",
		      pDeviceContext->display_config.blimit, (float)pDeviceContext->display_config.blimit / (1024 * 1024));
	} else {
		LOGI("USB device info string too short or invalid, using default configuration\n");
		LOGI("Default config: width=%d, height=%d, enc=%d, fps=%d\n",
		      pDeviceContext->display_config.w, pDeviceContext->display_config.h, pDeviceContext->display_config.enc, pDeviceContext->display_config.fps);
	}

	LOGI("USB device connected successfully\n");

	return status;
}

_Use_decl_annotations_
NTSTATUS IddSampleDevicePrepareHardware(
	WDFDEVICE Device,
	WDFCMRESLIST ResourceList,
	WDFCMRESLIST ResourceListTranslated
	)
{
	// Forward to USB display prepare hardware handler
	return idd_usbdisp_evt_device_prepareHardware(Device, ResourceList, ResourceListTranslated);
}

_Use_decl_annotations_
NTSTATUS IddSampleDeviceReleaseHardware(
	WDFDEVICE Device,
	WDFCMRESLIST ResourceListTranslated
	)
{
	UNREFERENCED_PARAMETER(ResourceListTranslated);
	auto* pDeviceContext = WdfObjectGet_IndirectDeviceContextWrapper(Device);

	LOGI("IddSampleDeviceReleaseHardware called\n");

	// Handle USB disconnection
	usb_device_disconnect(Device);

	// Update USB connection state
	pDeviceContext->usb_state = USB_STATE_DISCONNECTED;
	LOGI("USB device disconnected\n");

	return STATUS_SUCCESS;
}

_Use_decl_annotations_
VOID IddSampleDeviceSurpriseRemoval(
	WDFDEVICE Device
	)
{
	auto* pDeviceContext = WdfObjectGet_IndirectDeviceContextWrapper(Device);

	LOGW("!!! USB device surprise removal detected !!!\n");

	// Handle surprise removal
	usb_device_disconnect(Device);

	// Update USB connection state
	pDeviceContext->usb_state = USB_STATE_DISCONNECTED;

	// Print performance statistics
	tools_perf_stats_print(&pDeviceContext->perf_stats);

	LOGW("USB device surprise removal handled\n");
}

#pragma endregion



