/* OpenGL ES back end for acr_render.h (Android, and any other device that
 * only has GLES). It draws exactly what the OpenGL 2.1 back end draws
 * (acr_render_gl.c): GLES has no fixed-function pipeline, so one small
 * shader does the old fixed-function steps the game uses (vertex colour x
 * texture, the PS1's double-bright texels, alpha test, linear fog) and the
 * matrices live here instead of in GL. Works on a GLES 3.0 context, and on
 * GLES 2.0 too (mipmaps then only on power-of-two textures). Every GL entry
 * point is loaded at run time, as in the desktop back end. */
#include "acr_render.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "acr_font8x8.inc"

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef float GLfloat;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef char GLchar;
typedef unsigned char GLubyte;

#define GL_DEPTH_BUFFER_BIT 0x0100
#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_TRIANGLES 0x0004
#define GL_LINES 0x0001
#define GL_DEPTH_TEST 0x0B71
#define GL_SCISSOR_TEST 0x0C11
#define GL_BLEND 0x0BE2
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE0 0x84C0
#define GL_CULL_FACE 0x0B44
#define GL_LEQUAL 0x0203
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_ONE 1
#define GL_FLOAT 0x1406
#define GL_UNSIGNED_BYTE 0x1401
#define GL_RGBA 0x1908
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_NEAREST_MIPMAP_LINEAR 0x2702
#define GL_TEXTURE_MAX_LEVEL 0x813D
#define GL_POLYGON_OFFSET_FILL 0x8037
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_PACK_ALIGNMENT 0x0D05
#define GL_VERSION 0x1F02
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82

#define GLAPI_ENTRY
#if defined(_WIN32)
#undef GLAPI_ENTRY
#define GLAPI_ENTRY __stdcall
#endif

#define GL_FUNCS(X) \
    X(void, Viewport, (GLint, GLint, GLsizei, GLsizei)) \
    X(void, Scissor, (GLint, GLint, GLsizei, GLsizei)) \
    X(void, Enable, (GLenum)) \
    X(void, Disable, (GLenum)) \
    X(void, ClearColor, (GLfloat, GLfloat, GLfloat, GLfloat)) \
    X(void, Clear, (GLbitfield)) \
    X(void, BlendFunc, (GLenum, GLenum)) \
    X(void, DepthFunc, (GLenum)) \
    X(void, DepthMask, (GLboolean)) \
    X(void, PolygonOffset, (GLfloat, GLfloat)) \
    X(void, PixelStorei, (GLenum, GLint)) \
    X(void, GenTextures, (GLsizei, GLuint *)) \
    X(void, BindTexture, (GLenum, GLuint)) \
    X(void, ActiveTexture, (GLenum)) \
    X(void, TexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *)) \
    X(void, TexParameteri, (GLenum, GLenum, GLint)) \
    X(void, GenerateMipmap, (GLenum)) \
    X(void, ReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *)) \
    X(const GLubyte *, GetString, (GLenum)) \
    X(GLuint, CreateShader, (GLenum)) \
    X(void, ShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *)) \
    X(void, CompileShader, (GLuint)) \
    X(void, GetShaderiv, (GLuint, GLenum, GLint *)) \
    X(GLuint, CreateProgram, (void)) \
    X(void, AttachShader, (GLuint, GLuint)) \
    X(void, BindAttribLocation, (GLuint, GLuint, const GLchar *)) \
    X(void, LinkProgram, (GLuint)) \
    X(void, GetProgramiv, (GLuint, GLenum, GLint *)) \
    X(void, UseProgram, (GLuint)) \
    X(GLint, GetUniformLocation, (GLuint, const GLchar *)) \
    X(void, Uniform1i, (GLint, GLint)) \
    X(void, Uniform1f, (GLint, GLfloat)) \
    X(void, Uniform2f, (GLint, GLfloat, GLfloat)) \
    X(void, Uniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat)) \
    X(void, UniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat *)) \
    X(void, EnableVertexAttribArray, (GLuint)) \
    X(void, DisableVertexAttribArray, (GLuint)) \
    X(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *)) \
    X(void, DrawArrays, (GLenum, GLint, GLsizei))

