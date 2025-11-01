# BLE图片传输功能说明

## 概述

CyberGlass现在支持通过BLE（蓝牙低功耗）传输图片。该功能允许移动设备或其他BLE客户端通过蓝牙连接请求拍照并接收图片数据。

## BLE服务信息

**服务UUID**: `c153f40c-b1eb-11f0-bf8b-dbb9b632dc78`

**设备名称**: `CyberGlass-XXXX` (XXXX为设备MAC地址后4位)

## BLE特征（Characteristics）

### 1. 图片请求 (Image Request)

- **UUID**: `e3e6c310-b762-11f0-a4f8-d323d6ee8628`
- **属性**: WRITE
- **功能**: 请求拍摄并准备传输图片
- **数据格式**: `[resolution_index, quality]`
    - `resolution_index` (1字节): 分辨率索引 (0-7)
        - 0: QQVGA (160x120)
        - 1: QVGA (320x240)
        - 2: VGA (640x480)
        - 3: SVGA (800x600)
        - 4: XGA (1024x768)
        - 5: HD (1280x720)
        - 6: SXGA (1280x1024)
        - 7: UXGA (1600x1200)
    - `quality` (1字节): JPEG质量 (0-63, 0为最高质量)

### 2. 图片信息 (Image Info)

- **UUID**: `f182b9d4-b762-11f0-8cab-7b33d60d040f`
- **属性**: READ + NOTIFY
- **功能**: 获取图片元数据和传输状态
- **数据格式**: `[status, size_low, size_mid_low, size_mid_high, size_high, chunks_low, chunks_high]` (7字节)
    - `status` (1字节): 传输状态
        - 0: 空闲
        - 1: 就绪（图片已准备好）
        - 2: 错误
        - 3: 图片太大
        - 4: 传输完成
    - `size` (4字节, 小端序): 图片总大小（字节）
    - `chunks` (2字节, 小端序): 总块数

### 3. 图片数据 (Image Data)

- **UUID**: `f5009d24-b762-11f0-9826-2f5155dc5a7b`
- **属性**: READ + NOTIFY
- **功能**: 接收图片数据块
- **数据格式**: `[chunk_index_low, chunk_index_high, ...data...]`
    - `chunk_index` (2字节, 小端序): 当前块索引
    - `data` (最多480字节): 图片数据

### 4. 图片控制 (Image Control)

- **UUID**: `f79a5a02-b762-11f0-9a55-0fae30ddfe0c`
- **属性**: WRITE
- **功能**: 控制图片传输过程
- **命令格式**:
    - 取消传输: `[0]`
    - 请求特定块: `[1, chunk_index_low, chunk_index_high]`

## 使用流程

### 基本流程

1. **连接到设备**
    - 扫描并连接到 `CyberGlass-XXXX` 设备
    - 发现服务 `c153f40c-b1eb-11f0-bf8b-dbb9b632dc78`

2. **订阅通知**
    - 订阅 Image Info 特征的通知
    - 订阅 Image Data 特征的通知

3. **请求拍照**
    - 写入 Image Request 特征，例如: `[1, 10]` (QVGA分辨率，质量10)

4. **接收图片信息**
    - 通过 Image Info 通知接收图片元数据
    - 解析图片大小和总块数

5. **接收图片数据**
    - 自动接收第一个数据块
    - 根据需要请求后续数据块（写入 Image Control）
    - 重组所有数据块得到完整JPEG图片

### 示例：Python客户端

