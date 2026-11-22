"""Isolated perspective/depth renderer, with no compositor or screen access."""
import moderngl as gl
import numpy as np

UV = ((0., 0.), (1., 0.), (1., 1.), (0., 1.))
VERTEX = '''#version 330
in vec3 position; in vec2 texcoord, localcoord; in vec3 colour;
in float capture, light, highlight;
out vec2 uv, local; out vec3 rgb; out float value, edge; flat out int face;
void main() {
    float w = 3.4-position.z;
    // Invert y here: framebuffer readback is already in video scanline order.
    gl_Position=vec4(3.4*position.x, -3.4*position.y*(16./9.), 1.01*w-.25, w);
    uv=texcoord; local=localcoord; rgb=colour; value=light; edge=highlight; face=int(capture);
}'''
FRAGMENT = '''#version 330
uniform sampler2D desktop_a, desktop_b;
in vec2 uv, local; in vec3 rgb; in float value, edge; flat in int face; out vec4 f;
void main() {
    if (face>=0 && !gl_FrontFacing) discard;
    vec3 base = face==0 ? texture(desktop_a,uv).rgb : face==1 ? texture(desktop_b,uv).rgb : rgb;
    vec2 dist = min(local,1.-local)/max(fwidth(local),vec2(1e-6));
    float rim = (1.-smoothstep(.05,1.35,min(dist.x,dist.y))) * edge * .22;
    f=vec4(mix(base*value, vec3(.85,.89,.91),rim),1.);
}'''
SKY_VERTEX = '''#version 330
out vec2 pos;
void main() {
    pos = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2)*2.-1.;
    gl_Position=vec4(pos,0.,1.);
}'''
SKY_FRAGMENT = '''#version 330
in vec2 pos; out vec4 f;
void main() {
    float t = exp(-dot(pos*vec2(.88,.83)+vec2(.14,.19), pos*vec2(.88,.83)+vec2(.14,.19)));
    f = vec4(mix(vec3(.048,.060,.075), vec3(.119,.147,.173),t),1.);
}'''


class Scene:
    def __init__(self, images, size=(1920, 1080)):
        self.ctx = c = gl.create_standalone_context(backend='egl')
        self.size = size
        self.textures = []
        for i, image in enumerate(images):
            tex = c.texture(image.size, 3, image.tobytes(), alignment=1)
            tex.build_mipmaps()
            tex.filter = gl.LINEAR_MIPMAP_LINEAR, gl.LINEAR
            tex.anisotropy = min(8, c.max_anisotropy)
            tex.repeat_x = tex.repeat_y = False
            tex.use(i)
            self.textures.append(tex)
        self.sky = c.vertex_array(c.program(vertex_shader=SKY_VERTEX, fragment_shader=SKY_FRAGMENT), [])
        program = c.program(vertex_shader=VERTEX, fragment_shader=FRAGMENT)
        program['desktop_a'] = 0
        program['desktop_b'] = 1
        self.buffer = c.buffer(reserve=1024 * 1024, dynamic=True)
        self.mesh = c.vertex_array(program, [(self.buffer, '3f 2f 2f 3f 1f 1f 1f', 'position', 'texcoord',
                                                'localcoord', 'colour', 'capture', 'light', 'highlight')])
        samples = min(4, c.max_samples)
        self.target = c.framebuffer(c.renderbuffer(size, samples=samples), c.depth_renderbuffer(size, samples=samples))
        self.resolve = c.framebuffer(c.renderbuffer(size))
        c.front_face = 'ccw'
        self.verts = []

    def quad(self, vertices, material, uv=None, shade=1., edge=0.):
        capture, colour = ({'A': 0, 'B': 1}[material], (1.,1.,1.)) if isinstance(material, str) else (-1, material)
        coords = UV if uv is None else uv
        lights = (shade,)*4 if np.isscalar(shade) else shade
        for i in (0,1,2,0,2,3):
            self.verts.append((*vertices[i], *coords[i], *UV[i], *colour, capture, lights[i], edge))

    def frame(self, draw, progress):
        self.verts.clear()
        draw(self, progress)
        self.buffer.write(np.asarray(self.verts, dtype='f4'))
        self.target.use()
        self.target.clear(depth=1)
        self.ctx.disable(gl.DEPTH_TEST)
        self.sky.render(vertices=3)
        self.ctx.enable(gl.DEPTH_TEST)
        self.mesh.render(vertices=len(self.verts))
        self.ctx.copy_framebuffer(self.resolve, self.target)
        return self.resolve.read(components=3, alignment=1)