#define DECLARE(ret, name, args) typedef ret (GLAPI_ENTRY *pfn_##name) args; static pfn_##name gl##name;
GL_FUNCS(DECLARE)
#undef DECLARE

/* One shader for everything. GLSL ES 1.00, which GLES 2 and 3 both run.
 * The fixed-function steps it stands in for, in GL's order:
 *   texture env MODULATE  rgba = texel x vertex colour
 *   COMBINE RGB_SCALE 2   rgb x 2 (PS1 texels: colour 128 = texel as is)
 *   alpha test GREATER    drop the fragment when alpha <= the reference
 *   linear fog            rgb toward the fog colour by eye depth */
static const char *k_vs =
    "uniform mat4 u_proj;\n"
    "uniform mat4 u_mv;\n"
    "uniform vec2 u_fog;\n"            /* start, end; end <= start = off */
    "attribute vec3 a_pos;\n"
    "attribute vec4 a_col;\n"
    "attribute vec2 a_uv;\n"
    "varying vec4 v_col;\n"
    "varying vec2 v_uv;\n"
    "varying float v_fog;\n"
    "void main() {\n"
    "    vec4 e = u_mv * vec4(a_pos, 1.0);\n"
    "    gl_Position = u_proj * e;\n"
    "    v_col = a_col;\n"
    "    v_uv = a_uv;\n"
    "    v_fog = u_fog.y > u_fog.x ? clamp((u_fog.y - abs(e.z)) / (u_fog.y - u_fog.x), 0.0, 1.0) : 1.0;\n"
    "}\n";
static const char *k_fs =
    "precision mediump float;\n"
    "uniform sampler2D u_tex;\n"
    "uniform float u_scale;\n"          /* 1, or 2 for PS1 texels */
    "uniform float u_alpha_ref;\n"      /* < 0: no alpha test */
    "uniform vec4 u_fog_col;\n"
    "varying vec4 v_col;\n"
    "varying vec2 v_uv;\n"
    "varying float v_fog;\n"
    "void main() {\n"
    "    vec4 c = texture2D(u_tex, v_uv) * v_col;\n"
    "    c.rgb = min(c.rgb * u_scale, vec3(1.0));\n"
    "    if (c.a <= u_alpha_ref) discard;\n"
    "    c.rgb = mix(u_fog_col.rgb, c.rgb, v_fog);\n"
    "    gl_FragColor = c;\n"
    "}\n";

enum { A_POS = 0, A_COL = 1, A_UV = 2 };

static GLuint g_prog, g_white;
static GLint u_proj, u_mv, u_fog, u_tex, u_scale, u_alpha_ref, u_fog_col;
static int g_es3;                    /* GLES 3: mipmaps on any texture size */
static int g_w, g_h;                 /* drawable size in pixels */
static uint32_t g_font;
static float g_hud_w;

/* The fixed-function state the desktop back end keeps in GL. */
static float g_proj[16], g_mv[16];
static float g_fog_start, g_fog_end, g_fog_rgb[3];
static int g_fog_on;                 /* the current 3D view has fog */
static int g_fog_enabled;            /* fog applies to draws right now */
static float g_scale = 1.0f, g_alpha_ref = -1.0f;

