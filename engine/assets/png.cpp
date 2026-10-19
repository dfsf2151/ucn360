#include "assets/png.h"

#include <cstring>
#include <fstream>
#include <zlib.h>

namespace ucn::assets {

namespace {

constexpr uint8_t kSignature[8] = {137, 80, 78, 71, 13, 10, 26, 10};

uint32_t read_be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

uint16_t read_be16(const uint8_t* p) { return uint16_t((uint16_t(p[0]) << 8) | p[1]); }

uint8_t paeth(uint8_t a, uint8_t b, uint8_t c) {
    const int p = int(a) + int(b) - int(c);
    const int pa = p > a ? p - a : a - p;
    const int pb = p > b ? p - b : b - p;
    const int pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

bool inflate_all(const std::vector<uint8_t>& in, size_t expected, std::vector<uint8_t>* out,
                 std::string* error) {
    out->resize(expected);
    z_stream zs{};
    if (inflateInit(&zs) != Z_OK) {
        if (error) *error = "inflateInit failed";
        return false;
    }
    zs.next_in = const_cast<Bytef*>(in.data());
    zs.avail_in = static_cast<uInt>(in.size());
    zs.next_out = out->data();
    zs.avail_out = static_cast<uInt>(out->size());
    const int rc = inflate(&zs, Z_FINISH);
    const size_t produced = out->size() - zs.avail_out;
    inflateEnd(&zs);
    if (rc != Z_STREAM_END || produced != expected) {
        if (error) *error = "inflate size mismatch";
        return false;
    }
    return true;
}

} // namespace

bool png_decode(const uint8_t* data, size_t size, Image* out, std::string* error) {
    auto fail = [&](const char* what) {
        if (error) *error = what;
        return false;
    };
    if (size < 8 || std::memcmp(data, kSignature, 8) != 0) return fail("not a PNG");
    if (out == nullptr) return fail("no output");

    uint32_t width = 0, height = 0;
    uint8_t bit_depth = 0, color_type = 0, interlace = 0;
    std::vector<uint8_t> idat, palette, trns;
    bool have_ihdr = false, have_iend = false;

    size_t p = 8;
    while (p + 12 <= size) {
        const uint32_t len = read_be32(data + p);
        const uint8_t* type = data + p + 4;
        const uint8_t* chunk = data + p + 8;
        if (p + 12 + len > size) return fail("chunk overruns file");
        if (std::memcmp(type, "IHDR", 4) == 0) {
            if (len != 13) return fail("bad IHDR");
            width = read_be32(chunk);
            height = read_be32(chunk + 4);
            bit_depth = chunk[8];
            color_type = chunk[9];
            if (chunk[10] != 0 || chunk[11] != 0) return fail("unsupported compression/filter");
            interlace = chunk[12];
            have_ihdr = true;
        } else if (std::memcmp(type, "PLTE", 4) == 0) {
            palette.assign(chunk, chunk + len);
        } else if (std::memcmp(type, "tRNS", 4) == 0) {
            trns.assign(chunk, chunk + len);
        } else if (std::memcmp(type, "IDAT", 4) == 0) {
            idat.insert(idat.end(), chunk, chunk + len);
        } else if (std::memcmp(type, "IEND", 4) == 0) {
            have_iend = true;
            break;
        }
        p += 12 + len;
    }
    if (!have_ihdr || !have_iend) return fail("missing IHDR/IEND");
    if (width == 0 || height == 0 || width > 16384 || height > 16384) return fail("bad dimensions");
    if (interlace != 0) return fail("interlaced PNG unsupported");

    int channels = 0;
    switch (color_type) {
    case 0: channels = 1; break; // grey
    case 2: channels = 3; break; // RGB
    case 3: channels = 1; break; // palette
    case 4: channels = 2; break; // grey + alpha
    case 6: channels = 4; break; // RGBA
    default: return fail("unsupported color type");
    }
    if (bit_depth != 8 && bit_depth != 16) return fail("unsupported bit depth");
    const int sample_bytes = bit_depth / 8;
    const int bpp = channels * sample_bytes;
    const size_t row_bytes = size_t(width) * bpp;
    if (row_bytes == 0 || height > (SIZE_MAX - 1) / (row_bytes + 1)) return fail("image too large");

    std::vector<uint8_t> raw;
    if (!inflate_all(idat, (row_bytes + 1) * height, &raw, error)) return false;

    // Unfilter in place into a contiguous buffer.
    std::vector<uint8_t> pixels(row_bytes * height);
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t filter = raw[y * (row_bytes + 1)];
        const uint8_t* src = raw.data() + y * (row_bytes + 1) + 1;
        uint8_t* dst = pixels.data() + y * row_bytes;
        const uint8_t* up = y > 0 ? pixels.data() + (y - 1) * row_bytes : nullptr;
        for (size_t x = 0; x < row_bytes; ++x) {
            const uint8_t a = x >= size_t(bpp) ? dst[x - bpp] : 0;
            const uint8_t b = up ? up[x] : 0;
            const uint8_t c = (up && x >= size_t(bpp)) ? up[x - bpp] : 0;
            switch (filter) {
            case 0: dst[x] = src[x]; break;
            case 1: dst[x] = uint8_t(src[x] + a); break;
            case 2: dst[x] = uint8_t(src[x] + b); break;
            case 3: dst[x] = uint8_t(src[x] + ((int(a) + int(b)) >> 1)); break;
            case 4: dst[x] = uint8_t(src[x] + paeth(a, b, c)); break;
            default: return fail("bad filter type");
            }
        }
    }

