#include "bdfrpc/AdapterProtocol.h"
#include "bdfrpc/FusionCore.h"
#include "bdfrpc/FrameSynchronizer.h"
#include <cassert>
#include <iostream>

using namespace bdfrpc;

static DomainSample sample(Domain domain, float confidence, float value) {
    return {domain, confidence, {value}};
}

int main() {
    {
        FrameSynchronizer sync(5'000'000);
        sync.set_required_streams({"cam0", "cam1", "cam2"});
        sync.push({"camera", "cam0", 1, 100'000'000, {}});
        sync.push({"camera", "cam1", 1, 102'000'000, {}});
        sync.push({"camera", "cam2", 1, 104'000'000, {}});

        auto set = sync.try_pop();
        assert(set.has_value());
        assert(set->frames.size() == 3);
        assert(set->max_skew_ns == 4'000'000);
    }

    {
        SourceRouter router;
        router.set_route(Domain::Face, {
            {"bdfr_face", 0.70f, 20'000'000},
            {"easymocap", 0.40f, 20'000'000}
        });
        router.set_route(Domain::Body, {
            {"easymocap", 0.50f, 20'000'000}
        });

        SyncedFrameSet set;
        set.timestamp_ns = 1'000'000'000;
        set.frames = {
            {"bdfr_face", "face0", 10, 999'000'000,
                {sample(Domain::Face, 0.91f, 11.0f)}},
            {"easymocap", "body0", 10, 1'001'000'000,
                {sample(Domain::Face, 0.80f, 22.0f),
                 sample(Domain::Body, 0.95f, 33.0f)}}
        };

        FusionCore fusion(router);
        const auto out = fusion.fuse(set);
        assert(out.samples.size() == 2);
        assert(out.selected_sources.size() == 2);
        assert(out.selected_sources[0] == "bdfr_face");
        assert(out.selected_sources[1] == "easymocap");
        assert(out.samples[0].values[0] == 11.0f);
        assert(out.samples[1].values[0] == 33.0f);
    }

    {
        const AdapterHello hello{1, "BDFR_FacialAnimation", "0.1", "face,head"};
        const auto wire = encode_hello(hello);
        const auto decoded = decode_hello(wire);
        assert(decoded.has_value());
        assert(decoded->adapter_name == "BDFR_FacialAnimation");
        assert(decoded->protocol_version == 1);
    }

    std::cout << "bdfrpc_core_tests: OK\n";
    return 0;
}
