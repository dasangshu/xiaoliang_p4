/**
 * @file posture_example.cc
 * @brief 坐姿检测功能使用示例
 * 
 * 这个文件展示了如何在xiaoliang_p4项目中使用坐姿检测功能
 */

#include "posture_service.h"
#include "posture_detection.h"
#include "application.h"
#include <esp_log.h>

static const char* TAG = "PostureExample";

/**
 * 示例1: 基本使用方式
 * 通过Application类使用坐姿检测功能
 */
void BasicUsageExample() {
    ESP_LOGI(TAG, "=== 基本使用示例 ===");
    
    auto& app = Application::GetInstance();
    
    // 检查坐姿检测是否正在运行
    if (app.IsPostureDetectionRunning()) {
        ESP_LOGI(TAG, "坐姿检测正在运行");
        
        // 获取当前坐姿检测结果
        PostureResult result = app.GetCurrentPosture();
        ESP_LOGI(TAG, "当前坐姿: %s", result.status_text.c_str());
        ESP_LOGI(TAG, "建议: %s", result.detail_text.c_str());
        ESP_LOGI(TAG, "有效关键点数: %d", result.valid_keypoints_count);
        ESP_LOGI(TAG, "检测置信度: %.2f", result.confidence);
    } else {
        ESP_LOGI(TAG, "坐姿检测未启动");
        
        // 启动坐姿检测
        app.StartPostureDetection();
    }
}

/**
 * 示例2: 自定义配置
 * 调整坐姿检测的参数
 */
void CustomConfigExample() {
    ESP_LOGI(TAG, "=== 自定义配置示例 ===");
    
    auto& app = Application::GetInstance();
    
    // 创建自定义配置
    PostureServiceConfig config;
    config.enable_detection = true;
    config.enable_display_overlay = true;
    config.enable_voice_alerts = true;
    config.detection_interval_ms = 1500;      // 1.5秒检测一次
    config.alert_interval_ms = 8000;          // 8秒提醒间隔
    config.consecutive_bad_posture_count = 2; // 连续2次不良姿势就提醒
    config.min_detection_confidence = 0.4f;  // 最小置信度0.4
    
    // 应用配置
    app.SetPostureDetectionConfig(config);
    
    ESP_LOGI(TAG, "自定义配置已应用");
}

/**
 * 示例3: 直接使用PostureDetector类
 * 如果需要更细粒度的控制
 */
void DirectDetectorExample() {
    ESP_LOGI(TAG, "=== 直接检测器示例 ===");
    
    // 创建检测器实例
    PostureDetector detector;
    
    // 设置自定义阈值
    detector.SetThresholds(
        65.0f,  // 驼背阈值
        40.0f,  // 趴桌阈值
        20.0f,  // 身体倾斜阈值
        30.0f   // 撑头距离阈值
    );
    
    // 模拟关键点数据（17个点 * 2坐标 = 34个数值）
    std::vector<int> keypoints(34, 0);
    
    // 设置一些模拟的关键点（正常坐姿）
    keypoints[PostureDetector::NOSE*2] = 120; keypoints[PostureDetector::NOSE*2+1] = 80;
    keypoints[PostureDetector::LEFT_EYE*2] = 110; keypoints[PostureDetector::LEFT_EYE*2+1] = 75;
    keypoints[PostureDetector::RIGHT_EYE*2] = 130; keypoints[PostureDetector::RIGHT_EYE*2+1] = 75;
    keypoints[PostureDetector::LEFT_SHOULDER*2] = 90; keypoints[PostureDetector::LEFT_SHOULDER*2+1] = 140;
    keypoints[PostureDetector::RIGHT_SHOULDER*2] = 150; keypoints[PostureDetector::RIGHT_SHOULDER*2+1] = 140;
    
    // 执行检测
    PostureResult result = detector.AnalyzePosture(keypoints, 0.8f);
    
    ESP_LOGI(TAG, "检测结果: %s", PostureDetector::GetPostureTypeName(result.posture_type).c_str());
    ESP_LOGI(TAG, "头肩角度: %.1f°", result.head_shoulder_angle);
    ESP_LOGI(TAG, "头部倾斜: %.1f°", result.head_tilt_angle);
    ESP_LOGI(TAG, "是否撑头: %s", result.is_hand_supporting ? "是" : "否");
}

/**
 * 示例4: 使用绘制工具
 * 在图像上绘制人体骨架
 */
