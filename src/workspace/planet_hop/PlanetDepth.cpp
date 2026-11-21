#include <filesystem>
#include <fstream>
#include <iterator>
#include <new>

#include <png.h>

#include "workspace/planet_hop/PlanetGlRenderer.hpp"

namespace hyprimate::workspace {
    bool PlanetGlRenderer::prepare_depth(const std::string &path) {
        if (m_depth_prepared && path == m_depth_path) {
            return m_depth_error.empty();
        }
        // Decode into temporary CPU state. A transient failure must neither
        // authorize the previous setting's image nor cache the requested path.
        try {
            std::string prepared_path = path;
            std::string depth_error;
            std::vector<unsigned char> pixels;
            int width = 0;
            int height = 0;
            const auto DECODE = [&] {
                if (path.empty()) {
                    return true;
                }
                constexpr std::uintmax_t MAX_FILE = 16ULL * 1024ULL * 1024ULL;
                std::error_code error;
                if (!std::filesystem::is_regular_file(path, error)) {
                    depth_error = "depthSource must be a regular PNG file";
                    return false;
                }
                const auto SIZE = std::filesystem::file_size(path, error);
                if (error || SIZE == 0 || SIZE > MAX_FILE) {
                    depth_error = "depthSource exceeds the 16 MiB compressed input limit";
                    return false;
                }
                std::ifstream input(path, std::ios::binary);
                std::vector<char> bytes(static_cast<std::size_t>(SIZE));
                if (!input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) {
                    depth_error = "cannot read depthSource";
                    return false;
                }
                png_image image{};
                image.version = PNG_IMAGE_VERSION;
                struct ImageGuard {
                    png_image &image;
                    ~ImageGuard() {
                        png_image_free(&image);
                    }
                } guard{image};
                if (png_image_begin_read_from_memory(&image, bytes.data(), bytes.size()) == 0) {
                    depth_error = std::data(image.message);
                    return false;
                }
                if (image.width == 0 || image.height == 0 ||
                    image.width > 4096 || image.height > 4096) {
                    depth_error = "depthSource dimensions must be between 1 and 4096 pixels";
                    return false;
                }
                image.format = PNG_FORMAT_GRAY;
                pixels.resize(static_cast<std::size_t>(image.width) * image.height);
                if (png_image_finish_read(&image, nullptr, pixels.data(), 0, nullptr) == 0) {
                    depth_error = std::data(image.message);
                    pixels.clear();
                    return false;
                }
                width = static_cast<int>(image.width);
                height = static_cast<int>(image.height);
                return true;
            };
            const bool PREPARED = DECODE();
            m_depth_path = std::move(prepared_path);
            m_depth_error = std::move(depth_error);
            m_depth_pixels = std::move(pixels);
            m_depth_width = width;
            m_depth_height = height;
            m_depth_prepared = true;
            m_depth_dirty = true;
            return PREPARED;
        } catch (const std::bad_alloc &) {
            // A different setting is pending. Invalidate without allocating or
            // calling GL: interior draws use procedural depth until preparation
            // is retried, including when the setting switches back to the old path.
            m_depth_pixels.clear();
            m_depth_width = m_depth_height = 0;
            m_depth_path.clear();
            m_depth_error.clear();
            m_depth_prepared = false;
            m_depth_dirty = true;
            return false;
        }
    }
}  // namespace hyprimate::workspace