    if (color_type == 3 && palette.size() < 3 * 256) {
        if (palette.size() % 3 != 0) return fail("bad palette");
    }

    out->width = width;
    out->height = height;
    out->rgba.assign(size_t(width) * height * 4, 0);
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* row = pixels.data() + size_t(y) * row_bytes;
        uint8_t* dst = out->rgba.data() + size_t(y) * width * 4;
        for (uint32_t x = 0; x < width; ++x) {
            const uint8_t* px = row + size_t(x) * bpp;
            uint8_t r = 0, g = 0, b = 0, a = 255;
            switch (color_type) {
            case 0: {
                r = g = b = px[0];
                if (trns.size() >= 2) {
                    const uint16_t grey = read_be16(trns.data());
                    const uint16_t val = bit_depth == 16 ? read_be16(px) : px[0];
                    if (val == grey) a = 0;
                }
                break;
            }
            case 2: {
                r = px[0]; g = px[1]; b = px[2];
                if (trns.size() >= 6) {
                    const uint16_t tr = read_be16(trns.data());
                    const uint16_t tg = read_be16(trns.data() + 2);
                    const uint16_t tb = read_be16(trns.data() + 4);
                    const uint16_t vr = bit_depth == 16 ? read_be16(px) : px[0];
                    const uint16_t vg = bit_depth == 16 ? read_be16(px + 2) : px[1];
                    const uint16_t vb = bit_depth == 16 ? read_be16(px + 4) : px[2];
                    if (vr == tr && vg == tg && vb == tb) a = 0;
                }
                break;
            }
            case 3: {
                const uint8_t idx = px[0];
                if (size_t(idx) * 3 + 2 >= palette.size()) return fail("palette index out of range");
                r = palette[idx * 3];
                g = palette[idx * 3 + 1];
                b = palette[idx * 3 + 2];
                if (idx < trns.size()) a = trns[idx];
                break;
            }
            case 4: {
                r = g = b = px[0];
                a = px[sample_bytes];
                break;
            }
            case 6: {
                r = px[0]; g = px[sample_bytes]; b = px[2 * sample_bytes]; a = px[3 * sample_bytes];
                break;
            }
            default: break;
            }
            dst[0] = r; dst[1] = g; dst[2] = b; dst[3] = a;
            dst += 4;
        }
    }
    return true;
}

bool png_decode_file(const std::string& path, Image* out, std::string* error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        if (error) *error = "cannot open " + path;
        return false;
    }
    f.seekg(0, std::ios::end);
    const std::streamoff size = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(size < 0 ? 0 : size));
    if (!data.empty()) f.read(reinterpret_cast<char*>(data.data()), size);
    return png_decode(data.data(), data.size(), out, error);
}

bool png_write_file(const std::string& path, const Image& image, std::string* error) {
    if (image.width == 0 || image.height == 0 ||
        image.rgba.size() != size_t(image.width) * image.height * 4) {
        if (error) *error = "invalid image";
        return false;
    }
    // Raw scanlines with filter byte 0.
    std::vector<uint8_t> raw((size_t(image.width) * 4 + 1) * image.height);
    for (uint32_t y = 0; y < image.height; ++y) {
        uint8_t* dst = raw.data() + size_t(y) * (size_t(image.width) * 4 + 1);
        dst[0] = 0;
        std::memcpy(dst + 1, image.rgba.data() + size_t(y) * image.width * 4, size_t(image.width) * 4);
    }
    uLongf compressed = compressBound(raw.size());
    std::vector<uint8_t> z(compressed);
    if (compress2(z.data(), &compressed, raw.data(), raw.size(), 6) != Z_OK) {
        if (error) *error = "deflate failed";
        return false;
    }
    z.resize(compressed);

    auto put_be32 = [](uint8_t* p, uint32_t v) {
        p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16); p[2] = uint8_t(v >> 8); p[3] = uint8_t(v);
    };
    std::vector<uint8_t> out;
    out.insert(out.end(), kSignature, kSignature + 8);
    auto chunk = [&](const char* type, const uint8_t* body, size_t len) {
        const size_t start = out.size();
        out.resize(start + 12 + len);
        uint8_t* p = out.data() + start;
        put_be32(p, static_cast<uint32_t>(len));
        std::memcpy(p + 4, type, 4);
        if (len) std::memcpy(p + 8, body, len);
        uLong crc = crc32(0, p + 4, static_cast<uInt>(len + 4));
        put_be32(p + 8 + len, static_cast<uint32_t>(crc));
    };
    uint8_t ihdr[13];
    put_be32(ihdr, image.width);
    put_be32(ihdr + 4, image.height);
    ihdr[8] = 8;   // bit depth
    ihdr[9] = 6;   // RGBA
    ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    chunk("IHDR", ihdr, sizeof(ihdr));
    chunk("IDAT", z.data(), z.size());
    chunk("IEND", nullptr, 0);

    std::ofstream f(path, std::ios::binary);
    if (!f) {
        if (error) *error = "cannot write " + path;
        return false;
    }
    f.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
    if (!f) {
        if (error) *error = "short write " + path;
        return false;
    }
    return true;
}

} // namespace ucn::assets
