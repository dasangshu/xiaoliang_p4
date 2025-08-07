#include "posture_service.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <cstring>
#include <algorithm>

// ESP-DL姿态检测模型
#ifdef CONFIG_BOARD_TYPE_ESP32_P4_WIFI6_TOUCH_LCD_4B
// 暂时注释模型包含，解决依赖问题后启用
// #include "coco_pose.hpp"
#endif

const char* PostureService::TAG = "PostureService";

PostureService::PostureService() : detector_(std::make_unique<PostureDetector>()) {
    ESP_LOGI(TAG, "坐姿检测服务创建");
}

PostureService::~PostureService() {
    Stop();
    ESP_LOGI(TAG, "坐姿检测服务销毁");
}

bool PostureService::Initialize(std::shared_ptr<Camera> camera, std::shared_ptr<Display> display) {
    if (!camera || !display) {
        ESP_LOGE(TAG, "摄像头或显示器指针无效");
        return false;
    }
    
    camera_ = camera;
    display_ = display;
    
    // 初始化摄像头适配器
    camera_adapter_ = PostureCameraAdapterFactory::CreateAdapter(camera);
    if (!camera_adapter_) {
        ESP_LOGW(TAG, "摄像头适配器创建失败");
    }
    
    // 初始化姿态检测模型
    if (!InitializePoseModel()) {
        ESP_LOGW(TAG, "姿态检测模型初始化失败，将使用模拟数据");
    }
    
    // 初始化检测结果
    latest_result_.posture_type = PostureType::UNKNOWN;
    latest_result_.status_text = "服务已初始化";
    latest_result_.detail_text = "等待开始检测";
    
    ESP_LOGI(TAG, "坐姿检测服务初始化成功");
    return true;
}

bool PostureService::Start() {
    if (is_running_) {
        ESP_LOGW(TAG, "服务已经在运行");
        return true;
    }
    
    if (!camera_ || !display_) {
        ESP_LOGE(TAG, "服务未正确初始化");
        return false;
    }
    
    is_running_ = true;
    
    // 创建检测任务
    BaseType_t result = xTaskCreate(
        DetectionTask,
        "posture_detection",
        8192,  // 栈大小
        this,
        5,     // 优先级
        &detection_task_handle_
    );
    
    if (result != pdPASS) {
        ESP_LOGE(TAG, "创建检测任务失败");
        is_running_ = false;
        return false;
    }
    
    ESP_LOGI(TAG, "坐姿检测服务启动成功");
    return true;
}

void PostureService::Stop() {
    if (!is_running_) {
        return;
    }
    
    is_running_ = false;
    
    // 等待任务结束
    if (detection_task_handle_) {
        vTaskDelete(detection_task_handle_);
        detection_task_handle_ = nullptr;
    }
    
    ESP_LOGI(TAG, "坐姿检测服务已停止");
}

void PostureService::SetConfig(const PostureServiceConfig& config) {
    config_ = config;
    
    // 更新检测器阈值
    if (detector_) {
        detector_->SetThresholds(60.0f, 45.0f, 25.0f, 35.0f);
    }
    
    ESP_LOGI(TAG, "配置已更新: 检测间隔=%dms, 提醒间隔=%dms", 
             config_.detection_interval_ms, config_.alert_interval_ms);
}

void PostureService::SetResultCallback(PostureResultCallback callback) {
    result_callback_ = callback;
}

PostureResult PostureService::DetectPosture() {
    if (!detector_) {
        PostureResult result;
        result.posture_type = PostureType::UNKNOWN;
        result.status_text = "检测器未初始化";
        return result;
    }
    
    // 获取姿态关键点（这里需要集成具体的姿态检测模型）
    std::vector<int> keypoints = GetPoseKeypoints();
    
    if (keypoints.empty()) {
        PostureResult result;
        result.posture_type = PostureType::UNKNOWN;
        result.status_text = "未检测到人体";
        result.detail_text = "请确保摄像头正常工作";
        return result;
    }
    
    // 执行坐姿分析
    PostureResult result = detector_->AnalyzePosture(keypoints, config_.min_detection_confidence);
    
    // 更新最新结果
    {
        std::lock_guard<std::mutex> lock(result_mutex_);
        latest_result_ = result;
    }
    
    return result;
}

