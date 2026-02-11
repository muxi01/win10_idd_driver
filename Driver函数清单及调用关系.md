# Driver.cpp 函数清单及调用关系

## 一、系统入口接口 (由Windows内核/IDDCX框架调用)

| 函数名 | 行号 | 调用时机 | 说明 |
|--------|------|----------|------|
| **DriverEntry** | 72-96 | 驱动加载时 | **驱动主入口**,初始化WDF驱动 |
| **IddSampleDeviceAdd** | 100-177 | 设备添加时 | 创建设备对象,注册IDDCX回调 |
| **IddSampleDeviceD0Entry** | 180-190 | 设备进入D0状态 | 调用`InitAdapter()` |
| **IddSampleDevicePrepareHardware** | 1126-1134 | 硬件准备阶段 | 调用USB硬件初始化 |
| **IddSampleDeviceReleaseHardware** | 1137-1151 | 硬件释放阶段 | 调用`usb_device_disconnect()` |
| **IddSampleDeviceSurpriseRemoval** | 1154-1169 | 热插拔移除时 | 处理USB意外拔出 |
| **IddSampleAdapterInitFinished** | 821-833 | 适配器初始化完成 | 调用`FinishInit()` |
| **IddSampleAdapterCommitModes** | 836-850 | 模式提交时 | 空操作 |
| **IddSampleParseMonitorDescription** | 853-902 | 解析监视器描述符 | 返回支持的显示模式 |
| **IddSampleMonitorGetDefaultModes** | 905-921 | 无EDID时调用 | 返回默认模式(未实现) |
| **IddSampleMonitorQueryModes** | 946-1003 | 查询目标模式 | 返回设备支持的目标模式 |
| **IddSampleMonitorAssignSwapChain** | 1006-1011 | 分配交换链时 | 创建`SwapChainProcessor` |
| **IddSampleMonitorUnassignSwapChain** | 1014-1019 | 取消交换链时 | 销毁处理线程 |

---

## 二、驱动内部函数

### Direct3DDevice 类

| 函数名 | 行号 | 说明 |
|--------|------|------|
| **Direct3DDevice::Direct3DDevice(LUID)** | 194-197 | 构造函数,保存AdapterLuid |
| **Direct3DDevice::Direct3DDevice()** | 199-202 | 默认构造函数 |
| **Direct3DDevice::Init()** | 205-232 | 初始化DXGI工厂、适配器、D3D设备 |

### SwapChainProcessor 类

| 函数名 | 行号 | 说明 |
|--------|------|------|
| **SwapChainProcessor::SwapChainProcessor()** | 369-380 | 构造函数,初始化并启动处理线程 |
| **SwapChainProcessor::~SwapChainProcessor()** | 382-394 | 析构函数,终止处理线程 |
| **SwapChainProcessor::RunThread()** | 396-400 | 线程入口函数 |
| **SwapChainProcessor::Run()** | 402-428 | 线程主函数 |
| **SwapChainProcessor::main_function()** | 432-582 | **核心处理循环**,屏幕捕获→编码→USB传输 |

### IndirectDeviceContext 类

| 函数名 | 行号 | 说明 |
|--------|------|------|
| **IndirectDeviceContext::IndirectDeviceContext()** | 659-662 | 构造函数 |
| **IndirectDeviceContext::~IndirectDeviceContext()** | 664-667 | 析构函数 |
| **IndirectDeviceContext::InitAdapter()** | 671-723 | 初始化适配器 |
| **IndirectDeviceContext::FinishInit()** | 725-736 | 完成初始化,创建监视器 |
| **IndirectDeviceContext::CreateMonitor()** | 738-789 | 创建虚拟监视器 |
| **IndirectDeviceContext::AssignSwapChain()** | 791-807 | 分配交换链 |
| **IndirectDeviceContext::UnassignSwapChain()** | 809-813 | 取消交换链 |

### 辅助函数

