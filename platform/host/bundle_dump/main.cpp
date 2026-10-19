#include <cstdio>
#include "data/bundle.h"
#include "data/program.h"

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: bundle_dump BUNDLE.BIN\n"); return 2; }
    ucn::data::Bundle bundle;
    std::string error;
    if (!bundle.open(argv[1], &error)) { std::fprintf(stderr, "open failed: %s\n", error.c_str()); return 1; }
    ucn::data::Program program;
    if (!program.load(bundle, &error)) { std::fprintf(stderr, "program: %s\n", error.c_str()); return 1; }
    std::printf("frames=%u objects=%u images=%u resources=%u strings=%zu\n", bundle.frame_count(),
        bundle.object_count(), bundle.image_count(), bundle.resource_count(), bundle.strings().size());
    std::printf("program groups=%zu items=%zu params=%zu values=%zu tokens=%zu\n", program.groups.size(),
        program.items.size(), program.params.size(), program.values.size(), program.tokens.size());
    for (uint32_t i = 0; i < bundle.frame_count(); i++)
        std::printf("frame[%2u] name=%-16s instances=%u groups=%u\n", i, bundle.frame_name(i).c_str(),
            bundle.frame_instance_count(i), i < program.frames.size() ? program.frames[i].group_count : 0);
    return 0;
}
