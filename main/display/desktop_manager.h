#ifndef DESKTOP_MANAGER_H
#define DESKTOP_MANAGER_H

#include <lvgl.h>
#include <esp_timer.h>
#include <string>
#include <chrono>
#include <functional>
#include "assets/desk2.h"

class DesktopManager {
public:
    DesktopManager();
    ~DesktopManager();

    void Initialize(lv_obj_t* parent, int width, int height);
    void UpdateDateTime();
    void SetVisible(bool visible);
    void SetWakeUpCallback(std::function<void()> callback);
    void HandleWakeUp();

private:
    lv_obj_t* desktop_container_ = nullptr;
    lv_obj_t* background_image_ = nullptr;
    lv_obj_t* date_label_ = nullptr;
    lv_obj_t* time_label_ = nullptr;
    lv_obj_t* weekday_label_ = nullptr;
    
    esp_timer_handle_t datetime_timer_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    std::function<void()> wakeup_callback_;

    void CreateDesktop();
    void CreateDateTimeLabels();
    static void DateTimeTimerCallback(void* arg);
    std::string GetWeekdayString(int weekday);
};

#endif // DESKTOP_MANAGER_H