#include "bdfrpc/AdapterProtocol.h"
#include "bdfrpc/FrameSynchronizer.h"
#include <iostream>

int main() {
    using namespace bdfrpc;

    std::cout << "BDFR PerformanceCapture probe\n";
    std::cout << "protocol: "
              << encode_hello({1, "bdfrpc_probe", "0.1.0", "capture,sync,routing,fusion"})
              << "\n";

    FrameSynchronizer sync(8'000'000);
    sync.set_required_streams({"camera0", "camera1"});
    sync.push({"probe", "camera0", 1, 1'000'000'000, {}});
    sync.push({"probe", "camera1", 1, 1'004'000'000, {}});

    const auto set = sync.try_pop();
    if (!set) {
        std::cerr << "sync self-test failed\n";
        return 2;
    }

    std::cout << "sync self-test: OK, skew=" << set->max_skew_ns << " ns\n";
    return 0;
}
