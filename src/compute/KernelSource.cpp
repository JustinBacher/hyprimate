#include "KernelSource.hpp"

#include <algorithm>
#include <unordered_set>

namespace hyprimate::compute {
    namespace {
        constexpr std::array names{"float", "vec2", "vec3", "vec4", "int", "ivec2", "ivec3", "ivec4", "uint", "uvec2", "uvec3", "uvec4"};
        constexpr bool letter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
        constexpr bool digit(char c) { return c >= '0' && c <= '9'; }
        bool identifier(std::string_view s) {
            return !s.empty() && letter(s.front()) && std::ranges::all_of(s, [](char c) { return letter(c) || digit(c); });
        }
        bool blocked(std::string_view s) {
            constexpr std::array words{"main", "layout", "uniform", "buffer", "shared", "attribute", "varying", "precision",
                "coherent", "volatile", "restrict", "readonly", "writeonly", "discard", "barrier", "groupMemoryBarrier",
                "dFdx", "dFdy", "fwidth", "interpolateAtCentroid", "interpolateAtSample", "interpolateAtOffset",
                "texelFetch", "texelFetchOffset", "isampler2D", "usampler2D"};
            // Prefixes cover all sampler/image/atomic families and texture operations, including extension variants.
            constexpr std::array prefixes{"gl_", "GL_", "hypr_", "sampler", "isampler", "usampler", "image", "iimage", "uimage",
                "atomic", "memoryBarrier", "texture", "dFdx", "dFdy", "fwidth"};
            return std::ranges::find(words, s) != words.end() || std::ranges::any_of(prefixes, [s](auto p) { return s.starts_with(p); });
        }
        bool frame_parameter(std::string_view s) {
            constexpr std::array names{"hypr_delta", "hypr_elapsed", "hypr_progress",
                "hypr_iteration", "hypr_iterations", "hypr_seed"};
            return std::ranges::find(names, s) != names.end();
        }
        bool binding(std::string_view s) {
            return identifier(s) && !s.starts_with("hc_") && !s.contains("__") &&
                   (!blocked(s) || frame_parameter(s)) && s != "kernel";
        }
        struct Token { std::string_view text; unsigned line; };
        constexpr bool newline(char c) { return c == '\r' || c == '\n'; }
        std::size_t next_line(std::string_view source, std::size_t position) {
            const auto next = position + 1;
            return next < source.size() && newline(source[next]) && source[next] != source[position] ? next + 1 : next;
        }
        Result<std::vector<Token>> tokenize(std::string_view source) {
            auto fail = [](unsigned line, std::string_view token) -> Result<std::vector<Token>> {
                return std::unexpected("line " + std::to_string(line) + ": unsupported token " + std::string(token));
            };
            std::vector<Token> tokens;
            unsigned line = 1;
            for (std::size_t i = 0; i < source.size();) {
                const auto c = source[i];
                if (c == '\0' || c == '#' || c == '\\') return fail(line, c == '\0' ? "NUL" : source.substr(i, 1));
                if (newline(c)) { ++line; i = next_line(source, i); continue; }
                if (c == ' ' || c == '\t' || c == '\f' || c == '\v') { ++i; continue; }
                if (source.substr(i).starts_with("//")) {
                    const auto end = source.find_first_of("\r\n", i + 2);
                    const auto comment = source.substr(i, end == source.npos ? source.npos : end - i);
                    if (comment.ends_with('\\')) return fail(line, "line continuation");
                    i = end == source.npos ? source.size() : end;
                    continue;
                }
                if (source.substr(i).starts_with("/*")) {
                    const auto end = source.find("*/", i + 2);
                    if (end == source.npos) return fail(line, "unterminated comment");
                    for (; i < end + 2;) {
                        if (source.substr(i).starts_with("\\\n") || source.substr(i).starts_with("\\\r")) return fail(line, "line continuation");
                        if (newline(source[i])) { ++line; i = next_line(source, i); }
                        else ++i;
                    }
                    continue;
                }
                const auto start = i++;
                if (letter(c)) while (i < source.size() && (letter(source[i]) || digit(source[i]))) ++i;
                tokens.push_back({source.substr(start, i - start), line});
            }
            return tokens;
        }
        std::string raw_prefix(Type type) { return type.scalar == Element::Int ? "i" : "u"; }
        std::string raw_type(Type type) { return raw_prefix(type) + "vec4"; }
        std::string raw_format(Type type) {
            return std::string(type.lanes == 1 ? "r32" : "rgba32") + (type.scalar == Element::Int ? "i" : "ui");
        }
        constexpr std::array selectors{"", ".x", ".xy", ".xyz", ""};
        std::string zero(Type type) { return std::string(type_name(type)) + "(0)"; }
        std::string encoded(Type type, const std::string& name) {
            std::string value = type.scalar == Element::Float ? "floatBitsToUint(" + name + ")" : name;
            if (type.lanes == 4) return value;
            return raw_type(type) + "(" + value + (type.lanes <= 1 ? ",0,0,0)" : type.lanes == 2 ? ",0,0)" : ",0)");
        }
    }

