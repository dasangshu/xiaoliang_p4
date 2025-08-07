#ifndef DESKTOP_MJPEG_MANAGER_H
#define DESKTOP_MJPEG_MANAGER_H

#include "desktop_manager.h"
#include "mjpeg_player_port.h"
#include <functional>

class DesktopMjpegManager {
public:
    DesktopMjpegManager();
    ~DesktopMjpegManager();

    void Initialize(lv_obj_t* parent, int width, int height);
    void ShowDesktop();
    void ShowMjpegPlayer();
    void SetVisible(bool visible);
    
    // 设置MJPEG文件路径
    void SetMjpegFile(const std::string& filepath);

private:
    DesktopManager desktop_manager_;
    lv_obj_t* parent_container_ = nullptr;
    lv_obj_t* chat_container_ = nullptr;
    std::string mjpeg_filepath_;
    bool is_desktop_mode_ = true;
    int width_ = 0;
    int height_ = 0;

    void SwitchToMjpegMode();
    void SwitchToSimpleChatMode();
    void OnDesktopWakeUp();
};

#endif // DESKTOP_MJPEG_MANAGER_H