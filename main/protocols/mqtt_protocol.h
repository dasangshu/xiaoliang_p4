#ifndef MQTT_PROTOCOL_H
#define MQTT_PROTOCOL_H


#include "protocol.h"
#include <mqtt.h>
#include <udp.h>
#include <cJSON.h>
#include <mbedtls/aes.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

#include <functional>
#include <string>
#include <map>
#include <mutex>
#include <queue>
#include <memory>
#include <chrono>

#define MQTT_PING_INTERVAL_SECONDS 90
#define MQTT_RECONNECT_INTERVAL_MS 10000

#define MQTT_PROTOCOL_SERVER_HELLO_EVENT (1 << 0)

class MqttProtocol : public Protocol {
public:
    MqttProtocol();
    ~MqttProtocol();

    bool Start() override;
    bool SendAudio(std::unique_ptr<AudioStreamPacket> packet) override;
    bool OpenAudioChannel() override;
    void CloseAudioChannel() override;
    bool IsAudioChannelOpened() const override;

private:
    EventGroupHandle_t event_group_handle_;

    std::string publish_topic_;

    std::mutex channel_mutex_;
    std::unique_ptr<Mqtt> mqtt_;
    std::unique_ptr<Udp> udp_;
    mbedtls_aes_context aes_ctx_;
    std::string aes_nonce_;
    std::string udp_server_;
    int udp_port_;
    uint32_t local_sequence_;
    uint32_t remote_sequence_;
    
    // 音频包缓冲和重排序相关
    struct BufferedAudioPacket {
        uint32_t sequence;
        uint32_t timestamp;
        std::unique_ptr<AudioStreamPacket> packet;
        std::chrono::steady_clock::time_point received_time;
    };
    std::priority_queue<std::unique_ptr<BufferedAudioPacket>, 
                       std::vector<std::unique_ptr<BufferedAudioPacket>>,
                       std::function<bool(const std::unique_ptr<BufferedAudioPacket>&, 
                                        const std::unique_ptr<BufferedAudioPacket>&)>> audio_buffer_;
    static constexpr size_t MAX_AUDIO_BUFFER_SIZE = 10;  // 最多缓冲10个音频包
    static constexpr uint32_t MAX_SEQUENCE_GAP = 5;      // 最大允许的序列号间隔
    std::chrono::steady_clock::time_point last_audio_process_time_;

    bool StartMqttClient(bool report_error=false);
    void ParseServerHello(const cJSON* root);
    std::string DecodeHexString(const std::string& hex_string);
    
    // 音频包处理方法
    void ProcessAudioPacket(std::unique_ptr<BufferedAudioPacket> buffered_packet);
    void FlushAudioBuffer();
    bool ShouldProcessPacketImmediately(uint32_t sequence);

    bool SendText(const std::string& text) override;
    std::string GetHelloMessage();
};


#endif // MQTT_PROTOCOL_H
