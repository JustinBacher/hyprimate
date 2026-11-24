#include "Package.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <regex>
#include <set>
#include <stdexcept>
#include <unordered_set>

#include <nlohmann/json.hpp>
#include <png.h>

namespace hyprimate::shader {
namespace {
using Json = nlohmann::json;
constexpr std::size_t MiB = 1024 * 1024;
constexpr std::size_t MaxSource = 4 * MiB;
constexpr std::uint64_t MaxInvocations = 4'194'304;
[[noreturn]] void fail(const std::string &message) { throw std::runtime_error(message); }
void require(bool condition, const std::string &message) { if (!condition) fail(message); }
void fields(const Json &value, std::initializer_list<std::string_view> allowed) {
    require(value.is_object(), "expected an object");
    for (const auto &[key, unused] : value.items()) {
        (void)unused;
        require(std::find(allowed.begin(), allowed.end(), key) != allowed.end(), "unsupported field: " + key);
    }
}
void array(const Json &value, std::size_t maximum) {
    require(value.is_array() && value.size() <= maximum, "invalid array or array limit exceeded");
}
std::string text(const Json &value) {
    require(value.is_string(), "expected a string");
    auto result = value.get<std::string>();
    require(!result.empty() && result.find('\0') == std::string::npos, "empty or invalid string");
    return result;
}
std::uint64_t natural(const Json &value, std::uint64_t minimum, std::uint64_t maximum) {
    require(value.is_number_integer(), "expected an integer");
    require(!value.is_number_unsigned() ? value.get<std::int64_t>() >= 0 : true, "negative integer");
    const auto result = value.get<std::uint64_t>();
    require(result >= minimum && result <= maximum, "integer outside allowed range");
    return result;
}
float number(const Json &value) {
    require(value.is_number(), "expected a number");
    const auto result = value.get<double>();
    require(std::isfinite(result) && std::abs(result) <= std::numeric_limits<float>::max(), "nonfinite or overflowing number");
    return static_cast<float>(result);
}
double parameter_bound(const Json &value, ValueType type) {
    if (type != ValueType::Int) return number(value);
    require(value.is_number(), "expected a numeric parameter bound");
    const auto result = value.get<double>();
    require(std::isfinite(result), "nonfinite parameter bound");
    return result;
}
bool boolean(const Json &value) {
    require(value.is_boolean(), "expected a boolean");
    return value.get<bool>();
}
template <class T> T choice(const Json &value, std::initializer_list<std::pair<std::string_view, T>> choices) {
    const auto name = text(value);
    for (const auto &[key, result] : choices) if (key == name) return result;
    fail("unsupported value: " + name);
}
Json parse(std::string_view bytes) {
    require(bytes.size() <= 65536, "JSON exceeds 64 KiB");
    std::array<std::unordered_set<std::string>, 33> keys;
    return Json::parse(bytes, [&](int depth, Json::parse_event_t event, Json &value) {
        require(depth <= 32, "JSON nesting exceeds 32");
        if (event == Json::parse_event_t::object_start) {
            require(depth < 32, "JSON nesting exceeds 32");
            keys[depth + 1].clear();
        } else if (event == Json::parse_event_t::key) {
            require(keys[depth].insert(value.get<std::string>()).second, "duplicate JSON key: " + value.get<std::string>());
        } else if (event == Json::parse_event_t::array_start) {
            require(depth < 32, "JSON nesting exceeds 32");
        }
        return true;
    });
}
std::filesystem::path expand(const std::string &path) {
    if (path == "~" || path.starts_with("~/")) {
        const char *home = std::getenv("HOME");
        require(home && *home, "HOME is unavailable");
        return std::filesystem::path(home) / (path.size() > 2 ? path.substr(2) : "");
    }
    require(!path.starts_with('~'), "only ~/ home expansion is supported");
    return path;
}
std::string read_file(const std::filesystem::path &path, std::size_t maximum) {
    require(std::filesystem::is_regular_file(path), "not a regular file: " + path.string());
    std::ifstream stream(path, std::ios::binary);
    require(stream.is_open(), "cannot open: " + path.string());
    std::string result;
    std::array<char, 8192> chunk;
    while (stream) {
        stream.read(chunk.data(), chunk.size());
        auto count = static_cast<std::size_t>(stream.gcount());
        require(count <= maximum - result.size(), "file exceeds size limit: " + path.string());
        result.append(chunk.data(), count);
    }
    require(stream.eof(), "cannot read: " + path.string());
    return result;
}
bool identifier(std::string_view name) {
    if (name.empty() || name.starts_with("hypr_") || name.starts_with("gl_") || name.find("__") != name.npos) return false;
    auto alpha = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; };
    if (!alpha(name.front())) return false;
    for (char c : name) if (!alpha(c) && !(c >= '0' && c <= '9')) return false;
    // Includes ES 3.00 keywords and reserved future keywords, not merely C++ identifiers.
    static const std::unordered_set<std::string_view> reserved{
        "attribute","const","uniform","varying","layout","centroid","flat","smooth","break","continue","do","for","while","switch","case","default","if","else","subroutine","in","out","inout","float","double","int","void","bool","true","false","invariant","discard","return","mat2","mat3","mat4","mat2x2","mat2x3","mat2x4","mat3x2","mat3x3","mat3x4","mat4x2","mat4x3","mat4x4","vec2","vec3","vec4","ivec2","ivec3","ivec4","bvec2","bvec3","bvec4","uint","uvec2","uvec3","uvec4","lowp","mediump","highp","precision","sampler2D","sampler3D","samplerCube","sampler2DShadow","samplerCubeShadow","sampler2DArray","sampler2DArrayShadow","isampler2D","isampler3D","isamplerCube","isampler2DArray","usampler2D","usampler3D","usamplerCube","usampler2DArray","struct","common","partition","active","asm","class","union","enum","typedef","template","this","resource","goto","inline","noinline","public","static","extern","external","interface","long","short","half","fixed","unsigned","superp","input","output","hvec2","hvec3","hvec4","dvec2","dvec3","dvec4","fvec2","fvec3","fvec4","sampler2DRect","sampler3DRect","sampler2DRectShadow","sizeof","cast","namespace","using","row_major","main","texture","textureLod","textureSize","texelFetch","sin","cos","tan","asin","acos","atan","pow","exp","log","sqrt","abs","sign","floor","ceil","fract","mod","min","max","clamp","mix","step","smoothstep","length","distance","dot","cross","normalize","reflect","refract","matrixCompMult","lessThan","greaterThan","equal","notEqual","any","all","not","dFdx","dFdy","fwidth"};
    static const std::unordered_set<std::string_view> builtins{
        "radians", "degrees", "sinh", "cosh", "tanh", "asinh", "acosh", "atanh",
        "exp2", "log2", "inversesqrt", "trunc", "round", "roundEven", "modf",
        "isnan", "isinf", "floatBitsToInt", "floatBitsToUint", "intBitsToFloat",
        "uintBitsToFloat", "packSnorm2x16", "unpackSnorm2x16", "packUnorm2x16",
        "unpackUnorm2x16", "packHalf2x16", "unpackHalf2x16", "faceforward",
        "outerProduct", "transpose", "determinant", "inverse", "lessThanEqual",
        "greaterThanEqual", "textureQueryLevels", "textureProj", "textureOffset",
        "texelFetchOffset", "textureProjOffset", "textureLodOffset",
        "textureProjLod", "textureProjLodOffset", "textureGrad",
        "textureGradOffset", "textureProjGrad", "textureProjGradOffset"};
    return !reserved.contains(name) && !builtins.contains(name);
}
void named(std::string_view name) { require(identifier(name), "invalid or reserved identifier: " + std::string(name)); }
void label(std::string_view name) {
    require(!name.empty() && name.find('\0') == name.npos, "empty or invalid name");
}
void resource_name(std::string_view name) {
    label(name);
    require(name != "source" && name != "output" && !name.ends_with(".read") && !name.ends_with(".write"),
        "reserved resource name or side suffix");
}
unsigned components(ValueType type) {
    switch (type) {
        case ValueType::Float: case ValueType::Int: case ValueType::Bool: return 1;
        case ValueType::Vec2: return 2;
        case ValueType::Vec3: return 3;
        case ValueType::Vec4: return 4;
    }
    fail("invalid parameter type");
}
Value value(const Json &json, ValueType type) {
    Value result; result.type = type;
    if (type == ValueType::Int) {
        require(json.is_number_integer(), "integer parameter requires an integer");
        if (json.is_number_unsigned()) require(json.get<std::uint64_t>() <= static_cast<std::uint64_t>(std::numeric_limits<int>::max()), "integer overflow");
        else require(json.get<std::int64_t>() >= std::numeric_limits<int>::min() && json.get<std::int64_t>() <= std::numeric_limits<int>::max(), "integer overflow");
        result.integer = json.get<int>();
    } else if (type == ValueType::Bool) result.integer = boolean(json) ? 1 : 0;
    else if (type == ValueType::Float) result.numbers[0] = number(json);
    else {
        array(json, components(type));
        require(json.size() == components(type), "parameter vector length mismatch");
        for (unsigned i = 0; i < json.size(); ++i) result.numbers[i] = number(json[i]);
    }
    return result;
}
void range(const Parameter &parameter, const Value &v) {
    require(v.type == parameter.initial.type && std::isfinite(parameter.minimum) && std::isfinite(parameter.maximum) && parameter.minimum <= parameter.maximum, "invalid parameter range/type: " + parameter.name);
    if (v.type == ValueType::Bool) { require(v.integer == 0 || v.integer == 1, "invalid boolean"); return; }
    if (v.type == ValueType::Int) {
        require(static_cast<double>(v.integer) >= parameter.minimum && static_cast<double>(v.integer) <= parameter.maximum, "parameter outside range: " + parameter.name);
    } else for (unsigned i = 0; i < components(v.type); ++i)
        require(std::isfinite(v.numbers[i]) && v.numbers[i] >= parameter.minimum && v.numbers[i] <= parameter.maximum, "parameter outside range: " + parameter.name);
}
Ref reference(const Package &package, const Json &json) {
    auto name = text(json);
    if (name == "source") return {ResourceKind::Source, 0, Side::Single};
    if (name == "output") return {ResourceKind::Output, 0, Side::Single};
    Side side = Side::Single;
    if (name.ends_with(".read")) { side = Side::Read; name.resize(name.size() - 5); }
    else if (name.ends_with(".write")) { side = Side::Write; name.resize(name.size() - 6); }
    for (std::size_t i = 0; i < package.textures.size(); ++i) if (package.textures[i].name == name) return {ResourceKind::Texture, static_cast<std::uint16_t>(i), side};
    for (std::size_t i = 0; i < package.buffers.size(); ++i) if (package.buffers[i].name == name) return {ResourceKind::Buffer, static_cast<std::uint16_t>(i), side};
    fail("unknown resource: " + name);
}
Lifetime lifetime(const Json &json) {
    return choice<Lifetime>(json, {{"persistent",Lifetime::Persistent},{"transient",Lifetime::Transient}});
}
void image(Texture &texture, const std::string &encoded, std::size_t remaining) {
    png_image png{}; png.version = PNG_IMAGE_VERSION;
    require(png_image_begin_read_from_memory(&png, encoded.data(), encoded.size()) != 0, "invalid PNG asset");
    struct Cleanup { png_image *image; ~Cleanup() { png_image_free(image); } } cleanup{&png};
    require(png.width && png.height && png.width <= 4096 && png.height <= 4096, "PNG dimensions exceed 4096");
    require(static_cast<std::size_t>(png.width) * png.height * 4 <= remaining,
        "decoded PNG assets exceed 64 MiB");
    png.format = PNG_FORMAT_RGBA;
    texture.image_width = png.width; texture.image_height = png.height;
    texture.image.resize(PNG_IMAGE_SIZE(png));
    require(png_image_finish_read(&png, nullptr, texture.image.data(), 0, nullptr) != 0, "cannot decode PNG asset");
    // Auxiliary assets may contain numeric data; preserve straight RGBA channels.
}
unsigned pixel_bytes(Format format);
void resources(Package &package, const Json &root, const std::filesystem::path &base) {
    std::size_t storage = 0;
    if (root.contains("textures")) {
        const auto &textures = root.at("textures"); require(textures.is_object() && textures.size() <= MAX_TEXTURES, "invalid textures");
        std::size_t decoded = 0;
        for (const auto &[name, json] : textures.items()) {
            fields(json, {"path","format","scale","width","height","ping_pong","lifetime","filter","wrap"});
            Texture t; t.name = name;
            if (json.contains("format")) t.format = choice<Format>(json.at("format"), {{"r8",Format::R8},{"rg8",Format::RG8},{"rgba8",Format::RGBA8},{"r16f",Format::R16F},{"rg16f",Format::RG16F},{"rgba16f",Format::RGBA16F},{"r32f",Format::R32F},{"rg32f",Format::RG32F},{"rgba32f",Format::RGBA32F}});
            if (json.contains("scale")) t.size.scale = number(json.at("scale"));
            require(!json.contains("scale") || (!json.contains("width") && !json.contains("height")), "scale and explicit dimensions are exclusive");
            require(json.contains("width") == json.contains("height"), "width and height required together");
            if (json.contains("width")) { t.size.width = natural(json.at("width"),1,4096); t.size.height = natural(json.at("height"),1,4096); }
            if (json.contains("ping_pong")) t.ping_pong = boolean(json.at("ping_pong"));
            if (json.contains("lifetime")) t.lifetime = lifetime(json.at("lifetime"));
            if (json.contains("filter")) t.linear = choice<bool>(json.at("filter"), {{"nearest",false},{"linear",true}});
            if (json.contains("wrap")) t.repeat = choice<bool>(json.at("wrap"), {{"clamp",false},{"repeat",true}});
            if (json.contains("path")) {
                require(!json.contains("scale") && !json.contains("width") && !json.contains("height") && !json.contains("ping_pong") && !json.contains("lifetime") && !json.contains("format"), "PNG asset cannot declare mutable storage properties");
                image(t, read_file(base / expand(text(json.at("path"))), 16 * MiB), 64 * MiB - decoded);
                decoded += t.image.size();
            }
            const auto size = t.image.empty()
                ? static_cast<std::size_t>(t.size.width) * t.size.height * pixel_bytes(t.format)
                : t.image.size();
            storage += size * (t.ping_pong ? 2 : 1);
            require(storage <= MAX_INSTANCE_BYTES, "instance storage exceeds 128 MiB");
            package.textures.push_back(std::move(t));
        }
    }
    if (root.contains("buffers")) {
        const auto &buffers = root.at("buffers"); require(buffers.is_object() && buffers.size() <= MAX_BUFFERS, "invalid buffers");
        for (const auto &[name, json] : buffers.items()) {
            fields(json, {"bytes","ping_pong","lifetime","data","path"});
            Buffer b; b.name = name; b.bytes = natural(json.at("bytes"),1,32*MiB);
            if (json.contains("ping_pong")) b.ping_pong = boolean(json.at("ping_pong"));
            if (json.contains("lifetime")) b.lifetime = lifetime(json.at("lifetime"));
            storage += b.bytes * (b.ping_pong ? 2 : 1);
            require(storage <= MAX_INSTANCE_BYTES, "instance storage exceeds 128 MiB");
            require(!(json.contains("data") && json.contains("path")), "buffer data and path are exclusive");
            if (json.contains("data")) {
                const auto &data = json.at("data"); array(data,b.bytes/sizeof(float)); b.initial.resize(data.size()*sizeof(float));
                for (std::size_t i=0; i<data.size(); ++i) { float f=number(data[i]); std::memcpy(b.initial.data()+i*sizeof(float), &f, sizeof(f)); }
            }
            if (json.contains("path")) { auto raw=read_file(base / expand(text(json.at("path"))),std::min(b.bytes,32*MiB)); b.initial.assign(raw.begin(),raw.end()); }
            package.buffers.push_back(std::move(b));
        }
    }
}
void meshes(Package &package, const Json &root) {
    if (!root.contains("meshes")) return;
    const auto &meshes = root.at("meshes"); require(meshes.is_object() && meshes.size() <= MAX_MESHES, "invalid meshes");
    for (const auto &[name,json] : meshes.items()) {
        fields(json,{"primitive","count","instances","attributes","indices"});
        Mesh mesh; mesh.name=name; mesh.count=natural(json.at("count"),1,MaxInvocations);
        if (json.contains("instances")) mesh.instances=natural(json.at("instances"),1,MaxInvocations);
        if (json.contains("primitive")) mesh.primitive=choice<Primitive>(json.at("primitive"),{{"points",Primitive::Points},{"lines",Primitive::Lines},{"triangles",Primitive::Triangles},{"triangle_strip",Primitive::TriangleStrip}});
        if (json.contains("indices")) {
            array(json.at("indices"),MaxInvocations);
            require(!json.at("indices").empty(),"empty mesh indices");
            for (const auto &index : json.at("indices")) mesh.indices.push_back(natural(index,0,mesh.count-1));
        }
        if (json.contains("attributes")) {
            array(json.at("attributes"),16);
            for (const auto &a : json.at("attributes")) {
                fields(a,{"location","buffer","components","stride","offset","divisor"});
                Attribute attribute; attribute.location=natural(a.at("location"),0,15); attribute.buffer=reference(package,a.at("buffer"));
                attribute.components=natural(a.at("components"),1,4); attribute.stride=attribute.components*4;
                if (a.contains("stride")) attribute.stride=natural(a.at("stride"),1,std::numeric_limits<int>::max());
                if (a.contains("offset")) attribute.offset=natural(a.at("offset"),0,32*MiB);
                if (a.contains("divisor")) attribute.divisor=natural(a.at("divisor"),0,std::numeric_limits<unsigned>::max());
                mesh.attributes.push_back(attribute);
            }
        }
        package.meshes.push_back(std::move(mesh));
    }
}
std::string stage(const Json &path, const std::filesystem::path &base, const std::string &helpers) {
    auto body = read_file(base / expand(text(path)), MiB);
    require(body.find('\0') == body.npos, "NUL in shader source");
    return helpers + "\n" + body;
}
void passes(Package &package, const Json &root, const std::filesystem::path &base) {
    std::size_t source_bytes = 0;
    std::string helpers;
    if (root.contains("helpers")) {
        array(root.at("helpers"),16);
        for (const auto &path : root.at("helpers")) {
            auto helper=read_file(base / expand(text(path)),MiB);
            require(helper.find('\0')==helper.npos,"NUL in shader helper");
            require(helper.size()+helpers.size()+1 <= MaxSource,"helpers exceed source budget");
            helpers+=helper; helpers+='\n';
        }
    }
    require(root.contains("fragment") != root.contains("passes"), "exactly one of fragment or passes is required");
    if (root.contains("fragment")) {
        Pass pass; pass.name="render"; pass.vertex=helpers+"\n"+std::string(default_vertex()); pass.fragment=stage(root.at("fragment"),base,helpers);
        pass.outputs.push_back({ResourceKind::Output,0,Side::Single}); package.passes.push_back(std::move(pass)); return;
    }
    array(root.at("passes"),MAX_PASSES);
    for (const auto &json : root.at("passes")) {
        fields(json,{"name","phase","vertex","fragment","inputs","blocks","outputs","mesh","feedback","iterations","blend","clear"});
        Pass pass; pass.name=text(json.at("name"));
        if (json.contains("phase")) pass.phase=choice<Phase>(json.at("phase"),{{"initialize",Phase::Initialize},{"update",Phase::Update},{"render",Phase::Render}});
        if (json.contains("vertex")) pass.vertex=stage(json.at("vertex"),base,helpers); else pass.vertex=helpers+"\n"+std::string(default_vertex());
        if (json.contains("fragment")) pass.fragment=stage(json.at("fragment"),base,helpers);
        if (json.contains("iterations")) pass.iterations=natural(json.at("iterations"),1,MAX_ITERATIONS);
        if (json.contains("blend")) pass.blend=choice<bool>(json.at("blend"),{{"none",false},{"premultiplied",true}});
        if (json.contains("clear")) pass.clear=boolean(json.at("clear"));
        if (json.contains("mesh")) {
            const auto name=text(json.at("mesh"));
            for (std::size_t i=0;i<package.meshes.size();++i) if (package.meshes[i].name==name) pass.mesh=static_cast<int>(i);
            require(pass.mesh>=0,"unknown mesh: "+name);
        }
        auto bindings=[&](const char *key,std::vector<Binding> &bindings) {
            if (!json.contains(key)) return;
            require(json.at(key).is_object() && json.at(key).size()<=32,"invalid bindings");
            for (const auto &[name,resource] : json.at(key).items()) bindings.push_back({name,reference(package,resource)});
        };
        bindings("inputs",pass.inputs); bindings("blocks",pass.blocks);
        if (json.contains("outputs")) { array(json.at("outputs"),MAX_TEXTURES); for (const auto &ref : json.at("outputs")) pass.outputs.push_back(reference(package,ref)); }
        if (json.contains("feedback")) {
            const auto &feedback=json.at("feedback"); fields(feedback,{"buffer","varyings"});
            pass.transform_feedback=true; pass.feedback=reference(package,feedback.at("buffer"));
            array(feedback.at("varyings"),64);
            for (const auto &varying : feedback.at("varyings")) pass.varyings.push_back(text(varying));
            require(json.contains("vertex") && !json.contains("fragment") && !json.contains("outputs") && !json.contains("blend") && !json.contains("clear"),"feedback pass requires vertex only and no raster outputs/state");
        }
        source_bytes += pass.vertex.size() + pass.fragment.size();
        require(source_bytes <= MaxSource, "shader source exceeds 4 MiB");
        package.passes.push_back(std::move(pass));
    }
}
std::string uncomment(std::string_view body) {
    std::string result; result.reserve(body.size());
    bool line=false,block=false;
    for (std::size_t i=0;i<body.size();++i) {
        // GLSL splices continued lines before processing comments/directives.
        if(body[i]=='\\' && i+1<body.size() && body[i+1]=='\n') {++i;continue;}
        if(body[i]=='\\' && i+2<body.size() && body[i+1]=='\r' && body[i+2]=='\n') {i+=2;continue;}
        if (line) { if(body[i]=='\n') {line=false;result+='\n';} continue; }
        if (block) {if(body[i]=='*' && i+1<body.size() && body[i+1]=='/') {block=false;++i;result+=' ';} else if(body[i]=='\n')result+='\n'; continue;}
        if(body[i]=='/' && i+1<body.size() && body[i+1]=='/') {line=true;++i;continue;}
        if(body[i]=='/' && i+1<body.size() && body[i+1]=='*') {block=true;++i;result+=' ';continue;}
        result+=body[i];
    }
    return result;
}
void validate_stage(const Package &package, std::string_view source) {
    require(source.find('\0')==source.npos,"NUL in shader source");
    const auto clean=uncomment(source);
    static const std::regex version(R"((^|\n)\s*#\s*version\b)");
    require(!std::regex_search(clean,version),"shader bodies must omit #version");
    static const std::regex extension(R"((^|\n)\s*#\s*extension\b)");
    require(!std::regex_search(clean,extension),"shader extensions are outside the native GLES 3.0 interface");
    // Uniform declarations may contain precision qualifiers and comma-separated names.
    static const std::regex uniforms(R"(\buniform\b[^;{]*;)");
    for (auto it=std::sregex_iterator(clean.begin(),clean.end(),uniforms);it!=std::sregex_iterator();++it) {
        const auto declaration=it->str();
        for (const auto &p : package.parameters) {
            const std::regex name("\\b"+p.name+"\\b");
            require(!std::regex_search(declaration,name),"parameter uniform redeclared: "+p.name);
        }
    }
}
unsigned pixel_bytes(Format f) {
    switch(f) {
        case Format::R8:return 1; case Format::RG8:return 2;case Format::RGBA8:return 4;
        case Format::R16F:return 2;case Format::RG16F:return 4;case Format::RGBA16F:return 8;
        case Format::R32F:return 4;case Format::RG32F:return 8;case Format::RGBA32F:return 16;
    }
    fail("invalid texture format");
}
void validate(const Package &p) {
    require(!p.events.empty() && p.events.size()<=6,"invalid events");
    std::set<Event> events;
    for(auto e:p.events) require(e>=Event::Open && e<=Event::Wallpaper && events.insert(e).second,"invalid or duplicate event");
    for(float bound:p.bounds) require(std::isfinite(bound) && bound>=0 && bound<=2,"bounds must be in [0,2]");
    require(p.parameters.size()<=MAX_PARAMETERS && p.textures.size()<=MAX_TEXTURES && p.buffers.size()<=MAX_BUFFERS && p.meshes.size()<=MAX_MESHES && !p.passes.empty() && p.passes.size()<=MAX_PASSES,"package count limit exceeded");
    std::set<std::string> names,parameters,mesh_names,pass_names;
    for(const auto &parameter:p.parameters) { named(parameter.name); require(parameters.insert(parameter.name).second,"duplicate parameter"); range(parameter,parameter.initial); }
    std::size_t bytes=0,decoded=0,source_bytes=0;
    for(const auto &t:p.textures) {
        resource_name(t.name); require(names.insert(t.name).second,"duplicate resource name");
        require(t.lifetime==Lifetime::Persistent || t.lifetime==Lifetime::Transient,"invalid texture lifetime");
        require(std::isfinite(t.size.scale) && t.size.scale>0 && t.size.scale<=1 && (t.size.width==0)==(t.size.height==0) && t.size.width<=4096 && t.size.height<=4096,"invalid texture size");
        require(!t.size.width || t.size.scale==1,"explicit size and scale are exclusive");
        std::size_t size=static_cast<std::size_t>(t.size.width)*t.size.height*pixel_bytes(t.format);
        if(!t.image.empty()) {
            require(t.image_width && t.image_height && t.image_width<=4096 && t.image_height<=4096 && t.image.size()==static_cast<std::size_t>(t.image_width)*t.image_height*4,"invalid decoded PNG");
            require(!t.ping_pong && t.lifetime==Lifetime::Persistent && t.format==Format::RGBA8 && !t.size.width && t.size.scale==1,"invalid PNG storage properties");
            size=t.image.size(); decoded+=size;
        } else require(!t.image_width && !t.image_height,"missing PNG pixels");
        bytes+=size*(t.ping_pong?2:1);
    }
    require(decoded<=64*MiB,"decoded PNG assets exceed 64 MiB");
    for(const auto &b:p.buffers) {
        resource_name(b.name); require(names.insert(b.name).second,"duplicate resource name");
        require(b.lifetime==Lifetime::Persistent || b.lifetime==Lifetime::Transient,"invalid buffer lifetime");
        require(b.bytes && b.bytes<=32*MiB && b.initial.size()<=b.bytes,"invalid buffer size/data");
        bytes+=b.bytes*(b.ping_pong?2:1);
    }
    require(bytes<=MAX_INSTANCE_BYTES,"instance storage exceeds 128 MiB");
    auto info=[&](Ref ref) -> std::pair<bool,Lifetime> {
        bool ping=false; Lifetime life=Lifetime::Persistent;
        switch(ref.kind) {
            case ResourceKind::Texture:require(ref.index<p.textures.size(),"invalid texture ref");ping=p.textures[ref.index].ping_pong;life=p.textures[ref.index].lifetime;break;
            case ResourceKind::Buffer:require(ref.index<p.buffers.size(),"invalid buffer ref");ping=p.buffers[ref.index].ping_pong;life=p.buffers[ref.index].lifetime;break;
            case ResourceKind::Source:case ResourceKind::Output:require(ref.index==0,"invalid host ref");break;
            default:fail("invalid resource kind");
        }
        require(ping ? (ref.side==Side::Read || ref.side==Side::Write) : ref.side==Side::Single,"resource side does not match ping_pong declaration");
        return {ping,life};
    };
    for(const auto &mesh:p.meshes) {
        label(mesh.name);require(mesh_names.insert(mesh.name).second,"duplicate mesh");
        require(mesh.primitive>=Primitive::Points && mesh.primitive<=Primitive::TriangleStrip,"invalid mesh primitive");
        require(mesh.count && mesh.instances && mesh.count<=MaxInvocations && mesh.indices.size()<=MaxInvocations,"invalid mesh count");
        const auto count=mesh.indices.empty()?mesh.count:mesh.indices.size();
        require(count*static_cast<std::uint64_t>(mesh.instances)<=MaxInvocations,"vertex invocation budget exceeded");
        unsigned maximum=mesh.count-1;
        if(!mesh.indices.empty()) {maximum=*std::max_element(mesh.indices.begin(),mesh.indices.end());require(maximum<mesh.count,"index exceeds vertex count");}
        require(mesh.attributes.size()<=16,"too many attributes");std::set<unsigned> locations;
        for(const auto &a:mesh.attributes) {
            info(a.buffer);require(a.buffer.kind==ResourceKind::Buffer && a.buffer.side!=Side::Write,"attribute requires readable buffer");
            require(a.location<16 && locations.insert(a.location).second && a.components>=1 && a.components<=4 && a.stride && a.stride<=static_cast<unsigned>(std::numeric_limits<int>::max()) && a.stride%4==0 && a.offset%4==0,"invalid attribute layout");
            const auto last=a.divisor?(mesh.instances-1)/a.divisor:maximum;
            const auto end=static_cast<std::uint64_t>(a.offset)+static_cast<std::uint64_t>(last)*a.stride+a.components*4;
            require(end<=p.buffers[a.buffer.index].bytes,"attribute fetch exceeds buffer");
        }
    }
    Phase phase=Phase::Initialize; bool presentation=false;
    std::set<std::pair<ResourceKind,unsigned>> produced;
    for(const auto &pass:p.passes) {
        label(pass.name);require(pass_names.insert(pass.name).second,"duplicate pass name");
        require(pass.phase>=Phase::Initialize && pass.phase<=Phase::Render && pass.phase>=phase,"phases must be ordered initialize, update, render");
        if(pass.phase!=phase){produced.clear();phase=pass.phase;}
        require(pass.iterations>=1 && pass.iterations<=MAX_ITERATIONS,"invalid pass iterations");
        require(pass.mesh>=-1 && (pass.mesh<0 || static_cast<std::size_t>(pass.mesh)<p.meshes.size()),"invalid pass mesh");
        require(!pass.vertex.empty(),"missing vertex shader");
        require(pass.inputs.size()<=32 && pass.blocks.size()<=32,"too many pass bindings");
        source_bytes+=shader_source(p,pass.vertex,true).size();source_bytes+=shader_source(p,pass.fragment,false).size();
        require(source_bytes<=MaxSource,"assembled shader source exceeds 4 MiB");
        validate_stage(p,pass.vertex);validate_stage(p,pass.fragment);
        std::vector<Ref> reads,writes=pass.outputs;
        std::set<std::string> bindings;
        for(const auto &binding:pass.inputs) {named(binding.name);require(!parameters.contains(binding.name) && bindings.insert(binding.name).second,"duplicate/colliding sampler binding");require(binding.resource.kind==ResourceKind::Source || binding.resource.kind==ResourceKind::Texture,"sampler requires texture");reads.push_back(binding.resource);}
        for(const auto &binding:pass.blocks) {named(binding.name);require(!parameters.contains(binding.name) && bindings.insert(binding.name).second,"duplicate/colliding block binding");require(binding.resource.kind==ResourceKind::Buffer,"block requires buffer");reads.push_back(binding.resource);}
        if(pass.mesh>=0) for(const auto &a:p.meshes[pass.mesh].attributes)reads.push_back(a.buffer);
        if(pass.transform_feedback) {
            require(pass.phase!=Phase::Render && pass.outputs.empty() && pass.fragment.empty() && !pass.varyings.empty() && pass.varyings.size()<=64 && !pass.blend,"invalid transform feedback pass");
            require(pass.mesh >= 0, "transform feedback requires a named mesh");
            if (pass.mesh >= 0) {
                const auto &mesh = p.meshes[pass.mesh];
                require(mesh.indices.empty() && mesh.primitive != Primitive::TriangleStrip,
                    "transform feedback requires a non-indexed points, lines, or triangles mesh");
            }
            require(pass.feedback.kind==ResourceKind::Buffer,"feedback requires buffer");writes.push_back(pass.feedback);
            std::set<std::string> varyings;for(const auto &v:pass.varyings){named(v);require(varyings.insert(v).second,"duplicate feedback varying");}
        } else {require(!pass.fragment.empty() && !writes.empty() && pass.varyings.empty(),"raster pass requires fragment and outputs");}
        require(writes.size()<=MAX_TEXTURES,"too many outputs");
        for(auto read:reads) {
            const auto [ping,life]=info(read);(void)ping;
            require(read.kind!=ResourceKind::Output && read.side!=Side::Write,"invalid read reference");
            if(life==Lifetime::Transient)require(produced.contains({read.kind,read.index}),"transient resource read before producer in same phase");
        }
        std::set<std::pair<ResourceKind,unsigned>> outputs;
        for(auto write:writes) {
            const auto [ping,life]=info(write);(void)ping;
            require(write.kind!=ResourceKind::Source && write.side!=Side::Read,"invalid write reference");
            require(outputs.insert({write.kind,write.index}).second,"duplicate pass output");
            if(write.kind==ResourceKind::Output) {require(pass.phase==Phase::Render && writes.size()==1,"output must be sole render attachment");presentation=true;}
            else require(pass.phase!=Phase::Render || life==Lifetime::Transient,"render cannot write persistent resources");
            if(!pass.transform_feedback) {
                require(write.kind==ResourceKind::Output || write.kind==ResourceKind::Texture,"raster outputs require textures");
                if(write.kind==ResourceKind::Texture) {
                    const auto &t=p.textures[write.index];require(t.image.empty(),"PNG asset is read-only");
                    if (life == Lifetime::Transient) {
                        require(pass.clear ||
                            (!t.ping_pong && produced.contains({write.kind, write.index})),
                            "transient attachment must be cleared before preserving its contents");
                    }
                }
            }
            for(auto read:reads)if(read.kind==write.kind && read.index==write.index)require(read.side==Side::Read && write.side==Side::Write,"resource feedback requires opposite ping sides");
        }
        // A pass's own writes do not satisfy its first iteration's dependencies.
        produced.insert(outputs.begin(),outputs.end());
    }
    require(presentation,"package requires a render pass writing output");
}
} // namespace

