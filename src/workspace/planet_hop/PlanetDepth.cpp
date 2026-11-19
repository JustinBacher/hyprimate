#include <filesystem>
#include <fstream>
#include <iterator>

#include <png.h>

#include "workspace/planet_hop/PlanetGlRenderer.hpp"

namespace hyprimate::workspace {
    bool PlanetGlRenderer::prepare_depth(const std::string &path) {
        if (m_depth_prepared && path == m_depth_path) {
            return m_depth_error.empty();
        }
        m_depth_prepared = true;
        m_depth_path = path;
        m_depth_pixels.clear();
        m_depth_width = m_depth_height = 0;
        m_depth_dirty = true;
        m_depth_error.clear();
        if (path.empty()) {
            return true;
        }
        constexpr std::uintmax_t MAX_FILE = 16ULL * 1024ULL * 1024ULL;
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error)) {
            m_depth_error = "depthSource must be a regular PNG file";
            return false;
        }
        const auto SIZE = std::filesystem::file_size(path, error);
        if (error || SIZE == 0 || SIZE > MAX_FILE) {
            m_depth_error = "depthSource exceeds the 16 MiB compressed input limit";
            return false;
        }
        std::ifstream input(path, std::ios::binary);
        std::vector<char> bytes(static_cast<std::size_t>(SIZE));
        if (!input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) {
            m_depth_error = "cannot read depthSource";
            return false;
        }
        png_image image{};
        image.version = PNG_IMAGE_VERSION;
        if (png_image_begin_read_from_memory(&image, bytes.data(), bytes.size()) == 0) {
            m_depth_error = std::data(image.message);
            png_image_free(&image);
            return false;
        }
        if (image.width == 0 || image.height == 0 || image.width > 4096 || image.height > 4096) {
            m_depth_error = "depthSource dimensions must be between 1 and 4096 pixels";
            png_image_free(&image);
            return false;
        }
        image.format = PNG_FORMAT_GRAY;
        m_depth_width = static_cast<int>(image.width);
        m_depth_height = static_cast<int>(image.height);
        m_depth_pixels.resize(static_cast<std::size_t>(image.width) * image.height);
        if (png_image_finish_read(&image, nullptr, m_depth_pixels.data(), 0, nullptr) == 0) {
            m_depth_error = std::data(image.message);
            m_depth_pixels.clear();
            m_depth_width = m_depth_height = 0;
            png_image_free(&image);
            return false;
        }
        png_image_free(&image);
        return true;
    }
}  // namespace hyprimate::workspace
