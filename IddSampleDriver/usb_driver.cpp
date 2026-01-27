#include "Driver.h"
#include "usb_driver.h"
#include "tools.h"
#include <wdfusb.h>

// Declare context access macro for IndirectDeviceContextWrapper
WDF_DECLARE_CONTEXT_TYPE(IndirectDeviceContextWrapper);

// Global error counter for USB
static LONG g_usb_error_count = 0;
static usb_connection_state_t g_usb_state = USB_STATE_DISCONNECTED;
static WDFWAITLOCK g_usb_state_lock;

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

        // Increment global error counter
        InterlockedIncrement(&g_usb_error_count);

        // Update USB state if error threshold reached
        if (g_usb_error_count >= USB_ERROR_RESET_THRESHOLD) {
            WdfWaitLockAcquire(g_usb_state_lock, NULL);
            g_usb_state = USB_STATE_ERROR;
            WdfWaitLockRelease(g_usb_state_lock);
            LOGE("USB error threshold reached, state set to ERROR\n");
        }
    } else {
        // Reset error counter on success
        if (g_usb_error_count > 0) {
            InterlockedExchange(&g_usb_error_count, 0);
        }
    }

    if (urb->wdfMemory != NULL) {
        WdfObjectDelete(urb->wdfMemory);
        urb->wdfMemory = NULL;
    }

    LOGD("pipe:%p cpl urb id:%d\n", urb->pipe, urb->id);
    InterlockedPushEntrySList(urb->urb_list, &(urb->node));
}

NTSTATUS usb_send_msg_async(urb_item_t* urb, WDFUSBPIPE pipe, WDFREQUEST Request, PUCHAR msg, int tsize)
{
    WDFMEMORY wdfMemory;
    NTSTATUS status;

    // Check USB state before sending
    WdfWaitLockAcquire(g_usb_state_lock, NULL);
    usb_connection_state_t current_state = g_usb_state;
    WdfWaitLockRelease(g_usb_state_lock);

    if (current_state != USB_STATE_CONNECTED) {
        LOGW("USB not in connected state (state=%d), skipping send\n", current_state);
        return STATUS_DEVICE_NOT_READY;
    }

    status = WdfMemoryCreate(
        WDF_NO_OBJECT_ATTRIBUTES,
        NonPagedPool,
        0,
        tsize,
        &wdfMemory,
        NULL);

    if (!NT_SUCCESS(status)) {
        LOGE("WdfMemoryCreate failed: 0x%x\n", status);
        return status;
    }

    WdfMemoryCopyFromBuffer(wdfMemory, 0, msg, tsize);

    status = WdfUsbTargetPipeFormatRequestForWrite(pipe, Request, wdfMemory, NULL);
    if (!NT_SUCCESS(status)) {
        LOGE("WdfUsbTargetPipeFormatRequestForWrite failed: 0x%x\n", status);
        WdfObjectDelete(wdfMemory);
        return status;
    }

    urb->pipe = pipe;
    urb->wdfMemory = wdfMemory;

    WdfRequestSetCompletionRoutine(Request, EvtRequestWriteCompletionRoutine, urb);

    if (!WdfRequestSend(Request, WdfUsbTargetPipeGetIoTarget(pipe), WDF_NO_SEND_OPTIONS)) {
        status = WdfRequestGetStatus(Request);
        LOGE("WdfRequestSend failed: 0x%x\n", status);
        WdfObjectDelete(wdfMemory);
        return status;
    }

    return STATUS_SUCCESS;
}

