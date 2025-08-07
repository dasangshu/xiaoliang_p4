#ifndef POSTURE_CAMERA_ADAPTER_H
#define POSTURE_CAMERA_ADAPTER_H

#include "boards/common/camera.h"
#include "boards/common/esp32_camera.h"
#include <memory>

// ESP-DL图像结构
#ifdef CONFIG_BOARD_TYPE_ESP32_P4_WIFI6_TOUCH_LCD_4B
#include "dl_image.hpp"
#endif

/**
 * @brief 坐姿检测专用摄像头适配器
 * 
 * 这个类扩展了现有的Camera接口，提供了获取图像数据的功能，
 * 专门用于姿态检测模型的输入。
 */
class PostureCameraAdapter {
public:
    explicit PostureCameraAdapter(std::shared_ptr<Camera> camera);
    ~PostureCameraAdapter();
    
    // 初始化适配器
    bool Initialize();
    
    // 捕获图像并返回ESP-DL格式的图像数据
#ifdef CONFIG_BOARD_TYPE_ESP32_P4_WIFI6_TOUCH_LCD_4B
    bool CaptureForPoseDetection(dl::image::img_t& img);
#endif
    
    // 获取原始摄像头帧缓冲区
    bool GetFrameBuffer(uint8_t** buffer, size_t* len, int* width, int* height);
    
    // 释放帧缓冲区
    void ReleaseFrameBuffer();
    
    // 检查摄像头是否可用
    bool IsAvailable() const;
    
    // 获取摄像头规格
    void GetCameraSpecs(int& width, int& height, int& channels) const;

private:
    std::shared_ptr<Camera> camera_;
    void* current_fb_; // 改为void*以避免包含esp_camera.h
    
    // 图像转换缓冲区
    uint8_t* conversion_buffer_;
    size_t conversion_buffer_size_;
    
    // 摄像头规格
    int frame_width_;
    int frame_height_;
    int frame_channels_;
    
    // 转换函数
    bool ConvertToRGB888(const uint8_t* src, uint8_t* dst, size_t len);
    bool ResizeImage(const uint8_t* src, uint8_t* dst, int src_w, int src_h, int dst_w, int dst_h);
    
    static const char* TAG;
};

/**
 * @brief 摄像头适配器工厂
 * 
 * 根据不同的摄像头类型创建合适的适配器
 */
class PostureCameraAdapterFactory {
public:
    static std::unique_ptr<PostureCameraAdapter> CreateAdapter(std::shared_ptr<Camera> camera);
    
private:
    static bool IsEsp32Camera(std::shared_ptr<Camera> camera);
};

#endif // POSTURE_CAMERA_ADAPTER_H
