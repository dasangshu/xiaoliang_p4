#ifndef POSTURE_DETECTION_H
#define POSTURE_DETECTION_H

#include <vector>
#include <string>
#include <cstdint>
#include <esp_log.h>

// 坐姿类型枚举
enum class PostureType {
    NORMAL = 0,      // 正常坐姿
    LYING_DOWN,      // 趴桌
    HEAD_SUPPORT,    // 撑头
    SLOUCHING,       // 弯腰驼背
    LEAN_BACK,       // 后仰
    TILTED,          // 身体倾斜
    UNKNOWN          // 未知状态
};

// 检测结果结构体
struct PostureResult {
    PostureType posture_type;
    float head_shoulder_angle;
    float head_tilt_angle;
    bool is_hand_supporting;
    int valid_keypoints_count;
    float confidence;
    std::string status_text;
    std::string detail_text;
};

// 人体关键点索引定义（COCO 17点格式）
enum KeypointIndex {
    NOSE = 0,
    LEFT_EYE = 1,
    RIGHT_EYE = 2,
    LEFT_EAR = 3,
    RIGHT_EAR = 4,
    LEFT_SHOULDER = 5,
    RIGHT_SHOULDER = 6,
    LEFT_ELBOW = 7,
    RIGHT_ELBOW = 8,
    LEFT_WRIST = 9,
    RIGHT_WRIST = 10,
    LEFT_HIP = 11,
    RIGHT_HIP = 12,
    LEFT_KNEE = 13,
    RIGHT_KNEE = 14,
    LEFT_ANKLE = 15,
    RIGHT_ANKLE = 16
};

class PostureDetector {
public:
    PostureDetector();
    ~PostureDetector();

    // 分析坐姿，输入17个关键点的x,y坐标（共34个数值）
    PostureResult AnalyzePosture(const std::vector<int>& keypoints, float detection_confidence = 0.5f);
    
    // 设置检测阈值
    void SetThresholds(float slouch_threshold = 60.0f, 
                      float lying_down_threshold = 45.0f,
                      float body_tilt_threshold = 25.0f,
                      float hand_head_distance = 35.0f);
    
    // 获取坐姿类型名称
    static std::string GetPostureTypeName(PostureType type);
    
    // 获取坐姿建议
    static std::string GetPostureSuggestion(PostureType type);

private:
    // 计算两点间距离
    float CalculateDistance(int x1, int y1, int x2, int y2);
    
    // 计算两点连线与水平线的夹角
    float CalculateAngle(int x1, int y1, int x2, int y2);
    
    // 检查手是否在撑头
    bool CheckHandSupportingHead(const std::vector<int>& keypoints);
    
    // 计算头部参考点
    std::pair<int, int> GetHeadReferencePoint(const std::vector<int>& keypoints);
    
    // 检测阈值配置
    float head_shoulder_normal_min_ = 60.0f;
    float head_shoulder_normal_max_ = 120.0f;
    float head_tilt_threshold_ = 20.0f;
    float body_tilt_threshold_ = 25.0f;
    float slouch_threshold_ = 60.0f;
    float lying_down_threshold_ = 45.0f;
    float hand_head_distance_ = 35.0f;
    
    static const char* TAG;
};

// 人体骨架连接定义（用于绘制）
struct SkeletonConnection {
    int point1;
    int point2;
};

// 获取人体骨架连接线定义
std::vector<SkeletonConnection> GetSkeletonConnections();

// 绘制工具函数
namespace PostureDrawing {
    // 在RGB888图像上绘制关键点
    void DrawKeypoint(uint8_t* buffer, int width, int height, int x, int y, 
                     uint8_t r, uint8_t g, uint8_t b, int radius = 3);
    
    // 在RGB888图像上绘制连接线
    void DrawLine(uint8_t* buffer, int width, int height, int x1, int y1, int x2, int y2, 
                 uint8_t r, uint8_t g, uint8_t b, int thickness = 2);
    
    // 在RGB888图像上绘制矩形边框
    void DrawRectangle(uint8_t* buffer, int width, int height, int x1, int y1, int x2, int y2,
                      uint8_t r, uint8_t g, uint8_t b, int thickness = 2);
    
    // 绘制完整的人体骨架
    void DrawSkeleton(uint8_t* buffer, int width, int height, const std::vector<int>& keypoints);
}

#endif // POSTURE_DETECTION_H
