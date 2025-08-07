#include "posture_detection.h"
#include <cmath>
#include <algorithm>

const char* PostureDetector::TAG = "PostureDetector";

// 人体骨架连接定义
static const SkeletonConnection skeleton_connections[] = {
    // 头部连接
    {NOSE, LEFT_EYE},
    {NOSE, RIGHT_EYE},
    {LEFT_EYE, LEFT_EAR},
    {RIGHT_EYE, RIGHT_EAR},
    
    // 躯干连接
    {LEFT_SHOULDER, RIGHT_SHOULDER},
    {LEFT_SHOULDER, LEFT_HIP},
    {RIGHT_SHOULDER, RIGHT_HIP},
    {LEFT_HIP, RIGHT_HIP},
    
    // 左臂连接
    {LEFT_SHOULDER, LEFT_ELBOW},
    {LEFT_ELBOW, LEFT_WRIST},
    
    // 右臂连接
    {RIGHT_SHOULDER, RIGHT_ELBOW},
    {RIGHT_ELBOW, RIGHT_WRIST},
    
    // 左腿连接
    {LEFT_HIP, LEFT_KNEE},
    {LEFT_KNEE, LEFT_ANKLE},
    
    // 右腿连接
    {RIGHT_HIP, RIGHT_KNEE},
    {RIGHT_KNEE, RIGHT_ANKLE}
};

PostureDetector::PostureDetector() {
    ESP_LOGI(TAG, "坐姿检测器初始化完成");
}

PostureDetector::~PostureDetector() {
}

