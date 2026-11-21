#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>

#include "animations/burn/BurnGroundDebris.hpp"
#include "animations/burn/BurnGroundShader.hpp"
#include "gl/Storage.hpp"

namespace {
    constexpr GLsizei STRIP_VERTICES = 10;

    constexpr const char *VERTEX = R"GLSL(#version 300 es
precision highp float;
layout(location=0) in vec4 body;     // center relative to screen, radius, rotation
layout(location=1) in vec4 pigment;  // whole-output birth UV, signed fold, normalized age
layout(location=2) in vec3 style;    // char depth, shape seed, kind (paper, smoke, ember)
uniform vec4 clipTransform;          // clip scale xy, offset zw
uniform vec2 screenScale;
out vec2 local;
out vec2 source;
flat out vec4 flake;
flat out float kind;
void main() {
    // Five columns keep the projected curl curved rather than shearing a whole quad.
    local=vec2(float(gl_VertexID>>1)*0.5-1.0,float(gl_VertexID&1)*2.0-1.0);
    vec2 projected=local;
    source=pigment.xy;
    if(style.z<0.5) {
        // All corners (even before folding) are inside the logical bounding radius.
        vec2 sheet=local*mix(vec2(0.68),vec2(0.94,0.34),style.y);
        source+=sheet*body.z*screenScale;
        float fold=pigment.z;
        float turn=abs(fold);
        projected=vec2(sheet.x*(1.0-0.47*turn)+0.24*fold*sheet.x*sheet.x,
                       sheet.y*(1.0-0.11*turn));
    }
    float s=sin(body.w), c=cos(body.w);
    vec2 point=body.xy+mat2(c,s,-s,c)*projected*body.z;
    // In the work FBO, increasing framebuffer y is increasing world y.
    gl_Position=vec4(point*clipTransform.xy+clipTransform.zw,0.0,1.0);
    flake=vec4(pigment.zw,style.xy);
    kind=style.z;
}
)GLSL";

    constexpr const char *FRAGMENT_HEADER = R"GLSL(#version 300 es
precision highp float;
uniform sampler2D wallpaper;
uniform vec2 wallpaperOrigin;
uniform mat2 wallpaperBasis;
in vec2 local;
in vec2 source;
flat in vec4 flake;
flat in float kind;
out vec4 color;
)GLSL";

    constexpr const char *FRAGMENT_BODY = R"GLSL(
float tooth(float p) {
    return abs(fract(p)*2.0-1.0);
}

float dust(vec2 cell) {
    vec3 p=fract(cell.xyx*0.1031);
    p+=dot(p,p.yzx+33.33);
    return fract((p.x+p.y)*p.z);
}

vec4 paper(vec2 p, float fold, float life, float charDepth, float seed) {
    // Unequal torn lips and tips make both narrow strips and chipped, crooked flakes.
    float skew=(fract(seed*7.31)-0.5)*0.43;
    float y=p.y-skew*p.x;
    float top=0.57-0.17*p.x+0.15*tooth(p.x*3.73+seed);
    float bottom=0.58+0.14*p.x+0.17*tooth(p.x*2.61+seed*5.0);
    float left=0.84-abs(y+0.24)*0.37-0.11*tooth(y*3.37+seed);
    float right=0.87-abs(y-0.20)*0.31-0.08*tooth(y*4.68-seed);
    float edge=max(max(y-top,-y-bottom),max(-p.x-left,p.x-right));

    float late=smoothstep(0.45,0.95,life);
    float grain=dust(floor(p*vec2(11.0,7.0)+17.0*seed));
    // The lip frays into ash first; the middle retains recognizable wallpaper print.
    edge+=grain*(0.035+0.29*late)*smoothstep(-0.46,-0.04,edge);
    vec2 pixel=fwidth(p);
    float aa=clamp(0.55*max(pixel.x,pixel.y),0.025,0.24);
    float coverage=1.0-smoothstep(-aa,aa,edge);
    vec2 chip=vec2(0.74+0.15*late,-0.39+0.24*seed-0.12*late);
    float crumb=(1.0-smoothstep(0.055,0.095+0.4*aa,length(p-chip)))*late*0.65;
    float ashAlpha=crumb*(1.0-coverage);
    float fade=1.0-smoothstep(0.53,1.0,life);
    if((coverage+ashAlpha)*fade<=0.0) discard;

    vec3 lit=vec3(0.0);
    if(coverage>0.0) {
        // Lookup stays attached to the *birth* paper, never the moving display position.
        // The isolated wallpaper framebuffer has only level zero.
        vec2 uv=wallpaperOrigin+wallpaperBasis*clamp(source,0.0,1.0);
        vec3 print=textureLod(wallpaper,uv,0.0).rgb;
        float rim=smoothstep(-0.37,0.05,edge);
        float depth=mix(charDepth,1.0,rim*0.79+late*0.13);
        float ashing=late*(0.28+0.72*grain)*smoothstep(-0.52,0.05,edge);
        vec3 charred=burnPrintedChar(print,depth,ashing);

        // Signed curvature swings the crease between lit and shadowed as the paper tumbles.
        float curl=fold*(0.92*p.x+0.22*y+0.19);
        float light=0.98+0.32*curl-0.14*abs(fold);
        float crease=1.0-smoothstep(0.035,0.30,abs(p.x+0.22*y-0.20*fold));
        lit=min(charred*light+vec3(0.17,0.128,0.09)*abs(fold)*crease*(0.6+rim*0.4),
                vec3(1.0));
    }
    return vec4((lit*coverage+vec3(0.29,0.25,0.21)*ashAlpha)*fade,
                (coverage+ashAlpha)*fade);
}