```python
import asyncio
from bleak import BleakClient, BleakScanner

# UUIDs
SERVICE_UUID = "c153f40c-b1eb-11f0-bf8b-dbb9b632dc78"
IMAGE_REQUEST_UUID = "e3e6c310-b762-11f0-a4f8-d323d6ee8628"
IMAGE_INFO_UUID = "f182b9d4-b762-11f0-8cab-7b33d60d040f"
IMAGE_DATA_UUID = "f5009d24-b762-11f0-9826-2f5155dc5a7b"
IMAGE_CONTROL_UUID = "f79a5a02-b762-11f0-9a55-0fae30ddfe0c"

image_buffer = bytearray()
expected_size = 0
total_chunks = 0
received_chunks = set()


def image_info_callback(sender, data):
    global expected_size, total_chunks
    status = data[0]
    expected_size = int.from_bytes(data[1:5], 'little')
    total_chunks = int.from_bytes(data[5:7], 'little')

    print(f"Status: {status}, Size: {expected_size} bytes, Chunks: {total_chunks}")

    if status == 1:
        print("Image ready for transfer")
    elif status == 4:
        print("Transfer complete!")
        # Save image
        with open("captured_image.jpg", "wb") as f:
            f.write(image_buffer)


def image_data_callback(sender, data):
    global image_buffer, received_chunks
    chunk_index = int.from_bytes(data[0:2], 'little')
    chunk_data = data[2:]

    # Store chunk
    offset = chunk_index * 480
    if len(image_buffer) < offset + len(chunk_data):
        image_buffer.extend(b'\x00' * (offset + len(chunk_data) - len(image_buffer)))

    image_buffer[offset:offset + len(chunk_data)] = chunk_data
    received_chunks.add(chunk_index)

    print(f"Received chunk {chunk_index}/{total_chunks - 1} ({len(chunk_data)} bytes)")


async def main():
    # Scan for device
    print("Scanning for CyberGlass...")
    devices = await BleakScanner.discover()
    cyberglass_device = None
    for device in devices:
        if device.name and device.name.startswith("CyberGlass-"):
            cyberglass_device = device
            break

    if not cyberglass_device:
        print("CyberGlass device not found")
        return

    print(f"Found: {cyberglass_device.name}")

    # Connect
    async with BleakClient(cyberglass_device) as client:
        print("Connected!")

        # Subscribe to notifications
        await client.start_notify(IMAGE_INFO_UUID, image_info_callback)
        await client.start_notify(IMAGE_DATA_UUID, image_data_callback)

        # Request image capture (QVGA, quality 10)
        await client.write_gatt_char(IMAGE_REQUEST_UUID, bytes([1, 10]))
        print("Image capture requested")

        # Wait for transfer to complete
        await asyncio.sleep(30)

        # Request any missing chunks
        for i in range(total_chunks):
            if i not in received_chunks:
                print(f"Requesting missing chunk {i}")
                chunk_request = bytes([1, i & 0xFF, (i >> 8) & 0xFF])
                await client.write_gatt_char(IMAGE_CONTROL_UUID, chunk_request)
                await asyncio.sleep(0.1)


if __name__ == "__main__":
    asyncio.run(main())
```

## 配置参数

### 图片大小限制

- 最大图片大小: 64KB (`BLE_IMAGE_MAX_SIZE`)
- 如果图片超过此限制，请降低分辨率或增加JPEG质量值

### 块大小

- 每块480字节 (`BLE_IMAGE_CHUNK_SIZE`)
- 适配标准BLE MTU (512字节)

## 状态码说明

| 状态码 | 说明               |
|-----|------------------|
| 0   | 空闲状态             |
| 1   | 图片已准备好，可以开始传输    |
| 2   | 发生错误（拍照失败、内存不足等） |
| 3   | 图片太大，超过64KB限制    |
| 4   | 传输完成             |

## 性能优化建议

1. **选择合适的分辨率**
    - QQVGA (160x120): 约2-5KB，传输最快
    - QVGA (320x240): 约5-15KB，推荐用于BLE
    - VGA及以上: 可能超过64KB限制

2. **调整JPEG质量**
    - 较高的质量值(20-30)可减小文件大小
    - 质量值10-15可获得良好的图片质量和合理的大小

3. **网络延迟**
    - BLE传输速度约为1-2KB/s
    - QVGA图片传输时间约5-15秒

## 故障排除

### 问题: 图片传输失败

- **解决方案**: 检查图片大小是否超过64KB，尝试降低分辨率

### 问题: 缺少数据块

- **解决方案**: 使用 Image Control 特征请求缺失的块

### 问题: 连接断开

- **解决方案**: 确保设备在BLE范围内（约10米），重新连接并重新请求图片

### 问题: 图片质量差

- **解决方案**: 降低JPEG质量参数（使用0-10的值）

## 与WiFi功能的兼容性

BLE图片传输功能与现有的WiFi配网功能完全兼容。设备可以同时：

- 通过BLE进行WiFi配置
- 通过BLE传输图片
- 通过WiFi HTTP API提供更快的图片传输（如果已连接WiFi）

推荐使用场景：

- **BLE**: 初始设置、移动场景、低功耗需求
- **WiFi HTTP**: 高速连续图片传输、视频流

## 安全性说明

当前实现未启用BLE配对加密。如果需要安全传输：

1. 在物理隔离环境中使用
2. 或修改代码启用BLE Security（配对和加密）

## 技术规格总结

- **BLE版本**: 4.2+
- **最大MTU**: 512字节
- **数据块大小**: 480字节
- **最大图片大小**: 64KB
- **支持格式**: JPEG
- **传输速率**: 约1-2KB/s
- **有效范围**: 约10米

## 更新日志

### 2025-11-01

- 首次发布BLE图片传输功能
- 支持8种分辨率选择
- 支持可调JPEG质量
- 实现分块传输和断点续传
