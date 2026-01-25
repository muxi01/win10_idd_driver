QQ 交流群：496016248

# USB屏幕驱动工作原理
## 以微软idd驱动sampledriver（https://github.com/roshkins/IddSampleDriver）为BASE
- 1.SwapChainProcessor::RunCore()采集屏幕数据。
- 2.usb_send_jpeg_image()
- 3.usb_send_msg_async（）