    std::string_view type_name(Type type) noexcept {
        const auto element = static_cast<unsigned>(type.scalar);
        if (element >= 3 || type.lanes < 1 || type.lanes > 4) return {};
        return names[element * 4 + type.lanes - 1];
    }
    Result<Type> parse_type(std::string_view name) {
        const auto it = std::ranges::find(names, name);
        if (it == names.end()) return std::unexpected("unsupported compute type: " + std::string(name));
        const auto n = static_cast<unsigned>(it - names.begin());
        return Type{static_cast<Element>(n / 4), n % 4 + 1};
    }
    Result<Backend> parse_backend(std::string_view name) {
        if (name == "auto") return Backend::Auto;
        if (name == "native") return Backend::Native;
        if (name == "slibs") return Backend::Slibs;
        return std::unexpected("HYPRIMATE_COMPUTE_BACKEND must be auto, native or slibs; got '" + std::string(name) + "'");
    }
    Result<Layout> field_layout(FieldDesc desc, unsigned width, unsigned height, std::size_t budget) {
        const auto [x, y, z] = desc.domain;
        if (!x || !y || !z || type_name(desc.type).empty()) return std::unexpected("invalid field type or zero domain axis");
        width = std::min(width, MAX_EXTENT);
        height = std::min(height, MAX_EXTENT);
        if (!width || !height) return std::unexpected("no usable physical field extent");
        const std::uint64_t yz = std::uint64_t{y} * z;
        if (yz > MAX_PIXELS / x) return std::unexpected("field domain exceeds the element limit");
        const auto elements = yz * x;
        const auto w = x <= width && yz <= height ? x : std::min<std::uint64_t>(elements, width);
        const auto h = (elements + w - 1) / w;
        const auto texels = w * h;
        const auto bytes = texels * (desc.type.lanes == 1 ? 4 : 16);
        if (h > height || texels > MAX_PIXELS || bytes > budget) return std::unexpected("padded field exceeds physical extent or storage budget");
        return Layout{static_cast<unsigned>(w), static_cast<unsigned>(h), static_cast<unsigned>(elements),
                      static_cast<std::size_t>(elements * desc.type.lanes * 4), static_cast<std::size_t>(bytes)};
    }
    Result<void> validate_kernel(const KernelDesc& desc) {
        auto fail = [&](std::string reason) -> Result<void> { return std::unexpected("kernel '" + desc.name + "': " + std::move(reason)); };
        if (desc.name.empty() || desc.outputs.empty()) return fail("name and at least one output are required");
        std::unordered_set<std::string_view> bound;
        std::unordered_set<std::string> helpers{"hc_domain", "hc_index", "hypr_delta", "hypr_elapsed",
            "hypr_progress", "hypr_iteration", "hypr_iterations", "hypr_seed"};
        for (const auto* slots : {&desc.inputs, &desc.outputs, &desc.parameters}) {
            for (const auto& slot : *slots) {
                if (!binding(slot.name) || type_name(slot.type).empty()) return fail("invalid binding '" + slot.name + "'");
                if (!bound.insert(slot.name).second) return fail("duplicate binding '" + slot.name + "'");
                if (slots == &desc.inputs) {
                    helpers.insert("hc_read_" + slot.name);
                    helpers.insert(detail::size_name(slot.name));
                }
            }
        }
        auto tokens = tokenize(desc.source);
        if (!tokens) return fail(std::move(tokens.error()));
        auto at = [&](std::size_t i) -> std::string_view { return i < tokens->size() ? (*tokens)[i].text : ""; };
        unsigned depth = 0, parentheses = 0, definitions = 0;
        for (std::size_t i = 0; i < tokens->size(); ++i) {
            const auto [s, line] = (*tokens)[i];
            auto bad = [&](std::string_view reason) { return fail("line " + std::to_string(line) + ": " + std::string(reason) + " '" + std::string(s) + "'"); };
            if ((blocked(s) && !helpers.contains(std::string(s))) ||
                (s.starts_with("hc_") && !helpers.contains(std::string(s))) ||
                (identifier(s) && s.contains("__")))
                return bad("unsupported token");
            if (!depth && !parentheses && (s == "in" || s == "out" || s == "inout")) return bad("stage qualifier outside function signature");
            if (s == "kernel") {
                if (depth || parentheses || definitions++ || !i || at(i - 1) != "void" || at(i + 1) != "(" || at(i + 2) != "ivec3" || !binding(at(i + 3)))
                    return bad("expected one void kernel(ivec3 id, inout …) definition at");
                auto k = i + 4;
                for (const auto& output : desc.outputs) {
                    if (at(k) != "," || at(k + 1) != "inout" || at(k + 2) != type_name(output.type) || !binding(at(k + 3)))
                        return bad("output signature does not match descriptor at");
                    k += 4;
                }
                if (at(k) != ")" || at(k + 1) != "{") return bad("invalid entry signature at");
                i = k;
            } else if (s == "(") {
                ++parentheses;
            } else if (s == ")" && parentheses) {
                --parentheses;
            }
            if (s == "{") ++depth;
            if (s == "}" && depth) --depth;
        }
        if (definitions != 1) return fail("missing void kernel entry");
        return {};
    }