vec4 smoke(vec2 p, float life, float seed, float radial) {
    vec2 drift=vec2(0.19*(seed-0.5),-0.15-0.17*life);
    vec2 q=p-drift;
    float first=max(1.0-dot(q,q)*1.43,0.0);
    q=(p+drift*1.4)*vec2(1.25,1.0);
    float second=max(1.0-dot(q,q)*2.1,0.0);
    float density=max(first*first,second*second*0.74);
    float fade=smoothstep(0.0,0.12,life)*(1.0-smoothstep(0.13,0.91,life));
    float alpha=0.17*density*fade*radial;
    return vec4(vec3(0.27,0.255,0.24)*alpha,alpha);
}

vec4 ember(float life, float seed, float radial) {
    float glow=radial*radial;
    float pulse=0.79+0.21*sin(life*(21.0+10.0*seed)+31.0*seed);
    float alpha=glow*(1.0-life)*pulse;
    vec3 hot=mix(vec3(0.91,0.22,0.028),vec3(1.0,0.74,0.32),glow*(1.0-life));
    return vec4(hot*alpha,alpha);
}

void main() {
    if(kind<0.5) {
        color=paper(local,flake.x,flake.y,flake.z,flake.w);
    } else {
        float radial=max(1.0-dot(local,local),0.0);
        if(radial==0.0) discard;
        color=kind<1.5 ? smoke(local,flake.y,flake.w,radial) : ember(flake.y,flake.w,radial);
    }
}
)GLSL";

    template <std::size_t N>
    GLuint compile_shader(
        GLenum type, const std::array<const char *, N> &sources, const GLint *lengths = nullptr) {
        const GLuint SHADER = glCreateShader(type);
        if (SHADER == 0) {
            return 0;
        }
        glShaderSource(SHADER, static_cast<GLsizei>(N), sources.data(), lengths);
        glCompileShader(SHADER);
        GLint compiled = GL_FALSE;
        glGetShaderiv(SHADER, GL_COMPILE_STATUS, &compiled);
        if (compiled != GL_FALSE) {
            return SHADER;
        }
        std::array<GLchar, 1024> log{};
        glGetShaderInfoLog(SHADER, static_cast<GLsizei>(log.size()), nullptr, log.data());
        (void)std::fprintf(stderr, "hyprimate: Burn ground debris shader: %s\n", log.data());
        glDeleteShader(SHADER);
        return 0;
    }

    GLuint make_program() {
        const GLuint VERTEX_SHADER = compile_shader(GL_VERTEX_SHADER, std::array{VERTEX});
        if (VERTEX_SHADER == 0) {
            return 0;
        }
        constexpr auto CHAR = burn_ground_shader::PRINTED_CHAR;
        const std::array<GLint, 3> LENGTHS{-1, static_cast<GLint>(CHAR.size()), -1};
        const GLuint FRAGMENT_SHADER = compile_shader(GL_FRAGMENT_SHADER,
            // NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage): glShaderSource gets the
            // explicit segment length.
            std::array{FRAGMENT_HEADER, CHAR.data(), FRAGMENT_BODY},
            LENGTHS.data());
        const GLuint PROGRAM = FRAGMENT_SHADER != 0 ? glCreateProgram() : 0;
        GLint linked = GL_FALSE;
        if (PROGRAM != 0) {
            glAttachShader(PROGRAM, VERTEX_SHADER);
            glAttachShader(PROGRAM, FRAGMENT_SHADER);
            glLinkProgram(PROGRAM);
            glGetProgramiv(PROGRAM, GL_LINK_STATUS, &linked);
            glDetachShader(PROGRAM, VERTEX_SHADER);
            glDetachShader(PROGRAM, FRAGMENT_SHADER);
        }
        glDeleteShader(VERTEX_SHADER);
        if (FRAGMENT_SHADER != 0) {
            glDeleteShader(FRAGMENT_SHADER);
        }
        if (linked != GL_FALSE) {
            return PROGRAM;
        }
        if (PROGRAM != 0) {
            std::array<GLchar, 1024> log{};
            glGetProgramInfoLog(PROGRAM, static_cast<GLsizei>(log.size()), nullptr, log.data());
            (void)std::fprintf(stderr, "hyprimate: Burn ground debris program: %s\n", log.data());
            glDeleteProgram(PROGRAM);
        }
        return 0;
    }
}  // namespace

