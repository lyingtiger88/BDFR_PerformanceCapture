#pragma once

#include "bdfrpc/ICaptureSource.h"
#include <memory>
#include <string>
#include <vector>

namespace bdfrpc {

class CaptureManager {
public:
    bool add(std::unique_ptr<ICaptureSource> source);
    bool start_all();
    void stop_all() noexcept;
    std::vector<CaptureFrame> poll_all();
    std::size_t source_count() const noexcept;

private:
    std::vector<std::unique_ptr<ICaptureSource>> sources_;
};

} // namespace bdfrpc
