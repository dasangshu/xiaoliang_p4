# 坐姿检测功能集成指南

## 概述

本文档介绍了如何在xiaoliang_p4项目中使用坐姿检测功能。该功能基于人体姿态检测技术，能够实时监测用户的坐姿并提供健康提醒。

## 功能特性

### 🎯 支持的坐姿类型
- ✅ **正常坐姿** - 健康的坐姿状态
- ❌ **趴桌** - 头部过低，趴在桌子上
- ❌ **撑头** - 用手撑着头部
- ❌ **弯腰驼背** - 腰背弯曲，头部前倾
- ❌ **后仰** - 身体过度后仰
- ❌ **身体倾斜** - 身体向一侧倾斜

### 🛠️ 核心功能
- **实时检测**: 可配置的检测间隔（默认2秒）
- **智能提醒**: 连续不良姿势时语音和视觉提醒
- **显示集成**: 在屏幕上显示坐姿状态和建议
- **统计分析**: 记录坐姿质量统计数据
- **可视化**: 支持绘制人体骨架和关键点

## 文件结构

```
main/
├── posture_detection.h         # 坐姿检测核心算法头文件
├── posture_detection.cc        # 坐姿检测核心算法实现
├── posture_service.h           # 坐姿检测服务头文件
├── posture_service.cc          # 坐姿检测服务实现
├── posture_example.cc          # 使用示例代码
├── application.h               # 应用类（已添加坐姿检测方法）
├── application.cc              # 应用类实现（已集成坐姿检测）
└── CMakeLists.txt             # 构建配置（已添加相关文件）
```

## 快速开始

### 1. 自动集成

坐姿检测功能已经自动集成到Application类中。当系统启动时，如果检测到可用的摄像头，坐姿检测服务会自动初始化并启动。

### 2. 基本使用

```cpp
#include "application.h"

auto& app = Application::GetInstance();

// 检查坐姿检测状态
if (app.IsPostureDetectionRunning()) {
    // 获取当前坐姿
    PostureResult result = app.GetCurrentPosture();
    ESP_LOGI("TAG", "当前坐姿: %s", result.status_text.c_str());
}

// 手动启动/停止
app.StartPostureDetection();
app.StopPostureDetection();
```

### 3. 自定义配置

```cpp
// 创建自定义配置
PostureServiceConfig config;
config.enable_detection = true;
config.enable_display_overlay = true;
config.enable_voice_alerts = true;
config.detection_interval_ms = 1500;      // 1.5秒检测一次
config.alert_interval_ms = 8000;          // 8秒提醒间隔
config.consecutive_bad_posture_count = 2; // 连续2次不良姿势就提醒

// 应用配置
app.SetPostureDetectionConfig(config);
```

## API 参考

### Application 类新增方法

```cpp
class Application {
public:
    // 初始化坐姿检测（通常由系统自动调用）
    bool InitializePostureDetection();
    
    // 启动坐姿检测
    void StartPostureDetection();
    
    // 停止坐姿检测
    void StopPostureDetection();
    
    // 检查坐姿检测是否正在运行
    bool IsPostureDetectionRunning() const;
    
    // 获取当前坐姿检测结果
    PostureResult GetCurrentPosture() const;
    
    // 设置坐姿检测配置
    void SetPostureDetectionConfig(const PostureServiceConfig& config);
};
```

### PostureDetector 类

```cpp
class PostureDetector {
public:
    // 分析坐姿（输入17个关键点的x,y坐标）
    PostureResult AnalyzePosture(const std::vector<int>& keypoints, 
                                 float detection_confidence = 0.5f);
    
    // 设置检测阈值
    void SetThresholds(float slouch_threshold = 60.0f, 
                      float lying_down_threshold = 45.0f,
                      float body_tilt_threshold = 25.0f,
                      float hand_head_distance = 35.0f);
    
    // 获取坐姿类型名称
    static std::string GetPostureTypeName(PostureType type);
    
    // 获取坐姿建议
    static std::string GetPostureSuggestion(PostureType type);
};
```

### PostureResult 结构体

```cpp
struct PostureResult {
    PostureType posture_type;        // 坐姿类型
    float head_shoulder_angle;       // 头肩角度
    float head_tilt_angle;          // 头部倾斜角度
    bool is_hand_supporting;        // 是否撑头
    int valid_keypoints_count;      // 有效关键点数量
    float confidence;               // 检测置信度
    std::string status_text;        // 状态文本
    std::string detail_text;        // 详细建议
};
```

### PostureServiceConfig 配置

```cpp
struct PostureServiceConfig {
    bool enable_detection = true;            // 启用检测
    bool enable_display_overlay = true;      // 启用显示覆盖
    bool enable_voice_alerts = true;         // 启用语音提醒
    int detection_interval_ms = 1000;        // 检测间隔（毫秒）
    int alert_interval_ms = 5000;            // 提醒间隔（毫秒）
    int consecutive_bad_posture_count = 3;   // 连续不良姿势计数阈值
    float min_detection_confidence = 0.3f;   // 最小检测置信度
};
```

## 绘制工具

