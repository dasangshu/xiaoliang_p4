#include "desktop_manager.h"
#include <esp_log.h>
#include <time.h>
#include <sys/time.h>
#include "../assets/desk2.h"

#define TAG "DesktopManager"

DesktopManager::DesktopManager() {
}

DesktopManager::~DesktopManager() {
    if (datetime_timer_) {
        esp_timer_stop(datetime_timer_);
        esp_timer_delete(datetime_timer_);
        datetime_timer_ = nullptr;
    }
}

void DesktopManager::Initialize(lv_obj_t* parent, int width, int height) {
    width_ = width;
    height_ = height;

    // 创建桌面容器
    desktop_container_ = lv_obj_create(parent);
    lv_obj_set_size(desktop_container_, width_, height_);
    lv_obj_set_pos(desktop_container_, 0, 0);
    lv_obj_clear_flag(desktop_container_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(desktop_container_, 0, 0);
    lv_obj_set_style_pad_all(desktop_container_, 0, 0);

    // 创建桌面
    CreateDesktop();

    // 创建日期时间标签
    CreateDateTimeLabels();

    // 创建日期时间更新定时器
    esp_timer_create_args_t timer_args = {};
    timer_args.callback = DateTimeTimerCallback;
    timer_args.arg = this;
    timer_args.dispatch_method = ESP_TIMER_TASK;
    timer_args.name = "datetime_timer";

    esp_err_t ret = esp_timer_create(&timer_args, &datetime_timer_);
    if (ret == ESP_OK) {
        esp_timer_start_periodic(datetime_timer_, 1000000); // 每秒更新一次
    } else {
        ESP_LOGE(TAG, "Failed to create datetime timer");
    }

    ESP_LOGI(TAG, "Desktop manager initialized");
}

void DesktopManager::CreateDesktop() {
    // 创建背景图像
    background_image_ = lv_img_create(desktop_container_);
    lv_img_set_src(background_image_, &desk2);
    lv_obj_set_pos(background_image_, 0, 0);
}

void DesktopManager::CreateDateTimeLabels() {
    // 在桌面上创建日期时间标签
    if (!desktop_container_) return;

    // 创建日期标签 - 使用更大的字体，适合儿童青少年
    date_label_ = lv_label_create(desktop_container_);
    lv_obj_set_style_text_color(date_label_, lv_color_hex(0xFFE135), 0); // 亮黄色，更活泼
    lv_obj_set_style_text_font(date_label_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(date_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(date_label_, LV_ALIGN_TOP_MID, 0, 20);

    // 创建时间标签 - 最显眼的位置和大小
    time_label_ = lv_label_create(desktop_container_);
    lv_obj_set_style_text_color(time_label_, lv_color_hex(0x00E5FF), 0); // 青蓝色，清新活泼
    lv_obj_set_style_text_font(time_label_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(time_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(time_label_, LV_ALIGN_TOP_MID, 0, 50);

    // 创建星期标签 - 使用可爱的颜色
    weekday_label_ = lv_label_create(desktop_container_);
    lv_obj_set_style_text_color(weekday_label_, lv_color_hex(0xFF69B4), 0); // 粉红色，更有趣
    lv_obj_set_style_text_font(weekday_label_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(weekday_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(weekday_label_, LV_ALIGN_TOP_MID, 0, 85);

    // 立即更新一次
    UpdateDateTime();
}


void DesktopManager::UpdateDateTime() {
    if (!date_label_ || !time_label_ || !weekday_label_) return;

    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    // 格式化日期 - 更友好的显示格式
    char date_str[64];
    snprintf(date_str, sizeof(date_str), "%d月%d日", 
             timeinfo.tm_mon + 1, timeinfo.tm_mday);
    lv_label_set_text(date_label_, date_str);

    // 格式化时间 - 更大更清晰
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%H:%M", &timeinfo);
    lv_label_set_text(time_label_, time_str);

    // 格式化星期
    std::string weekday = GetWeekdayString(timeinfo.tm_wday);
    lv_label_set_text(weekday_label_, weekday.c_str());
}

void DesktopManager::SetVisible(bool visible) {
    if (!desktop_container_) return;

    if (visible) {
        lv_obj_clear_flag(desktop_container_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(desktop_container_, LV_OBJ_FLAG_HIDDEN);
    }
}

std::string DesktopManager::GetWeekdayString(int weekday) {
    const char* weekdays[] = {
        "星期天", "星期一", "星期二", "星期三", 
        "星期四", "星期五", "星期六"
    };
    
    if (weekday >= 0 && weekday <= 6) {
        return std::string(weekdays[weekday]);
    }
    return "未知";
}

void DesktopManager::DateTimeTimerCallback(void* arg) {
    DesktopManager* manager = static_cast<DesktopManager*>(arg);
    if (manager) {
        manager->UpdateDateTime();
    }
}

void DesktopManager::SetWakeUpCallback(std::function<void()> callback) {
    wakeup_callback_ = callback;
}

void DesktopManager::HandleWakeUp() {
    if (wakeup_callback_) {
        wakeup_callback_();
    }
}