PostureResult PostureService::GetLatestResult() const {
    std::lock_guard<std::mutex> lock(result_mutex_);
    return latest_result_;
}

PostureService::Statistics PostureService::GetStatistics() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return statistics_;
}

void PostureService::ResetStatistics() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    statistics_ = Statistics{};
    ESP_LOGI(TAG, "统计信息已重置");
}

void PostureService::DetectionTask(void* param) {
    PostureService* service = static_cast<PostureService*>(param);
    service->RunDetection();
}

void PostureService::RunDetection() {
    ESP_LOGI(TAG, "检测任务开始运行");
    
    TickType_t last_wake_time = xTaskGetTickCount();
    
    while (is_running_) {
        if (config_.enable_detection) {
            PostureResult result = DetectPosture();
            ProcessResult(result);
            
            // 调用回调函数
            if (result_callback_) {
                result_callback_(result);
            }
        }
        
        // 等待下一次检测
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(config_.detection_interval_ms));
    }
    
    ESP_LOGI(TAG, "检测任务结束");
}

void PostureService::ProcessResult(const PostureResult& result) {
    // 更新统计信息
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        statistics_.total_detections++;
        
        if (result.posture_type == PostureType::NORMAL) {
            statistics_.good_posture_count++;
            consecutive_bad_posture_count_ = 0;  // 重置连续不良姿势计数
        } else if (result.posture_type != PostureType::UNKNOWN) {
            statistics_.bad_posture_count++;
            
            // 检查是否是相同的不良姿势
            if (result.posture_type == last_posture_type_) {
                consecutive_bad_posture_count_++;
            } else {
                consecutive_bad_posture_count_ = 1;
            }
        }
    }
    
    // 检查是否需要触发提醒
    if (config_.enable_voice_alerts && 
        result.posture_type != PostureType::NORMAL && 
        result.posture_type != PostureType::UNKNOWN &&
        consecutive_bad_posture_count_ >= config_.consecutive_bad_posture_count) {
        
        uint64_t current_time = esp_timer_get_time() / 1000;  // 转换为毫秒
        if (current_time - last_alert_time_ms_ >= config_.alert_interval_ms) {
            TriggerAlert(result);
            last_alert_time_ms_ = current_time;
            
            std::lock_guard<std::mutex> lock(stats_mutex_);
            statistics_.alerts_triggered++;
        }
    }
    
    // 更新显示覆盖
    if (config_.enable_display_overlay) {
        UpdateDisplayOverlay(result);
    }
    
    last_posture_type_ = result.posture_type;
}

void PostureService::TriggerAlert(const PostureResult& result) {
    if (!display_) return;
    
    std::string alert_message = "坐姿提醒: " + result.status_text;
    std::string detail_message = result.detail_text;
    
    // 显示通知
    display_->ShowNotification(alert_message, 3000);
    
    ESP_LOGI(TAG, "触发坐姿提醒: %s - %s", 
             result.status_text.c_str(), result.detail_text.c_str());
}

void PostureService::UpdateDisplayOverlay(const PostureResult& result) {
    if (!display_) return;
    
    // 更新状态显示
    std::string status = "坐姿: " + result.status_text;
    if (result.valid_keypoints_count > 0) {
        status += " (关键点:" + std::to_string(result.valid_keypoints_count) + ")";
    }
    
    display_->SetStatus(status.c_str());
    
    // 根据坐姿类型设置不同的表情
    switch (result.posture_type) {
        case PostureType::NORMAL:
            display_->SetEmotion("😊");  // 微笑
            break;
        case PostureType::SLOUCHING:
        case PostureType::LYING_DOWN:
            display_->SetEmotion("😟");  // 担心
            break;
        case PostureType::HEAD_SUPPORT:
            display_->SetEmotion("🤔");  // 思考
            break;
        case PostureType::LEAN_BACK:
            display_->SetEmotion("😴");  // 困倦
            break;
        case PostureType::TILTED:
            display_->SetEmotion("😵");  // 晕眩
            break;
        default:
            display_->SetEmotion("🤖");  // 机器人
            break;
    }
}

