#include "gvr_parser.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>

namespace {

uint16_t readBigEndian16(const uint8_t* bytes) {
    return static_cast<uint16_t>((static_cast<uint16_t>(bytes[0]) << 8u) | bytes[1]);
}

uint32_t readLittleEndian32(const uint8_t* bytes) {
    return static_cast<uint32_t>(bytes[0])
            | (static_cast<uint32_t>(bytes[1]) << 8u)
            | (static_cast<uint32_t>(bytes[2]) << 16u)
            | (static_cast<uint32_t>(bytes[3]) << 24u);
}

uint8_t expand4(uint8_t value) {
    return static_cast<uint8_t>((value << 4u) | value);
}

uint8_t expand5(uint16_t value) {
    return static_cast<uint8_t>((value << 3u) | (value >> 2u));
}

uint8_t expand6(uint16_t value) {
    return static_cast<uint8_t>((value << 2u) | (value >> 4u));
}

void writePixel(std::vector<uint8_t>& rgba, uint32_t width, uint32_t height,
                size_t x, size_t y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (x >= width || y >= height) return;
    const size_t offset = (y * width + x) * 4u;
    rgba[offset] = r;
    rgba[offset + 1u] = g;
    rgba[offset + 2u] = b;
    rgba[offset + 3u] = a;
}

void decodeRgb565(uint16_t pixel, uint8_t& r, uint8_t& g, uint8_t& b) {
    r = expand5((pixel >> 11u) & 0x1Fu);
    g = expand6((pixel >> 5u) & 0x3Fu);
    b = expand5(pixel & 0x1Fu);
}

void decodeRgb5A3(uint16_t pixel, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) {
    if ((pixel & 0x8000u) != 0) {
        r = expand5((pixel >> 10u) & 0x1Fu);
        g = expand5((pixel >> 5u) & 0x1Fu);
        b = expand5(pixel & 0x1Fu);
        a = 255;
    } else {
        a = static_cast<uint8_t>(((pixel >> 12u) & 7u) * 255u / 7u);
        r = expand4((pixel >> 8u) & 0x0Fu);
        g = expand4((pixel >> 4u) & 0x0Fu);
        b = expand4(pixel & 0x0Fu);
    }
}

bool decodeTexture(const uint8_t* pixels, size_t dataSize, uint32_t width,
                   uint32_t height, uint8_t format, std::vector<uint8_t>& rgba,
                   std::string& error) {
    size_t tileWidth = 0, tileHeight = 0, bytesPerTile = 0;
    switch (format) {
        case 0x00: tileWidth = 8; tileHeight = 8; bytesPerTile = 32; break; // I4
        case 0x01: tileWidth = 8; tileHeight = 4; bytesPerTile = 32; break; // I8
        case 0x02: tileWidth = 8; tileHeight = 4; bytesPerTile = 32; break; // IA4
        case 0x03: tileWidth = 4; tileHeight = 4; bytesPerTile = 32; break; // IA8
        case 0x04: // RGB565
        case 0x05: tileWidth = 4; tileHeight = 4; bytesPerTile = 32; break; // RGB5A3
        case 0x06: tileWidth = 4; tileHeight = 4; bytesPerTile = 64; break; // RGBA8
        case 0x0E: tileWidth = 8; tileHeight = 8; bytesPerTile = 32; break; // CMPR/DXT1
        case 0x08:
        case 0x09:
            error = "GVR indexado precisa da paleta interna ou do arquivo GVP correspondente.";
            return false;
        default:
            error = "Formato de dados GVR desconhecido.";
            return false;
    }

    const size_t tilesX = (static_cast<size_t>(width) + tileWidth - 1u) / tileWidth;
    const size_t tilesY = (static_cast<size_t>(height) + tileHeight - 1u) / tileHeight;
    if (tilesX == 0 || tilesY == 0
            || tilesX > std::numeric_limits<size_t>::max() / tilesY / bytesPerTile
            || static_cast<size_t>(width) > std::numeric_limits<size_t>::max() / height) {
        error = "Dimensões GVR excedem o limite suportado.";
        return false;
    }
    const size_t required = tilesX * tilesY * bytesPerTile;
    const size_t pixelCount = static_cast<size_t>(width) * height;
    if (required > dataSize || pixelCount > std::numeric_limits<size_t>::max() / 4u) {
        error = "Dados de pixels GVR incompletos.";
        return false;
    }
    rgba.assign(pixelCount * 4u, 0);

    for (size_t tileY = 0; tileY < tilesY; ++tileY) {
        for (size_t tileX = 0; tileX < tilesX; ++tileX) {
            const uint8_t* tile = pixels + (tileY * tilesX + tileX) * bytesPerTile;
            for (size_t py = 0; py < tileHeight; ++py) {
                for (size_t px = 0; px < tileWidth; ++px) {
                    const size_t x = tileX * tileWidth + px;
                    const size_t y = tileY * tileHeight + py;
                    uint8_t r = 0, g = 0, b = 0, a = 255;
                    size_t i = 0;
                    switch (format) {
                        case 0x00: {
                            i = py * 4u + px / 2u;
                            const uint8_t intensity = expand4((px & 1u) == 0
                                    ? tile[i] >> 4u : tile[i] & 0x0Fu);
                            r = g = b = intensity;
                            break;
                        }
                        case 0x01:
                            r = g = b = tile[py * 8u + px];
                            break;
                        case 0x02: {
                            const uint8_t value = tile[py * 8u + px];
                            a = expand4(value >> 4u);
                            r = g = b = expand4(value & 0x0Fu);
                            break;
                        }
                        case 0x03: {
                            i = (py * 4u + px) * 2u;
                            a = tile[i];
                            r = g = b = tile[i + 1u];
                            break;
                        }
                        case 0x04:
                        case 0x05: {
                            const uint16_t value = readBigEndian16(
                                    tile + (py * 4u + px) * 2u);
                            if (format == 0x04) decodeRgb565(value, r, g, b);
                            else decodeRgb5A3(value, r, g, b, a);
                            break;
                        }
                        case 0x06: {
                            // GX RGBA8 stores A/R and G/B planes in each 4x4 tile.
                            i = (py * 4u + px) * 2u;
                            a = tile[i];
                            r = tile[i + 1u];
                            g = tile[32u + i];
                            b = tile[32u + i + 1u];
                            break;
                        }
                        case 0x0E:
                            // CMPR is decoded below as four DXT1 4x4 subblocks.
                            continue;
                        default:
                            continue;
                    }
                    writePixel(rgba, width, height, x, y, r, g, b, a);
                }
            }

            if (format == 0x0E) {
                for (size_t sub = 0; sub < 4u; ++sub) {
                    const size_t subX = (sub & 1u) * 4u;
                    const size_t subY = (sub >> 1u) * 4u;
                    const uint8_t* block = tile + sub * 8u;
                    const uint16_t c0 = readBigEndian16(block);
                    const uint16_t c1 = readBigEndian16(block + 2u);
                    uint8_t colors[4][4]{};
                    decodeRgb565(c0, colors[0][0], colors[0][1], colors[0][2]);
                    decodeRgb565(c1, colors[1][0], colors[1][1], colors[1][2]);
                    colors[0][3] = colors[1][3] = 255;
                    if (c0 > c1) {
                        for (size_t c = 0; c < 3; ++c) {
                            colors[2][c] = static_cast<uint8_t>((2u * colors[0][c] + colors[1][c]) / 3u);
                            colors[3][c] = static_cast<uint8_t>((colors[0][c] + 2u * colors[1][c]) / 3u);
                        }
                        colors[2][3] = colors[3][3] = 255;
                    } else {
                        for (size_t c = 0; c < 3; ++c)
                            colors[2][c] = static_cast<uint8_t>((colors[0][c] + colors[1][c]) / 2u);
                        colors[2][3] = 255;
                        colors[3][0] = colors[3][1] = colors[3][2] = colors[3][3] = 0;
                    }
                    const uint32_t indices = readLittleEndian32(block + 4u);
                    for (size_t py = 0; py < 4u; ++py) {
                        for (size_t px = 0; px < 4u; ++px) {
                            const size_t shift = (py * 4u + px) * 2u;
                            const uint8_t* color = colors[(indices >> shift) & 3u];
                            writePixel(rgba, width, height,
                                    tileX * 8u + subX + px,
                                    tileY * 8u + subY + py,
                                    color[0], color[1], color[2], color[3]);
                        }
                    }
                }
            }
        }
    }
    return true;
}

} // namespace