NTSTATUS get_usb_dev_string_info(WDFDEVICE Device, TCHAR* stringBuf)
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
NTSTATUS SelectInterfaces(WDFDEVICE Device)
{
    WDF_USB_DEVICE_SELECT_CONFIG_PARAMS configParams;
    NTSTATUS status = STATUS_SUCCESS;
    auto* pDeviceContext = WdfObjectGet_IndirectDeviceContextWrapper(Device);
    WDFUSBPIPE pipe;
    WDF_USB_PIPE_INFORMATION pipeInfo;
    WDFUSBINTERFACE usbInterface;

    PAGED_CODE();

    if (pDeviceContext->UsbDevice == NULL) {
        LOGE("USB device not initialized\n");
        return STATUS_INVALID_DEVICE_STATE;
    }

    // Use single interface configuration (interface 0 for Bulk only)
    WDF_USB_DEVICE_SELECT_CONFIG_PARAMS_INIT_SINGLE_INTERFACE(&configParams);

    // Get interface 0 (Bulk interface)
    usbInterface = WdfUsbTargetDeviceGetInterface(pDeviceContext->UsbDevice, 0);
    if (usbInterface == NULL) {
        LOGE("Failed to get USB interface 0 (Bulk interface)\n");
        return STATUS_UNSUCCESSFUL;
    }

    UCHAR interfaceNumber = WdfUsbInterfaceGetInterfaceNumber(usbInterface);
    LOGI("Configured USB interface %d for Bulk transfer\n", interfaceNumber);

    configParams.Types.SingleInterface.ConfiguredUsbInterface = usbInterface;
    configParams.Types.SingleInterface.NumberConfiguredPipes =
        WdfUsbInterfaceGetNumConfiguredPipes(usbInterface);

    pDeviceContext->UsbInterface = usbInterface;

    const UCHAR numberConfiguredPipes = configParams.Types.SingleInterface.NumberConfiguredPipes;
    LOGI("Number of configured pipes on interface 0: %d\n", numberConfiguredPipes);

    for (UCHAR index = 0; index < numberConfiguredPipes; index++) {
        WDF_USB_PIPE_INFORMATION_INIT(&pipeInfo);

        pipe = WdfUsbInterfaceGetConfiguredPipe(pDeviceContext->UsbInterface, index, &pipeInfo);
        WdfUsbTargetPipeSetNoMaximumPacketSizeCheck(pipe);

        // Log pipe information
        LOGI("Pipe %d: Type=%d, Direction=%s, Endpoint=0x%02x, MaxPacket=%d\n",
             index,
             pipeInfo.PipeType,
             WdfUsbTargetPipeIsInEndpoint(pipe) ? "IN" : "OUT",
             pipeInfo.EndpointAddress,
             pipeInfo.MaximumPacketSize);

        if (pipeInfo.PipeType == WdfUsbPipeTypeBulk && WdfUsbTargetPipeIsInEndpoint(pipe)) {
            pDeviceContext->BulkReadPipe = pipe;
            pDeviceContext->max_in_pkg_size = pipeInfo.MaximumPacketSize;
            LOGI("BulkRead Pipe: 0x%p, max_packet_size: %d\n", pipe, pDeviceContext->max_in_pkg_size);
        }

        if (pipeInfo.PipeType == WdfUsbPipeTypeBulk && WdfUsbTargetPipeIsOutEndpoint(pipe)) {
            pDeviceContext->BulkWritePipe = pipe;
            pDeviceContext->max_out_pkg_size = pipeInfo.MaximumPacketSize;
            LOGI("BulkWrite Pipe: 0x%p, max_packet_size: %d\n", pipe, pDeviceContext->max_out_pkg_size);
        }
    }

    if (pDeviceContext->BulkWritePipe == NULL || pDeviceContext->BulkReadPipe == NULL) {
        status = STATUS_INVALID_DEVICE_STATE;
        LOGE("Device not properly configured: BulkReadPipe=%p, BulkWritePipe=%p\n",
             pDeviceContext->BulkReadPipe, pDeviceContext->BulkWritePipe);
        return status;
    }

    LOGI("USB interface 0 (Bulk) configured successfully\n");
    return STATUS_SUCCESS;
}

int usb_transf_init(SLIST_HEADER* urb_list)
{
    NTSTATUS status;

    LOGI("%s: Initializing URB list\n", __func__);
    InitializeSListHead(urb_list);

    // Initialize USB state lock for UMDF
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    status = WdfWaitLockCreate(&attributes, &g_usb_state_lock);
    if (!NT_SUCCESS(status)) {
        LOGE("WdfWaitLockCreate failed: 0x%x\n", status);
        return -1;
    }

    for (int i = 1; i <= MAX_URB_SIZE; i++) {
        purb_item_t purb = (urb_item_t*)_aligned_malloc(sizeof(urb_item_t), MEMORY_ALLOCATION_ALIGNMENT);
        if (purb == NULL) {
            LOGE("Memory allocation failed for URB item %d\n", i);
            break;
        }

        status = WdfRequestCreate(WDF_NO_OBJECT_ATTRIBUTES, NULL, &purb->Request);
        if (!NT_SUCCESS(status)) {
            LOGE("WdfRequestCreate failed: 0x%x\n", status);
            _aligned_free(purb);
            break;
        }

        purb->id = i;
        purb->urb_list = urb_list;
        purb->wdfMemory = NULL;

        InterlockedPushEntrySList(urb_list, &(purb->node));
        LOGD("Created URB item %d\n", purb->id);
    }

    return 0;
}

int usb_transf_exit(SLIST_HEADER* urb_list)
{
    LOGD("%s: Cleaning up URB list\n", __func__);

    for (int i = 1; i <= MAX_URB_SIZE; i++) {
        PSLIST_ENTRY pentry = InterlockedPopEntrySList(urb_list);
        urb_item_t* purb = (urb_item_t*)pentry;

        if (purb == NULL) {
            LOGW("URB list is empty at iteration %d\n", i);
            break;
        }

        LOGD("Cleaning up URB item id:%d\n", purb->id);

        if (purb->Request != NULL) {
            WdfObjectDelete(purb->Request);
        }

        _aligned_free(purb);
    }

    return 0;
}

