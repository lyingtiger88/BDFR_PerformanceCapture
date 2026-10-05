#include "bdfrpc/EasyMocapTcpSource.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

int main(int argc, char** argv) {
    using namespace bdfrpc;

    EasyMocapTcpConfig config;
    if (argc > 1) {
        config.port = static_cast<std::uint16_t>(std::atoi(argv[1]));
    }

    EasyMocapTcpSource source(config);
    if (!source.start()) {
        std::cerr << "EasyMocap listener failed: "
                  << source.last_error() << "\n";
        return 2;
    }

    std::cout << "EasyMocap-compatible TCP listener on port "
              << source.local_port() << "\n";
    std::cout << "Point EasyMocap BaseSocketClient(host, port) here and use "
                 "send_smpl(data).\n";

    const auto until =
        std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (std::chrono::steady_clock::now() < until) {
        if (auto frame = source.poll()) {
            std::cout << "frame seq=" << frame->sequence
                      << " samples=" << frame->samples.size() << "\n";
            for (const auto& sample : frame->samples) {
                std::cout << "  " << to_string(sample.domain)
                          << " channels=" << sample.channels.size() << "\n";
            }
            return 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    std::cout << "No EasyMocap frame received during probe window.\n";
    return 0;
}
