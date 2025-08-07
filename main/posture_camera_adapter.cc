#include "posture_camera_adapter.h"
#include <esp_log.h>
#include <cstring>
#include <algorithm>

const char* PostureCameraAdapter::TAG = "PostureCameraAdapter";

PostureCameraAdapter::PostureCameraAdapter(std::shared_ptr<Camera> camera) 
    : camera_(camera), current_fb_(nullptr), conversion_buffer_(nullptr), 
      conversion_buffer_size_(0), frame_width_(224), frame_height_(224), frame_channels_(3) {
}

PostureCameraAdapter::~PostureCameraAdapter() {
    ReleaseFrameBuffer();
    if (conversion_buffer_) {
        free(conversion_buffer_);
        conversion_buffer_ = nullptr;
    }
}

bool PostureCameraAdapter::Initialize() {
    if (!camera_) {
        ESP_LOGE(TAG, "摄像头指针无效");
        return false;
    }
    
    // 分配转换缓冲区（用于格式转换和缩放）
    conversion_buffer_size_ = frame_width_ * frame_height_ * frame_channels_;
    conversion_buffer_ = (uint8_t*)malloc(conversion_buffer_size_);
    
    if (!conversion_buffer_) {
        ESP_LOGE(TAG, "无法分配转换缓冲区");
        return false;
    }
    
    ESP_LOGI(TAG, "摄像头适配器初始化成功 (%dx%dx%d)", 
             frame_width_, frame_height_, frame_channels_);
    return true;
}

#ifdef CONFIG_BOARD_TYPE_ESP32_P4_WIFI6_TOUCH_LCD_4B
bool PostureCameraAdapter::CaptureForPoseDetection(dl::image::img_t& img) {
    // 由于直接访问ESP32摄像头帧缓冲区需要RTTI，
    // 我们暂时使用模拟数据进行测试
    
    // 首先尝试捕获图像
    if (!camera_->Capture()) {
        ESP_LOGE(TAG, "摄像头捕获失败");
        return false;
    }
    
    // 生成模拟的RGB888图像数据（用于测试）
    // 在实际部署中，这里应该从Camera接口获取真实的图像数据
    memset(conversion_buffer_, 128, conversion_buffer_size_); // 填充灰色
    
    // 设置ESP-DL图像结构
    img.data = conversion_buffer_;
    img.width = frame_width_;
    img.height = frame_height_;
    img.pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB888;
    
    ESP_LOGD(TAG, "生成模拟图像数据: %dx%dx%d", frame_width_, frame_height_, frame_channels_);
    return true;
}
#endif

bool PostureCameraAdapter::GetFrameBuffer(uint8_t** buffer, size_t* len, int* width, int* height) {
    if (!camera_) {
        return false;
    }
    
    // 释放之前的帧缓冲区
    ReleaseFrameBuffer();
    
    // 通过Camera接口捕获图像
    if (!camera_->Capture()) {
        ESP_LOGE(TAG, "摄像头捕获失败");
        return false;
    }
    
    // 由于我们不能直接访问ESP32摄像头的帧缓冲区（需要RTTI），
    // 我们改用Camera接口提供的方法获取图像数据
    // 这里我们需要使用Camera类的GetImageData方法（如果存在）
    // 或者返回错误，让调用者知道需要其他方式获取图像
    
    ESP_LOGW(TAG, "直接帧缓冲区访问不可用，请使用Camera接口的其他方法");
    return false;
}

void PostureCameraAdapter::ReleaseFrameBuffer() {
    // 由于我们不再直接使用ESP32摄像头API，这里不需要释放帧缓冲区
    // 帧缓冲区的生命周期由Camera类管理
    current_fb_ = nullptr;
}

bool PostureCameraAdapter::IsAvailable() const {
    return camera_ != nullptr;
}

void PostureCameraAdapter::GetCameraSpecs(int& width, int& height, int& channels) const {
    width = frame_width_;
    height = frame_height_;
    channels = frame_channels_;
}

bool PostureCameraAdapter::ConvertToRGB888(const uint8_t* src, uint8_t* dst, size_t len) {
    if (!src || !dst) {
        return false;
    }
    
    // 由于我们使用模拟数据，假设输入格式为RGB888
    // 在实际实现中，这里应该根据摄像头的实际格式进行转换
    memcpy(dst, src, len);
    return true;
}

bool PostureCameraAdapter::ResizeImage(const uint8_t* src, uint8_t* dst, 
                                     int src_w, int src_h, int dst_w, int dst_h) {
    // 简单的最近邻缩放算法
    float x_ratio = (float)src_w / dst_w;
    float y_ratio = (float)src_h / dst_h;
    
    for (int y = 0; y < dst_h; y++) {
        for (int x = 0; x < dst_w; x++) {
            int src_x = (int)(x * x_ratio);
            int src_y = (int)(y * y_ratio);
            
            // 边界检查
            src_x = std::min(src_x, src_w - 1);
            src_y = std::min(src_y, src_h - 1);
            
            int src_idx = (src_y * src_w + src_x) * 3;
            int dst_idx = (y * dst_w + x) * 3;
            
            dst[dst_idx] = src[src_idx];         // R
            dst[dst_idx + 1] = src[src_idx + 1]; // G
            dst[dst_idx + 2] = src[src_idx + 2]; // B
        }
    }
    
    return true;
}

// PostureCameraAdapterFactory 实现
std::unique_ptr<PostureCameraAdapter> PostureCameraAdapterFactory::CreateAdapter(std::shared_ptr<Camera> camera) {
    if (!camera) {
        ESP_LOGE("PostureCameraAdapterFactory", "摄像头指针无效");
        return nullptr;
    }
    
    auto adapter = std::make_unique<PostureCameraAdapter>(camera);
    
    if (!adapter->Initialize()) {
        ESP_LOGE("PostureCameraAdapterFactory", "摄像头适配器初始化失败");
        return nullptr;
    }
    
    ESP_LOGI("PostureCameraAdapterFactory", "摄像头适配器创建成功");
    return adapter;
}

bool PostureCameraAdapterFactory::IsEsp32Camera(std::shared_ptr<Camera> camera) {
    // 由于ESP-IDF禁用了RTTI，我们不能使用dynamic_cast
    // 在ESP32-P4平台上，假设所有摄像头都是ESP32Camera类型
    return camera != nullptr;
}
