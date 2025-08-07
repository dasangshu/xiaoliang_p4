// 这是一个集成示例，展示如何在主应用程序中使用DesktopMjpegManager

#include "desktop_mjpeg_manager.h"
#include "application.h" 
#include <esp_log.h>

#define TAG "IntegrationExample"

// 全局管理器实例
static DesktopMjpegManager* g_desktop_mjpeg_manager = nullptr;

/**
 * @brief 在应用程序初始化时调用此函数
 * @param parent_obj LVGL父对象
 * @param width 屏幕宽度
 * @param height 屏幕高度
 */
void InitializeDesktopMjpegManager(lv_obj_t* parent_obj, int width, int height) {
    if (!g_desktop_mjpeg_manager) {
        g_desktop_mjpeg_manager = new DesktopMjpegManager();
        g_desktop_mjpeg_manager->Initialize(parent_obj, width, height);
        
        // 设置MJPEG文件路径 - 使用对话时的动画
        g_desktop_mjpeg_manager->SetMjpegFile("/sdcard/talk.mjpeg");
        
        ESP_LOGI(TAG, "Desktop MJPEG manager initialized successfully");
    }
}

/**
 * @brief 处理语音唤醒事件，在Application::WakeWordInvoke中调用
 * 支持语音唤醒（如"小亮同学"）和串口唤醒信号
 * 这个函数应该在application.cc的WakeWordInvoke方法中被调用
 */
void HandleWakeUpEvent() {
    ESP_LOGI(TAG, "HandleWakeUpEvent called!");
    if (g_desktop_mjpeg_manager) {
        ESP_LOGI(TAG, "Voice/Serial wake up event received, switching to MJPEG chat mode");
        g_desktop_mjpeg_manager->ShowMjpegPlayer();
    } else {
        ESP_LOGE(TAG, "g_desktop_mjpeg_manager is NULL!");
    }
}

/**
 * @brief 切换回桌面模式，在对话结束后调用
 * 这个函数应该在设备状态变为idle时被调用
 */
void ReturnToDesktopMode() {
    if (g_desktop_mjpeg_manager) {
        ESP_LOGI(TAG, "Returning to desktop mode");
        g_desktop_mjpeg_manager->ShowDesktop();
    }
}

/**
 * @brief 设置管理器可见性
 * @param visible 是否可见
 */
void SetDesktopMjpegVisible(bool visible) {
    if (g_desktop_mjpeg_manager) {
        g_desktop_mjpeg_manager->SetVisible(visible);
    }
}

/**
 * @brief 清理资源
 */
void CleanupDesktopMjpegManager() {
    if (g_desktop_mjpeg_manager) {
        delete g_desktop_mjpeg_manager;
        g_desktop_mjpeg_manager = nullptr;
        ESP_LOGI(TAG, "Desktop MJPEG manager cleaned up");
    }
}

/*
集成步骤说明：

1. 在board配置文件中 (如 esp32-s3-touch-lcd-1.85.cc)：
   - 在显示初始化完成后调用 InitializeDesktopMjpegManager()

2. 在application.cc中：
   - 在WakeWordInvoke()方法中添加对HandleWakeUpEvent()的调用
   - 在SetDeviceState()方法中，当状态变为kDeviceStateIdle时调用ReturnToDesktopMode()

3. 文件系统准备：
   - 确保SPIFFS或其他文件系统中有对应的MJPEG/AVI文件
   - 文件路径需要根据实际情况进行调整

唤醒方式支持：
- 语音唤醒：说"小亮同学"等唤醒词
- 串口唤醒：发送特定的串口信号 (0xAA 0x55 0x00 0x55)
- 不支持触摸唤醒（已移除触摸功能）

示例代码片段：

// 在board文件中:
void InitDisplay() {
    // ... 显示初始化代码 ...
    InitializeDesktopMjpegManager(screen, display_width, display_height);
}

// 在application.cc的WakeWordInvoke中:
void Application::WakeWordInvoke(const std::string& wake_word) {
    HandleWakeUpEvent(); // 添加这行 - 语音或串口唤醒时切换到MJPEG模式
    
    if (device_state_ == kDeviceStateIdle) {
        ToggleChatState();
        // ... 其他代码 ...
    }
}

// 在application.cc的SetDeviceState中:
void Application::SetDeviceState(DeviceState state) {
    // ... 其他代码 ...
    
    if (state == kDeviceStateIdle) {
        ReturnToDesktopMode(); // 添加这行 - 对话结束后返回桌面
    }
    
    // ... 其他代码 ...
}
*/