bool PostureService::InitializePoseModel() {
#ifdef CONFIG_BOARD_TYPE_ESP32_P4_WIFI6_TOUCH_LCD_4B
    try {
        pose_model_ = std::make_unique<COCOPose>(COCOPose::YOLO11N_POSE_224_P4);
        if (pose_model_) {
            ESP_LOGI(TAG, "YOLO11N姿态检测模型初始化成功");
            return true;
        }
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "姿态检测模型初始化异常: %s", e.what());
    }
#endif
    ESP_LOGW(TAG, "姿态检测模型不可用，将使用模拟数据");
    return false;
}

std::vector<int> PostureService::GetPoseKeypoints() {
    std::vector<int> keypoints(34, 0);  // 17个点 * 2坐标
    
#ifdef CONFIG_BOARD_TYPE_ESP32_P4_WIFI6_TOUCH_LCD_4B
    if (pose_model_ && camera_adapter_) {
        try {
            dl::image::img_t img;
            
            // 从摄像头适配器获取图像
            if (camera_adapter_->CaptureForPoseDetection(img)) {
                // 运行姿态检测模型
                auto& pose_results = pose_model_->run(img);
                
                if (!pose_results.empty()) {
                    // 获取第一个检测到的人体
                    const auto& first_person = pose_results.front();
                    
                    // 提取关键点坐标
                    const auto& pose_keypoints = first_person.keypoint;
                    
                    // 转换为我们的格式（17个点 * 2坐标）
                    for (int i = 0; i < 17 && i < pose_keypoints.size(); i++) {
                        keypoints[i*2] = static_cast<int>(pose_keypoints[i].x);
                        keypoints[i*2+1] = static_cast<int>(pose_keypoints[i].y);
                    }
                    
                    ESP_LOGD(TAG, "成功检测到人体姿态，关键点数: %d", pose_keypoints.size());
                    return keypoints;
                } else {
                    ESP_LOGD(TAG, "未检测到人体");
                }
            } else {
                ESP_LOGD(TAG, "摄像头图像捕获失败");
            }
        } catch (const std::exception& e) {
            ESP_LOGW(TAG, "姿态检测异常: %s", e.what());
        }
    }
#endif
    
    // 如果模型检测失败或不可用，返回模拟数据
    if (std::all_of(keypoints.begin(), keypoints.end(), [](int i){ return i == 0; })) {
        // 模拟一个正常坐姿的关键点数据
        keypoints[NOSE*2] = 120; keypoints[NOSE*2+1] = 80;           // 鼻子
        keypoints[LEFT_EYE*2] = 110; keypoints[LEFT_EYE*2+1] = 75;   // 左眼
        keypoints[RIGHT_EYE*2] = 130; keypoints[RIGHT_EYE*2+1] = 75; // 右眼
        
        // 模拟肩膀关键点
        keypoints[LEFT_SHOULDER*2] = 90; keypoints[LEFT_SHOULDER*2+1] = 140;   // 左肩
        keypoints[RIGHT_SHOULDER*2] = 150; keypoints[RIGHT_SHOULDER*2+1] = 140; // 右肩
        
        // 模拟手部关键点（不撑头）
        keypoints[LEFT_WRIST*2] = 60; keypoints[LEFT_WRIST*2+1] = 200;   // 左手腕
        keypoints[RIGHT_WRIST*2] = 180; keypoints[RIGHT_WRIST*2+1] = 200; // 右手腕
        
        ESP_LOGD(TAG, "使用模拟关键点数据");
    }
    
    return keypoints;
}

// PostureServiceManager 实现
bool PostureServiceManager::InitializeService(std::shared_ptr<Camera> camera, std::shared_ptr<Display> display) {
    if (service_) {
        ESP_LOGW("PostureServiceManager", "服务已经初始化");
        return true;
    }
    
    service_ = std::make_unique<PostureService>();
    
    if (!service_->Initialize(camera, display)) {
        ESP_LOGE("PostureServiceManager", "坐姿检测服务初始化失败");
        service_.reset();
        return false;
    }
    
    ESP_LOGI("PostureServiceManager", "坐姿检测服务管理器初始化成功");
    return true;
}