| 函数名 | 行号 | 说明 |
|--------|------|------|
| **fetch_grab_surface()** | 244-322 | 从DXGI表面抓取屏幕数据到framebuffer |
| **registry_config_base()** | 324-362 | 从注册表加载debug_level配置 |
| **IndirectDeviceContextWrapper::Cleanup()** | 238-242 | 清理设备上下文 |
| **CreateTargetMode()** | 926-943 | 创建目标模式结构 |
| **idd_usbdisp_evt_device_prepareHardware()** | 1027-1123 | USB硬件准备回调(内部函数) |
| **dispinfo()** | 594-605 | 生成视频信号信息(内联函数) |

---

## 三、完整调用顺序图

### 1. 驱动加载阶段 (DriverEntry)

```
DriverEntry() [72]
    ↓
    ┌───────────────┴───────────────┐
    ↓                               ↓
registry_config_base()         IddSampleDeviceAdd() [100]
[加载注册表配置]                       ↓
                          ┌─────────────────────────────────────┐
                          │ 注册WDF电源回调:                      │
                          │ • EvtDeviceD0Entry                 │
                          │ • EvtDevicePrepareHardware         │
                          │ • EvtDeviceReleaseHardware         │
                          │ • EvtDeviceSurpriseRemoval        │
                          └─────────────────────────────────────┘
                                     ↓
                              创建WDFDEVICE和IndirectDeviceContext
```

### 2. 设备初始化阶段 (D0Entry + PrepareHardware)

```
IddSampleDeviceD0Entry() [180]
    ↓
IndirectDeviceContext::InitAdapter() [671]
    ↓
IddCxAdapterInitAsync() → 异步初始化
    ↓
      (异步回调)
    ↓
IddSampleAdapterInitFinished() [821]
    ↓
IndirectDeviceContext::FinishInit() [725]
    ↓
IndirectDeviceContext::CreateMonitor() [738]
    ↓
IddCxMonitorCreate() → IddCxMonitorArrival()
    ↓
    ┌───────────────────┴───────────────────┐
    ↓                                       ↓
IddSampleDevicePrepareHardware() [1126]   (监视器已创建)
    ↓
idd_usbdisp_evt_device_prepareHardware() [1027]
    ↓
    ┌────────────────────────────────────────────────┐
    │ 1. WdfUsbTargetDeviceCreate()                   │
    │ 2. usb_get_discribe_info() - 读取USB描述符      │
    │ 3. tools_parse_usb_dev_info() - 解析配置字符串   │
    │ 4. 更新display_config_t配置                       │
    └────────────────────────────────────────────────┘
```

### 3. 交换链分配阶段 (用户选择显示模式后)

```
IddSampleMonitorAssignSwapChain() [1006]
    ↓
IndirectDeviceContext::AssignSwapChain() [791]
    ┌────────────────────────────────────┐
    │ 1. 创建Direct3DDevice               │
    │ 2. Direct3DDevice::Init()            │
    │    - CreateDXGIFactory2()            │
    │    - EnumAdapterByLuid()             │
    │    - D3D11CreateDevice()             │
    │ 3. 创建SwapChainProcessor            │
    └────────────────────────────────────┘
    ↓
SwapChainProcessor::SwapChainProcessor() [369]
    ┌────────────────────────────────────┐
    │ 1. 创建ImageEncoder                 │
    │ 2. usb_resouce_init() - 初始化URB   │
    │ 3. CreateThread() → 启动处理线程      │
    └────────────────────────────────────┘
    ↓
SwapChainProcessor::RunThread() [396]
    ↓
SwapChainProcessor::Run() [402]
    ↓
SwapChainProcessor::main_function() [432]
```

### 4. 屏幕捕获与传输循环 (main_function核心循环)

```
    ┌─────────┴─────────┐
    ↓                   ↓
IddCxSwapChainReleaseAnd    m_hAvailableBufferEvent
AcquireBuffer() [466]      (等待帧可用)
    ↓
    ┌───────────┴──────────────────────────────┐
    │ 检查USB状态: usb_is_connected() [501]   │
    └───────────┬──────────────────────────────┘
    ↓
InterlockedPopEntrySList(&urb_list) [508] - 获取URB
    ↓
    ┌───────────┴──────────────────────────────┐
    │ 1. fetch_grab_surface() [514]           │
    │    - 获取IDXGIResource                   │
    │    - 创建staging texture                │
    │    - CopyResource()                     │
    │    - Map() → memcpy到fb_buf             │
    │                                          │
    │ 2. ImageEncoder::encode() [519]         │
    │    - 编码为JPEG/RGB                     │
    │                                          │
    │ 3. usb_send_data_async() [527]          │
    │    - WdfUsbTargetPipeFormatRequest...   │
    │    - WdfRequestSetCompletionRoutine()    │
    │    - WdfRequestSend()                   │
    │                                          │
    │ 4. tools_perf_stats_update() [541]     │
    │    - 更新性能统计                       │
    └───────────┬──────────────────────────────┘
    ↓
IddCxSwapChainFinishedProcessingFrame() [562]
    ↓
    (循环往复)
```

