#pragma once
// The console's renderer: images.pak's DXT blocks as Xenos textures, collect_world's draw list as textured quads,
// resolved into the front buffer. The CPU only lays the quads out, so a frame costs it about a millisecond.
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "assets/png.h"
#include "render/world.h"

extern "C" {
#include "xsdk/gpu.h"
}

// A pixel's word in a tiled 32-bit surface `width` wide (libxenon console.c, as m0_smoke uses it).
inline uint32_t tiled_index(uint32_t x, uint32_t y, uint32_t width) {
    return ((y >> 5) * 32 * width + ((x >> 5) << 10) + (x & 3) + ((y & 1) << 2) + (((x & 31) >> 2) << 3) +
            (((y & 31) >> 1) << 6)) ^ ((y & 8) << 2);
}

class GpuDraw {
public:
    // Uploads every image of the pack (a piece at a time) and builds the shaders; false: `error` says why.
    bool open(const char* pak, std::string* error);
    // An image's size at the pack's scale, all collect_world reads of it; nullptr: no such image.
    const ucn::assets::Image* image(int32_t handle) const;
    // A pack image's pixels, decoded on the CPU (the guide's are recoloured into an overlay).
    bool decode(int32_t handle, ucn::assets::Image* out) const;
    // A texture of straight-alpha RGBA pixels for the overlays; returns its handle, below 0.
    int32_t add(const ucn::assets::Image& rgba);
    // Draws the list over the background colour (0xRRGGBB) into the next front buffer and shows it.
    void present(const std::vector<ucn::render::Draw>& draws, uint32_t background);
    // The frame present() last drew, as RGBA (debug dumps; Xenia needs --readback_resolve=full).
    void read_back(std::vector<uint8_t>* rgba);

private:
    struct Tex {
        XsdkTexture t;
        ucn::assets::Image size; // no pixels
        float u1 = 1, v1 = 1;    // the image's corner in the texture
        uint32_t offset = 0, bytes = 0; // its blocks in the pack
        bool dxt5 = false;
    };
    struct Vertex { float x, y, u, v, r, g, b, a; };
    struct Batch { const Tex* tex; bool linear, additive; uint32_t first, count; };
    bool place(Tex* tex, std::string* error); // GPU memory for an initialised texture, from the current chunk
    const Tex& atlas(const ucn::render::Font* font);
    void quad(const Tex& tex, bool linear, bool additive, const float xy[4][2], float u0, float v0, float u1, float v1,
              const float rgba[4][4]);

    std::string pak_;
    std::unordered_map<int32_t, Tex> tex_;
    struct Atlas { Tex tex; std::vector<float> uv; }; // per glyph: u0, v0, u1, v1
    std::unordered_map<const ucn::render::Font*, Atlas> atlases_;
    int32_t next_extra_ = -2;
    volatile uint32_t* chunk_ = nullptr;
    uint32_t chunk_used_ = 0, chunk_size_ = 0, allocated_ = 0;
    XsdkSamplerState point_{}, linear_{};
    XsdkBlendState normal_{}, add_{};
    Tex white_;
    static constexpr uint32_t kBuffers = 3, kMaxVertices = 48 * 1024;
    volatile uint32_t* vertices_[kBuffers] = {};
    uint32_t fences_[kBuffers] = {}, frame_ = 0, last_address_ = 0;
    std::vector<Vertex> verts_;
    std::vector<Batch> batches_;
};