static void identity(float m[16]) {
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static GLuint compile(GLenum type, const char *src) {
    const GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    return ok ? s : 0;
}

int acr_render_init(acr_getproc getproc) {
    int ok = 1;
    /* memcpy, because ISO C has no cast from void * to a function pointer */
#define LOAD(ret, name, args) { void *p_ = getproc("gl" #name); memcpy(&gl##name, &p_, sizeof p_); if (!p_) ok = 0; }
    GL_FUNCS(LOAD)
#undef LOAD
    if (!ok) return 0;
    const char *ver = (const char *)glGetString(GL_VERSION);
    g_es3 = ver && strstr(ver, "OpenGL ES ") && ver[10] >= '3';
    const GLuint vs = compile(GL_VERTEX_SHADER, k_vs), fs = compile(GL_FRAGMENT_SHADER, k_fs);
    if (!vs || !fs) return 0;
    g_prog = glCreateProgram();
    glAttachShader(g_prog, vs);
    glAttachShader(g_prog, fs);
    glBindAttribLocation(g_prog, A_POS, "a_pos");
    glBindAttribLocation(g_prog, A_COL, "a_col");
    glBindAttribLocation(g_prog, A_UV, "a_uv");
    glLinkProgram(g_prog);
    GLint linked = 0;
    glGetProgramiv(g_prog, GL_LINK_STATUS, &linked);
    if (!linked) return 0;
    glUseProgram(g_prog);
    u_proj = glGetUniformLocation(g_prog, "u_proj");
    u_mv = glGetUniformLocation(g_prog, "u_mv");
    u_fog = glGetUniformLocation(g_prog, "u_fog");
    u_tex = glGetUniformLocation(g_prog, "u_tex");
    u_scale = glGetUniformLocation(g_prog, "u_scale");
    u_alpha_ref = glGetUniformLocation(g_prog, "u_alpha_ref");
    u_fog_col = glGetUniformLocation(g_prog, "u_fog_col");
    glUniform1i(u_tex, 0);
    glActiveTexture(GL_TEXTURE0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glEnableVertexAttribArray(A_POS);
    glEnableVertexAttribArray(A_COL);
    glEnableVertexAttribArray(A_UV);
    identity(g_proj);
    identity(g_mv);
    /* untextured draws sample a white texel: texel x colour = colour */
    static const uint8_t white[4] = { 255, 255, 255, 255 };
    g_white = acr_render_texture(white, 1, 1, 1);
    /* font atlas: 16 x 6 glyphs of 8x8, white with alpha */
    static uint8_t px[128 * 48 * 4];
    memset(px, 0, sizeof px);
    for (int g = 0; g < 94; ++g) {
        const int gx = (g % 16) * 8, gy = (g / 16) * 8;
        for (int row = 0; row < 8; ++row)
            for (int col = 0; col < 8; ++col)
                if (k_font8x8[g][row] & (1u << col)) {
                    uint8_t *p = &px[((gy + row) * 128 + gx + col) * 4];
                    p[0] = p[1] = p[2] = p[3] = 255;
                }
    }
    g_font = acr_render_texture(px, 128, 48, 1);
    return 1;
}

void acr_render_begin(int pixel_w, int pixel_h, uint8_t r, uint8_t g, uint8_t b) {
    g_w = pixel_w;
    g_h = pixel_h;
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, pixel_w, pixel_h);
    glDepthMask(1);
    glClearColor(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

/* Pixel rectangle with a top-left origin -> GL viewport and scissor. */
static void set_rect(int x, int y, int w, int h) {
    const int gy = g_h - (y + h);
    glViewport(x, gy, w, h);
    glScissor(x, gy, w, h);
    glEnable(GL_SCISSOR_TEST);
}

void acr_render_view3d(int x, int y, int w, int h, const float proj[16], const float view[16], const acr_fog *fog) {
    set_rect(x, y, w, h);
    glDepthMask(1);
    glClear(GL_DEPTH_BUFFER_BIT);
    memcpy(g_proj, proj, sizeof g_proj);
    memcpy(g_mv, view, sizeof g_mv);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (fog && fog->end > fog->start) {
        g_fog_on = g_fog_enabled = 1;
        g_fog_start = fog->start;
        g_fog_end = fog->end;
        g_fog_rgb[0] = fog->r / 255.0f; g_fog_rgb[1] = fog->g / 255.0f; g_fog_rgb[2] = fog->b / 255.0f;
    } else {
        g_fog_on = g_fog_enabled = 0;
    }
}

static void draw(GLenum mode, const acr_vtx *v, int count, uint32_t texture) {
    if (count <= 0) return;
    glUseProgram(g_prog);
    glUniformMatrix4fv(u_proj, 1, 0, g_proj);
    glUniformMatrix4fv(u_mv, 1, 0, g_mv);
    if (g_fog_enabled) glUniform2f(u_fog, g_fog_start, g_fog_end);
    else glUniform2f(u_fog, 0.0f, 0.0f);
    glUniform4f(u_fog_col, g_fog_rgb[0], g_fog_rgb[1], g_fog_rgb[2], 1.0f);
    /* texture env and alpha test only act on textured draws, as in GL */
    glUniform1f(u_scale, texture ? g_scale : 1.0f);
    glUniform1f(u_alpha_ref, texture ? g_alpha_ref : -1.0f);
    glBindTexture(GL_TEXTURE_2D, texture ? texture : g_white);
    glVertexAttribPointer(A_POS, 3, GL_FLOAT, 0, sizeof(acr_vtx), &v->x);
    glVertexAttribPointer(A_COL, 4, GL_UNSIGNED_BYTE, 1, sizeof(acr_vtx), &v->r);
    glVertexAttribPointer(A_UV, 2, GL_FLOAT, 0, sizeof(acr_vtx), &v->u);
    glDrawArrays(mode, 0, count);
}

void acr_render_tris(const acr_vtx *v, int count, uint32_t texture) { draw(GL_TRIANGLES, v, count, texture); }
void acr_render_lines(const acr_vtx *v, int count) { draw(GL_LINES, v, count, 0); }

void acr_render_mesh(const acr_vtx *v, int count, uint32_t texture, int flags) {
    if (count <= 0) return;
    if (texture && (flags & ACR_MESH_PS1_TEXEL)) g_scale = 2.0f;
    if (texture) g_alpha_ref = 0.5f / 255.0f;
    if (flags & (ACR_MESH_SEMI | ACR_MESH_ADD)) glDepthMask(0);
    const int fogged = (flags & ACR_MESH_ADD) && g_fog_enabled;
    if (flags & ACR_MESH_ADD) {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        if (fogged) g_fog_enabled = 0;
    }
    if (flags & ACR_MESH_OVERLAY) {
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(-1.0f, -4.0f);
    }
    draw(GL_TRIANGLES, v, count, texture);
    if (flags & ACR_MESH_OVERLAY) glDisable(GL_POLYGON_OFFSET_FILL);
    if (flags & ACR_MESH_ADD) {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        if (fogged) g_fog_enabled = 1;
    }
    if (flags & (ACR_MESH_SEMI | ACR_MESH_ADD)) glDepthMask(1);
    g_alpha_ref = -1.0f;
    g_scale = 1.0f;
}

void acr_render_sky(const acr_vtx *v, int count, uint32_t texture, const float eye[3]) {
    if (count <= 0) return;
    const int fog = g_fog_enabled;
    g_fog_enabled = 0;
    glDisable(GL_DEPTH_TEST);
    glDepthMask(0);
    /* glPushMatrix + glTranslatef(eye) */
    float saved[16];
    memcpy(saved, g_mv, sizeof saved);
    for (int r = 0; r < 4; ++r)
        g_mv[12 + r] = saved[r] * eye[0] + saved[4 + r] * eye[1] + saved[8 + r] * eye[2] + saved[12 + r];
    acr_render_mesh(v, count, texture, ACR_MESH_PS1_TEXEL);
    memcpy(g_mv, saved, sizeof saved);
    glDepthMask(1);
    glEnable(GL_DEPTH_TEST);
    g_fog_enabled = fog;
}

float acr_render_hud(int x, int y, int w, int h) {
    set_rect(x, y, w, h);
    g_hud_w = h > 0 ? 480.0f * (float)w / (float)h : 640.0f;
    /* glOrtho(0, hud_w, 480, 0, -1, 1) */
    identity(g_proj);
    g_proj[0] = 2.0f / g_hud_w;
    g_proj[5] = -2.0f / 480.0f;
    g_proj[10] = -1.0f;
    g_proj[12] = -1.0f;
    g_proj[13] = 1.0f;
    identity(g_mv);
    glDisable(GL_DEPTH_TEST);
    g_fog_enabled = 0;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    return g_hud_w;
}

static acr_vtx hv(float x, float y, uint8_t r, uint8_t g, uint8_t b, uint8_t a, float u, float v) {
    acr_vtx o = { x, y, 0, r, g, b, a, u, v };
    return o;
}

void acr_hud_rect(float x, float y, float w, float h, uint8_t r, uint8_t g, uint8_t b, uint8_t a, int filled) {
    if (filled) {
        const acr_vtx q[6] = { hv(x, y, r, g, b, a, 0, 0), hv(x + w, y, r, g, b, a, 0, 0),
                               hv(x + w, y + h, r, g, b, a, 0, 0), hv(x, y, r, g, b, a, 0, 0),
                               hv(x + w, y + h, r, g, b, a, 0, 0), hv(x, y + h, r, g, b, a, 0, 0) };
        draw(GL_TRIANGLES, q, 6, 0);
    } else {
        const acr_vtx l[8] = { hv(x, y, r, g, b, a, 0, 0), hv(x + w, y, r, g, b, a, 0, 0),
                               hv(x + w, y, r, g, b, a, 0, 0), hv(x + w, y + h, r, g, b, a, 0, 0),
                               hv(x + w, y + h, r, g, b, a, 0, 0), hv(x, y + h, r, g, b, a, 0, 0),
                               hv(x, y + h, r, g, b, a, 0, 0), hv(x, y, r, g, b, a, 0, 0) };
        draw(GL_LINES, l, 8, 0);
    }
}

void acr_hud_line(float x0, float y0, float x1, float y1, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    const acr_vtx l[2] = { hv(x0, y0, r, g, b, a, 0, 0), hv(x1, y1, r, g, b, a, 0, 0) };
    draw(GL_LINES, l, 2, 0);
}

void acr_hud_text(float x, float y, const char *text, uint8_t r, uint8_t g, uint8_t b) {
    acr_hud_text_size(x, y, text, 8.0f, r, g, b, 255);
}

void acr_hud_text_size(float x, float y, const char *text, float size, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    static acr_vtx q[6 * 256];
    int n = 0;
    for (const unsigned char *c = (const unsigned char *)text; *c && n < 6 * 256; ++c, x += size) {
        if (*c <= 32 || *c > 126) continue;
        const int gi = *c - 33;
        const float u0 = (gi % 16) * 8 / 128.0f, v0 = (gi / 16) * 8 / 48.0f;
        const float u1 = u0 + 8 / 128.0f, v1 = v0 + 8 / 48.0f;
        q[n++] = hv(x, y, r, g, b, a, u0, v0);
        q[n++] = hv(x + size, y, r, g, b, a, u1, v0);
        q[n++] = hv(x + size, y + size, r, g, b, a, u1, v1);
        q[n++] = hv(x, y, r, g, b, a, u0, v0);
        q[n++] = hv(x + size, y + size, r, g, b, a, u1, v1);
        q[n++] = hv(x, y + size, r, g, b, a, u0, v1);
    }
    draw(GL_TRIANGLES, q, n, g_font);
}

void acr_hud_sprite(uint32_t texture, int tex_w, int tex_h, float u, float v, float tw, float th,
                    float x, float y, float w, float h, uint8_t r, uint8_t g, uint8_t b, uint8_t a, int blend) {
    const float u0 = tex_w > 0 ? u / (float)tex_w : 0, u1 = tex_w > 0 ? (u + tw) / (float)tex_w : 0;
    const float v0 = tex_h > 0 ? v / (float)tex_h : 0, v1 = tex_h > 0 ? (v + th) / (float)tex_h : 0;
    /* ADD and HALF: the texture's alpha picks the drawn texels, the colour
     * is added (or averaged) with what is on the screen. */
    if (blend == ACR_HUD_ADD) { a = 255; glBlendFunc(GL_SRC_ALPHA, GL_ONE); }
    else if (blend == ACR_HUD_HALF) { a = 128; glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); }
    const acr_vtx q[6] = { hv(x, y, r, g, b, a, u0, v0), hv(x + w, y, r, g, b, a, u1, v0),
                           hv(x + w, y + h, r, g, b, a, u1, v1), hv(x, y, r, g, b, a, u0, v0),
                           hv(x + w, y + h, r, g, b, a, u1, v1), hv(x, y + h, r, g, b, a, u0, v1) };
    draw(GL_TRIANGLES, q, 6, texture);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

static int pow2(int n) { return n > 0 && (n & (n - 1)) == 0; }

/* acr_hud_sprite turned by `turns` quarter turns, as the PS1 HUD's icon
 * drawer does (VS 0x80065494): 1 = the picture turned a quarter
 * anticlockwise (its top-left texel at the bottom-left), 2 = half a turn,
 * 3 = a quarter clockwise. w, h are the size on screen. */
void acr_hud_sprite_turned(uint32_t texture, int tex_w, int tex_h, float u, float v, float tw, float th,
                           float x, float y, float w, float h, uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                           int blend, int turns) {
    const float u0 = u / (float)tex_w, u1 = (u + tw) / (float)tex_w;
    const float v0 = v / (float)tex_h, v1 = (v + th) / (float)tex_h;
    /* texture corners for the screen's top-left, top-right, bottom-right, bottom-left */
    const float cu[4][4] = { { u0, u1, u1, u0 }, { u1, u1, u0, u0 }, { u1, u0, u0, u1 }, { u0, u0, u1, u1 } };
    const float cv[4][4] = { { v0, v0, v1, v1 }, { v0, v1, v1, v0 }, { v1, v1, v0, v0 }, { v1, v0, v0, v1 } };
    const int t = turns & 3;
    if (blend == ACR_HUD_ADD) { a = 255; glBlendFunc(GL_SRC_ALPHA, GL_ONE); }
    else if (blend == ACR_HUD_HALF) { a = 128; glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); }
    const acr_vtx c0 = hv(x, y, r, g, b, a, cu[t][0], cv[t][0]), c1 = hv(x + w, y, r, g, b, a, cu[t][1], cv[t][1]),
                  c2 = hv(x + w, y + h, r, g, b, a, cu[t][2], cv[t][2]), c3 = hv(x, y + h, r, g, b, a, cu[t][3], cv[t][3]);
    const acr_vtx q[6] = { c0, c1, c2, c0, c2, c3 };
    draw(GL_TRIANGLES, q, 6, texture);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

uint32_t acr_render_texture(const uint8_t *rgba, int w, int h, int nearest) {
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    /* nearest 2: crisp up close, mipmapped further off (no shimmer on
     * floors far away); a few levels only, so a texture page's neighbouring
     * pictures barely bleed into each other. GLES 2 can only mipmap
     * power-of-two textures; others stay crisp. */
    int mip = nearest == 2 && (g_es3 || (pow2(w) && pow2(h)));
    if (mip) {
        if (g_es3) glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 3);
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    mip ? GL_NEAREST_MIPMAP_LINEAR : nearest ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

void acr_render_read(int w, int h, uint8_t *rgba) {
    glDisable(GL_SCISSOR_TEST);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

void acr_mat_perspective(float m[16], float fov_y, float aspect, float n, float f) {
    const float t = 1.0f / tanf(fov_y * 0.5f);
    memset(m, 0, 16 * sizeof(float));
    m[0] = t / aspect;
    m[5] = t;
    m[10] = (f + n) / (n - f);
    m[11] = -1.0f;
    m[14] = 2.0f * f * n / (n - f);
}

/* View matrix from an eye position and forward/up directions (right-handed:
 * the camera looks down -z in view space). */
void acr_mat_look(float m[16], const float e[3], const float fwd[3], const float upv[3]) {
    float f[3] = { fwd[0], fwd[1], fwd[2] };
    float fl = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    for (int i = 0; i < 3; ++i) f[i] /= fl;
    float s[3] = { f[1] * upv[2] - f[2] * upv[1], f[2] * upv[0] - f[0] * upv[2], f[0] * upv[1] - f[1] * upv[0] };
    float sl = sqrtf(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
    for (int i = 0; i < 3; ++i) s[i] /= sl;
    const float u[3] = { s[1] * f[2] - s[2] * f[1], s[2] * f[0] - s[0] * f[2], s[0] * f[1] - s[1] * f[0] };
    m[0] = s[0]; m[4] = s[1]; m[8] = s[2];
    m[1] = u[0]; m[5] = u[1]; m[9] = u[2];
    m[2] = -f[0]; m[6] = -f[1]; m[10] = -f[2];
    m[3] = m[7] = m[11] = 0;
    m[12] = -(s[0] * e[0] + s[1] * e[1] + s[2] * e[2]);
    m[13] = -(u[0] * e[0] + u[1] * e[1] + u[2] * e[2]);
    m[14] = f[0] * e[0] + f[1] * e[1] + f[2] * e[2];
    m[15] = 1;
}
