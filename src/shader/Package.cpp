#include "Package.hpp"
#include "PackageValidationPrivate.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <unordered_set>

#include <nlohmann/json.hpp>
#include <png.h>

namespace hyprimate::shader {
using namespace package_detail;
namespace {
using Json = nlohmann::json;
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
Ref reference(const Package &package, const Json &json) {
    auto name = text(json);
    if (name == "source") return {ResourceKind::Source, 0, Side::Single};
    if (name == "output") return {ResourceKind::Output, 0, Side::Single};
    Side side = Side::Single;
    if (name.ends_with(".read")) { side = Side::Read; name.resize(name.size() - 5); }
    else if (name.ends_with(".write")) { side = Side::Write; name.resize(name.size() - 6); }
    for (std::size_t i = 0; i < package.textures.size(); ++i) if (package.textures[i].name == name) return {ResourceKind::Texture, static_cast<std::uint16_t>(i), side};
    for (std::size_t i = 0; i < package.fields.size(); ++i) if (package.fields[i].name == name) return {ResourceKind::Field, static_cast<std::uint16_t>(i), side};
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
    if (root.contains("fields")) {
        const auto &items = root.at("fields");
        require(items.is_object() && items.size() <= MAX_TEXTURES, "invalid fields");
        for (const auto &[name, json] : items.items()) {
            fields(json, {"type", "domain", "scale", "ping_pong", "lifetime"});
            Field field;
            field.name = name;
            auto parsed_type = compute::parse_type(text(json.at("type")));
            require(bool(parsed_type), "invalid field type: " + name);
            field.type = *parsed_type;
            require(json.contains("domain") != json.contains("scale"),
                "field requires exactly one of domain or scale");
            if (json.contains("domain")) {
                const auto &domain = json.at("domain");
                array(domain, 3);
                require(!domain.empty(), "field domain cannot be empty");
                std::array<std::uint32_t, 3> axes{1, 1, 1};
                for (std::size_t i = 0; i < domain.size(); ++i)
                    axes[i] = natural(domain[i], 1, compute::MAX_PIXELS);
                field.domain = compute::Domain{axes[0], axes[1], axes[2]};
            } else {
                field.scale = number(json.at("scale"));
                require(*field.scale > 0 && *field.scale <= 1, "field scale must be in (0,1]");
            }
            if (json.contains("ping_pong")) field.ping_pong = boolean(json.at("ping_pong"));
            if (json.contains("lifetime")) field.lifetime = lifetime(json.at("lifetime"));
            if (field.domain) {
                auto layout = compute::field_layout({field.type, *field.domain});
                require(bool(layout), "invalid field domain or size: " + name);
                storage += layout->storage_bytes * (field.ping_pong ? 2 : 1);
                require(storage <= MAX_INSTANCE_BYTES, "instance storage exceeds 128 MiB");
            }
            package.fields.push_back(std::move(field));
        }
    }
    require(package.textures.size() + package.fields.size() <= MAX_TEXTURES,
        "texture and field count limit exceeded");
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
        fields(json, {"name","phase","vertex","fragment","kernel","inputs","blocks","outputs","mesh","feedback","iterations","blend","clear"});
        Pass pass; pass.name=text(json.at("name"));
        if (json.contains("phase")) pass.phase=choice<Phase>(json.at("phase"),{{"initialize",Phase::Initialize},{"update",Phase::Update},{"render",Phase::Render}});
        const bool compute_pass = json.contains("kernel");
        require(!compute_pass || (!json.contains("vertex") && !json.contains("fragment") &&
                    !json.contains("blocks") && !json.contains("mesh") && !json.contains("feedback") &&
                    !json.contains("blend") && !json.contains("clear")),
            "compute pass cannot declare raster stages or state");
        pass.compute = compute_pass;
        if (compute_pass) pass.clear = false;
        if (compute_pass) pass.kernel = read_file(base / expand(text(json.at("kernel"))), MiB);
        if (!compute_pass && json.contains("vertex")) pass.vertex = stage(json.at("vertex"), base, helpers);
        else if (!compute_pass) pass.vertex = helpers + "\n" + std::string(default_vertex());
        if (!compute_pass && json.contains("fragment")) pass.fragment = stage(json.at("fragment"), base, helpers);
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
        source_bytes += pass.vertex.size() + pass.fragment.size() + pass.kernel.size();
        require(source_bytes <= MaxSource, "shader source exceeds 4 MiB");
        package.passes.push_back(std::move(pass));
    }
}
} // namespace