bool parseGvrTexture(const std::vector<uint8_t>& fileData, GvrTexture& texture,
                     std::string& error) {
    texture = {};
    error.clear();
    for (size_t offset = 0; offset + 16u <= fileData.size(); ++offset) {
        if (std::memcmp(fileData.data() + offset, "GVRT", 4u) != 0) continue;
        const uint32_t chunkSize = readLittleEndian32(fileData.data() + offset + 4u);
        if (chunkSize < 8u
                || static_cast<size_t>(chunkSize) > fileData.size() - offset - 8u) {
            error = "Tamanho do bloco GVRT inválido ou truncado.";
            return false;
        }
        const uint8_t* header = fileData.data() + offset + 8u;
        texture.flags = header[2];
        texture.pixelFormat = static_cast<uint8_t>((texture.flags >> 4u) & 0x0Fu);
        texture.dataFormat = header[3];
        texture.width = readBigEndian16(header + 4u);
        texture.height = readBigEndian16(header + 6u);
        if (texture.width == 0 || texture.height == 0) {
            error = "Dimensões inválidas no cabeçalho GVRT.";
            return false;
        }
        const size_t pixelOffset = offset + 16u;
        const size_t pixelSize = static_cast<size_t>(chunkSize) - 8u;
        if (pixelOffset > fileData.size() || pixelSize > fileData.size() - pixelOffset) {
            error = "Bloco de pixels GVRT ultrapassa o fim do arquivo.";
            return false;
        }
        return decodeTexture(fileData.data() + pixelOffset, pixelSize,
                texture.width, texture.height, texture.dataFormat, texture.rgba, error);
    }
    error = "Cabeçalho GVRT não encontrado no arquivo.";
    return false;
}
