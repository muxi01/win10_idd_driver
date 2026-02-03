#include "Driver.h"
#include "usb_driver.h"
#include "tools.h"
#include "encoder.h"
#include <wdfusb.h>

// Declare context access macro for IndirectDeviceContextWrapper
WDF_DECLARE_CONTEXT_TYPE(IndirectDeviceContextWrapper);

// Global error counter for USB
static usb_connection_state_t g_usb_state = USB_STATE_DISCONNECTED;
static volatile LONG g_usb_init_flag = 0;
static WDFWAITLOCK g_usb_state_lock = NULL;

#define LOG_DEBUG()  LOGI("%s.%d\n",__func__,__LINE__)


static VOID EvtRequestWriteCompletionRoutine(
    WDFREQUEST Request,
    WDFIOTARGET Target,
    PWDF_REQUEST_COMPLETION_PARAMS CompletionParams,
    WDFCONTEXT Context)
{
    NTSTATUS status;
    size_t bytesWritten = 0;
    PWDF_USB_REQUEST_COMPLETION_PARAMS usbCompletionParams;
    urb_item_t* urb = (urb_item_t*)Context;

    UNREFERENCED_PARAMETER(Request);
    UNREFERENCED_PARAMETER(Target);

    status = CompletionParams->IoStatus.Status;
    usbCompletionParams = CompletionParams->Parameters.Usb.Completion;
    bytesWritten = usbCompletionParams->Parameters.PipeWrite.Length;

    if (!NT_SUCCESS(status)) {
        LOGE("Write failed: request Status 0x%x UsbdStatus 0x%x\n",
             status, usbCompletionParams->UsbdStatus);
    }

    InterlockedPushEntrySList(urb->urb_list, &(urb->node));
    LOGI("insert URB id=%d to list, bytesWritten=%d\n", urb->id, bytesWritten);
}

NTSTATUS usb_send_data_async(urb_item_t* urb, WDFUSBPIPE pipe, int tsize)
{
    NTSTATUS status;
    WDFREQUEST Request = urb->Request;
    PUCHAR msg = urb->urb_msg;

    // Validate buffer size
    if (urb->wdfMemory == NULL) {
        LOGE("URB wdfMemory is NULL for URB id=%d\n", urb->id);
        return STATUS_INVALID_DEVICE_STATE;
    }

    if (tsize > urb->urb_msg_size) {
        LOGE("Transfer size %d exceeds buffer size %d for URB id=%d\n",tsize, urb->urb_msg_size, urb->id);
        return STATUS_BUFFER_TOO_SMALL;
    }

    LOG_DEBUG();
    // Copy to pre-allocated WDF memory
    WdfMemoryCopyFromBuffer(urb->wdfMemory, 0, msg, tsize);

    // Reuse and initialize the request before formatting
    // WdfRequestReuse(Request, STATUS_SUCCESS);

    // Format request for write
    LOG_DEBUG();
    status = WdfUsbTargetPipeFormatRequestForWrite(pipe, Request, urb->wdfMemory, NULL);
    if (!NT_SUCCESS(status)) {
        LOGE("WdfUsbTargetPipeFormatRequestForWrite failed: 0x%x\n", status);
        return status;
    }

    urb->pipe = pipe;
    LOG_DEBUG();
    WdfRequestSetCompletionRoutine(Request, EvtRequestWriteCompletionRoutine, urb);

    LOG_DEBUG();
    if (!WdfRequestSend(Request, WdfUsbTargetPipeGetIoTarget(pipe), WDF_NO_SEND_OPTIONS)) {
        status = WdfRequestGetStatus(Request);
        LOGE("WdfRequestSend failed: 0x%x, URB id=%d\n", status, urb->id);
        return status;
    }

    LOGI("usb_send_data_async: Request sent successfully, URB id=%d\n", urb->id);
    return STATUS_SUCCESS;
}

NTSTATUS usb_get_discribe_info(WDFDEVICE Device, TCHAR* stringBuf)
{
    NTSTATUS status;
    USHORT numCharacters = 0;
    USB_DEVICE_DESCRIPTOR udesc;
    auto* pDeviceContext = WdfObjectGet_IndirectDeviceContextWrapper(Device);

    if (pDeviceContext->UsbDevice == NULL) {
        LOGE("USB device not initialized\n");
        return STATUS_INVALID_DEVICE_STATE;
    }

    WdfUsbTargetDeviceGetDeviceDescriptor(pDeviceContext->UsbDevice, &udesc);

    LOGI("USB device VID:PID: 0x%04x:0x%04x\n", udesc.idVendor, udesc.idProduct);

    status = WdfUsbTargetDeviceQueryString(
        pDeviceContext->UsbDevice,
        NULL,
        NULL,
        NULL,
        &numCharacters,
        udesc.iProduct,
        0x0409);

    if (!NT_SUCCESS(status)) {
        LOGW("Failed to query product string length: 0x%x\n", status);
        return status;
    }

    status = WdfUsbTargetDeviceQueryString(
        pDeviceContext->UsbDevice,
        NULL,
        NULL,
        stringBuf,
        &numCharacters,
        udesc.iProduct,
        0x0409);

    if (NT_SUCCESS(status)) {
        stringBuf[numCharacters] = L'\0';
        LOGI("Product: %d chars, %S\n", numCharacters, stringBuf);
    }

    return status;
}

