#include "../Package.hpp"

#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unistd.h>

#include <nlohmann/json.hpp>
#include <png.h>

using namespace hyprimate::shader;
namespace compute = hyprimate::compute;
using Json = nlohmann::json;
namespace {
void check(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }
struct Fixture {
    std::filesystem::path directory;
    Fixture() {
        auto pattern=(std::filesystem::temp_directory_path()/"hyprimate-package-XXXXXX").string();
        char *result=mkdtemp(pattern.data());
        check(result,"mkdtemp");directory=result;
        file("simple.frag","layout(location=0) out vec4 color; void main() { color=hypr_sample_source(hypr_uv); }\n");
        file("custom.vert","out vec2 hypr_uv; out vec4 nextPosition; void main() { hypr_uv=vec2(0.0); nextPosition=vec4(0.0); gl_Position=vec4(0.0); }\n");
    }
    ~Fixture() { std::error_code error;std::filesystem::remove_all(directory,error); }
    void file(const char *name,const std::string &contents) {
        std::ofstream stream(directory/name,std::ios::binary);stream.write(contents.data(),contents.size());check(bool(stream),"write fixture");
    }
    std::expected<Package,std::string> load(const Json &json) { return raw(json.dump()); }
    std::expected<Package,std::string> raw(const std::string &json) {file("effect.json",json);return load_package((directory/"effect.json").string());}
    void png() {
        png_image image{};image.version=PNG_IMAGE_VERSION;image.width=2;image.height=1;image.format=PNG_FORMAT_RGBA;
        const std::array<unsigned char,8> pixels{255,64,0,128,0,255,0,255};
        png_alloc_size_t size=0;
        check(png_image_write_to_memory(&image,nullptr,&size,0,pixels.data(),0,nullptr),"size PNG");
        std::string data(size,'\0');
        check(png_image_write_to_memory(&image,data.data(),&size,0,pixels.data(),0,nullptr),"encode PNG");
        data.resize(size);file("art.png",data);png_image_free(&image);
    }
};
Json minimal() {return {{"version",1},{"fragment","simple.frag"}};}
Json raster(const char *name,const char *phase,const char *output) {
    return {{"name",name},{"phase",phase},{"fragment","simple.frag"},{"outputs",Json::array({output})}};
}
void shorthand_and_abi() {
    Fixture f;f.file("math.glsl","float user_helper(float x) { return x*x; }\n");
    auto j=minimal();j["helpers"]={"math.glsl"};j["bounds"]={0.1,0.2,0.3,0.4};
    auto p=f.load(j);check(bool(p),"minimal load");
    check(p->supports(Event::Open) && p->supports(Event::Close) && !p->supports(Event::Move),"default events");
    check(p->bounds == std::array<float,4>{0.1F,0.2F,0.3F,0.4F},
        "capture-relative expanded bounds retain each edge");
    j["events"]={"wallpaper","resize","move","workspace"};check(bool(f.load(j)),"declared future events supported");
}
void strict_schema() {
    Fixture f;
    for(auto j : {Json{{"fragment","simple.frag"}}, Json{{"version",2},{"fragment","simple.frag"}}, Json{{"version",1.0},{"fragment","simple.frag"}},Json{{"version",1},{"fragment","simple.frag"},{"unsupported",true}}})check(!f.load(j),"reject invalid root");
    check(!f.raw("{\"version\":1,\"version\":1,\"fragment\":\"simple.frag\"}"),"duplicate keys");
    check(!f.raw(std::string(65537,' ')),"manifest size bound");
    auto j=minimal();j["passes"]=Json::array();check(!f.load(j),"exclusive shorthand");
    j=minimal();j["fragment"]=".";check(!f.load(j),"shader input must be a regular file");
    j=minimal();j["events"]={"open","open"};check(!f.load(j),"duplicate events");
    j["events"]=Json::array();check(!f.load(j),"empty events");
    j=minimal();j["bounds"]={0,0,-0.1,0};check(!f.load(j),"negative bounds");
    j["bounds"]={0,0,2.1,0};check(!f.load(j),"large bounds");
    f.file("simple.frag","// legal comment\n#version 300 es\nvoid main() {}\n");check(!f.load(minimal()),"version forbidden in body");
    f.file("simple.frag","#extension GL_EXT_shader_framebuffer_fetch : require\nvoid main() {}\n");
    check(!f.load(minimal()),"extensions cannot expose undeclared host resources");
    f.file("simple.frag","#exten\\\nsion GL_EXT_shader_framebuffer_fetch : require\nvoid main() {}\n");
    check(!f.load(minimal()),"continued directives remain outside the GLES 3.0 interface");
    f.file("simple.frag",std::string(1024*1024+1,' '));check(!f.load(minimal()),"stage size bound");
    std::string nested="0";for(int i=0;i<33;++i)nested="["+nested+"]";
    check(!f.raw(nested),"nesting limit");
}
void parameters() {
    Fixture f;auto j=minimal();j["parameters"]={
        {"gain",{{"type","float"},{"default",1},{"min",0},{"max",2}}},
        {"count",{{"type","int"},{"default",3}}},
        {"enabled",{{"type","bool"},{"default",true}}},
        {"tint",{{"type","vec3"},{"default",{1,0.5,0}}}}};
    auto p=f.load(j);check(bool(p),"typed parameters");
    auto overrides=parameter_values(*p,"{\"gain\":1.5,\"count\":4,\"enabled\":false,\"tint\":[0,1,0]}");
    check(bool(overrides),"typed overrides");
    for (std::size_t i=0;i<p->parameters.size();++i) {
        const auto &name=p->parameters[i].name;
        const auto &resolved=(*overrides)[i];
        if(name=="gain") check(resolved.numbers[0]==1.5F,"resolved float");
        if(name=="count") check(resolved.integer==4,"resolved integer");
        if(name=="enabled") check(resolved.integer==0,"resolved bool");
        if(name=="tint") check(resolved.numbers==std::array<float,4>{0,1,0,0},"resolved vector");
    }
    for(const auto *bad:{"{\"gain\":3}","{\"count\":1.5}","{\"enabled\":1}","{\"tint\":[1,0]}","{\"other\":0}","{\"gain\":1,\"gain\":2}","[]"})check(!parameter_values(*p,bad),"reject invalid override");
    f.file("simple.frag","uniform float gain; void main() {}\n");check(!f.load(j),"reject parameter redeclaration");
    f.file("simple.frag","// uniform float gain;\nvoid main() {}\n");check(bool(f.load(j)),"ignore commented declaration");
    j["parameters"]["gl_bad"]={{"type","float"},{"default",0}};check(!f.load(j),"reject reserved name");
}
void precision_and_transient_initialization() {
    Fixture f;
    auto j=minimal();
    j["parameters"]={{"count",{{"type","int"},{"default",16777216},{"min",16777217}}}};
    check(!f.load(j),"integer minimum does not round down beyond float precision");
    j["parameters"]["count"]["default"]=16777217;
    auto p=f.load(j);
    check(bool(p),"exact large integer default");
    check(!parameter_values(*p,"{\"count\":16777216}"),"integer override below exact minimum");
    j["parameters"]={{"gain",{{"type","float"},{"default",0.3},{"max",0.3}}}};
    check(bool(f.load(j)),"float bounds share float parameter representation");

    j={{"version",1},{"textures",{{"scratch",{{"lifetime","transient"}}}}},
        {"passes",Json::array({raster("partial","render","scratch"),raster("present","render","output")})}};
    j["passes"][0]["clear"]=false;
    j["passes"][0]["blend"]="premultiplied";
    j["passes"][1]["inputs"]={{"scratchInput","scratch"}};
    check(!f.load(j),"first transient writer cannot preserve undefined pixels");
    j["passes"][0]["clear"]=true;
    check(bool(f.load(j)),"cleared transient output is safe to consume");
    auto preserve=raster("preserve","render","scratch");
    preserve["clear"]=false;
    preserve["blend"]="premultiplied";
    j["passes"].insert(j["passes"].begin()+1,preserve);
    check(bool(f.load(j)),"later pass can blend into initialized single storage");
    j["textures"]["scratch"]["ping_pong"]=true;
    j["passes"][0]["outputs"]={"scratch.write"};
    j["passes"][1]["outputs"]={"scratch.write"};
    j["passes"][2]["inputs"]["scratchInput"]="scratch.read";
    check(!f.load(j),"logical ping production does not initialize opposite storage");
}
void dependencies() {
    Fixture f;Json j={{"version",1},{"textures",{{"state",{{"ping_pong",true}}},{"scratch",{{"lifetime","transient"}}}}},
        {"passes",Json::array({raster("step","update","state.write"),raster("scratchpass","render","scratch"),raster("draw","render","output")})}};
    j["passes"][0]["inputs"]={{"previous","state.read"}};
    j["passes"][2]["inputs"]={{"previous","scratch"}};
    check(bool(f.load(j)),"ordered transient and persistent ping feedback");
    auto bad=j;bad["passes"][0]["inputs"]["previous"]="state.write";check(!f.load(bad),"write side cannot be input");
    bad=j;bad["passes"][0]["outputs"]={"state.read"};check(!f.load(bad),"read side cannot be output");
    bad=j;bad["passes"][0]["outputs"]={"state.write","state.write"};check(!f.load(bad),"duplicate MRT output");
    bad=j;bad["passes"][1]["outputs"]={"state.write"};check(!f.load(bad),"render persistent writes forbidden");
    bad=j;bad["passes"][1]["inputs"]={{"previous","scratch"}};check(!f.load(bad),"transient self read");
    bad=j;bad["passes"][1]["phase"]="update";check(!f.load(bad),"transient not inherited across phases");
    bad=j;bad["passes"][0]["outputs"]={"output"};check(!f.load(bad),"output only render");
    bad=j;bad["passes"][2]["outputs"]={"output","scratch"};check(!f.load(bad),"output sole attachment");
    bad=j;bad["passes"][0]["iterations"]=33;check(!f.load(bad),"iteration limit");
    bad=j;bad["passes"][0]["phase"]="init";check(!f.load(bad),"exact initialize phase");
    bad=j;bad["textures"]["state"]["ping_pong"]=false;bad["passes"][0]["inputs"]["previous"]="state";bad["passes"][0]["outputs"]={"state"};check(!f.load(bad),"nonping feedback forbidden");
    bad=j;bad["textures"]["small"]={{"scale",0.5}};bad["passes"][0]["outputs"]={"state.write","small"};check(bool(f.load(bad)),"MRT dimensions are resolved against the actual capture");
    bad=j;bad["buffers"]={{"state",{{"bytes",16}}}};check(!f.load(bad),"resource namespace shared");
}
void assets_and_buffers() {
    Fixture f;f.png();auto j=minimal();j["textures"]={{"art",{{"path","art.png"},{"filter","linear"},{"wrap","repeat"}}}};
    j["buffers"]={{"Constants",{{"bytes",16},{"data",{1,2}}}}};
    auto p=f.load(j);check(bool(p),"PNG and float buffer load");
    check(p->textures[0].image_width==2 && p->textures[0].image[0]==255 &&
        p->textures[0].image[1]==64 && p->textures[0].image[2]==0 &&
        p->textures[0].image[3]==128, "PNG preserves translucent RGBA channels");
    float first=0;std::memcpy(&first,p->buffers[0].initial.data(),sizeof(first));check(first==1 && p->buffers[0].initial.size()==8,"float buffer encoding and unfilled tail");
    f.file("raw.bin",std::string("\x01\x00\xff",3));j["buffers"]["Constants"].erase("data");j["buffers"]["Constants"]["path"]="raw.bin";
    p=f.load(j);check(bool(p) && p->buffers[0].initial.size()==3 && p->buffers[0].initial[2]==255,"raw byte buffer");
    auto bad=j;bad["buffers"]["Constants"]["data"]={1};check(!f.load(bad),"raw and float data exclusive");
    bad=j;bad["buffers"]["Constants"]["bytes"]=2;check(!f.load(bad),"raw data larger than buffer");
    bad=j;bad["textures"]["art"]["ping_pong"]=true;check(!f.load(bad),"PNG cannot ping pong");
    bad=j;bad.erase("fragment");bad["passes"]=Json::array({raster("overwrite","update","art"),raster("draw","render","output")});check(!f.load(bad),"PNG immutable");
    f.file("art.png","not png");check(!f.load(j),"malformed PNG");
    bad=minimal();bad["buffers"]={{"big",{{"bytes",32*1024*1024},{"ping_pong",true}}},{"bigger",{{"bytes",32*1024*1024},{"ping_pong",true}}},{"extra",{{"bytes",1}}}};check(!f.load(bad),"instance storage budget");
}
void meshes_and_feedback() {
    Fixture f;Json j={{"version",1},{"buffers",{{"particles",{{"bytes",64},{"ping_pong",true}}}}},
        {"meshes",{{"points",{{"primitive","points"},{"count",4},{"attributes",Json::array({{{"location",0},{"buffer","particles.read"},{"components",4}}})}}}}},
        {"passes",Json::array({{{"name","integrate"},{"phase","update"},{"vertex","custom.vert"},{"mesh","points"},{"feedback",{{"buffer","particles.write"},{"varyings",{"nextPosition"}}}}},raster("draw","render","output")})}};
    j["passes"][1]["vertex"]="custom.vert";j["passes"][1]["mesh"]="points";
    check(bool(f.load(j)),"feedback buffer feeds render mesh");
    auto missing_mesh=j;missing_mesh["passes"][0].erase("mesh");
    check(!f.load(missing_mesh),"feedback requires explicit geometry");
    auto bad=j;bad["meshes"]["points"]["count"]=5;check(!f.load(bad),"attribute fetch bounds");
    bad=j;bad["meshes"]["points"]["attributes"][0]["offset"]=2;check(!f.load(bad),"float alignment");
    bad=j;bad["meshes"]["points"]["indices"]={0,1,2};check(!f.load(bad),"feedback indexed meshes forbidden");
    bad=j;bad["meshes"]["points"]["primitive"]="triangle_strip";check(!f.load(bad),"feedback strip forbidden");
    bad=j;bad["passes"][0]["phase"]="render";check(!f.load(bad),"render feedback forbidden");
    bad=j;bad["meshes"]["points"]["instances"]=4194304;check(!f.load(bad),"invocation budget");
    j["passes"].erase(j["passes"].begin());j["meshes"]["points"]["indices"]={0,1,2};check(bool(f.load(j)),"indexed raster mesh accepted");
    j["meshes"]["points"]["indices"]={0,1,4};check(!f.load(j),"index out of range");
    j["meshes"]["points"].erase("indices");j["meshes"]["points"]["count"]=100;j["meshes"]["points"]["instances"]=4;j["meshes"]["points"]["attributes"][0]["divisor"]=1;check(bool(f.load(j)),"instanced fetch uses divisor rather than vertex count");
}

void environment_contracts() {
    Fixture f;
    f.file("simple.frag", "layout(location=0) out vec4 color; void main(){color=vec4(0.0);}");
    Json wallpaper = {
        {"version", 1}, {"events", {"wallpaper"}},
        {"environment", {{"original", true}, {"interactions", true}, {"settle_ms", 600},
                         {"publish", true}, {"channel", "weather"}, {"state", "height.read"}}},
        {"textures", {{"height", {{"format", "rgba8"}, {"scale", .25}, {"ping_pong", true}}}}},
        {"passes", Json::array({raster("evolve", "update", "height.write"),
                                raster("compose", "render", "output")})}};
    check(bool(f.load(wallpaper)), "wallpaper may publish and export a persistent update result");
    auto bad = wallpaper; bad["environment"]["processed"] = true;
    check(!f.load(bad), "a wallpaper cannot sample its processed output");
    bad = wallpaper; bad["environment"]["state"] = "height.write";
    check(!f.load(bad), "only committed ping sides can be exported");
    bad = wallpaper; bad["passes"][0]["phase"] = "initialize";
    check(!f.load(bad), "only update-produced resources can be exported");
    bad = wallpaper; bad["events"].push_back("move");
    check(!f.load(bad), "one package must not share a wallpaper and window scope");

    Json window = {
        {"version", 1}, {"events", {"move"}},
        {"environment", {{"processed", true}, {"channel", "weather"}, {"sample_state", true},
                         {"emit", "signal"}}},
        {"textures", {{"signal", {{"format", "rgba8"}, {"width", 1}, {"height", 1}}}}},
        {"passes", Json::array({raster("emit_signal", "update", "signal"),
                                raster("compose", "render", "output")})}};
    check(bool(f.load(window)), "window can emit and read matching state in separate phases");
    bad = window; bad["textures"]["signal"]["width"] = 2;
    check(!f.load(bad), "an emitter may not export a larger texture");
    bad = window; bad["environment"].erase("channel");
    check(!f.load(bad), "coordination cannot use an implicit channel");
}
void compute_fields_and_kernels() {
    Fixture f;
    f.file("seed.glsl", "void kernel(ivec3 id, inout float value) { value = float(id.x); }\n");
    f.file("step.glsl", "void kernel(ivec3 id, inout float a, inout float b) { a = hc_read_heat(id); b = a; }\n");
    f.file("illegal.glsl", "void kernel(ivec3 id, inout float value) { value = texture(hc_tex, vec2(0.0)).x; }\n");
    f.file("host.glsl", "void kernel(ivec3 id, inout float value) { value = hypr_wallpaper_original.x; }\n");
    f.file("oversized.glsl", std::string(1024 * 1024 + 1, ' '));
    f.file("render.frag", "layout(location=0) out vec4 color; void main(){color=texture(hypr_wallpaper_original,vec2(0.0));}\n");
    Json j = Json::parse(R"json({
        "version": 1,
        "events": ["wallpaper"],
        "environment": {"original": true},
        "parameters": {"enabled": {"type": "bool", "default": true}},
        "fields": {
            "scratch": {"type": "float", "domain": [7, 5], "lifetime": "transient"},
            "heat": {"type": "float", "domain": [7, 5], "ping_pong": true},
            "velocity": {"type": "vec2", "scale": 0.5},
            "other": {"type": "float", "domain": [7, 5]}
        },
        "passes": [
            {"name": "seed", "phase": "initialize", "kernel": "seed.glsl", "outputs": ["scratch"]},
            {"name": "evolve", "phase": "initialize", "kernel": "step.glsl",
             "inputs": {"heat": "scratch"}, "outputs": ["heat.write", "other"]},
            {"name": "present", "phase": "render", "vertex": "display.vert", "fragment": "render.frag",
             "inputs": {"heat": "heat.read"}, "outputs": ["output"]}
        ]
    })json");
    f.file("display.vert", "out vec2 hypr_uv; void main(){hypr_uv=vec2(0.0);gl_Position=vec4(0.0);}\n");
    auto loaded = f.load(j);
    check(bool(loaded), "typed fields and ordered compute passes load");
    check(loaded->fields.size() == 4 && loaded->passes[0].compute &&
            loaded->passes[0].kernel.find("kernel") != std::string::npos,
        "compute fields and kernel source survive loading");
    auto descriptor = kernel_descriptor(*loaded, loaded->passes[1]);
    check(descriptor.inputs.size() == 1 && descriptor.inputs[0].name == "heat" &&
            descriptor.inputs[0].type == compute::Type{compute::Element::Float,1} &&
            descriptor.outputs.size() == 2 && descriptor.outputs[0].type == descriptor.inputs[0].type &&
            descriptor.outputs[1].type == descriptor.inputs[0].type,
        "kernel descriptor uses typed aliases and ordered outputs");
    check(descriptor.parameters.size() == 7 && descriptor.parameters[0].name == "enabled" &&
            descriptor.parameters[0].type == compute::Type{compute::Element::Int,1} &&
            descriptor.parameters[1].name == "hypr_delta" &&
            descriptor.parameters[6].name == "hypr_seed",
        "kernel descriptor orders typed package parameters before timing uniforms");
    auto helper = compute::sample_source("heat", descriptor.inputs[0].type);
    check(bool(helper), "typed rendering helper generation");
    const auto assembled = shader_source(*loaded, loaded->passes[2].fragment, false, *helper);
    check(assembled.find("uniform sampler2D hypr_wallpaper_original") != std::string::npos &&
            assembled.find(*helper) != std::string::npos &&
            assembled.find(*helper) < assembled.find("layout(location=0)"),
        "field consumers may combine typed field helpers with host original helpers");

