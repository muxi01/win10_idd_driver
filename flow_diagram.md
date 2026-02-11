# IDD USB Graphic Driver 流程图

## 系统概述

本文档描述了 Windows Indirect Display Driver (IDD) 的完整工作流程，该驱动通过 USB 将显示数据传输到外部设备。

---

## 系统流程图

```mermaid
sequenceDiagram
    participant OS as Windows OS
    participant DD as IddSampleDriver
    participant DC as IndirectDeviceContext
    participant SCP as SwapChainProcessor
    participant USB as USB Driver
    participant ENC as ImageEncoder
    participant DEV as USB Device

    %% 初始化阶段
    OS->>DD: DriverEntry
    activate DD
    DD->>DD: registry_config_base()
    DD->>DD: IddSampleDeviceAdd()
    create participant DC
    DD->>DC: 创建 IndirectDeviceContext
    DC-->>DD: 完成
    DD-->>OS: STATUS_SUCCESS
    deactivate DD

    %% 设备启动
    OS->>DD: IddSampleDeviceD0Entry
    activate DD
    DD->>DC: InitAdapter()
    DC->>DC: 创建适配器
    DC-->>DD: 完成
    DD-->>OS: STATUS_SUCCESS
    deactivate DD

    %% USB设备准备
    OS->>DD: IddSampleDevicePrepareHardware
    activate DD
    DD->>USB: WdfUsbTargetDeviceCreate()
    USB-->>DD: 完成
    DD->>USB: SelectInterfaces()
    USB-->>DD: BulkReadPipe/BulkWritePipe
    DD->>DD: get_usb_dev_string_info()
    DD->>DD: tools_parse_usb_dev_info()
    Note over DD: 解析配置(宽/高/编码/质量/FPS)
    DD->>DD: usb_device_connect()
    DD-->>OS: STATUS_SUCCESS
    deactivate DD

    %% 监视器创建
    DC->>DC: CreateMonitor()
    DC->>OS: IddCxMonitorCreate()
    DC->>OS: IddCxMonitorArrival()
    OS-->>DC: Monitor创建完成

    %% SwapChain分配
    OS->>DD: IddSampleMonitorAssignSwapChain
    activate DD
    DD->>DC: AssignSwapChain()
    DC->>SCP: 创建 SwapChainProcessor
    create participant SCP
    SCP->>SCP: 创建处理线程
    SCP->>SCP: decision_runtime_encoder()
    SCP->>ENC: image_encoder_create_jpeg(quality)
    create participant ENC
    ENC-->>SCP: JPEG编码器
    SCP->>USB: usb_transf_init()
    SCP->>USB: 初始化URB队列
    USB-->>SCP: 完成
    SCP-->>DC: 处理器就绪
    DD-->>OS: STATUS_SUCCESS
    deactivate DD

    %% 主处理循环
    activate SCP
    loop 帧处理循环 (60 FPS)
        OS->>SCP: m_hAvailableBufferEvent
        SCP->>OS: IddCxSwapChainReleaseAndAcquireBuffer()
        OS-->>SCP: AcquiredBuffer (帧数据)

        alt USB已连接
            SCP->>USB: InterlockedPopEntrySList(urb_list)
            USB-->>SCP: urb_item_t

            alt URB可用
                SCP->>SCP: enc_grab_surface()
                Note over SCP: 拷贝帧数据到 fb_buf

                SCP->>SCP: tools_get_time_us() [开始计时]

                SCP->>ENC: encoder->encode()
                Note over ENC: RGBX → JPEG压缩
                ENC-->>SCP: total_bytes

                SCP->>SCP: tools_get_time_us() [编码结束]

                SCP->>ENC: encoder->encode_header()
                Note over ENC: 设置帧头(magic_id/img_type/img_len)
                ENC-->>SCP: header

                SCP->>USB: usb_send_msg_async()
                USB->>DEV: 发送帧数据到USB
                DEV-->>USB: 确认
                USB-->>SCP: NTSTATUS

                SCP->>SCP: tools_get_time_us() [发送结束]
                SCP->>SCP: tools_perf_stats_update()
                Note over SCP: 更新统计(grab/encode/send时间)

                alt 发送成功 && 需要ZLP
                    SCP->>USB: 发送零长度包
                    USB->>DEV: ZLP
                else 发送失败
                    SCP->>USB: usb_error_recovery()
                    USB->>USB: usb_device_reset()
                    Note over USB: 重置USB设备
                end

                SCP->>SCP: tools_sample_tick(fps)
                Note over SCP: 帧率控制
            else URB不可用
                Note over SCP: 帧丢弃 (dropped_frames++)
            end
        else USB未连接
            Note over SCP: 帧丢弃 (USB未连接)
        end

        SCP->>OS: IddCxSwapChainFinishedProcessingFrame()
        OS-->>SCP: 下一帧
    end

    %% 设备断开
    OS->>DD: IddSampleDeviceReleaseHardware
    activate DD
    DD->>USB: usb_device_disconnect()
    USB-->>DD: 完成
    DD-->>OS: STATUS_SUCCESS
    deactivate DD

    %% SwapChain释放
    OS->>DD: IddSampleMonitorUnassignSwapChain
    activate DD
    DD->>DC: UnassignSwapChain()
    DC->>SCP: 析构 ~SwapChainProcessor
    SCP->>SCP: SetEvent(m_hTerminateEvent)
    SCP->>USB: usb_transf_exit()
    USB->>USB: 释放URB队列
    SCP->>ENC: image_encoder_destroy()
    ENC->>ENC: jpeg_destroy_compress()
    ENC->>ENC: free(row_buffer)
    ENC->>ENC: free(jpeg_private)
    destroy ENC
    destroy SCP
    DC-->>DD: 完成
    DD-->>OS: STATUS_SUCCESS
    deactivate DD

    %% 设备清理
    OS->>DD: EvtCleanupCallback
    activate DD
    DD->>DC: Cleanup()
    DC->>DC: delete pContext
    destroy DC
    DD-->>OS: 清理完成
    deactivate DD
```

