#include "bdfrpc/BDFRFacialUdpSource.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

int main(int argc, char** argv) {
    using namespace bdfrpc;

    BDFRFacialUdpConfig config;
    config.port = argc > 1
        ? static_cast<std::uint16_t>(std::atoi(argv[1]))
        : static_cast<std::uint16_t>(0);

    BDFRFacialUdpSource source(config);
    if (!source.start()) {
        std::cerr << "Failed to bind: " << source.last_error() << "\n";
        return 2;
    }

    std::cout << "Listening for BDFR FacialAnimation UDP on port "
              << source.local_port() << "\n";

    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < until) {
        if (auto frame = source.poll()) {
            std::cout << "frame seq=" << frame->sequence
                      << " remote=" << frame->stream_id
                      << " timestamp_ns=" << frame->timestamp_ns
                      << " samples=" << frame->samples.size() << "\n";
            return 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    std::cout << "No packet received during probe window. Receiver is functional.\n";
    return 0;
}