_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS usb_select_interface(WDFDEVICE Device)
{
    WDF_USB_DEVICE_SELECT_CONFIG_PARAMS configParams;
    NTSTATUS status = STATUS_SUCCESS;
    auto* pDeviceContext = WdfObjectGet_IndirectDeviceContextWrapper(Device);
    WDFUSBPIPE pipe;
    WDF_USB_PIPE_INFORMATION pipeInfo;
    WDFUSBINTERFACE usbInterface;
    UCHAR numInterfaces;

    PAGED_CODE();

    if (pDeviceContext->UsbDevice == NULL) {
        LOGE("USB device not initialized\n");
        return STATUS_INVALID_DEVICE_STATE;
    }

    // Scan all interfaces to find Bulk endpoints
    numInterfaces = WdfUsbTargetDeviceGetNumInterfaces(pDeviceContext->UsbDevice);
    LOGI("Device has %d interfaces\n", numInterfaces);

    pDeviceContext->BulkReadPipe = NULL;
    pDeviceContext->BulkWritePipe = NULL;

    // Scan all interfaces to find Bulk endpoints
    for (UCHAR ifIndex = 0; ifIndex < numInterfaces; ifIndex++) {
        usbInterface = WdfUsbTargetDeviceGetInterface(pDeviceContext->UsbDevice, ifIndex);
        if (usbInterface == NULL) {
            LOGE("Failed to get USB interface %d\n", ifIndex);
            continue;
        }

        UCHAR interfaceNumber = WdfUsbInterfaceGetInterfaceNumber(usbInterface);
        UCHAR numPipes = WdfUsbInterfaceGetNumConfiguredPipes(usbInterface);
        LOGI("Interface %d (bInterfaceNumber=%d): %d pipes\n", ifIndex, interfaceNumber, numPipes);

        // Scan all pipes in this interface
        for (UCHAR pipeIndex = 0; pipeIndex < numPipes; pipeIndex++) {
            WDF_USB_PIPE_INFORMATION_INIT(&pipeInfo);
            pipe = WdfUsbInterfaceGetConfiguredPipe(usbInterface, pipeIndex, &pipeInfo);
            WdfUsbTargetPipeSetNoMaximumPacketSizeCheck(pipe);

            // Log pipe information
            LOGI("  Pipe %d: Type=%d, Direction=%s, Endpoint=0x%02x, MaxPacket=%d\n",
                 pipeIndex,
                 pipeInfo.PipeType,
                 WdfUsbTargetPipeIsInEndpoint(pipe) ? "IN" : "OUT",
                 pipeInfo.EndpointAddress,
                 pipeInfo.MaximumPacketSize);

            if (pipeInfo.PipeType == WdfUsbPipeTypeBulk && WdfUsbTargetPipeIsInEndpoint(pipe)) {
                pDeviceContext->BulkReadPipe = pipe;
                pDeviceContext->max_in_pkg_size = pipeInfo.MaximumPacketSize;
                pDeviceContext->UsbInterface = usbInterface;
                LOGI("Found BulkRead Pipe on interface %d: 0x%p, max_packet_size: %d\n",
                     interfaceNumber, pipe, pDeviceContext->max_in_pkg_size);
            }

            if (pipeInfo.PipeType == WdfUsbPipeTypeBulk && WdfUsbTargetPipeIsOutEndpoint(pipe)) {
                pDeviceContext->BulkWritePipe = pipe;
                pDeviceContext->max_out_pkg_size = pipeInfo.MaximumPacketSize;
                pDeviceContext->UsbInterface = usbInterface;
                LOGI("Found BulkWrite Pipe on interface %d: 0x%p, max_packet_size: %d\n",
                     interfaceNumber, pipe, pDeviceContext->max_out_pkg_size);
            }
        }
    }

    if (pDeviceContext->BulkWritePipe == NULL || pDeviceContext->BulkReadPipe == NULL) {
        status = STATUS_INVALID_DEVICE_STATE;
        LOGE("Device not properly configured: BulkReadPipe=%p, BulkWritePipe=%p\n",
             pDeviceContext->BulkReadPipe, pDeviceContext->BulkWritePipe);
        return status;
    }

    LOGI("USB configured successfully with BulkRead and BulkWrite pipes\n");
    return STATUS_SUCCESS;
}