    Result<std::string> sample_source(std::string_view alias, Type type) {
        if (!binding(alias) || type_name(type).empty()) return std::unexpected("invalid sampled field alias or type: " + std::string(alias));
        const auto sampler = detail::sampler_name(alias), size = detail::size_name(alias);
        const auto read = "hc_read_" + std::string(alias);
        const std::string t(type_name(type));
        auto value = "texelFetch(" + sampler + ",ivec2(i%w,i/w),0)" + selectors[type.lanes];
        if (type.scalar == Element::Float) value = "uintBitsToFloat(" + value + ")";
        return "\nuniform highp " + raw_prefix(type) + "sampler2D " + sampler + ";\nuniform highp ivec3 " + size + ";\nhighp " + t + " " + read +
            "(highp int i){ if(i<0 || i>=" + size + ".x*" + size + ".y*" + size + ".z) return " + zero(type) +
            "; highp int w=textureSize(" + sampler + ",0).x; return " + value + "; }\nhighp " + t + " " + read +
            "(highp ivec3 p){ if(any(lessThan(p,ivec3(0))) || any(greaterThanEqual(p," + size + "))) return " + zero(type) +
            "; return " + read + "(p.x+" + size + ".x*(p.y+" + size + ".y*p.z)); }\n";
    }

    namespace detail {
        std::string sampler_name(std::string_view alias) { return "hc_tex_" + std::string(alias); }
        std::string size_name(std::string_view alias) { return "hc_size_" + std::string(alias); }
        std::string kernel_source(const KernelDesc& desc, Backend backend, bool validation_only) {
            const bool native = backend == Backend::Native;
            std::string result = native ? "#version 310 es\n" : "#version 300 es\n";
            result += "precision highp float;\nprecision highp int;\nuniform highp ivec3 hc_domain;\nuniform highp int hc_width;\n"
                      "highp int hc_index(highp ivec3 p){return p.x+hc_domain.x*(p.y+hc_domain.y*p.z);}\n";
            for (const auto& slot : desc.inputs) result += *sample_source(slot.name, slot.type);
            for (const auto& slot : desc.parameters) result += "uniform highp " + std::string(type_name(slot.type)) + " " + slot.name + ";\n";
            result += "#line 1 1\n" + desc.source + "\n#line 1 2\n";
            if (validation_only) return result + "void main() {}\n";
            if (native) result += "layout(local_size_x=8,local_size_y=8,local_size_z=1) in;\n";
            for (std::size_t i = 0; i < desc.outputs.size(); ++i) {
                const auto type = desc.outputs[i].type;
                const auto n = std::to_string(i);
                result += native ? "layout(binding=" + n + "," + raw_format(type) + ") writeonly uniform highp " + raw_prefix(type) + "image2D hc_out" + n + ";\n"
                                 : "layout(location=" + n + ") out highp " + raw_type(type) + " hc_out" + n + ";\n";
            }
            result += native ? "void main(){highp ivec2 hc_p=ivec2(gl_GlobalInvocationID.xy);\n" : "void main(){highp ivec2 hc_p=ivec2(gl_FragCoord.xy);\n";
            result += "highp int hc_i=hc_p.x+hc_width*hc_p.y;\n"
                      "if(hc_p.x>=hc_width || hc_i>=hc_domain.x*hc_domain.y*hc_domain.z)";
            result += native ? " return;\n" : " discard;\n";
            result +=
                      "highp int hc_yz=hc_i/hc_domain.x;\n"
                      "highp ivec3 hc_id=ivec3(hc_i%hc_domain.x,hc_yz%hc_domain.y,hc_yz/hc_domain.y);\n";
            std::string call = "kernel(hc_id";
            for (std::size_t i = 0; i < desc.outputs.size(); ++i) {
                const auto type = desc.outputs[i].type;
                const auto var = "hc_value" + std::to_string(i);
                result += "highp " + std::string(type_name(type)) + " " + var + "=" + zero(type) + ";\n";
                call += "," + var;
            }
            result += call + ");\n";
            for (std::size_t i = 0; i < desc.outputs.size(); ++i) {
                const auto n = std::to_string(i);
                const auto value = encoded(desc.outputs[i].type, "hc_value" + n);
                result += native ? "imageStore(hc_out" + n + ",hc_p," + value + ");\n" : "hc_out" + n + "=" + value + ";\n";
            }
            return result + "}\n";
        }
    }
}
