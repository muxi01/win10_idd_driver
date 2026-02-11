# USB ACM设备支持说明

## 修改概述

本次修改将 `IddSampleDriver` 从专用USB Bulk显示设备驱动，扩展为支持**标准USB ACM设备**的显示驱动。

## 主要修改内容

### 1. INF文件修改 (`IddSampleDriver.inf`)

#### 新增设备匹配规则：
```inf
[Standard.NT$ARCH$]
; 原有特定VID/PID设备（保留兼容性）
%DeviceName%=MyDevice_Install, USB\VID_303A&PID_2987&MI_00

; USB CDC/ACM设备类匹配
; 设备类: 0x02 (Communications and CDC Control)
; 接口子类: 0x02 (Abstract Control Model)
; 接口协议: 0x01 (V.25ter, AT commands)
%ACMDeviceName%=MyDevice_Install, USB\Class_02&SubClass_02
%ACMDeviceName%=MyDevice_Install, USB\Class_02&SubClass_02&Prot_01

; CDC数据接口 (接口类: 0x0A)
%CDCDataDeviceName%=MyDevice_Install, USB\Class_02&SubClass_0A

; 通配符匹配所有CDC设备（谨慎使用）
%DeviceName%=MyDevice_Install, USB\Class_02
```

#### 新增设备名称：
```inf
ACMDeviceName="USB ACM Display Device"
CDCDataDeviceName="USB CDC Data Display Device"
```

### 2. USB接口选择修改 (`usb_driver.cpp`)

#### 原有实现：
- 只选择接口0（单一接口）
- 假设设备是单一Bulk接口

#### 修改后实现：
```cpp
NTSTATUS usb_select_interface(WDFDEVICE Device)
{
    // 扫描所有USB接口
    for (UCHAR ifIndex = 0; ifIndex < numInterfaces; ifIndex++) {
        // 获取接口描述符
        WdfUsbInterfaceGetDescriptor(usbInterface, &interfaceDesc);

        // 检查接口类代码
        interfaceClass = interfaceDesc.bInterfaceClass;
        interfaceSubClass = interfaceDesc.bInterfaceSubClass;
        interfaceProtocol = interfaceDesc.bInterfaceProtocol;

        // 枚举所有管道
        for (UCHAR pipeIndex = 0; pipeIndex < numPipes; pipeIndex++) {
            // 查找Bulk IN/OUT管道
            if (pipeInfo.PipeType == WdfUsbPipeTypeBulk) {
                if (WdfUsbTargetPipeIsInEndpoint(pipe)) {
                    // Bulk IN管道
                }
                else {
                    // Bulk OUT管道
                }
            }
        }
    }
}
```

**改进点**：
- 支持ACM多接口架构（控制接口 + 数据接口）
- 自动识别并绑定Bulk数据端点
- 打印详细的接口和管道信息便于调试

### 3. 默认编码器配置修改 (`Driver.cpp`)

#### 修改前：
```cpp
pContext->config.img_type = IMAGE_TYPE_RGB888;  // 未压缩
pContext->config.img_qlt = 5;              // JPEG质量
pContext->config.fps = 60;                  // 帧率
```

#### 修改后：
```cpp
pContext->config.img_type = IMAGE_TYPE_JPG;   // JPEG压缩
pContext->config.img_qlt = 60;              // 更高质量 (libjpeg 0-100)
pContext->config.fps = 30;                   // 降低帧率节省带宽
```

**修改原因**：
- ACM设备带宽有限，需要JPEG压缩
- 提高JPEG质量（60）保证显示效果
- 降低FPS（30）以适应ACM传输能力

## ACM设备架构

### 标准ACM设备接口结构

```
ACM复合设备
├── 接口0: CDC Control (Class=0x02, SubClass=0x02)
│   ├── 端点1: Interrupt IN (状态通知)
│   └── 端点2: Control (AT命令)
│
└── 接口1: CDC Data (Class=0x0A, SubClass=0x00)
    ├── 端点3: Bulk IN (数据接收)
    └── 端点4: Bulk OUT (数据发送) ← 用于发送JPEG帧
```

### 数据流程

```
Windows桌面
    ↓ DXGI SwapChain
IDDCX回调
    ↓ Direct3D
GPU Texture
    ↓ CPU映射
帧缓冲区 (fb_buf)
    ↓ ImageEncoder
JPEG压缩
    ↓ 添加帧头
[image_frame_header_t + JPEG数据]
    ↓ usb_send_data_async()
Bulk OUT Pipe
    ↓
ACM设备
    ↓ 显示
外部屏幕
```

## 使用指南

### 1. 编译驱动

```cmd
# 在Visual Studio中
1. 打开 IddSampleDriver.sln
2. 选择配置: Release x64
3. 构建 (Ctrl+Shift+B)
```

### 2. 签名驱动

```cmd
# 添加测试证书
certmgr /add test_certificate.cer /s /r localMachine root

# 或使用提供的批处理文件
add_cert.bat
```

### 3. 安装驱动

#### 方法1：设备管理器安装
1. 打开设备管理器
2. 查看您的ACM设备（通常在"端口(COM和LPT)"或"通用串行总线控制器"下）
3. 右键设备 → 更新驱动程序 → 浏览计算机上的驱动程序
4. 选择驱动程序位置 → IddSampleDriver.inf

#### 方法2：强制安装（用于非标准ACM设备）
1. 设备管理器 → 操作 → 添加过时硬件
2. 选择"显示适配器"
3. "从磁盘安装" → 选择INF文件
4. 选择"USB ACM Display Device"

### 4. 验证安装

