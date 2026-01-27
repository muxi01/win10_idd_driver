# IddSampleDriver 调试指南

## 目录
1. [日志系统](#日志系统)
2. [实时日志查看](#实时日志查看)
3. [日志级别配置](#日志级别配置)
4. [性能分析](#性能分析)
5. [常见问题调试](#常见问题调试)
6. [高级调试技巧](#高级调试技巧)

---

## 日志系统

### 日志机制
驱动使用两种日志系统：

1. **自定义日志系统**（tools.c）
   - `LOGE()` - 错误日志
   - `LOGW()` - 警告日志
   - `LOGI()` - 信息日志
   - `LOGD()` - 调试日志
   - `LOGM()` - 追踪日志

2. **WPP 跟踪系统**（Trace.h）
   - Windows 驱动框架的标准跟踪系统
   - GUID: `{b254994f-46e6-4718-80a0-0a3aa50d6ce4}`

### 日志输出位置
- **默认**：`OutputDebugStringA()` → Windows 调试输出流
- **查看工具**：DebugView、Visual Studio 输出窗口、VS Code 调试控制台

---

## 实时日志查看

### 方法 1：使用 DebugView（推荐）

1. **下载 DebugView**：
   - 从 Microsoft Sysinternals 下载：https://learn.microsoft.com/en-us/sysinternals/downloads/debugview
   - 或使用便携版本

2. **配置 DebugView**：
   ```
   Capture → Capture Kernel
   Capture → Capture Global Win32
   Filter → Highlight → 添加 "ERROR" (红色)
   Filter → Include → 添加 "INFO", "DEBUG"
   ```

3. **启用内核日志捕获**：
   - 以管理员身份运行 DebugView
   - 勾选 `Capture` → `Capture Kernel`
   - 勾选 `Capture` → `Capture Global Win32`

4. **过滤日志**：
   ```
   Filter → Filter → 设置过滤条件：
   Include: *INFO*, *WARN*, *ERROR*
   Exclude: *TRACE*
   ```

### 方法 2：使用 Visual Studio

1. **附加到进程**：
   ```
   调试 → 附加到进程
   选择: WUDFHost.exe (进程 ID 需要通过 PID 或驱动加载时机确定)
   ```

2. **查看输出窗口**：
   ```
   调试 → Windows → 输出
   选择：调试输出
   ```

3. **配置远程调试**（如果需要）：
   ```
   工具 → 选项 → 调试 → 远程
   设置远程调试服务器
   ```

### 方法 3：使用 VS Code

1. **安装扩展**：
   - C/C++ 扩展（Microsoft）
   - Native Debug 扩展

2. **创建启动配置**（`.vscode/launch.json`）：
   ```json
   {
       "version": "0.2.0",
       "configurations": [
           {
               "name": "Debug UMDF Driver",
               "type": "cppvsdbg",
               "request": "attach",
               "processId": "${command:pickProcess}",
               "processName": "WUDFHost.exe"
           }
       ]
   }
   ```

---

## 日志级别配置

### 通过注册表配置

1. **注册表位置**：
   ```
   HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Class\{4D36E968-E325-11CE-BFC1-08002BE10318}\####
   ```

2. **添加注册表项**：
   ```powershell
   # 以管理员身份运行 PowerShell
   reg add "HKLM\SYSTEM\CurrentControlSet\Control\Class\{4D36E968-E325-11CE-BFC1-08002BE10318}\0000" /v "DebugLevel" /t REG_DWORD /d 2 /f
   ```

3. **日志级别值**：
   ```c
   LOG_LEVEL_ERROR = 0  // 仅错误
   LOG_LEVEL_WARN  = 1  // 错误 + 警告
   LOG_LEVEL_INFO  = 2  // 错误 + 警告 + 信息（默认）
   LOG_LEVEL_DEBUG = 3  // 全部 + 调试
   LOG_LEVEL_TRACE = 4  // 全部 + 追踪
   ```

4. **修改默认日志级别**（编译时）：
   编辑 `Driver.cpp:31`：
   ```cpp
   LONG debug_level = LOG_LEVEL_DEBUG; // 改为 DEBUG 或 TRACE 级别
   ```

---

## 性能分析

### 性能统计功能

驱动内置性能统计（`tools.c:105-119`）：

```c
LOGI("=== Performance Statistics ===\n");
LOGI("Total frames: %llu\n", stats->total_frames);
LOGI("Dropped frames: %llu\n", stats->dropped_frames);
LOGI("Error frames: %llu\n", stats->error_frames);
LOGI("Total bytes: %llu MB\n", stats->total_bytes / (1024 * 1024));
LOGI("URBs sent: %llu\n", stats->urbs_sent);
LOGI("URBs failed: %llu\n", stats->urbs_failed);
LOGI("Avg grab time: %lld us\n", stats->avg_grab_time_us);
LOGI("Avg encode time: %lld us\n", stats->avg_encode_time_us);
LOGI("Avg send time: %lld us\n", stats->avg_send_time_us);
LOGI("Avg total time: %lld us\n", stats->avg_total_time_us);
LOGI("Success rate: %.2f%%\n", success_rate);
```

### 性能基准

800x480@60Hz 目标性能：
- **帧周期**：~16.67ms
- **JPEG 编码**：< 8ms
- **USB 传输**：< 6ms
- **总延迟**：< 16.67ms

### 性能分析工具

1. **Windows Performance Analyzer (WPA)**：
   ```
   1. 录制 ETW 跟踪：wpr -start GeneralProfile
   2. 运行测试场景
   3. 停止跟踪：wpr -stop trace.etl
   4. 分析：wpa trace.etl
   ```

2. **USB 协议分析**：
   - USBPcap：https://desowin.org/usbpcap/
   - Wireshark：打开 USBPcap 捕获文件

3. **GPU 性能分析**：
   - GPUView：https://learn.microsoft.com/en-us/windows-hardware/drivers/display/gpuview

---

## 常见问题调试

### 问题 1：驱动未加载

**症状**：
- 设备管理器中显示黄色感叹号
- DebugView 无日志输出

**调试步骤**：
1. 检查驱动签名：
   ```powershell
   # 以管理员身份运行
   Get-WindowsDriver -Online -Driver "IddSampleDriver.inf"
   ```

2. 查看事件查看器：
   ```
   事件查看器 → Windows 日志 → 系统 → 筛选当前日志 → 事件 ID: 219
   ```

3. 检查注册表：
   ```powershell
   reg query "HKLM\SYSTEM\CurrentControlSet\Control\Class\{4D36E968-E325-11CE-BFC1-08002BE10318}"
   ```

4. 启用内核调试：
   ```powershell
   bcdedit /set debug on
   bcdedit /set testsigning on
   ```

### 问题 2：USB 连接失败

**症状**：
- 日志显示 "Failed to get USB interface"
- 错误代码：`STATUS_UNSUCCESSFUL` 或 `STATUS_INVALID_DEVICE_STATE`

**调试步骤**：
1. 检查 USB 设备枚举：
   ```
   设备管理器 → 通用串行总线控制器 → 查看设备
   ```

2. 使用 USBView（Windows SDK）：
   ```
   USBView.exe → 查看设备描述符和配置描述符
   ```

3. 验证 VID/PID：
   - INF 文件：`USB\VID_303A&PID_2987&MI_00`
   - USB 设备：确认实际 VID/PID 是否匹配

4. 检查接口索引：
   ```cpp
   // 日志显示的接口信息：
   // "Configured USB interface %d for Bulk transfer\n", interfaceNumber
   // "Pipe %d: Type=%d, Direction=%s, Endpoint=0x%02x, MaxPacket=%d\n"
   ```

### 问题 3：图像显示异常

**症状**：
- 屏幕无显示
- 图像撕裂/闪烁
- 色彩失真

**调试步骤**：
1. 检查显示模式：
   ```
   显示设置 → 高级显示设置 → 查看当前分辨率和刷新率
   ```

2. 验证帧缓冲区大小：
   ```cpp
   // Driver.h:84
   uint8_t fb_buf[DISP_MAX_HEIGHT * DISP_MAX_WIDTH * 4];
   // 确保缓冲区大小足够：800 * 480 * 4 = 1,536,000 bytes
   ```

3. 检查 JPEG 编码：
   ```cpp
   // 查找编码日志：
   // "Encoding time: %lld us\n", encode_time
   // "Encoded size: %d bytes\n", jpeg_size
   ```

4. 分析 USB 传输：
   - 使用 USBPcap 捕获数据包
   - 验证端点地址和包大小

### 问题 4：性能问题

**症状**：
- 帧率低于 60fps
- CPU 占用率高
- 延迟过高

**调试步骤**：
1. 启用详细日志：
   ```cpp
   LONG debug_level = LOG_LEVEL_DEBUG;
   ```

2. 分析性能日志：
   ```
   === Performance Statistics ===
   Total frames: 3600        # 60 秒
   Dropped frames: 0         # 理想值为 0
   Avg total time: 15000 us  # 应 < 16667 us (60fps)
   ```

3. 优化建议：
   - 减小 JPEG 压缩质量
   - 使用硬件 JPEG 编码
   - 优化 USB 批量传输大小

---

## 高级调试技巧

### 使用 WinDbg 内核调试

1. **配置内核调试**：
   ```powershell
   # 启用串口调试（虚拟机）
   bcdedit /debug on
   bcdedit /dbgsettings serial debugport:1 baudrate:115200

   # 启用网络调试
   bcdedit /dbgsettings net hostip:192.168.1.100 port:50000
   ```

2. **加载符号**：
   ```
   .symfix
   .sympath+ SRV*C:\Symbols*https://msdl.microsoft.com/download/symbols
   .reload /f
   ```

3. **断点调试**：
   ```
   x IddSampleDriver!*           # 列出所有符号
   bp IddSampleDriver!SelectInterfaces  # 设置断点
   g                             # 继续执行
   k                             # 查看调用栈
   r                             # 查看寄存器
   dv                            # 查看局部变量
   ```

### ETW 跟踪

1. **启用 WPP 跟踪**：
   ```powershell
   # 创建跟踪会话
   logman create trace IddSampleDriver -p {b254994f-46e6-4718-80a0-0a3aa50d6ce4} 0xFF 0xFF -o IddTrace.etl

   # 启动跟踪
   logman start IddSampleDriver

   # 运行测试...

   # 停止跟踪
   logman stop IddSampleDriver

   # 转换为文本格式
   tracerpt IddTrace.etl -o IddTrace.txt -of CSV
   ```

2. **使用 TraceView**：
   - 打开 TraceView（Windows SDK）
   - File → Create New Log Session
   - 添加提供程序 GUID：`{b254994f-46e6-4718-80a0-0a3aa50d6ce4}`

### 内存泄漏检测

1. **启用用户模式堆跟踪**：
   ```powershell
   gflags /p /enable IddSampleDriver.dll /full
   ```

2. **使用 Application Verifier**：
   ```
   启用：Heaps、Handles、Locks 检查
   ```

3. **分析泄漏**：
   ```
   DebugView + UMDH 工具
   umdh -pn:WUDFHost.exe -f:1.txt
   # 运行测试...
   umdh -pn:WUDFHost.exe -f:2.txt
   umdh 1.txt 2.txt > diff.txt  # 差异分析
   ```

---

## 调试工作流程推荐

### 标准调试流程

```
1. 准备环境
   ├─ 安装 DebugView
   ├─ 启用测试签名：bcdedit /set testsigning on
   └─ 以管理员身份运行工具

2. 配置日志
   ├─ 注册表设置日志级别：reg add ... /v DebugLevel /d 3
   └─ 启动 DebugView 并配置过滤

3. 安装驱动
   ├─ 编译驱动
   ├─ 安装 INF 文件
   └─ 重启设备管理器（扫描硬件改动）

4. 观察日志
   ├─ 驱动初始化日志
   ├─ USB 设备连接日志
   ├─ 接口配置日志
   └─ 运行时日志

5. 定位问题
   ├─ 根据错误代码定位
   ├─ 查看调用栈（WinDbg）
   └─ 分析性能统计

6. 验证修复
   ├─ 重新编译驱动
   ├─ 卸载旧驱动：pnputil /delete-driver oemXX.inf
   ├─ 安装新驱动
   └─ 重复步骤 4-5
```

---

## 附录

### 常用命令速查

```powershell
# 驱动管理
pnputil /enum-drivers                          # 列出所有驱动
pnputil /delete-driver oemXX.inf               # 删除驱动
pnputil /add-driver IddSampleDriver.inf        # 添加驱动

# 设备管理
devcon status USB\VID_303A&PID_2987             # 查看设备状态
devcon rescan                                   # 扫描硬件改动
devcon remove USB\VID_303A&PID_2987            # 移除设备

# 注册表
reg query HKLM\SYSTEM\CurrentControlSet\Control\Class\{4D36E968-E325-11CE-BFC1-08002BE10318}
reg export HKLM\SYSTEM\CurrentControlSet\Control\Class\{4D36E968-E325-11CE-BFC1-08002BE10318} backup.reg

# 事件查看器
wevtutil qe System /c:20 /rd:true /f:text       # 查看最近 20 条系统日志
wevtutil qe System /q:"*[System[(EventID=219)]]"  # 过滤事件 ID 219

# USB 工具
usbview.exe                                     # USB 设备查看器
lsusb -v                                        # Linux USB 工具（WSL）
```

### 有用的链接

- [Windows 驱动调试](https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/debugging-a-driver)
- [ETW 跟踪](https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/etw)
- [UMDF 调试](https://learn.microsoft.com/en-us/windows-hardware/drivers/wdf/debugging-a-umdf-2-0-driver)
- [间接显示](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/indirect-display)

---

**最后更新**：2026-01-27
**驱动版本**：IddSampleDriver v1.0
**文档版本**：1.0
