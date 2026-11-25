cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED SOURCE_DIR OR NOT DEFINED OUTPUT)
    message(FATAL_ERROR "SOURCE_DIR and OUTPUT are required")
endif()

get_filename_component(output_dir "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${output_dir}")
set(temporary "${OUTPUT}.tmp")
file(WRITE "${temporary}" "#include \"window/swarm/SwarmEmbeddedSprites.hpp\"\n\nnamespace {\n")
foreach(sprite IN ITEMS bird cat spider)
    file(READ "${SOURCE_DIR}/${sprite}.png" png_hex HEX)
    string(LENGTH "${png_hex}" hex_length)
    if(hex_length EQUAL 0)
        message(FATAL_ERROR "${SOURCE_DIR}/${sprite}.png is empty")
    endif()
    file(APPEND "${temporary}" "constexpr unsigned char ${sprite}[] =\n")
    set(offset 0)
    while(offset LESS hex_length)
        math(EXPR count "${hex_length} - ${offset}")
        if(count GREATER 16384)
            set(count 16384)
        endif()
        string(SUBSTRING "${png_hex}" ${offset} ${count} chunk)
        string(REGEX REPLACE "([0-9a-f][0-9a-f])" "\\\\x\\1" chunk "${chunk}")
        file(APPEND "${temporary}" "\"${chunk}\"\n")
        math(EXPR offset "${offset} + ${count}")
    endwhile()
    file(APPEND "${temporary}" ";\n")
endforeach()
file(APPEND "${temporary}" [=[
} // namespace

namespace hyprimate {
    std::span<const unsigned char> embedded_swarm_sprite(std::string_view name, bool enabled) noexcept {
        if (!enabled || name.contains('/')) return {};
        if (name.ends_with(".png")) name.remove_suffix(4);
        if (name == "bird") return {bird, sizeof(bird) - 1};
        if (name == "cat") return {cat, sizeof(cat) - 1};
        if (name == "spider") return {spider, sizeof(spider) - 1};
        return {};
    }
} // namespace hyprimate
]=])
file(RENAME "${temporary}" "${OUTPUT}")
