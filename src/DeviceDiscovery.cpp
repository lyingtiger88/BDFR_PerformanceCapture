#include "bdfrpc/DeviceDiscovery.h"

#include <algorithm>
#include <unordered_map>
#include <utility>

namespace bdfrpc {

DeviceChangeSet DeviceMonitor::update(std::vector<CaptureDeviceDescriptor> snapshot) {
    DeviceChangeSet changes;

    std::unordered_map<std::string, CaptureDeviceDescriptor> old_by_id;
    std::unordered_map<std::string, CaptureDeviceDescriptor> new_by_id;

    for (const auto& item : snapshot_) old_by_id[item.id] = item;
    for (const auto& item : snapshot) new_by_id[item.id] = item;

    for (const auto& [id, item] : new_by_id) {
        const auto it = old_by_id.find(id);
        if (it == old_by_id.end()) {
            changes.added.push_back(item);
        } else if (!(it->second == item)) {
            changes.changed.push_back(item);
        }
    }

    for (const auto& [id, item] : old_by_id) {
        if (new_by_id.find(id) == new_by_id.end()) changes.removed.push_back(item);
    }

    auto by_id = [](const CaptureDeviceDescriptor& a,
                    const CaptureDeviceDescriptor& b) {
        return a.id < b.id;
    };
    std::sort(changes.added.begin(), changes.added.end(), by_id);
    std::sort(changes.removed.begin(), changes.removed.end(), by_id);
    std::sort(changes.changed.begin(), changes.changed.end(), by_id);
    std::sort(snapshot.begin(), snapshot.end(), by_id);

    snapshot_ = std::move(snapshot);
    return changes;
}

const std::vector<CaptureDeviceDescriptor>& DeviceMonitor::snapshot() const noexcept {
    return snapshot_;
}

} // namespace bdfrpc
