#include "desktop_mjpeg_manager.h"
#include <esp_log.h>

#define TAG "DesktopMjpegManager"

DesktopMjpegManager::DesktopMjpegManager() {
}

DesktopMjpegManager::~DesktopMjpegManager() {
    mjpeg_player_port_deinit();
}

void DesktopMjpegManager::Initialize(lv_obj_t* parent, int width, int height) {
    parent_container_ = parent;
    width_ = width;
    height_ = height;
    
    // 初始化桌面管理器
    desktop_manager_.Initialize(parent, width, height);
    
    // 设置唤醒回调
    desktop_manager_.SetWakeUpCallback([this]() {
        OnDesktopWakeUp();
    });
    
    // 初始化MJPEG播放器
    mjpeg_player_port_config_t config = {
        .buffer_size = 0,  // 使用默认缓冲区大小
        .core_id = 1,      // 使用CPU核心1
        .use_psram = true, // 使用PSRAM
        .task_priority = 2 // 较低优先级
    };
    
    esp_err_t ret = mjpeg_player_port_init(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize MJPEG player: %s", esp_err_to_name(ret));
    }
    
    // 默认显示桌面
    ShowDesktop();
    
    ESP_LOGI(TAG, "Desktop MJPEG manager initialized");
}

void DesktopMjpegManager::ShowDesktop() {
    if (!is_desktop_mode_) {
        // 停止MJPEG播放
        mjpeg_player_port_stop();
        
        // 隐藏聊天容器
        if (chat_container_) {
            lv_obj_add_flag(chat_container_, LV_OBJ_FLAG_HIDDEN);
        }
        
        // 显示桌面
        desktop_manager_.SetVisible(true);
        
        is_desktop_mode_ = true;
        ESP_LOGI(TAG, "Switched to desktop mode");
    }
}

void DesktopMjpegManager::ShowMjpegPlayer() {
    if (is_desktop_mode_) {
        // 无论是否有MJPEG文件都尝试切换到对话模式
        if (!mjpeg_filepath_.empty()) {
            SwitchToMjpegMode();
        } else {
            // 没有MJPEG文件时，隐藏桌面显示一个简单的对话界面
            SwitchToSimpleChatMode();
        }
    }
}

void DesktopMjpegManager::SetVisible(bool visible) {
    if (is_desktop_mode_) {
        desktop_manager_.SetVisible(visible);
    }
}

void DesktopMjpegManager::SetMjpegFile(const std::string& filepath) {
    mjpeg_filepath_ = filepath;
    ESP_LOGI(TAG, "MJPEG file set to: %s", filepath.c_str());
}

void DesktopMjpegManager::SwitchToMjpegMode() {
    // 隐藏桌面
    desktop_manager_.SetVisible(false);

    
    // 播放MJPEG视频
    esp_err_t ret = mjpeg_player_port_play_file(mjpeg_filepath_.c_str());
    if (ret == ESP_OK) {
        // 设置循环播放
        mjpeg_player_port_set_loop(true);
        
        is_desktop_mode_ = false;
        ESP_LOGI(TAG, "Switched to MJPEG mode, playing: %s", mjpeg_filepath_.c_str());
    } else {
        ESP_LOGE(TAG, "Failed to play MJPEG file: %s", esp_err_to_name(ret));
        // 播放失败，保持桌面模式
        desktop_manager_.SetVisible(true);
    }
}

void DesktopMjpegManager::OnDesktopWakeUp() {
    ESP_LOGI(TAG, "Desktop wake up detected!");
    ShowMjpegPlayer();
}

void DesktopMjpegManager::SwitchToSimpleChatMode() {
    // 隐藏桌面
    desktop_manager_.SetVisible(false);
    
    // 创建一个简单的对话界面
    if (!chat_container_) {
        chat_container_ = lv_obj_create(parent_container_);
        lv_obj_set_size(chat_container_, width_, height_);
        lv_obj_set_style_bg_color(chat_container_, lv_color_hex(0x000000), 0);
        lv_obj_set_style_border_width(chat_container_, 0, 0);
        lv_obj_set_style_pad_all(chat_container_, 0, 0);
        lv_obj_align(chat_container_, LV_ALIGN_CENTER, 0, 0);
        
        // 添加对话状态标签
        lv_obj_t* chat_label = lv_label_create(chat_container_);
        lv_label_set_text(chat_label, "AI对话模式\n正在聆听...");
        lv_obj_set_style_text_color(chat_label, lv_color_hex(0x00FF00), 0);
        lv_obj_set_style_text_font(chat_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_align(chat_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(chat_label, LV_ALIGN_CENTER, 0, 0);
    }
    
    lv_obj_clear_flag(chat_container_, LV_OBJ_FLAG_HIDDEN);
    is_desktop_mode_ = false;
    ESP_LOGI(TAG, "Switched to simple chat mode");
}