### 5. 设备卸载/热插拔阶段

```
    ┌─────────────────────┴─────────────────────┐
    ↓                                           ↓
IddSampleDeviceReleaseHardware()      IddSampleDeviceSurpriseRemoval()
[1137]                                    [1154]
    ↓                                           ↓
usb_device_disconnect()                       ↓
    ↓                           usb_device_disconnect()
    ↓                                           ↓
SwapChainProcessor析构                           ↓
    ↓                                    tools_perf_stats_print()
usb_resouce_distory()
```

---

## 四、关键依赖关系

| 模块 | 依赖的外部模块 | 被依赖的内部模块 |
|------|---------------|----------------|
| **Driver.cpp** | usb_driver.cpp, encoder.cpp, tools.c | (主模块) |
| **SwapChainProcessor** | Direct3DDevice, ImageEncoder, USB模块 | IndirectDeviceContext |
| **IndirectDeviceContext** | (独立核心) | SwapChainProcessor |
| **Direct3DDevice** | DXGI, D3D11 | SwapChainProcessor |
| **ImageEncoder** | turbojpeg库 | SwapChainProcessor |
| **USB模块** (usb_driver.cpp) | WDF USB | SwapChainProcessor, Driver.cpp |
| **tools模块** | (独立工具) | 所有模块 |

---

## 五、数据流向

```
DXGI表面 → fetch_grab_surface() → fb_buf →
ImageEncoder::encode() → JPEG数据 →
urb_msg缓冲区 → usb_send_data_async() →
USB BulkOut端点 → 外部显示设备
```

---

## 六、关键函数详细说明

### DriverEntry (72-96行)
- **作用**: 驱动主入口函数,系统加载驱动时首先调用
- **关键操作**:
  - 调用`registry_config_base()`加载注册表配置
  - 初始化`WDF_DRIVER_CONFIG`并设置`IddSampleDeviceAdd`回调
  - 调用`WdfDriverCreate()`创建WDF驱动对象

### IddSampleDeviceAdd (100-177行)
- **作用**: 设备添加回调,创建设备对象并注册所有IDDCX和WDF回调
- **关键操作**:
  - 注册WDF电源管理回调(D0Entry, PrepareHardware, ReleaseHardware, SurpriseRemoval)
  - 初始化`IDD_CX_CLIENT_CONFIG`并设置所有IDDCX回调
  - 调用`IddCxDeviceInitConfig()`和`IddCxDeviceInitialize()`
  - 创建`IndirectDeviceContext`并初始化默认配置(分辨率1920x1080, JPEG编码, 30fps)

### IddSampleDeviceD0Entry (180-190行)
- **作用**: 设备进入D0(完全运行)状态时调用
- **关键操作**: 调用`IndirectDeviceContext::InitAdapter()`启动适配器初始化

### IndirectDeviceContext::InitAdapter (671-723行)
- **作用**: 初始化IDDCX适配器
- **关键操作**:
  - 设置适配器能力(IDDCX_ADAPTER_CAPS)
  - 调用`IddCxAdapterInitAsync()`异步初始化适配器
  - 保存适配器句柄和设备上下文

### IddSampleAdapterInitFinished (821-833行)
- **作用**: 适配器初始化完成的异步回调
- **关键操作**: 调用`IndirectDeviceContext::FinishInit()`开始创建监视器

### IndirectDeviceContext::CreateMonitor (738-789行)
- **作用**: 创建虚拟监视器
- **关键操作**:
  - 使用硬编码的EDID(s_KnownMonitorEdid)
  - 调用`IddCxMonitorCreate()`创建监视器对象
  - 调用`IddCxMonitorArrival()`通知系统监视器已插入

