#pragma once

#include "bdfrpc/Types.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace bdfrpc {

struct LatestFrameStoreStats {
    std::uint64_t accepted_frames{0};
    std::uint64_t rejected_out_of_order{0};
};

class LatestFrameStore {
public:
    explicit LatestFrameStore(
        TimestampNs retention_ns = 1'000'000'000);

    bool push(CaptureFrame frame);
    SyncedFrameSet snapshot(TimestampNs now_ns) const;

    void set_retention_ns(TimestampNs retention_ns) noexcept;
    void clear();

    LatestFrameStoreStats stats() const noexcept;

private:
    mutable std::mutex mutex_;
    TimestampNs retention_ns_;
    std::unordered_map<std::string, CaptureFrame> latest_;
    LatestFrameStoreStats stats_;
};

} // namespace bdfrpc
