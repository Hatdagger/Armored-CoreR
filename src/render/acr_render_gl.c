/* OpenGL 2.1 (fixed function) back end for acr_render.h. Every GL entry
 * point is loaded at run time, so nothing links against a GL library. */
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
typedef double GLdouble;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;

#define GL_DEPTH_BUFFER_BIT 0x0100
#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_TRIANGLES 0x0004
#define GL_LINES 0x0001
#define GL_DEPTH_TEST 0x0B71
#define GL_SCISSOR_TEST 0x0C11
#define GL_BLEND 0x0BE2
#define GL_FOG 0x0B60
#define GL_TEXTURE_2D 0x0DE1
#define GL_CULL_FACE 0x0B44
#define GL_LEQUAL 0x0203
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_ONE 1
#define GL_PROJECTION 0x1701
#define GL_MODELVIEW 0x1700
#define GL_VERTEX_ARRAY 0x8074
#define GL_COLOR_ARRAY 0x8076
#define GL_TEXTURE_COORD_ARRAY 0x8078
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
#define GL_GENERATE_MIPMAP 0x8191
#define GL_TEXTURE_MAX_LEVEL 0x813D
#define GL_POLYGON_OFFSET_FILL 0x8037
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_REPEAT 0x2901
#define GL_FOG_MODE 0x0B65
#define GL_FOG_START 0x0B63
#define GL_FOG_END 0x0B64
#define GL_FOG_COLOR 0x0B66
#define GL_SMOOTH 0x1D01
#define GL_TEXTURE_ENV 0x2300
#define GL_TEXTURE_ENV_MODE 0x2200
#define GL_MODULATE 0x2100
#define GL_COMBINE 0x8570
#define GL_COMBINE_RGB 0x8571
#define GL_RGB_SCALE 0x8573
#define GL_ALPHA_TEST 0x0BC0
#define GL_GREATER 0x0204

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
    X(void, MatrixMode, (GLenum)) \
    X(void, LoadMatrixf, (const GLfloat *)) \
    X(void, PushMatrix, (void)) \
    X(void, PopMatrix, (void)) \
    X(void, Translatef, (GLfloat, GLfloat, GLfloat)) \
    X(GLboolean, IsEnabled, (GLenum)) \
    X(void, LoadIdentity, (void)) \
    X(void, Ortho, (GLdouble, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble)) \
    X(void, EnableClientState, (GLenum)) \
    X(void, DisableClientState, (GLenum)) \
    X(void, VertexPointer, (GLint, GLenum, GLsizei, const void *)) \
    X(void, ColorPointer, (GLint, GLenum, GLsizei, const void *)) \
    X(void, TexCoordPointer, (GLint, GLenum, GLsizei, const void *)) \
    X(void, DrawArrays, (GLenum, GLint, GLsizei)) \
    X(void, BlendFunc, (GLenum, GLenum)) \
    X(void, DepthFunc, (GLenum)) \
    X(void, DepthMask, (GLboolean)) \
    X(void, Fogi, (GLenum, GLint)) \
    X(void, Fogf, (GLenum, GLfloat)) \
    X(void, Fogfv, (GLenum, const GLfloat *)) \
    X(void, GenTextures, (GLsizei, GLuint *)) \
    X(void, BindTexture, (GLenum, GLuint)) \
    X(void, TexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *)) \
    X(void, TexParameteri, (GLenum, GLenum, GLint)) \
    X(void, ReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *)) \
    X(void, ShadeModel, (GLenum)) \
    X(void, TexEnvi, (GLenum, GLenum, GLint)) \
    X(void, TexEnvf, (GLenum, GLenum, GLfloat)) \
    X(void, AlphaFunc, (GLenum, GLfloat)) \
    X(void, PolygonOffset, (GLfloat, GLfloat))

#define DECLARE(ret, name, args) typedef ret (GLAPI_ENTRY *pfn_##name) args; static pfn_##name gl##name;
GL_FUNCS(DECLARE)
#undef DECLARE

static int g_w, g_h;                 /* drawable size in pixels */
static uint32_t g_font;
static float g_hud_w;

int acr_render_init(acr_getproc getproc) {
    int ok = 1;
    /* memcpy, because ISO C has no cast from void * to a function pointer */
#define LOAD(ret, name, args) { void *p_ = getproc("gl" #name); memcpy(&gl##name, &p_, sizeof p_); if (!p_) ok = 0; }
    GL_FUNCS(LOAD)
#undef LOAD
    if (!ok) return 0;
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
    glShadeModel(GL_SMOOTH);
    return 1;
}

void acr_render_begin(int pixel_w, int pixel_h, uint8_t r, uint8_t g, uint8_t b) {
    g_w = pixel_w;
    g_h = pixel_h;
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, pixel_w, pixel_h);
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

static int g_fog_on;   /* the current 3D view has fog */