---

## 模块说明

### 1. 驱动入口与初始化

| 组件 | 功能 |
|------|------|
| `DriverEntry` | 驱动入口点，加载注册表配置 |
| `IddSampleDeviceAdd` | 创建 WDF 设备和设备上下文 |
| `registry_config_base()` | 从注册表读取 debug_level |
| `IndirectDeviceContext` | 设备上下文，管理适配器和监视器 |

### 2. USB 设备管理

| 组件 | 功能 |
|------|------|
| `IddSampleDevicePrepareHardware` | USB 设备初始化 |
| `WdfUsbTargetDeviceCreate()` | 创建 USB 目标设备 |
| `SelectInterfaces()` | 选择 USB 接口，获取 BulkReadPipe/BulkWritePipe |
| `get_usb_dev_string_info()` | 获取 USB 设备描述字符串 |
| `tools_parse_usb_dev_info()` | 解析配置：宽/高/编码类型/质量/FPS |
| `usb_device_connect()` / `usb_device_disconnect()` | USB 连接状态管理 |

### 3. SwapChain 处理器

| 组件 | 功能 |
|------|------|
| `SwapChainProcessor` | 帧处理主线程 |
| `Run()` | 主处理循环入口 |
| `RunCore()` | 帧处理核心逻辑 |
| `decision_runtime_encoder()` | 根据配置创建编码器 |
| `enc_grab_surface()` | 从 GPU 拷贝帧数据到 CPU |
| `tools_sample_tick()` | 帧率控制（60 FPS） |

### 4. 图像编码器

| 编码类型 | 说明 |
|----------|------|
| `IMAGE_TYPE_RGB565` | 16位 RGB 压缩 |
| `IMAGE_TYPE_RGB888` | 24位 RGB 无压缩 |
| `IMAGE_TYPE_JPG` | JPEG 有损压缩 |

**JPEG 编码器优化**：
- 初始化时分配 `jpeg_compress_struct` 和 `row_buffer`
- 每帧使用 `jpeg_abort_compress()` 重用压缩对象
- 动态调整 `row_buffer` 大小（仅在需要更大时重新分配）
- 销毁时释放所有资源

### 5. USB 数据传输

| 组件 | 功能 |
|------|------|
| `usb_transf_init()` | 初始化 URB 队列 |
| `usb_transf_exit()` | 清理 URB 队列 |
| `usb_send_msg_async()` | 异步发送数据 |
| `urb_item_t` | URB 传输单元（1920x1080x4 缓冲区） |
| `ZLP (Zero Length Packet)` | 零长度包，用于数据包边界对齐 |

### 6. 帧头结构

```c
typedef struct _image_frame_header_t {
    _u32 magic_id;  // 0x55AA55AA
    _u32 img_type;  // 图像类型 (0=RGB565, 1=RGB888, 3=JPEG)
    _u32 img_len;   // 图像数据长度
    _u32 img_cnt;   // 帧计数
} image_frame_header_t;
```

### 7. 性能统计

| 指标 | 说明 |
|------|------|
| `grab_time` | 帧数据获取时间 (us) |
| `encode_time` | 图像编码时间 (us) |
| `send_time` | USB 发送时间 (us) |
| `total_frames` | 总帧数 |
| `dropped_frames` | 丢弃帧数 |
| `error_count` | 错误计数 |

### 8. 错误恢复

| 场景 | 处理 |
|------|------|
| USB 发送失败 | `usb_error_recovery()` → `usb_device_reset()` |
| USB 未连接 | 帧丢弃，等待重新连接 |
| URB 不可用 | 帧丢弃，记录 `dropped_frames++` |

---

## 数据流向

```
[Windows Desktop] → [DXGI SwapChain] → [IDDCX] → [SwapChainProcessor]
    ↓
[Direct3DDevice] (GPU) → [StagingTexture] → [fb_buf] (CPU)
    ↓
[ImageEncoder] (RGBX → JPEG/RGB565/RGB888)
    ↓
[image_frame_header_t + payload] → [URB Queue]
    ↓
[USB BulkWritePipe] → [USB Device]
```

---

## 配置参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `width` | 1920 | 显示宽度 |
| `height` | 1080 | 显示高度 |
| `enc` | IMAGE_TYPE_RGB888 | 编码类型 |
| `quality` | 5 | JPEG 质量 (0-100) |
| `fps` | 60 | 目标帧率 |
| `blimit` | 1920*1080*4 | 缓冲区限制 |
| `debug_level` | LOG_LEVEL_INFO | 日志级别 |

---

## 文件结构

```
IddSampleDriver/
├── Driver.cpp          # 驱动主逻辑
├── Driver.h            # 驱动头文件
├── image_encoder.c     # 图像编码器实现
├── image_encoder.h     # 图像编码器接口
├── usb_driver.c        # USB 驱动实现
├── usb_driver.h        # USB 驱动接口
├── tools.c             # 工具函数
├── tools.h             # 工具函数接口
└── basetype.h          # 基础类型定义
```
