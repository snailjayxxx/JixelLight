#include <cstddef>
#include <cstdint>

// Independent PNG byte-filter validation utility. No Qt, renderer, filesystem or image
// pipeline dependencies. Based on the W3C PNG filter arithmetic/tie rules.
#if defined(_WIN32)
#define PNG_ROWS_API __declspec(dllexport)
#else
#define PNG_ROWS_API __attribute__((visibility("default")))
#endif
namespace {
int magnitude(int value) noexcept { return value < 0 ? -value : value; }
int paeth(int left, int up, int corner) noexcept {
    const int estimate = left + up - corner;
    const int a = magnitude(estimate - left);
    const int b = magnitude(estimate - up);
    const int c = magnitude(estimate - corner);
    return a <= b && a <= c ? left : b <= c ? up : corner;
}
}
extern "C" PNG_ROWS_API int jixel_png_unfilter_row(
    std::uint8_t *row, const std::uint8_t *previous, std::size_t size,
    std::size_t bpp, unsigned int kind) noexcept {
    if (!row || !previous || row == previous || !size || size > 64 * 1024 * 1024 || !bpp || bpp > 8 || bpp > size || kind > 4)
        return 0;
    switch (kind) {
    case 0: break;
    case 1:
        for (std::size_t x = bpp; x < size; ++x)
            row[x] = std::uint8_t(unsigned(row[x]) + row[x - bpp]);
        break;
    case 2:
        for (std::size_t x = 0; x < size; ++x)
            row[x] = std::uint8_t(unsigned(row[x]) + previous[x]);
        break;
    case 3:
        for (std::size_t x = 0; x < bpp; ++x)
            row[x] = std::uint8_t(unsigned(row[x]) + unsigned(previous[x]) / 2);
        for (std::size_t x = bpp; x < size; ++x)
            row[x] = std::uint8_t(unsigned(row[x]) + (unsigned(row[x - bpp]) + previous[x]) / 2);
        break;
    case 4:
        for (std::size_t x = 0; x < bpp; ++x)
            row[x] = std::uint8_t(unsigned(row[x]) + previous[x]);
        for (std::size_t x = bpp; x < size; ++x)
            row[x] = std::uint8_t(unsigned(row[x]) + paeth(row[x - bpp], previous[x], previous[x - bpp]));
        break;
    }
    return 1;
}

extern "C" PNG_ROWS_API unsigned int jixel_png_rows_abi() noexcept { return 1; }
#ifndef JIXELLIGHT_PNG_ROWS_COMMIT
#define JIXELLIGHT_PNG_ROWS_COMMIT "unknown"
#endif
extern "C" PNG_ROWS_API const char *jixel_png_rows_commit() noexcept { return JIXELLIGHT_PNG_ROWS_COMMIT; }
