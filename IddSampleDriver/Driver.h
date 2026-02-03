#pragma once

#define NOMINMAX
#include <windows.h>
#include <bugcodes.h>
#include <wudfwdm.h>
#include <wdf.h>
#include <iddcx.h>

#include <dxgi1_5.h>
#include <d3d11_2.h>
#include <avrt.h>
#include <wrl.h>

#include <memory>
#include <vector>

#include "usbdi.h"
#include <wdf.h>
#include <wdfusb.h>
#include "Trace.h"
#include "encoder.h"
#include "basetype.h"


#define DISP_MAX_WIDTH  1920
#define DISP_MAX_HEIGHT 1080
#define UDISP_CONFIG_STR_LEN  256

namespace Microsoft
{
    namespace WRL
    {
        namespace Wrappers
        {
            // Adds a wrapper for thread handles to the existing set of WRL handle wrapper classes
            typedef HandleT<HandleTraits::HANDLENullTraits> Thread;
        }
    }
}

namespace Microsoft
{
    namespace IndirectDisp
    {
        /// <summary>
        /// Manages the creation and lifetime of a Direct3D render device.
        /// </summary>
        struct Direct3DDevice
        {
            Direct3DDevice(LUID AdapterLuid);
            Direct3DDevice();
            HRESULT Init();

            LUID AdapterLuid;
            Microsoft::WRL::ComPtr<IDXGIFactory5> DxgiFactory;
            Microsoft::WRL::ComPtr<IDXGIAdapter1> Adapter;
            Microsoft::WRL::ComPtr<ID3D11Device> Device;
            Microsoft::WRL::ComPtr<ID3D11DeviceContext> DeviceContext;
        };



        /// <summary>
        /// Manages a thread that consumes buffers from an indirect display swap-chain object.
        /// </summary>
        class SwapChainProcessor
        {
        public:
            SwapChainProcessor(IDDCX_SWAPCHAIN hSwapChain, std::shared_ptr<Direct3DDevice> Device, WDFDEVICE WdfDevice, HANDLE NewFrameEvent);
            ~SwapChainProcessor();

        private:
            static DWORD CALLBACK RunThread(LPVOID Argument);

            void Run();
            void main_function();

        public:
            IDDCX_SWAPCHAIN m_hSwapChain;
            std::shared_ptr<Direct3DDevice> m_Device;
            WDFDEVICE  mp_WdfDevice;
            uint8_t		fb_buf[DISP_MAX_HEIGHT*DISP_MAX_WIDTH*4];

            ImageEncoder *m_pEncoder;
            SLIST_HEADER urb_list;
            int max_out_pkg_size;

            HANDLE m_hAvailableBufferEvent;
            Microsoft::WRL::Wrappers::Thread m_hThread;
            Microsoft::WRL::Wrappers::Event m_hTerminateEvent;
        };

        /// <summary>
        /// Provides a sample implementation of an indirect display driver.
        /// </summary>
        class IndirectDeviceContext
        {
        public:
            IndirectDeviceContext(_In_ WDFDEVICE WdfDevice);
            virtual ~IndirectDeviceContext();

            void InitAdapter();
            void FinishInit();

            void CreateMonitor(unsigned int index);

            void AssignSwapChain(IDDCX_SWAPCHAIN SwapChain, LUID RenderAdapter, HANDLE NewFrameEvent);
            void UnassignSwapChain();

        protected:

            WDFDEVICE m_WdfDevice;
            IDDCX_ADAPTER m_Adapter;
            IDDCX_MONITOR m_Monitor;

            std::unique_ptr<SwapChainProcessor> m_ProcessingThread;

        public:
            static const DISPLAYCONFIG_VIDEO_SIGNAL_INFO s_KnownMonitorModes[];
            static const BYTE s_KnownMonitorEdid[];
        };
    }
}

// Forward declaration from usb_driver.h


typedef struct{
	char   cstr[UDISP_CONFIG_STR_LEN];
} config_cstr_t;

typedef struct {
    int w;
    int h;
    int img_type;
    int img_qlt;
    int fps;
    int blimit;
    int sample_only;
    int debug_level;
    int sleep;
} display_config_t;

class IndirectDeviceContextWrapper {
public:
    Microsoft::IndirectDisp::IndirectDeviceContext* pContext;
    //usb disp
    WDFUSBDEVICE                    UsbDevice;
    WDFUSBINTERFACE                 UsbInterface;

    WDFUSBPIPE                      BulkReadPipe;
	ULONG max_in_pkg_size;

    WDFUSBPIPE                      BulkWritePipe;
	ULONG max_out_pkg_size;
    ULONG							UsbDeviceTraits;

    PSLIST_HEADER purb_list;

	config_cstr_t udisp_registry_dev_info;
	config_cstr_t udisp_dev_info;
	TCHAR   tchar_udisp_devinfo[UDISP_CONFIG_STR_LEN];
	display_config_t config;

    // Error recovery state
    usb_connection_state_t usb_state;
    perf_stats_t perf_stats;

    void Cleanup();
};