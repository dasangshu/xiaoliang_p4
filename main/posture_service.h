#ifndef POSTURE_SERVICE_H
#define POSTURE_SERVICE_H

#include "posture_detection.h"
#include "boards/common/camera.h"
#include "display/display.h"
#include "posture_camera_adapter.h"
#include <memory>
#include <thread>
#include <atomic>
#include <functional>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

// 坐姿检测服务配置
struct PostureServiceConfig {
    bool enable_detection = true;
    bool enable_display_overlay = true;
    bool enable_voice_alerts = true;
    int detection_interval_ms = 1000;      // 检测间隔（毫秒）
    int alert_interval_ms = 5000;          // 提醒间隔（毫秒）
    int consecutive_bad_posture_count = 3; // 连续不良姿势次数触发提醒
    float min_detection_confidence = 0.3f; // 最小检测置信度
};

// 坐姿检测结果回调类型
using PostureResultCallback = std::function<void(const PostureResult&)>;

class PostureService {
public:
    PostureService();
    ~PostureService();
    
    // 初始化服务
    bool Initialize(std::shared_ptr<Camera> camera, std::shared_ptr<Display> display);
    
    // 启动/停止服务
    bool Start();
    void Stop();
    bool IsRunning() const { return is_running_; }
    
    // 配置管理
    void SetConfig(const PostureServiceConfig& config);
    PostureServiceConfig GetConfig() const { return config_; }
    
    // 回调管理
    void SetResultCallback(PostureResultCallback callback);
    
    // 手动触发检测
    PostureResult DetectPosture();
    
    // 获取最新检测结果
    PostureResult GetLatestResult() const;
    
    // 统计信息
    struct Statistics {
        uint32_t total_detections = 0;
        uint32_t good_posture_count = 0;
        uint32_t bad_posture_count = 0;
        uint32_t alerts_triggered = 0;
        std::string most_common_bad_posture;
    };
    Statistics GetStatistics() const;
    void ResetStatistics();

private:
    // 检测任务
    static void DetectionTask(void* param);
    void RunDetection();
    
    // 处理检测结果
    void ProcessResult(const PostureResult& result);
    
    // 触发提醒
    void TriggerAlert(const PostureResult& result);
    
    // 更新显示覆盖
    void UpdateDisplayOverlay(const PostureResult& result);
    
    // 从摄像头获取姿态数据
    std::vector<int> GetPoseKeypoints();
    
    // 初始化姿态检测模型
    bool InitializePoseModel();
    
private:
    std::shared_ptr<Camera> camera_;
    std::shared_ptr<Display> display_;
    std::unique_ptr<PostureDetector> detector_;
    std::unique_ptr<PostureCameraAdapter> camera_adapter_;
    
    PostureServiceConfig config_;
    PostureResultCallback result_callback_;
    
    std::atomic<bool> is_running_{false};
    TaskHandle_t detection_task_handle_ = nullptr;
    
    mutable std::mutex result_mutex_;
    PostureResult latest_result_;
    
    // 状态跟踪
    PostureType last_posture_type_ = PostureType::UNKNOWN;
    int consecutive_bad_posture_count_ = 0;
    uint64_t last_alert_time_ms_ = 0;
    
    // 统计信息
    mutable std::mutex stats_mutex_;
    Statistics statistics_;
    
    // 姿态检测模型
#ifdef CONFIG_BOARD_TYPE_ESP32_P4_WIFI6_TOUCH_LCD_4B
    std::unique_ptr<COCOPose> pose_model_;
#endif
    
    static const char* TAG;
};

// 全局坐姿检测服务实例
class PostureServiceManager {
public:
    static PostureServiceManager& GetInstance() {
        static PostureServiceManager instance;
        return instance;
    }
    
    bool InitializeService(std::shared_ptr<Camera> camera, std::shared_ptr<Display> display);
    PostureService* GetService() { return service_.get(); }
    
    PostureServiceManager(const PostureServiceManager&) = delete;
    PostureServiceManager& operator=(const PostureServiceManager&) = delete;

private:
    PostureServiceManager() = default;
    std::unique_ptr<PostureService> service_;
};

#endif // POSTURE_SERVICE_H