### IddSampleDevicePrepareHardware (1126-1134行)
- **作用**: 硬件准备阶段回调
- **关键操作**: 转发到`idd_usbdisp_evt_device_prepareHardware()`

### idd_usbdisp_evt_device_prepareHardware (1027-1123行)
- **作用**: USB设备硬件初始化
- **关键操作**:
  - 调用`WdfUsbTargetDeviceCreate()`创建USB设备对象
  - 调用`usb_get_discribe_info()`读取USB描述符中的配置字符串
  - 调用`tools_parse_usb_dev_info()`解析配置字符串并更新display_config_t
  - 解析内容示例: `U1_R1920x1080x30_E3x60_D1x0`

### IddSampleMonitorAssignSwapChain (1006-1011行)
- **作用**: 系统分配交换链时调用
- **关键操作**: 调用`IndirectDeviceContext::AssignSwapChain()`

### IndirectDeviceContext::AssignSwapChain (791-807行)
- **作用**: 分配交换链并启动处理线程
- **关键操作**:
  - 创建`Direct3DDevice`并调用`Init()`初始化DXGI/D3D11
  - 创建`SwapChainProcessor`对象(构造函数会自动启动处理线程)

### SwapChainProcessor::SwapChainProcessor (369-380行)
- **作用**: 构造函数,初始化处理线程
- **关键操作**:
  - 创建ImageEncoder对象
  - 调用`usb_resouce_init()`初始化URB池(5个urb_item)
  - 调用`CreateThread()`创建线程,入口为`RunThread()`

### SwapChainProcessor::Run (402-428行)
- **作用**: 线程主函数
- **关键操作**:
  - 调用`AvSetMmThreadCharacteristics()`设置多媒体线程优先级
  - 调用`main_function()`进入主处理循环
  - 退出时调用`usb_resouce_distory()`清理URB资源

### SwapChainProcessor::main_function (432-582行)
- **作用**: **核心处理循环**,负责屏幕捕获、编码、USB传输
- **关键流程**:
  1. 调用`IddCxSwapChainReleaseAndAcquireBuffer()`获取帧
  2. 检查USB连接状态
  3. 从URB池中获取空闲URB
  4. 调用`fetch_grab_surface()`抓取屏幕数据到fb_buf
  5. 调用`ImageEncoder::encode()`编码为JPEG
  6. 调用`usb_send_data_async()`异步发送到USB
  7. 更新性能统计
  8. 调用`IddCxSwapChainFinishedProcessingFrame()`通知处理完成
  9. 根据配置的FPS休眠或继续下一帧

### fetch_grab_surface (244-322行)
- **作用**: 从DXGI表面抓取屏幕数据
- **关键操作**:
  - 将GPU表面复制到CPU可访问的staging texture
  - 调用`Map()`锁定内存
  - 逐行拷贝到fb_buf(处理pitch对齐)
  - 调用`Unmap()`释放内存

### IddSampleParseMonitorDescription (853-902行)
- **作用**: 解析监视器EDID,返回支持的显示模式列表
- **关键操作**:
  - 遍历`s_KnownMonitorModes[]`硬编码的模式列表
  - 根据全局g_maxWidth和g_maxHeight过滤模式
  - 返回符合USB配置限制的模式

### IddSampleMonitorQueryModes (946-1003行)
- **作用**: 查询设备支持的目标模式(扫描输出能力)
- **关键操作**:
  - 创建24种常见分辨率的模式列表(1920x1080, 1280x720等)
  - 根据全局限制过滤模式
  - 返回过滤后的模式列表

### IddSampleDeviceReleaseHardware (1137-1151行)
- **作用**: 硬件释放阶段回调
- **关键操作**: 调用`usb_device_disconnect()`断开USB连接

### IddSampleDeviceSurpriseRemoval (1154-1169行)
- **作用**: USB设备意外拔出时调用
- **关键操作**:
  - 调用`usb_device_disconnect()`处理断开
  - 调用`tools_perf_stats_print()`打印性能统计

---

## 七、配置参数说明

### USB描述符配置字符串格式
设备通过USB产品描述符字符串传递配置,格式为: `U{reg_idx}_R{w}x{h}x{fps}_E{enc}x{qlt}_D{debug}x{sleep}`

