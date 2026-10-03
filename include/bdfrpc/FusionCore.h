#pragma once

#include "bdfrpc/SourceRouter.h"

namespace bdfrpc {

class FusionCore {
public:
    explicit FusionCore(SourceRouter router = {});

    SourceRouter& router() noexcept { return router_; }
    const SourceRouter& router() const noexcept { return router_; }

    FusedPerformanceFrame fuse(const SyncedFrameSet& synced) const;

private:
    SourceRouter router_;
};

} // namespace bdfrpc
