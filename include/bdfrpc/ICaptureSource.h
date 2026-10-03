#pragma once

#include "bdfrpc/Types.h"
#include <optional>
#include <string>

namespace bdfrpc {

class ICaptureSource {
public:
    virtual ~ICaptureSource() = default;

    virtual const std::string& id() const noexcept = 0;
    virtual bool start() = 0;
    virtual void stop() noexcept = 0;
    virtual bool running() const noexcept = 0;

    // Non-blocking poll. A live source should timestamp as close to acquisition as possible.
    virtual std::optional<CaptureFrame> poll() = 0;
};

} // namespace bdfrpc