void DrawingExample() {
    ESP_LOGI(TAG, "=== 绘制工具示例 ===");
    
    // 假设有一个240x240的RGB888图像缓冲区
    const int width = 240, height = 240;
    uint8_t* image_buffer = (uint8_t*)malloc(width * height * 3);
    
    if (!image_buffer) {
        ESP_LOGE(TAG, "无法分配图像缓冲区");
        return;
    }
    
    // 清空缓冲区（设为黑色）
    memset(image_buffer, 0, width * height * 3);
    
    // 模拟关键点数据
    std::vector<int> keypoints(34, 0);
    keypoints[PostureDetector::NOSE*2] = 120; keypoints[PostureDetector::NOSE*2+1] = 80;
    keypoints[PostureDetector::LEFT_EYE*2] = 110; keypoints[PostureDetector::LEFT_EYE*2+1] = 75;
    keypoints[PostureDetector::RIGHT_EYE*2] = 130; keypoints[PostureDetector::RIGHT_EYE*2+1] = 75;
    keypoints[PostureDetector::LEFT_SHOULDER*2] = 90; keypoints[PostureDetector::LEFT_SHOULDER*2+1] = 140;
    keypoints[PostureDetector::RIGHT_SHOULDER*2] = 150; keypoints[PostureDetector::RIGHT_SHOULDER*2+1] = 140;
    keypoints[PostureDetector::LEFT_WRIST*2] = 60; keypoints[PostureDetector::LEFT_WRIST*2+1] = 200;
    keypoints[PostureDetector::RIGHT_WRIST*2] = 180; keypoints[PostureDetector::RIGHT_WRIST*2+1] = 200;
    
    // 绘制检测框
    PostureDrawing::DrawRectangle(image_buffer, width, height, 50, 50, 190, 220, 255, 0, 0, 2);
    
    // 绘制完整的人体骨架
    PostureDrawing::DrawSkeleton(image_buffer, width, height, keypoints);
    
    // 单独绘制一些关键点
    PostureDrawing::DrawKeypoint(image_buffer, width, height, 120, 80, 255, 255, 0, 5); // 鼻子（黄色）
    PostureDrawing::DrawKeypoint(image_buffer, width, height, 90, 140, 0, 255, 255, 4); // 左肩（青色）
    PostureDrawing::DrawKeypoint(image_buffer, width, height, 150, 140, 0, 255, 255, 4); // 右肩（青色）
    
    ESP_LOGI(TAG, "人体骨架绘制完成");
    
    // 释放缓冲区
    free(image_buffer);
}

/**
 * 示例5: 统计信息
 * 获取坐姿检测的统计数据
 */
void StatisticsExample() {
    ESP_LOGI(TAG, "=== 统计信息示例 ===");
    
    auto& manager = PostureServiceManager::GetInstance();
    auto service = manager.GetService();
    
    if (!service) {
        ESP_LOGW(TAG, "坐姿检测服务未初始化");
        return;
    }
    
    // 获取统计信息
    auto stats = service->GetStatistics();
    
    ESP_LOGI(TAG, "总检测次数: %" PRIu32, stats.total_detections);
    ESP_LOGI(TAG, "良好坐姿次数: %" PRIu32, stats.good_posture_count);
    ESP_LOGI(TAG, "不良坐姿次数: %" PRIu32, stats.bad_posture_count);
    ESP_LOGI(TAG, "触发提醒次数: %" PRIu32, stats.alerts_triggered);
    ESP_LOGI(TAG, "最常见不良坐姿: %s", stats.most_common_bad_posture.c_str());
    
    if (stats.total_detections > 0) {
        float good_percentage = (float)stats.good_posture_count / stats.total_detections * 100.0f;
        ESP_LOGI(TAG, "良好坐姿比例: %.1f%%", good_percentage);
    }
}

/**
 * 运行所有示例
 * 这个函数可以在适当的时候调用来演示所有功能
 */
void RunPostureDetectionExamples() {
    ESP_LOGI(TAG, "开始坐姿检测功能示例演示");
    
    // 等待一段时间确保系统初始化完成
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    BasicUsageExample();
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    CustomConfigExample();
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    DirectDetectorExample();
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    DrawingExample();
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    StatisticsExample();
    
    ESP_LOGI(TAG, "坐姿检测功能示例演示完成");
}

/**
 * 创建示例任务
 * 可以在需要时调用此函数创建一个独立的任务来运行示例
 */
void CreatePostureExampleTask() {
    xTaskCreate([](void* param) {
        RunPostureDetectionExamples();
        vTaskDelete(NULL);
    }, "posture_example", 4096, NULL, 2, NULL);
}