PostureResult PostureDetector::AnalyzePosture(const std::vector<int>& keypoints, float detection_confidence) {
    PostureResult result;
    result.posture_type = PostureType::UNKNOWN;
    result.head_shoulder_angle = 0.0f;
    result.head_tilt_angle = 0.0f;
    result.is_hand_supporting = false;
    result.valid_keypoints_count = 0;
    result.confidence = detection_confidence;
    
    // 检查关键点数据有效性
    if (keypoints.size() != 34) {  // 17个点 * 2坐标
        ESP_LOGW(TAG, "关键点数据无效，需要34个数值，实际: %d", keypoints.size());
        result.status_text = "数据无效";
        result.detail_text = "关键点数据格式错误";
        return result;
    }
    
    // 计算有效关键点数量
    for (int i = 0; i < 17; i++) {
        if (keypoints[i*2] > 0 && keypoints[i*2+1] > 0) {
            result.valid_keypoints_count++;
        }
    }
    
    // 检测质量控制
    if (result.valid_keypoints_count < 2 || detection_confidence < 0.3f) {
        result.posture_type = PostureType::NORMAL;  // 质量不佳时默认正常坐姿
        result.status_text = "检测质量不佳，默认正常坐姿";
        result.detail_text = "关键点: " + std::to_string(result.valid_keypoints_count) + 
                           ", 置信度: " + std::to_string(detection_confidence);
        ESP_LOGD(TAG, "检测质量不佳(关键点:%d, 置信度:%.2f)，默认正常坐姿", 
                result.valid_keypoints_count, detection_confidence);
        return result;
    }
    
    // 获取关键点坐标
    int nose_x = keypoints[NOSE*2], nose_y = keypoints[NOSE*2+1];
    int left_eye_x = keypoints[LEFT_EYE*2], left_eye_y = keypoints[LEFT_EYE*2+1];
    int right_eye_x = keypoints[RIGHT_EYE*2], right_eye_y = keypoints[RIGHT_EYE*2+1];
    int left_ear_x = keypoints[LEFT_EAR*2], left_ear_y = keypoints[LEFT_EAR*2+1];
    int right_ear_x = keypoints[RIGHT_EAR*2], right_ear_y = keypoints[RIGHT_EAR*2+1];
    int left_shoulder_x = keypoints[LEFT_SHOULDER*2], left_shoulder_y = keypoints[LEFT_SHOULDER*2+1];
    int right_shoulder_x = keypoints[RIGHT_SHOULDER*2], right_shoulder_y = keypoints[RIGHT_SHOULDER*2+1];
    
    // 检查头部特征点
    bool has_head_point = (nose_x > 0 && nose_y > 0) || 
                         (left_eye_x > 0 && left_eye_y > 0) || 
                         (right_eye_x > 0 && right_eye_y > 0) ||
                         (left_ear_x > 0 && left_ear_y > 0) ||
                         (right_ear_x > 0 && right_ear_y > 0);
    
    if (!has_head_point) {
        result.posture_type = PostureType::SLOUCHING;  // 检测不到头部时默认为驼背
        result.status_text = "未检测到头部";
        result.detail_text = "请调整摄像头角度";
        return result;
    }
    
    // 检查撑头状态（优先级最高）
    if (result.valid_keypoints_count >= 2) {
        result.is_hand_supporting = CheckHandSupportingHead(keypoints);
        if (result.is_hand_supporting) {
            result.posture_type = PostureType::HEAD_SUPPORT;
            result.status_text = "撑头";
            result.detail_text = "请将手放下，保持正确坐姿";
            return result;
        }
    }
    
    // 获取头部参考点
    auto head_point = GetHeadReferencePoint(keypoints);
    int head_x = head_point.first, head_y = head_point.second;
    
    // 检查肩膀信息
    bool has_shoulder = (left_shoulder_x > 0 && left_shoulder_y > 0) || 
                       (right_shoulder_x > 0 && right_shoulder_y > 0);
    
    if (has_shoulder && head_x > 0 && head_y > 0) {
        // 计算头肩角度
        if (left_shoulder_x > 0 && right_shoulder_x > 0 && 
            left_shoulder_y > 0 && right_shoulder_y > 0) {
            // 双肩都可见，计算头部到肩膀中心的角度
            int shoulder_center_x = (left_shoulder_x + right_shoulder_x) / 2;
            int shoulder_center_y = (left_shoulder_y + right_shoulder_y) / 2;
            result.head_shoulder_angle = CalculateAngle(head_x, head_y, shoulder_center_x, shoulder_center_y);
        } else if (left_shoulder_x > 0 && left_shoulder_y > 0) {
            // 只有左肩可见
            result.head_shoulder_angle = CalculateAngle(head_x, head_y, left_shoulder_x, left_shoulder_y);
        } else if (right_shoulder_x > 0 && right_shoulder_y > 0) {
            // 只有右肩可见
            result.head_shoulder_angle = CalculateAngle(head_x, head_y, right_shoulder_x, right_shoulder_y);
        }
        
        // 计算身体倾斜角度（基于肩膀连线）
        if (left_shoulder_x > 0 && right_shoulder_x > 0 && 
            left_shoulder_y > 0 && right_shoulder_y > 0) {
            float shoulder_line_angle = CalculateAngle(left_shoulder_x, left_shoulder_y, 
                                                      right_shoulder_x, right_shoulder_y);
            result.head_tilt_angle = std::abs(shoulder_line_angle - 90.0f);  // 与垂直线的夹角
            
            // 头部相对肩膀中心的水平偏移检测
            int shoulder_center_x = (left_shoulder_x + right_shoulder_x) / 2;
            float shoulder_width = std::abs(left_shoulder_x - right_shoulder_x);
            float horizontal_offset = std::abs(head_x - shoulder_center_x) / shoulder_width;
            
            if (horizontal_offset > 0.4f) {  // 头部偏离肩膀中心40%以上
                result.head_tilt_angle = std::max(result.head_tilt_angle, 30.0f);
            }
        }
        
        // 坐姿判断逻辑（按优先级顺序）
        if (result.head_shoulder_angle < lying_down_threshold_) {
            result.posture_type = PostureType::LYING_DOWN;
        } else if (result.head_shoulder_angle > 135.0f) {
            result.posture_type = PostureType::LEAN_BACK;
        } else if (result.head_tilt_angle > body_tilt_threshold_) {
            result.posture_type = PostureType::TILTED;
        } else if (result.head_shoulder_angle < slouch_threshold_) {
            result.posture_type = PostureType::SLOUCHING;
        } else if (result.head_shoulder_angle >= head_shoulder_normal_min_ && 
                  result.head_shoulder_angle <= head_shoulder_normal_max_) {
            result.posture_type = PostureType::NORMAL;
        } else {
            result.posture_type = PostureType::SLOUCHING;
        }
    } else {
        // 没有肩膀信息时，基于头部位置判断
        if (head_y < 100) {          // 头部很高，可能是正常坐姿
            result.posture_type = PostureType::NORMAL;
        } else if (head_y > 160) {   // 头部很低，可能是趴桌
            result.posture_type = PostureType::LYING_DOWN;
        } else {                     // 中间位置，可能是驼背
            result.posture_type = PostureType::SLOUCHING;
        }
    }
    
    // 生成状态文本
    result.status_text = GetPostureTypeName(result.posture_type);
    result.detail_text = GetPostureSuggestion(result.posture_type);
    
    return result;
}

