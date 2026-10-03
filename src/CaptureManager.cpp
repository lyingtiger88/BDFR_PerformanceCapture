#include "bdfrpc/CaptureManager.h"
#include <unordered_set>

namespace bdfrpc {

bool CaptureManager::add(std::unique_ptr<ICaptureSource> source) {
    if (!source || source->id().empty()) return false;

    for (const auto& existing : sources_) {
        if (existing->id() == source->id()) return false;
    }
    sources_.push_back(std::move(source));
    return true;
}

bool CaptureManager::start_all() {
    std::vector<ICaptureSource*> started;
    for (auto& source : sources_) {
        if (!source->start()) {
            for (auto* item : started) item->stop();
            return false;
        }
        started.push_back(source.get());
    }
    return true;
}

void CaptureManager::stop_all() noexcept {
    for (auto& source : sources_) source->stop();
}

std::vector<CaptureFrame> CaptureManager::poll_all() {
    std::vector<CaptureFrame> out;
    out.reserve(sources_.size());
    for (auto& source : sources_) {
        if (!source->running()) continue;
        if (auto frame = source->poll()) out.push_back(std::move(*frame));
    }
    return out;
}

std::size_t CaptureManager::source_count() const noexcept {
    return sources_.size();
}

} // namespace bdfrpc
