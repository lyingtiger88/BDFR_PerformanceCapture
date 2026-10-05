#include "bdfrpc/OpenCVDeviceEnumerator.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

int main(int argc, char** argv) {
    using namespace bdfrpc;

    const int last = argc > 1 ? std::atoi(argv[1]) : 9;
    const auto devices = OpenCVDeviceEnumerator::probe(0, last);

    std::cout << "BDFR PerformanceCapture - live video devices\n";
    std::cout << "ID\tBACKEND\tMODE\n";
    for (const auto& d : devices) {
        std::cout << d.id << "\t" << d.backend << "\t"
                  << d.width << "x" << d.height << " @ "
                  << d.fps << " fps\n";
    }

    std::cout << "Detected: " << devices.size() << "\n";
    return 0;
}