void PostureDetector::SetThresholds(float slouch_threshold, float lying_down_threshold,
                                   float body_tilt_threshold, float hand_head_distance) {
    slouch_threshold_ = slouch_threshold;
    lying_down_threshold_ = lying_down_threshold;
    body_tilt_threshold_ = body_tilt_threshold;
    hand_head_distance_ = hand_head_distance;
    
    ESP_LOGI(TAG, "检测阈值已更新: 驼背=%.1f°, 趴桌=%.1f°, 倾斜=%.1f°, 撑头距离=%.1f", 
             slouch_threshold, lying_down_threshold, body_tilt_threshold, hand_head_distance);
}

std::string PostureDetector::GetPostureTypeName(PostureType type) {
    switch (type) {
        case PostureType::NORMAL: return "正常坐姿";
        case PostureType::LYING_DOWN: return "趴桌";
        case PostureType::HEAD_SUPPORT: return "撑头";
        case PostureType::SLOUCHING: return "弯腰驼背";
        case PostureType::LEAN_BACK: return "后仰";
        case PostureType::TILTED: return "身体倾斜";
        case PostureType::UNKNOWN: return "未知状态";
        default: return "未知状态";
    }
}

std::string PostureDetector::GetPostureSuggestion(PostureType type) {
    switch (type) {
        case PostureType::NORMAL: 
            return "坐姿良好，请保持";
        case PostureType::LYING_DOWN: 
            return "请抬起头部，挺直腰背";
        case PostureType::HEAD_SUPPORT: 
            return "请将手放下，保持正确坐姿";
        case PostureType::SLOUCHING: 
            return "请挺直腰背，调整坐姿";
        case PostureType::LEAN_BACK: 
            return "请不要过度后仰，保持端正";
        case PostureType::TILTED: 
            return "请调整身体位置，避免倾斜";
        case PostureType::UNKNOWN: 
            return "请调整摄像头角度";
        default: 
            return "请保持正确坐姿";
    }
}

float PostureDetector::CalculateDistance(int x1, int y1, int x2, int y2) {
    return std::sqrt((x1 - x2) * (x1 - x2) + (y1 - y2) * (y1 - y2));
}

float PostureDetector::CalculateAngle(int x1, int y1, int x2, int y2) {
    float dx = x2 - x1;
    float dy = y2 - y1;
    float angle = std::atan2(dy, dx) * 180.0f / M_PI;
    if (angle < 0) angle += 360.0f;  // 转换为0-360度
    return angle;
}

