#include "bdfrpc/BvhExport.h"
#include "bdfrpc/TakeReader.h"

#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    using namespace bdfrpc;

    if (argc < 3) {
        std::cerr
            << "Usage: bdfrpc_export_bvh <take.bdfrtake|csv> <output.bvh> "
               "[subject-id] [fps]\n";
        return 2;
    }

    const std::string take_path = argv[1];
    const std::string output_path = argv[2];
    const int subject_id = argc > 3 ? std::atoi(argv[3]) : 0;
    const double fps = argc > 4 ? std::atof(argv[4]) : 0.0;

    TakeReader reader;
    if (!reader.load(take_path)) {
        std::cerr
            << "Take load failed: "
            << reader.last_error() << "\n";
        return 3;
    }

    auto skeletons =
        extract_skeleton_sequence(
            reader.frames(),
            subject_id);
    if (skeletons.empty()) {
        std::cerr
            << "No subject " << subject_id
            << " skeleton data found in take.\n";
        return 4;
    }

    BvhExportOptions options;
    options.frame_rate = fps;

    std::string error;
    if (!export_bvh(
            output_path,
            skeletons,
            options,
            &error)) {
        std::cerr
            << "BVH export failed: "
            << error << "\n";
        return 5;
    }

    std::cout
        << "Exported " << skeletons.size()
        << " skeleton frames to "
        << output_path << "\n";
    return 0;
}
