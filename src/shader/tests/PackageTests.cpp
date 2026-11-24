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
} // namespace
int main() {
    try {
        shorthand_and_abi();strict_schema();parameters();dependencies();assets_and_buffers();meshes_and_feedback();
        precision_and_transient_initialization();
        std::cout << "PackageTests passed\n";return 0;
    } catch(const std::exception &error) {std::cerr << "PackageTests: " << error.what() << '\n';return 1;}
}