bool Package::supports(Event event) const noexcept {
    return std::find(events.begin(),events.end(),event)!=events.end();
}
std::expected<Package,std::string> load_package(const std::string &path) {
    try {
        Package package; const auto file=std::filesystem::absolute(expand(path)).lexically_normal();package.path=file.string();
        const auto root=parse(read_file(file,65536));
        fields(root, {"version","events","bounds","blur_mask","helpers","parameters","textures","fields","buffers","meshes","fragment","passes","environment"});
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
        resources(package,root,file.parent_path());meshes(package,root);passes(package,root,file.parent_path());
        if (root.contains("environment")) {
            const auto &e = root.at("environment");
            fields(e,{"original","processed","interactions","publish","settle_ms","channel","state","sample_state","emit"});
            auto &env = package.environment;
            for (auto [key, member] : {std::pair{"original", &Environment::original},
                     {"processed", &Environment::processed}, {"interactions", &Environment::interactions},
                     {"publish", &Environment::publish}, {"sample_state", &Environment::sample_state}}) {
                if (e.contains(key)) env.*member = boolean(e.at(key));
            }
            if (e.contains("settle_ms")) env.settle_ms = natural(e.at("settle_ms"), 1, 60000);
            if (e.contains("channel")) env.channel = text(e.at("channel"));
            if (e.contains("state")) env.state = reference(package, e.at("state"));
            if (e.contains("emit")) env.emit = reference(package, e.at("emit"));
        }
        validate(package);
        return package;
    } catch(const std::exception &error) {return std::unexpected("package "+path+": "+error.what());}
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
compute::KernelDesc kernel_descriptor(const Package &package, const Pass &pass) {
    compute::KernelDesc descriptor;
    descriptor.name = pass.name;
    descriptor.source = pass.kernel;
    for (const auto &binding : pass.inputs)
        descriptor.inputs.push_back({binding.name, package.fields[binding.resource.index].type});
    for (const auto &output : pass.outputs) {
        std::string name = "output" + std::to_string(descriptor.outputs.size());
        const auto used = [&](std::string_view candidate) {
            return std::ranges::any_of(descriptor.inputs, [&](const compute::Slot &slot) {
                       return slot.name == candidate;
                   }) ||
                   std::ranges::any_of(package.parameters, [&](const Parameter &parameter) {
                       return parameter.name == candidate;
                   }) ||
                   std::ranges::any_of(descriptor.outputs, [&](const compute::Slot &slot) {
                       return slot.name == candidate;
                   });
        };
        while (used(name)) name += '_';
        descriptor.outputs.push_back({std::move(name), package.fields[output.index].type});
    }
    for (const auto &parameter : package.parameters) {
        compute::Type type{compute::Element::Float, 1};
        switch (parameter.initial.type) {
            case ValueType::Float: type = {compute::Element::Float, 1}; break;
            case ValueType::Int:
            case ValueType::Bool: type = {compute::Element::Int, 1}; break;
            case ValueType::Vec2: type = {compute::Element::Float, 2}; break;
            case ValueType::Vec3: type = {compute::Element::Float, 3}; break;
            case ValueType::Vec4: type = {compute::Element::Float, 4}; break;
        }
        descriptor.parameters.push_back({parameter.name, type});
    }
    descriptor.parameters.push_back({"hypr_delta", {compute::Element::Float, 1}});
    descriptor.parameters.push_back({"hypr_elapsed", {compute::Element::Float, 1}});
    descriptor.parameters.push_back({"hypr_progress", {compute::Element::Float, 1}});
    descriptor.parameters.push_back({"hypr_iteration", {compute::Element::Int, 1}});
    descriptor.parameters.push_back({"hypr_iterations", {compute::Element::Int, 1}});
    descriptor.parameters.push_back({"hypr_seed", {compute::Element::Float, 4}});
    return descriptor;
}
} // namespace hyprimate::shader