    auto bad = j;
    bad["fields"]["other"]["domain"] = {8,5};
    check(!f.load(bad), "compute outputs must share one domain");
    bad = j;
    bad["textures"] = {{"other", {{"width",1},{"height",1}}}};
    check(!f.load(bad), "fields share the texture resource namespace");
    bad = j;
    bad["fields"]["scratch"]["scale"] = 0.5;
    check(!f.load(bad), "field cannot declare both fixed domain and scale");
    bad = j;
    bad["passes"][1]["inputs"]["heat"] = "missing";
    check(!f.load(bad), "kernel cannot import undeclared or host resources");
    bad = j;
    bad["passes"][1]["clear"] = true;
    check(!f.load(bad), "compute passes cannot request raster clear");
    bad = j;
    bad["passes"][1]["outputs"] = {"output"};
    check(!f.load(bad), "compute passes cannot write presentation output");
    bad = j;
    bad["passes"][1]["kernel"] = "illegal.glsl";
    check(!f.load(bad), "kernel cannot use raster texture imports");
    bad = j;
    bad["passes"][1]["kernel"] = "host.glsl";
    check(!f.load(bad), "kernel cannot import host rendering resources");
    bad = j;
    bad["passes"][1]["kernel"] = "oversized.glsl";
    check(!f.load(bad), "kernel source file budget");
    Package invalid = *loaded;
    invalid.passes[1].clear = true;
    check(!validate_package(invalid), "programmatic compute pass must disable clearing");
    invalid = *loaded;
    invalid.passes[1].inputs[0].resource = {ResourceKind::Source, 0, Side::Single};
    check(!validate_package(invalid), "programmatic kernels cannot import host resources");
    invalid = *loaded;
    invalid.passes[2].outputs = {{ResourceKind::Field, 0, Side::Single}};
    check(!validate_package(invalid), "raster passes cannot write fields");
    invalid = *loaded;
    invalid.environment.state = Ref{ResourceKind::Field, 0, Side::Read};
    check(!validate_package(invalid), "programmatic fields cannot use environment export ABI");
    Package changed = *loaded;
    changed.passes[0].kernel += " ";
    check(changed != *loaded, "kernel source participates in package identity");
    bad = j;
    bad["passes"][1]["phase"] = "update";
    check(!f.load(bad), "transient field producer is scoped to its phase");

    Package programmatic = *loaded;
    programmatic.fields[0].type = {compute::Element::Float, 5};
    check(!validate_package(programmatic), "programmatic field type validation");
}
} // namespace
int main() {
    try {
        shorthand_and_abi();strict_schema();parameters();dependencies();assets_and_buffers();meshes_and_feedback();
        precision_and_transient_initialization();
        environment_contracts();
        compute_fields_and_kernels();
        std::cout << "PackageTests passed\n";return 0;
    } catch(const std::exception &error) {std::cerr << "PackageTests: " << error.what() << '\n';return 1;}
}