| 配置项 | 格式 | 说明 | 默认值 |
|--------|------|------|--------|
| 注册索引 | `U{idx}` | 设备注册索引 | 0 |
| 分辨率+FPS | `R{w}x{h}x{fps}` | 例如: R1920x1080x30 | 1920x1080x30 |
| 编码+质量 | `E{type}x{qlt}` | 0=RGB565, 1=RGB888, 3=JPEG | 3x60 (JPEG质量60) |
| 调试+睡眠 | `D{level}x{sleep}` | 调试级别0-4,睡眠ms | 1x0 |

示例:
```
U1_R1280x720x60_E3x80_D3x100
```
表示: 注册索引1, 分辨率1280x720, 60fps, JPEG编码质量80, 调试级别3, 每帧后睡眠100ms

---

## 八、性能统计

驱动提供性能统计功能,统计指标包括:
- 总帧数
- 丢帧数
- 错误帧数
- 总传输字节数
- URB发送成功/失败数
- 平均抓取时间 (us)
- 平均编码时间 (us)
- 平均发送时间 (us)
- 平均总时间 (us)
- 成功率

调用方式:
```cpp
tools_perf_stats_update(&pContext->perf_stats, frame_size, grab_time, encode_time, send_time, success);
tools_perf_stats_print(&pContext->perf_stats);
```

---

## 九、调试支持

### 日志级别
- `LOG_LEVEL_ERROR = 0`: 仅错误
- `LOG_LEVEL_WARN = 1`: 警告及以上
- `LOG_LEVEL_INFO = 2`: 信息及以上
- `LOG_LEVEL_DEBUG = 3`: 调试及以上
- `LOG_LEVEL_TRACE = 4`: 全部日志

### 日志宏
- `LOGE(fmt, ...)`: 错误日志
- `LOGW(fmt, ...)`: 警告日志
- `LOGI(fmt, ...)`: 信息日志
- `LOGD(fmt, ...)`: 调试日志
- `LOGM(fmt, ...)`: 消息日志

### 配置方式
日志级别可通过以下方式配置:
1. 注册表: `HKLM\SYSTEM\CurrentControlSet\Services\IddSampleDriver\Parameters\debug_level`
2. USB描述符: `D{level}x{sleep}`

### 查看日志
使用DebugView工具查看`OutputDebugStringA()`输出的日志

---

## 十、注意事项

### 1. USB传输
- 使用异步USB传输,避免阻塞主循环
- URB池大小为5个,循环使用
- 发送失败时URB会自动回收到池中
- USB断开时会跳过帧捕获和传输

### 2. 内存管理
- fb_buf大小为`DISP_MAX_HEIGHT * DISP_MAX_WIDTH * 4` (约8MB)
- URB缓冲区动态分配,大小根据USB配置自动计算
- staging texture每帧创建和释放

### 3. 性能优化
- 使用多媒体线程优先级(AvSetMmThreadCharacteristics)
- 快速路径: 当DXGI pitch等于预期pitch时使用memcpy
- 支持FPS限流和自定义睡眠时间

### 4. 错误处理
- USB发送失败仅记录警告,不中断循环
- URB分配失败时跳过当前帧
- D3D初始化失败时删除交换链让系统重试

---

## 十一、外部依赖库

| 库名 | 用途 | 来源 |
|------|------|------|
| turbojpeg | JPEG编码 | libjpeg-turbo |
| jpeg-static.lib | JPEG静态库 | IddSampleDriver/lib/ |
| iddcx.lib | IDDCX框架 | Windows SDK |
| wdf.lib | WDF框架 | Windows SDK |
| dxgi.lib/d3d11.lib | DXGI/D3D11 | Windows SDK |

---

## 十二、编译要求

- **Windows SDK**: 10.0或更高版本
- **Visual Studio**: 2019或更高版本
- **驱动平台**: x64
- **驱动类型**: UMDF (User-Mode Driver Framework)
- **UMDF版本**: 2.x (支持UMDF_VERSION_MINOR >= 25)

---

**文档生成时间**: 2026-02-03
**驱动版本**: 1.0
**代码文件**: Driver.cpp (1174行)