项目提供了丰富的绘制工具，用于在图像上显示检测结果：

```cpp
namespace PostureDrawing {
    // 绘制关键点
    void DrawKeypoint(uint8_t* buffer, int width, int height, int x, int y, 
                     uint8_t r, uint8_t g, uint8_t b, int radius = 3);
    
    // 绘制连接线
    void DrawLine(uint8_t* buffer, int width, int height, int x1, int y1, int x2, int y2, 
                 uint8_t r, uint8_t g, uint8_t b, int thickness = 2);
    
    // 绘制矩形边框
    void DrawRectangle(uint8_t* buffer, int width, int height, int x1, int y1, int x2, int y2,
                      uint8_t r, uint8_t g, uint8_t b, int thickness = 2);
    
    // 绘制完整的人体骨架
    void DrawSkeleton(uint8_t* buffer, int width, int height, const std::vector<int>& keypoints);
}
```

## 使用示例

详细的使用示例请参考 `posture_example.cc` 文件，其中包含：

1. **基本使用示例** - 通过Application类使用
2. **自定义配置示例** - 调整检测参数
3. **直接检测器示例** - 使用PostureDetector类
4. **绘制工具示例** - 在图像上绘制骨架
5. **统计信息示例** - 获取检测统计数据

运行示例：
```cpp
// 在适当的地方调用
RunPostureDetectionExamples();

// 或创建独立任务
CreatePostureExampleTask();
```

## 配置说明

### 检测阈值配置

```cpp
// 默认阈值（可通过SetThresholds调整）
#define HEAD_SHOULDER_NORMAL_MIN    60.0f   // 正常头肩角度最小值
#define HEAD_SHOULDER_NORMAL_MAX    120.0f  // 正常头肩角度最大值
#define SLOUCH_THRESHOLD           60.0f    // 驼背检测阈值
#define LYING_DOWN_THRESHOLD       45.0f    // 趴桌检测阈值
#define BODY_TILT_THRESHOLD        25.0f    // 身体倾斜阈值
#define HAND_HEAD_DISTANCE         35.0f    // 撑头检测距离阈值
```

### 服务配置

```cpp
// 推荐的配置值
PostureServiceConfig config;
config.detection_interval_ms = 2000;       // 2秒检测一次（平衡性能和及时性）
config.alert_interval_ms = 10000;          // 10秒提醒间隔（避免过于频繁）
config.consecutive_bad_posture_count = 3;  // 连续3次确认（减少误报）
config.min_detection_confidence = 0.3f;    // 0.3最小置信度（适应不同光照）
```

## 系统要求

### 硬件要求
- ESP32-P4 开发板
- 支持的摄像头模块
- LCD显示屏（用于状态显示）

### 软件依赖
- ESP-IDF v5.4+
- LVGL（用于UI显示）
- 现有的xiaoliang_p4框架组件

### 内存使用
- 坐姿检测器：约2KB RAM
- 坐姿服务：约4KB RAM
- 检测任务栈：8KB
- 图像缓冲区：根据图像大小而定

## 性能优化

### 1. 检测频率优化
```cpp
// 根据应用场景调整检测频率
config.detection_interval_ms = 3000;  // 学习场景：3秒
config.detection_interval_ms = 1000;  // 办公场景：1秒
```

### 2. 质量控制
```cpp
// 调整质量阈值以平衡准确性和性能
config.min_detection_confidence = 0.4f;  // 提高准确性
config.min_detection_confidence = 0.2f;  // 提高检测率
```

### 3. 任务优先级
检测任务使用中等优先级(5)，可根据需要调整。

## 故障排除

### 常见问题

1. **坐姿检测不启动**
   - 检查摄像头是否正确连接
   - 确认Board::GetCamera()返回有效指针
   - 查看初始化日志

2. **检测结果不准确**
   - 调整摄像头角度和距离
   - 增加检测置信度阈值
   - 检查光照条件

3. **提醒过于频繁**
   - 增加alert_interval_ms
   - 增加consecutive_bad_posture_count
   - 调整检测阈值

4. **内存不足**
   - 减少检测频率
   - 优化图像缓冲区大小
   - 调整任务栈大小

### 调试日志

启用详细日志：
```cpp
esp_log_level_set("PostureDetector", ESP_LOG_DEBUG);
esp_log_level_set("PostureService", ESP_LOG_DEBUG);
```

## 扩展功能

### 1. 自定义姿态检测模型
可以通过修改`PostureService::GetPoseKeypoints()`方法集成不同的姿态检测模型。

### 2. 数据持久化
可以扩展统计功能，将数据保存到NVS或外部存储。

### 3. 网络集成
可以将检测结果通过MQTT或WebSocket发送到服务器。

### 4. 多用户支持
可以扩展以支持多用户坐姿检测和个性化配置。

## 更新日志

### v1.0.0 (2024-12)
- 初始版本发布
- 支持6种坐姿类型检测
- 集成到xiaoliang_p4框架
- 提供完整的API和示例

## 许可证

本坐姿检测功能遵循xiaoliang_p4项目的许可证条款。

## 支持

如有问题或建议，请联系开发团队或提交Issue。
