#ifndef SSID_MANAGER_H
#define SSID_MANAGER_H

#include <cstdint>
#include <string>
#include <vector>

struct SsidItem {
    std::string ssid;
    std::string password;
    // 0 = unknown. 1-14 = 2.4 GHz, 36-177 = 5 GHz. The ranges do not overlap,
    // so one uint8_t NVS key (channel / channelN) covers both bands.
    uint8_t channel = 0;
};

class SsidManager {
public:
    static SsidManager& GetInstance() {
        static SsidManager instance;
        return instance;
    }

    void AddSsid(const std::string& ssid, const std::string& password, uint8_t channel = 0);
    void UpdateSsidChannel(const std::string& ssid, uint8_t channel);
    void RemoveSsid(int index);
    void SetDefaultSsid(int index);
    void Clear();
    const std::vector<SsidItem>& GetSsidList() const { return ssid_list_; }
    std::vector<uint8_t> GetSavedChannels() const;

private:
    SsidManager();
    ~SsidManager();

    void LoadFromNvs();
    void SaveToNvs();

    std::vector<SsidItem> ssid_list_;
};

#endif // SSID_MANAGER_H