#### 检查设备状态
```cmd
# 使用USBView查看设备接口
USBView.exe

# 查看驱动日志
DebugView++ (设置Capture → Enable Verbose Kernel Output)
```

#### 检查日志输出
驱动会输出以下信息：
```
USB device VID:PID: 0xXXXX:0xXXXX
USB device has X interfaces
Interface 0: Class=0x02, SubClass=0x02, Protocol=0x01
  Number of pipes: X
  Pipe 0: Type=2, Direction=IN, Endpoint=0x81, MaxPacket=64
  Pipe 1: Type=2, Direction=OUT, Endpoint=0x01, MaxPacket=512
  -> Assigned as BulkWritePipe: 0x..., max_packet_size: 512
USB interface configured successfully for ACM device
```

### 5. 配置显示

#### Windows显示设置
1. 打开显示设置
2. 会看到新增的虚拟显示器
3. 调整分辨率（建议：1280x720或800x600）
4. 设置为扩展显示器

#### 调整性能参数

通过注册表修改配置：
```
HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\IddSampleDriver\Parameters
```

可配置参数：
- `debug_level`: 日志级别 (0=ERROR, 1=WARN, 2=INFO, 3=DEBUG, 4=TRACE)
- 其他参数需在代码中修改编译

## 故障排查

### 问题1：驱动无法加载
**可能原因**：
- 未签名或签名不信任
- 设备匹配规则不正确

**解决方法**：
```cmd
# 检查驱动签名
signtool verify /pa /v IddSampleDriver.sys

# 查看设备管理器中的错误码
# 常见错误：
#   Code 10: 设备无法启动
#   Code 28: 未安装驱动程序
#   Code 37: 驱动程序需要重新安装
```

### 问题2：未找到Bulk管道
**日志显示**：
```
ERROR Device not properly configured: BulkReadPipe=00000000, BulkWritePipe=00000000
```

**解决方法**：
1. 使用USBView检查设备端点
2. 确认设备有Bulk IN/OUT端点
3. 检查是否需要配置（ACM控制接口）

### 问题3：帧率过低或卡顿
**原因**：
- ACM带宽不足
- JPEG质量过高
- URB池耗尽

**解决方法**：
```cpp
// 降低JPEG质量
pContext->config.img_qlt = 50;

// 降低FPS
pContext->config.fps = 15;

// 增加URB池大小
#define MAX_URB_SIZE 10  // usb_driver.h
```

### 问题4：显示画面有伪影或花屏
**原因**：
- 帧同步问题
- JPEG解码错误
- USB传输错误

**解决方法**：
```cpp
// 检查帧魔数（设备端）
if (header.magic_id != FRAME_MAGIC_ID) {
    // 丢弃帧
}

// 增加JPEG质量
pContext->config.img_qlt = 80;

// 检查USB错误计数
// 查看日志中的urbs_failed统计
```

## 性能优化建议

### 1. 网络带宽优化
```
分辨率      RGB888未压缩    JPEG 60%质量    JPEG 80%质量
1920x1080   ~6.2MB/frame     ~150KB/frame    ~300KB/frame
1280x720    ~2.7MB/frame     ~80KB/frame     ~160KB/frame
800x600      ~1.4MB/frame     ~40KB/frame     ~80KB/frame

所需带宽 (30 FPS):
1920x1080   ~186 MB/s         ~4.5 MB/s       ~9 MB/s
1280x720    ~81 MB/s          ~2.4 MB/s       ~4.8 MB/s
800x600      ~42 MB/s          ~1.2 MB/s       ~2.4 MB/s
```

**建议**：
- ACM全速设备（12 Mbps）：使用800x600@15fps
- ACM高速设备（480 Mbps）：使用1280x720@30fps

### 2. 编码参数调优
```cpp
// 高性能（低质量，高帧率）
img_type = IMAGE_TYPE_JPG
img_qlt = 40
fps = 60

// 平衡模式
img_type = IMAGE_TYPE_JPG
img_qlt = 60
fps = 30

- 高质量（低帧率）
img_type = IMAGE_TYPE_JPG
img_qlt = 85
fps = 15
```

### 3. URB池大小
```cpp
// 低配置设备
#define MAX_URB_SIZE 3

- 中等配置
#define MAX_URB_SIZE 5

- 高性能设备
#define MAX_URB_SIZE 10
```

## 帧头格式

设备需要识别以下帧头：

```c
typedef struct _image_frame_header_t {
    _u32 magic_id;   // 0x6C76736E ('lvsn')
    _u32 img_type;   // 3 = JPEG
    _u32 img_len;    // JPEG数据长度（不含帧头）
    _u32 img_cnt;    // 帧计数
} image_frame_header_t;
```

**接收端处理流程**：
1. 读取16字节帧头
2. 验证magic_id == 0x6C76736E
3. 验证img_type == 3 (JPEG)
4. 读取img_len字节的JPEG数据
5. 解码JPEG并显示

## 参考资料

### USB ACM规范
- USB CDC 1.2 Specification
- PSTN 1.2 (WMC) Specification

### JPEG编码
- libjpeg-turbo Documentation
- JPEG File Interchange Format (JFIF)

### Windows驱动开发
- UMDF 2.0 Programming Guide
- IDDCX (Indirect Display) Documentation

## 版本历史

| 版本 | 日期 | 修改内容 |
|------|------|----------|
| v1.0 | 2026-01-28 | 初始版本，支持Bulk显示设备 |
| v1.1 | 2026-02-01 | 添加USB ACM设备支持，默认JPEG编码 |

## 联系方式

- 作者: mrsha1195@163.com
- 项目: IddSampleDriver