NTSTATUS usb_device_connect(WDFDEVICE Device)
{
    NTSTATUS status;
    auto* pDeviceContext = WdfObjectGet_IndirectDeviceContextWrapper(Device);

    LOGI("USB device connect event\n");

    // Update USB state
    WdfWaitLockAcquire(g_usb_state_lock, NULL);
    g_usb_state = USB_STATE_CONNECTED;
    InterlockedExchange(&g_usb_error_count, 0);
    WdfWaitLockRelease(g_usb_state_lock);

    // Re-select interfaces
    status = SelectInterfaces(Device);
    if (!NT_SUCCESS(status)) {
        LOGE("Failed to select interfaces on reconnect: 0x%x\n", status);
        return status;
    }

    LOGI("USB device connected successfully\n");
    return STATUS_SUCCESS;
}

NTSTATUS usb_device_disconnect(WDFDEVICE Device)
{
    UNREFERENCED_PARAMETER(Device);

    LOGI("USB device disconnect event\n");

    // Update USB state
    WdfWaitLockAcquire(g_usb_state_lock, NULL);
    g_usb_state = USB_STATE_DISCONNECTED;
    WdfWaitLockRelease(g_usb_state_lock);

    LOGI("USB device disconnected\n");
    return STATUS_SUCCESS;
}

NTSTATUS usb_device_reset(WDFDEVICE Device)
{
    NTSTATUS status;
    auto* pDeviceContext = WdfObjectGet_IndirectDeviceContextWrapper(Device);

    LOGI("USB device reset initiated\n");

    // Update USB state
    WdfWaitLockAcquire(g_usb_state_lock, NULL);
    g_usb_state = USB_STATE_RECOVERING;
    WdfWaitLockRelease(g_usb_state_lock);

    // Reset USB pipe
    if (pDeviceContext->BulkWritePipe != NULL) {
        status = WdfUsbTargetPipeAbortSynchronously(pDeviceContext->BulkWritePipe,
                                                      WDF_NO_HANDLE, NULL);
        if (!NT_SUCCESS(status)) {
            LOGW("Failed to abort write pipe: 0x%x\n", status);
        }

        status = WdfUsbTargetPipeResetSynchronously(pDeviceContext->BulkWritePipe,
                                                     WDF_NO_HANDLE, NULL);
        if (!NT_SUCCESS(status)) {
            LOGE("Failed to reset write pipe: 0x%x\n", status);
        }
    }

    if (pDeviceContext->BulkReadPipe != NULL) {
        status = WdfUsbTargetPipeAbortSynchronously(pDeviceContext->BulkReadPipe,
                                                      WDF_NO_HANDLE, NULL);
        if (!NT_SUCCESS(status)) {
            LOGW("Failed to abort read pipe: 0x%x\n", status);
        }

        status = WdfUsbTargetPipeResetSynchronously(pDeviceContext->BulkReadPipe,
                                                     WDF_NO_HANDLE, NULL);
        if (!NT_SUCCESS(status)) {
            LOGE("Failed to reset read pipe: 0x%x\n", status);
        }
    }

    // Reset error counter
    InterlockedExchange(&g_usb_error_count, 0);

    // Re-select interfaces
    status = SelectInterfaces(Device);
    if (NT_SUCCESS(status)) {
        WdfWaitLockAcquire(g_usb_state_lock, NULL);
        g_usb_state = USB_STATE_CONNECTED;
        WdfWaitLockRelease(g_usb_state_lock);
        LOGI("USB device reset successful\n");
    } else {
        WdfWaitLockAcquire(g_usb_state_lock, NULL);
        g_usb_state = USB_STATE_ERROR;
        WdfWaitLockRelease(g_usb_state_lock);
        LOGE("USB device reset failed: 0x%x\n", status);
    }

    return status;
}

NTSTATUS usb_error_recovery(WDFDEVICE Device, NTSTATUS error_code)
{
    NTSTATUS status = STATUS_SUCCESS;
    LONG error_count = InterlockedIncrement(&g_usb_error_count);

    LOGE("USB error recovery triggered (error=0x%x, count=%ld)\n", error_code, error_count);

    // Select recovery strategy based on error count
    if (error_count < MAX_RETRY_COUNT) {
        LOGI("Recovery strategy: RETRY\n");
        // Just retry, no action needed
    } else if (error_count < USB_ERROR_RESET_THRESHOLD) {
        LOGI("Recovery strategy: RESET\n");
        status = usb_device_reset(Device);
    } else {
        LOGI("Recovery strategy: REINIT\n");
        status = usb_device_reset(Device);
        if (!NT_SUCCESS(status)) {
            LOGE("Failed to recover, device may need manual intervention\n");
        }
    }

    return status;
}

NTSTATUS usb_check_connection_state(WDFDEVICE Device, usb_connection_state_t* state)
{
    UNREFERENCED_PARAMETER(Device);

    if (state == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    WdfWaitLockAcquire(g_usb_state_lock, NULL);
    *state = g_usb_state;
    WdfWaitLockRelease(g_usb_state_lock);

    return STATUS_SUCCESS;
}