bool Package::supports(Event event) const noexcept {
    return std::find(events.begin(),events.end(),event)!=events.end();
}
std::expected<Package,std::string> load_package(const std::string &path) {
    try {
        Package package; const auto file=std::filesystem::absolute(expand(path)).lexically_normal();package.path=file.string();
        const auto root=parse(read_file(file,65536));
        fields(root,{"version","events","bounds","blur_mask","helpers","parameters","textures","buffers","meshes","fragment","passes"});
        require(natural(root.at("version"),SPEC_VERSION,SPEC_VERSION)==SPEC_VERSION,"unsupported version");
        if(root.contains("events")) {
            array(root.at("events"),6);package.events.clear();
            for(const auto &e:root.at("events"))package.events.push_back(choice<Event>(e,{{"open",Event::Open},{"close",Event::Close},{"move",Event::Move},{"resize",Event::Resize},{"workspace",Event::Workspace},{"wallpaper",Event::Wallpaper}}));
        }
        if(root.contains("bounds")) {array(root.at("bounds"),4);require(root.at("bounds").size()==4,"bounds requires four fractions");for(unsigned i=0;i<4;++i)package.bounds[i]=number(root.at("bounds")[i]);}
        if (root.contains("blur_mask")) package.blur_mask = root.at("blur_mask").get<bool>();
        if(root.contains("parameters")) {
            const auto &parameters=root.at("parameters");require(parameters.is_object() && parameters.size()<=MAX_PARAMETERS,"invalid parameters");
            for(const auto &[name,json]:parameters.items()) {
                fields(json,{"type","default","min","max"});Parameter parameter;parameter.name=name;
                const auto type=choice<ValueType>(json.at("type"),{{"float",ValueType::Float},{"int",ValueType::Int},{"bool",ValueType::Bool},{"vec2",ValueType::Vec2},{"vec3",ValueType::Vec3},{"vec4",ValueType::Vec4}});
                parameter.initial=value(json.at("default"),type);
                require(type!=ValueType::Bool || (!json.contains("min") && !json.contains("max")),"boolean parameters do not accept ranges");
                if(json.contains("min"))parameter.minimum=parameter_bound(json.at("min"),type);
                if(json.contains("max"))parameter.maximum=parameter_bound(json.at("max"),type);
                package.parameters.push_back(std::move(parameter));
            }
        }
        resources(package,root,file.parent_path());meshes(package,root);passes(package,root,file.parent_path());validate(package);
        return package;
    } catch(const std::exception &error) {return std::unexpected("package "+path+": "+error.what());}
}
std::expected<void,std::string> validate_package(const Package &package) {
    try {validate(package);return {};}catch(const std::exception &error){return std::unexpected(error.what());}
}
std::expected<Values,std::string> parameter_values(const Package &package,std::string_view overrides) {
    try {
        const auto json=parse(overrides);require(json.is_object(),"parameter overrides must be an object");
        Values result;result.reserve(package.parameters.size());
        for(const auto &[key,unused]:json.items()) {(void)unused;require(std::any_of(package.parameters.begin(),package.parameters.end(),[&](const Parameter &p){return p.name==key;}),"unknown parameter: "+key);}
        for(const auto &p:package.parameters) {auto v=json.contains(p.name)?value(json.at(p.name),p.initial.type):p.initial;range(p,v);result.push_back(v);}
        return result;
    }catch(const std::exception &error){return std::unexpected(error.what());}
}
std::expected<void,std::string> validate_values(const Package &package,const Values &values) {
    try {
        require(values.size()==package.parameters.size(),"parameter value count mismatch");
        for(std::size_t i=0;i<values.size();++i) range(package.parameters[i],values[i]);
        return {};
    } catch(const std::exception &error) {
        return std::unexpected(error.what());
    }
}
std::string_view default_vertex() noexcept {
    return R"(out vec2 hypr_uv;
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    hypr_uv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";
}
std::string shader_source(const Package &package,std::string_view body,bool vertex) {
    std::string source=R"(#version 300 es
precision highp float;
precision highp int;
uniform sampler2D hypr_source, hypr_destination;
uniform float hypr_progress, hypr_elapsed, hypr_delta, hypr_duration, hypr_scale, hypr_direction;
uniform vec2 hypr_size, hypr_target_size, hypr_pointer;
uniform vec4 hypr_seed, hypr_source_uv, hypr_domain, hypr_destination_uv;
uniform vec4 hypr_geometry, hypr_previous_geometry, hypr_velocity;
uniform int hypr_event, hypr_iteration, hypr_iterations;
uniform bool hypr_pointer_valid, hypr_fullscreen, hypr_held, hypr_settling, hypr_blur_mask;
vec4 hypr_premultiply(vec4 c) { return vec4(c.rgb * c.a, c.a); }
vec4 hypr_unpremultiply(vec4 c) { return vec4(c.a > 0.0 ? c.rgb / c.a : vec3(0.0), c.a); }
vec4 hypr_sample_source(vec2 uv) {
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) return vec4(0.0);
    vec2 half_texel = 0.5 / vec2(textureSize(hypr_source, 0));
    vec2 lo = hypr_source_uv.xy + half_texel;
    vec2 hi = max(lo, hypr_source_uv.xy + hypr_source_uv.zw - half_texel);
    vec2 mapped = clamp(hypr_source_uv.xy + uv * hypr_source_uv.zw, lo, hi);
)";
    source+=vertex?"    return textureLod(hypr_source, mapped, 0.0);\n}\n":"    return texture(hypr_source, mapped);\n}\nin vec2 hypr_uv;\nvec2 hypr_window_uv() { return hypr_domain.xy + hypr_uv * hypr_domain.zw; }\n";
    source += R"(vec4 hypr_sample_destination(vec2 uv) {
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) return vec4(0.0);
    return textureLod(hypr_destination, hypr_destination_uv.xy + uv * hypr_destination_uv.zw, 0.0);
}
)";
    constexpr std::array<std::string_view,6> types{"float","int","bool","vec2","vec3","vec4"};
    for(const auto &p:package.parameters) {const auto type=static_cast<unsigned>(p.initial.type);if(type<types.size())source+="uniform "+std::string(types[type])+" "+p.name+";\n";}
    source+="#line 1\n";source.append(body);return source;
}
} // namespace hyprimate::shader