void acr_render_view3d(int x, int y, int w, int h, const float proj[16], const float view[16], const acr_fog *fog) {
    set_rect(x, y, w, h);
    glClear(GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(proj);
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(view);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(1);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (fog && fog->end > fog->start) {
        const GLfloat c[4] = { fog->r / 255.0f, fog->g / 255.0f, fog->b / 255.0f, 1.0f };
        glEnable(GL_FOG);
        g_fog_on = 1;
        glFogi(GL_FOG_MODE, GL_LINEAR);
        glFogf(GL_FOG_START, fog->start);
        glFogf(GL_FOG_END, fog->end);
        glFogfv(GL_FOG_COLOR, c);
    } else {
        glDisable(GL_FOG);
        g_fog_on = 0;
    }
}

static void draw(GLenum mode, const acr_vtx *v, int count, uint32_t texture) {
    if (count <= 0) return;
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3, GL_FLOAT, sizeof(acr_vtx), &v->x);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(acr_vtx), &v->r);
    if (texture) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, texture);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glTexCoordPointer(2, GL_FLOAT, sizeof(acr_vtx), &v->u);
    } else {
        glDisable(GL_TEXTURE_2D);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    }
    glDrawArrays(mode, 0, count);
}

void acr_render_tris(const acr_vtx *v, int count, uint32_t texture) { draw(GL_TRIANGLES, v, count, texture); }
void acr_render_lines(const acr_vtx *v, int count) { draw(GL_LINES, v, count, 0); }

void acr_render_mesh(const acr_vtx *v, int count, uint32_t texture, int flags) {
    if (count <= 0) return;
    if (texture && (flags & ACR_MESH_PS1_TEXEL)) {
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
        glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
        glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, 2.0f);
    }
    if (texture) {
        glEnable(GL_ALPHA_TEST);
        glAlphaFunc(GL_GREATER, 0.5f / 255.0f);
    }
    if (flags & (ACR_MESH_SEMI | ACR_MESH_ADD)) glDepthMask(0);
    const int fogged = (flags & ACR_MESH_ADD) && g_fog_on;
    if (flags & ACR_MESH_ADD) {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        if (fogged) glDisable(GL_FOG);
    }
    if (flags & ACR_MESH_OVERLAY) {
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(-1.0f, -4.0f);
    }
    draw(GL_TRIANGLES, v, count, texture);
    if (flags & ACR_MESH_OVERLAY) glDisable(GL_POLYGON_OFFSET_FILL);
    if (flags & ACR_MESH_ADD) {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        if (fogged) glEnable(GL_FOG);
    }
    if (flags & (ACR_MESH_SEMI | ACR_MESH_ADD)) glDepthMask(1);
    if (texture) glDisable(GL_ALPHA_TEST);
    if (texture && (flags & ACR_MESH_PS1_TEXEL)) {
        glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, 1.0f);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    }
}

void acr_render_sky(const acr_vtx *v, int count, uint32_t texture, const float eye[3]) {
    if (count <= 0) return;
    const GLboolean fog = glIsEnabled(GL_FOG);
    glDisable(GL_FOG);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glTranslatef(eye[0], eye[1], eye[2]);
    acr_render_mesh(v, count, texture, ACR_MESH_PS1_TEXEL);
    glPopMatrix();
    glDepthMask(1);
    glEnable(GL_DEPTH_TEST);
    if (fog) glEnable(GL_FOG);
}

float acr_render_hud(int x, int y, int w, int h) {
    set_rect(x, y, w, h);
    g_hud_w = h > 0 ? 480.0f * (float)w / (float)h : 640.0f;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, g_hud_w, 480, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_FOG);
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
    static acr_vtx q[6 * 256];
    int n = 0;
    for (const unsigned char *c = (const unsigned char *)text; *c && n < 6 * 256; ++c, x += 8) {
        if (*c <= 32 || *c > 126) continue;
        const int gi = *c - 33;
        const float u0 = (gi % 16) * 8 / 128.0f, v0 = (gi / 16) * 8 / 48.0f;
        const float u1 = u0 + 8 / 128.0f, v1 = v0 + 8 / 48.0f;
        q[n++] = hv(x, y, r, g, b, 255, u0, v0);
        q[n++] = hv(x + 8, y, r, g, b, 255, u1, v0);
        q[n++] = hv(x + 8, y + 8, r, g, b, 255, u1, v1);
        q[n++] = hv(x, y, r, g, b, 255, u0, v0);
        q[n++] = hv(x + 8, y + 8, r, g, b, 255, u1, v1);
        q[n++] = hv(x, y + 8, r, g, b, 255, u0, v1);
    }
    draw(GL_TRIANGLES, q, n, g_font);
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

uint32_t acr_render_texture(const uint8_t *rgba, int w, int h, int nearest) {
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    /* nearest 2: crisp up close, mipmapped further off (no shimmer on
     * floors far away); a few levels only, so a texture page's neighbouring
     * pictures barely bleed into each other. */
    if (nearest == 2) {
        glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, 1);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 3);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    nearest == 2 ? GL_NEAREST_MIPMAP_LINEAR : nearest ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
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