bool PostureDetector::CheckHandSupportingHead(const std::vector<int>& keypoints) {
    // 获取头部参考点
    auto head_point = GetHeadReferencePoint(keypoints);
    int head_x = head_point.first, head_y = head_point.second;
    
    if (head_x <= 0 || head_y <= 0) return false;
    
    // 检查左手和右手
    int left_wrist_x = keypoints[LEFT_WRIST*2], left_wrist_y = keypoints[LEFT_WRIST*2+1];
    int right_wrist_x = keypoints[RIGHT_WRIST*2], right_wrist_y = keypoints[RIGHT_WRIST*2+1];
    
    bool left_hand_supporting = false, right_hand_supporting = false;
    
    // 检查左手
    if (left_wrist_x > 0 && left_wrist_y > 0) {
        float distance = CalculateDistance(head_x, head_y, left_wrist_x, left_wrist_y);
        int vertical_diff = left_wrist_y - head_y;  // 正值表示手在头部下方
        int horizontal_diff = std::abs(left_wrist_x - head_x);
        
        if (distance < hand_head_distance_) {
            // 手在头部下方30像素内，或侧面且高度差不超过20像素
            if ((vertical_diff >= -10 && vertical_diff <= 30) || 
                (horizontal_diff > 15 && std::abs(vertical_diff) <= 20)) {
                left_hand_supporting = true;
            }
        }
    }
    
    // 检查右手
    if (right_wrist_x > 0 && right_wrist_y > 0) {
        float distance = CalculateDistance(head_x, head_y, right_wrist_x, right_wrist_y);
        int vertical_diff = right_wrist_y - head_y;
        int horizontal_diff = std::abs(right_wrist_x - head_x);
        
        if (distance < hand_head_distance_) {
            if ((vertical_diff >= -10 && vertical_diff <= 30) || 
                (horizontal_diff > 15 && std::abs(vertical_diff) <= 20)) {
                right_hand_supporting = true;
            }
        }
    }
    
    return left_hand_supporting || right_hand_supporting;
}

std::pair<int, int> PostureDetector::GetHeadReferencePoint(const std::vector<int>& keypoints) {
    int nose_x = keypoints[NOSE*2], nose_y = keypoints[NOSE*2+1];
    int left_eye_x = keypoints[LEFT_EYE*2], left_eye_y = keypoints[LEFT_EYE*2+1];
    int right_eye_x = keypoints[RIGHT_EYE*2], right_eye_y = keypoints[RIGHT_EYE*2+1];
    int left_ear_x = keypoints[LEFT_EAR*2], left_ear_y = keypoints[LEFT_EAR*2+1];
    int right_ear_x = keypoints[RIGHT_EAR*2], right_ear_y = keypoints[RIGHT_EAR*2+1];
    
    // 优先使用鼻子
    if (nose_x > 0 && nose_y > 0) {
        return {nose_x, nose_y};
    }
    
    // 其次使用双眼中点
    if (left_eye_x > 0 && left_eye_y > 0 && right_eye_x > 0 && right_eye_y > 0) {
        return {(left_eye_x + right_eye_x) / 2, (left_eye_y + right_eye_y) / 2};
    }
    
    // 使用单眼
    if (left_eye_x > 0 && left_eye_y > 0) {
        return {left_eye_x, left_eye_y};
    }
    if (right_eye_x > 0 && right_eye_y > 0) {
        return {right_eye_x, right_eye_y};
    }
    
    // 使用耳朵
    if (left_ear_x > 0 && left_ear_y > 0) {
        return {left_ear_x, left_ear_y};
    }
    if (right_ear_x > 0 && right_ear_y > 0) {
        return {right_ear_x, right_ear_y};
    }
    
    return {0, 0};  // 没有有效的头部点
}

std::vector<SkeletonConnection> GetSkeletonConnections() {
    std::vector<SkeletonConnection> connections;
    int num_connections = sizeof(skeleton_connections) / sizeof(skeleton_connections[0]);
    for (int i = 0; i < num_connections; i++) {
        connections.push_back(skeleton_connections[i]);
    }
    return connections;
}

// 绘制工具函数实现
namespace PostureDrawing {
    void DrawKeypoint(uint8_t* buffer, int width, int height, int x, int y, 
                     uint8_t r, uint8_t g, uint8_t b, int radius) {
        for (int dx = -radius; dx <= radius; ++dx) {
            for (int dy = -radius; dy <= radius; ++dy) {
                int nx = x + dx;
                int ny = y + dy;
                
                if (nx >= 0 && nx < width && ny >= 0 && ny < height) {
                    if (dx * dx + dy * dy <= radius * radius) {
                        int idx = (ny * width + nx) * 3;
                        buffer[idx] = r;     // R
                        buffer[idx + 1] = g; // G
                        buffer[idx + 2] = b; // B
                    }
                }
            }
        }
    }
    