bool CBurnGroundDebris::initialize() {
    if (m_program != 0) {
        return true;
    }
    m_program = make_program();
    if (m_program == 0) {
        return false;
    }
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_instances);
    if (m_vao == 0 || m_instances == 0) {
        release();
        return false;
    }
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_instances);
    if (!hyprimate::gl::allocate_buffer(
            GL_ARRAY_BUFFER, sizeof(Instance) * MAX_INSTANCES, nullptr, GL_STREAM_DRAW)) {
        release();
        return false;
    }
    constexpr std::array<GLint, 3> ATTRIBUTE_SIZES{4, 4, 3};
    for (GLuint index = 0; index < ATTRIBUTE_SIZES.size(); ++index) {
        glEnableVertexAttribArray(index);
        glVertexAttribPointer(index,
            ATTRIBUTE_SIZES[index],
            GL_FLOAT,
            GL_FALSE,
            static_cast<GLsizei>(sizeof(Instance)),
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr):
            // GLES interprets this as an offset into the bound instance buffer.
            reinterpret_cast<const void *>(static_cast<std::size_t>(index) * 4 * sizeof(GLfloat)));
        glVertexAttribDivisor(index, 1);
    }
    m_clip_transform = glGetUniformLocation(m_program, "clipTransform");
    m_screen_scale = glGetUniformLocation(m_program, "screenScale");
    m_origin = glGetUniformLocation(m_program, "wallpaperOrigin");
    m_basis = glGetUniformLocation(m_program, "wallpaperBasis");
    const GLint WALLPAPER = glGetUniformLocation(m_program, "wallpaper");
    glUseProgram(m_program);
    glUniform1i(WALLPAPER, 0);
    return true;
}

void CBurnGroundDebris::release() {
    if (m_vao != 0) {
        glDeleteVertexArrays(1, &m_vao);
    }
    if (m_instances != 0) {
        glDeleteBuffers(1, &m_instances);
    }
    if (m_program != 0) {
        glDeleteProgram(m_program);
    }
    m_program = m_vao = m_instances = 0;
}

CBurnGroundDebris::Instance CBurnGroundDebris::pack(
    const CBurnGround::Particle &particle, hyprimate::Box screen) {
    using Kind = CBurnGround::Particle::Kind;
    static_assert(static_cast<float>(Kind::Paper) == 0.F &&
                  static_cast<float>(Kind::Smoke) == 1.F && static_cast<float>(Kind::Ember) == 2.F);
    const bool PAPER = particle.kind == Kind::Paper;
    return {
        static_cast<float>(particle.position.x - screen.x),
        static_cast<float>(particle.position.y - screen.y),
        static_cast<float>(particle.size),
        static_cast<float>(particle.angle),
        PAPER ? static_cast<float>((particle.source.x - screen.x) / screen.w) : 0.F,
        PAPER ? static_cast<float>((particle.source.y - screen.y) / screen.h) : 0.F,
        static_cast<float>(std::clamp(particle.fold, -1.0, 1.0)),
        static_cast<float>(std::clamp(particle.age / particle.lifetime, 0.0, 1.0)),
        static_cast<float>(std::clamp(particle.char_depth, 0.0, 1.0)),
        static_cast<float>(std::clamp(particle.shape, 0.0, 1.0)),
        static_cast<float>(particle.kind),
    };
}

void CBurnGroundDebris::draw(std::span<const Instance> instances,
    hyprimate::Box viewport,
    hyprimate::Box screen,
    hyprimate::Vec2 origin,
    hyprimate::Vec2 axis_x,
    hyprimate::Vec2 axis_y) const {
    if (m_program == 0 || instances.empty() || instances.size() > MAX_INSTANCES ||
        !hyprimate::is_usable(viewport) || !hyprimate::is_usable(screen)) {
        return;
    }

    const std::array<GLfloat, 4> BASIS{static_cast<float>(axis_x.x),
        static_cast<float>(axis_x.y),
        static_cast<float>(axis_y.x),
        static_cast<float>(axis_y.y)};
    const double CLIP_SCALE_X = 2.0 / viewport.w;
    const double CLIP_SCALE_Y = 2.0 / viewport.h;
    glUseProgram(m_program);
    glUniform4f(m_clip_transform,
        static_cast<float>(CLIP_SCALE_X),
        static_cast<float>(CLIP_SCALE_Y),
        static_cast<float>((screen.x - viewport.x) * CLIP_SCALE_X - 1.0),
        static_cast<float>((screen.y - viewport.y) * CLIP_SCALE_Y - 1.0));
    glUniform2f(
        m_screen_scale, static_cast<float>(1.0 / screen.w), static_cast<float>(1.0 / screen.h));
    glUniform2f(m_origin, static_cast<float>(origin.x), static_cast<float>(origin.y));
    glUniformMatrix2fv(m_basis, 1, GL_FALSE, BASIS.data());
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_instances);
    glBufferSubData(
        GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(instances.size_bytes()), instances.data());
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArraysInstanced(
        GL_TRIANGLE_STRIP, 0, STRIP_VERTICES, static_cast<GLsizei>(instances.size()));
}