int usb_resouce_init(SLIST_HEADER* urb_list, int width, int height)
{
    NTSTATUS status;

    LOGI("%s: Initializing URB list for %dx%d\n", __func__, width, height);
    InitializeSListHead(urb_list);

    // Calculate buffer size based on screen dimensions
    int buffer_size = width * height * 4;  // RGB888 = 4 bytes per pixel
    int max_transfer_size = buffer_size + 128;

    // Initialize USB state lock for UMDF
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    status = WdfWaitLockCreate(&attributes, &g_usb_state_lock);
    if (!NT_SUCCESS(status)) {
        LOGE("WdfWaitLockCreate failed: 0x%x\n", status);
        return -1;
    }

    // Set lock initialized flag
    for (int i = 0; i < MAX_URB_SIZE; i++) {

        urb_item_t* purb = (urb_item_t*)_aligned_malloc(sizeof(urb_item_t), MEMORY_ALLOCATION_ALIGNMENT);
        if (purb == NULL) {
            LOGE("Memory allocation failed for URB item %d\n", i);
            return -2;
        }

        status = WdfRequestCreate(WDF_NO_OBJECT_ATTRIBUTES, NULL, &purb->Request);
        if (!NT_SUCCESS(status)) {
            LOGE("WdfRequestCreate failed: 0x%x\n", status);
            return -3;
        }

        purb->id = i;
        purb->urb_list = urb_list;

        // Allocate urb_msg buffer
        purb->urb_msg = (uint8_t*)_aligned_malloc(max_transfer_size, MEMORY_ALLOCATION_ALIGNMENT);
        if (purb->urb_msg == NULL) {
            LOGE("Failed to allocate urb_msg for URB %d\n", purb->id);
            return -4;
        }
        purb->urb_msg_size = max_transfer_size;

        // Create pre-allocated WDF memory for USB transfer
        status = WdfMemoryCreate(WDF_NO_OBJECT_ATTRIBUTES,NonPagedPool,0,max_transfer_size,&purb->wdfMemory,NULL);
        if (!NT_SUCCESS(status)) {
            LOGE("WdfMemoryCreate failed for URB %d: 0x%x\n", purb->id, status);
            return -5;
        }

        InterlockedPushEntrySList(urb_list, &(purb->node));
        LOGD("Created URB item %d: urb_msg=%p, size=%d, wdfMemory=%p\n",purb->id, purb->urb_msg, purb->urb_msg_size, purb->wdfMemory);
    }
    g_usb_init_flag = 1;
    return 0;
}

int usb_resouce_distory(SLIST_HEADER* urb_list)
{
    LOGD("%s: Cleaning up URB list\n", __func__);

    for (int i = 0; i < MAX_URB_SIZE; i++) {
        PSLIST_ENTRY pentry = InterlockedPopEntrySList(urb_list);
        urb_item_t* purb = (urb_item_t*)pentry;

        if (purb == NULL) {
            LOGW("URB list is empty at iteration %d\n", i);
            break;
        }

        LOGD("Cleaning up URB item id:%d\n", purb->id);

        // Free dynamically allocated urb_msg buffer
        if (purb->urb_msg != NULL) {
            _aligned_free(purb->urb_msg);
            purb->urb_msg = NULL;
        }

        // Delete pre-allocated WDF memory
        if (purb->wdfMemory != NULL) {
            WdfObjectDelete(purb->wdfMemory);
            purb->wdfMemory = NULL;
        }

        if (purb->Request != NULL) {
            WdfObjectDelete(purb->Request);
        }
        _aligned_free(purb);
    }
    g_usb_init_flag =0;
    return 0;
}

NTSTATUS usb_device_connect(WDFDEVICE Device)
{
    NTSTATUS status;
    if(g_usb_init_flag > 0) {
        // Re-select interfaces
        status = usb_select_interface(Device);
        if (!NT_SUCCESS(status)) {
            LOGE("Failed to select interfaces on reconnect: 0x%x\n", status);
            return status;
        }

        WdfWaitLockAcquire(g_usb_state_lock, NULL);
        g_usb_state = USB_STATE_CONNECTED;
        WdfWaitLockRelease(g_usb_state_lock);

    }
    LOGI("USB device connected successfully\n");
    return STATUS_SUCCESS;
}

NTSTATUS usb_device_disconnect(WDFDEVICE Device)
{
    UNREFERENCED_PARAMETER(Device);
    // Update USB state
    if(g_usb_init_flag > 0) {
        WdfWaitLockAcquire(g_usb_state_lock, NULL);
        g_usb_state = USB_STATE_DISCONNECTED;
        WdfWaitLockRelease(g_usb_state_lock);
    }
    LOGI("USB device disconnected\n");
    return STATUS_SUCCESS;
}

BOOLEAN usb_is_connected(WDFDEVICE Device)
{
    UNREFERENCED_PARAMETER(Device);
    usb_connection_state_t state;
    if(g_usb_init_flag > 0) {
        WdfWaitLockAcquire(g_usb_state_lock, NULL);
        state = g_usb_state;
        WdfWaitLockRelease(g_usb_state_lock);
        LOGI("usb state (0=connected 1=disconnected) %d\n",state);
    }
    return (state == USB_STATE_CONNECTED);
}