    void DrawLine(uint8_t* buffer, int width, int height, int x1, int y1, int x2, int y2, 
                 uint8_t r, uint8_t g, uint8_t b, int thickness) {
        // 使用Bresenham算法绘制直线
        int dx = std::abs(x2 - x1);
        int dy = std::abs(y2 - y1);
        int sx = (x1 < x2) ? 1 : -1;
        int sy = (y1 < y2) ? 1 : -1;
        int err = dx - dy;
        
        int x = x1, y = y1;
        
        while (true) {
            // 绘制粗线（在当前点周围绘制thickness像素的区域）
            for (int tx = -thickness/2; tx <= thickness/2; tx++) {
                for (int ty = -thickness/2; ty <= thickness/2; ty++) {
                    int px = x + tx;
                    int py = y + ty;
                    if (px >= 0 && px < width && py >= 0 && py < height) {
                        int idx = (py * width + px) * 3;
                        buffer[idx] = r;
                        buffer[idx + 1] = g;
                        buffer[idx + 2] = b;
                    }
                }
            }
            
            if (x == x2 && y == y2) break;
            
            int e2 = 2 * err;
            if (e2 > -dy) {
                err -= dy;
                x += sx;
            }
            if (e2 < dx) {
                err += dx;
                y += sy;
            }
        }
    }
    
    void DrawRectangle(uint8_t* buffer, int width, int height, int x1, int y1, int x2, int y2,
                      uint8_t r, uint8_t g, uint8_t b, int thickness) {
        // 边界检查
        if (x1 < 0) x1 = 0;
        if (y1 < 0) y1 = 0;
        if (x2 >= width) x2 = width - 1;
        if (y2 >= height) y2 = height - 1;
        if (x1 > x2) std::swap(x1, x2);
        if (y1 > y2) std::swap(y1, y2);
        
        // 绘制四条边
        for (int t = 0; t < thickness; ++t) {
            // 上下边
            for (int x = x1; x <= x2; ++x) {
                if (y1 + t >= 0 && y1 + t < height) {
                    int idx = ((y1 + t) * width + x) * 3;
                    buffer[idx] = r; buffer[idx + 1] = g; buffer[idx + 2] = b;
                }
                if (y2 - t >= 0 && y2 - t < height) {
                    int idx = ((y2 - t) * width + x) * 3;
                    buffer[idx] = r; buffer[idx + 1] = g; buffer[idx + 2] = b;
                }
            }
            // 左右边
            for (int y = y1; y <= y2; ++y) {
                if (x1 + t >= 0 && x1 + t < width) {
                    int idx = (y * width + (x1 + t)) * 3;
                    buffer[idx] = r; buffer[idx + 1] = g; buffer[idx + 2] = b;
                }
                if (x2 - t >= 0 && x2 - t < width) {
                    int idx = (y * width + (x2 - t)) * 3;
                    buffer[idx] = r; buffer[idx + 1] = g; buffer[idx + 2] = b;
                }
            }
        }
    }
    
    void DrawSkeleton(uint8_t* buffer, int width, int height, const std::vector<int>& keypoints) {
        if (keypoints.size() != 34) return;
        
        // 绘制关键点（绿色圆点）
        for (int i = 0; i < 17; ++i) {
            int x = keypoints[i*2];
            int y = keypoints[i*2+1];
            if (x > 0 && y > 0) {
                DrawKeypoint(buffer, width, height, x, y, 0, 255, 0, 3);
            }
        }
        
        // 绘制骨架连接线（蓝色线条）
        auto connections = GetSkeletonConnections();
        for (const auto& conn : connections) {
            int x1 = keypoints[conn.point1*2];
            int y1 = keypoints[conn.point1*2+1];
            int x2 = keypoints[conn.point2*2];
            int y2 = keypoints[conn.point2*2+1];
            
            if (x1 > 0 && y1 > 0 && x2 > 0 && y2 > 0) {
                DrawLine(buffer, width, height, x1, y1, x2, y2, 0, 100, 255, 2);
            }
        }
    }
}
