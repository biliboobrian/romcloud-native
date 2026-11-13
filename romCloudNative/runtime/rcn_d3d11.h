// RomCloud Native : Direct3D 11 et DXGI des jeux 64 bits, au-dessus d'OpenGL (ES 3.1+ sous Android,
// OpenGL de bureau 4.3+ sur PC pour les essais). Déclarations partagées entre :
//   rcn_d3d11_gl.c   fonctions OpenGL, contextes (un par thread du jeu, objets partagés), présentation ;
//   rcn_d3d11_com.c  objets COM : tables virtuelles, QueryInterface, références, données privées ;
//   rcn_dxgi.c       fabrique, adaptateur, écran, chaîne d'échange ;
//   rcn_d3d11.c      appareil : ressources, vues, nuanceurs, états ;
//   rcn_d3d11_ctx.c  contexte immédiat : liaisons, dessin, copies, Map ;
//   rcn_dxbc.c       nuanceurs DXBC (modèles 4 et 5) → GLSL.
//
// Conventions : l'image est dessinée retournée verticalement (gl_Position.y = -y), si bien que la
// ligne 0 d'une texture est en haut, comme sous Direct3D : textures, cibles de rendu, fenêtres
// d'affichage et rectangles de découpe gardent leurs coordonnées Direct3D. La profondeur passe de
// [0, w] à [-w, w] dans le nuanceur de sommets. Seule la présentation retourne l'image.
#ifndef RCN_D3D11_H
#define RCN_D3D11_H

#include "rcn_internal.h"

#if defined(_WIN32)
#define GLAPIENTRY __stdcall
#else
#define GLAPIENTRY
#endif

typedef unsigned int GLenum, GLuint, GLbitfield;
typedef int GLint, GLsizei;
typedef unsigned char GLboolean, GLubyte;
typedef float GLfloat;
typedef double GLdouble;
typedef char GLchar;
typedef ptrdiff_t GLintptr, GLsizeiptr;
typedef struct __GLsync *GLsync;
typedef uint64_t GLuint64;

// Fonctions OpenGL employées : X(type rendu, nom, paramètres).
#define D3DGL_FUNCS(X)                                                                                         \
    X(GLenum, glGetError, (void))                                                                              \
    X(const GLubyte *, glGetString, (GLenum))                                                                  \
    X(const GLubyte *, glGetStringi, (GLenum, GLuint))                                                         \
    X(void, glGetIntegerv, (GLenum, GLint *))                                                                  \
    X(void, glEnable, (GLenum))                                                                                \
    X(void, glDisable, (GLenum))                                                                               \
    X(void, glEnablei, (GLenum, GLuint))                                                                       \
    X(void, glDisablei, (GLenum, GLuint))                                                                      \
    X(void, glViewportIndexedf, (GLuint, GLfloat, GLfloat, GLfloat, GLfloat))                                  \
    X(void, glViewport, (GLint, GLint, GLsizei, GLsizei))                                                      \
    X(void, glDepthRangef, (GLfloat, GLfloat))                                                                 \
    X(void, glScissor, (GLint, GLint, GLsizei, GLsizei))                                                       \
    X(void, glColorMask, (GLboolean, GLboolean, GLboolean, GLboolean))                                         \
    X(void, glColorMaski, (GLuint, GLboolean, GLboolean, GLboolean, GLboolean))                                \
    X(void, glDepthMask, (GLboolean))                                                                          \
    X(void, glDepthFunc, (GLenum))                                                                             \
    X(void, glStencilFuncSeparate, (GLenum, GLenum, GLint, GLuint))                                            \
    X(void, glStencilOpSeparate, (GLenum, GLenum, GLenum, GLenum))                                             \
    X(void, glStencilMask, (GLuint))                                                                           \
    X(void, glBlendFuncSeparate, (GLenum, GLenum, GLenum, GLenum))                                             \
    X(void, glBlendFuncSeparatei, (GLuint, GLenum, GLenum, GLenum, GLenum))                                    \
    X(void, glBlendEquationSeparate, (GLenum, GLenum))                                                         \
    X(void, glBlendEquationSeparatei, (GLuint, GLenum, GLenum))                                                \
    X(void, glBlendColor, (GLfloat, GLfloat, GLfloat, GLfloat))                                                \
    X(void, glCullFace, (GLenum))                                                                              \
    X(void, glFrontFace, (GLenum))                                                                             \
    X(void, glPolygonOffset, (GLfloat, GLfloat))                                                               \
    X(void, glPolygonMode, (GLenum, GLenum))                                                                   \
    X(void, glClearBufferfv, (GLenum, GLint, const GLfloat *))                                                 \
    X(void, glClearBufferuiv, (GLenum, GLint, const GLuint *))                                                 \
    X(void, glClearBufferfi, (GLenum, GLint, GLfloat, GLint))                                                  \
    X(void, glGenBuffers, (GLsizei, GLuint *))                                                                 \
    X(void, glDeleteBuffers, (GLsizei, const GLuint *))                                                        \
    X(void, glBindBuffer, (GLenum, GLuint))                                                                    \
    X(void, glBufferData, (GLenum, GLsizeiptr, const void *, GLenum))                                          \
    X(void, glBufferSubData, (GLenum, GLintptr, GLsizeiptr, const void *))                                     \
    X(void, glBindBufferBase, (GLenum, GLuint, GLuint))                                                        \
    X(void, glBindBufferRange, (GLenum, GLuint, GLuint, GLintptr, GLsizeiptr))                                 \
    X(void *, glMapBufferRange, (GLenum, GLintptr, GLsizeiptr, GLbitfield))                                    \
    X(GLboolean, glUnmapBuffer, (GLenum))                                                                      \
    X(void, glCopyBufferSubData, (GLenum, GLenum, GLintptr, GLintptr, GLsizeiptr))                             \
    X(void, glGenTextures, (GLsizei, GLuint *))                                                                \
    X(void, glDeleteTextures, (GLsizei, const GLuint *))                                                       \
    X(void, glBindTexture, (GLenum, GLuint))                                                                   \
    X(void, glActiveTexture, (GLenum))                                                                         \
    X(void, glTexStorage2D, (GLenum, GLsizei, GLenum, GLsizei, GLsizei))                                       \
    X(void, glTexStorage3D, (GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLsizei))                              \
    X(void, glTexStorage2DMultisample, (GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLboolean))                 \
    X(void, glTexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *))    \
    X(void, glTexSubImage3D,                                                                                   \
      (GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, const void *))           \
    X(void, glCompressedTexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLsizei, const void *)) \
    X(void, glCompressedTexSubImage3D,                                                                         \
      (GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLsizei, const void *))          \
    X(void, glTexParameteri, (GLenum, GLenum, GLint))                                                          \
    X(void, glTexBuffer, (GLenum, GLenum, GLuint))                                                             \
    X(void, glTexBufferRange, (GLenum, GLenum, GLuint, GLintptr, GLsizeiptr))                                  \
    X(void, glGenerateMipmap, (GLenum))                                                                        \
    X(void, glGenSamplers, (GLsizei, GLuint *))                                                                \
    X(void, glDeleteSamplers, (GLsizei, const GLuint *))                                                       \
    X(void, glBindSampler, (GLuint, GLuint))                                                                   \
    X(void, glSamplerParameteri, (GLuint, GLenum, GLint))                                                      \
    X(void, glSamplerParameterf, (GLuint, GLenum, GLfloat))                                                    \
    X(void, glSamplerParameterfv, (GLuint, GLenum, const GLfloat *))                                           \
    X(void, glGenFramebuffers, (GLsizei, GLuint *))                                                            \
    X(void, glDeleteFramebuffers, (GLsizei, const GLuint *))                                                   \
    X(void, glBindFramebuffer, (GLenum, GLuint))                                                               \
    X(void, glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint))                                   \
    X(void, glFramebufferTextureLayer, (GLenum, GLenum, GLuint, GLint, GLint))                                 \
    X(void, glFramebufferTexture, (GLenum, GLenum, GLuint, GLint))                                             \
    X(GLenum, glCheckFramebufferStatus, (GLenum))                                                              \
    X(void, glDrawBuffers, (GLsizei, const GLenum *))                                                          \
    X(void, glReadBuffer, (GLenum))                                                                            \
    X(void, glBlitFramebuffer, (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum))   \
    X(void, glReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *))                            \
    X(void, glPixelStorei, (GLenum, GLint))                                                                    \
    X(void, glGenVertexArrays, (GLsizei, GLuint *))                                                            \
    X(void, glBindVertexArray, (GLuint))                                                                       \
    X(void, glEnableVertexAttribArray, (GLuint))                                                               \
    X(void, glDisableVertexAttribArray, (GLuint))                                                              \
    X(void, glVertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *))                  \
    X(void, glVertexAttribIPointer, (GLuint, GLint, GLenum, GLsizei, const void *))                            \
    X(void, glVertexAttribDivisor, (GLuint, GLuint))                                                           \
    X(void, glVertexAttrib4f, (GLuint, GLfloat, GLfloat, GLfloat, GLfloat))                                    \
    X(GLuint, glCreateShader, (GLenum))                                                                        \
    X(void, glShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *))                           \
    X(void, glCompileShader, (GLuint))                                                                         \
    X(void, glGetShaderiv, (GLuint, GLenum, GLint *))                                                          \
    X(void, glGetShaderInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *))                                        \
    X(void, glDeleteShader, (GLuint))                                                                          \
    X(GLuint, glCreateProgram, (void))                                                                         \
    X(void, glAttachShader, (GLuint, GLuint))                                                                  \
    X(void, glBindAttribLocation, (GLuint, GLuint, const GLchar *))                                            \
    X(void, glLinkProgram, (GLuint))                                                                           \
    X(void, glGetProgramiv, (GLuint, GLenum, GLint *))                                                         \
    X(void, glGetProgramInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *))                                       \
    X(void, glDeleteProgram, (GLuint))                                                                         \
    X(void, glUseProgram, (GLuint))                                                                            \
    X(GLuint, glGetUniformBlockIndex, (GLuint, const GLchar *))                                                \
    X(void, glUniformBlockBinding, (GLuint, GLuint, GLuint))                                                   \
    X(GLint, glGetUniformLocation, (GLuint, const GLchar *))                                                   \
    X(void, glUniform1i, (GLint, GLint))                                                                       \
    X(void, glDrawElementsInstanced, (GLenum, GLsizei, GLenum, const void *, GLsizei)) \
    X(void, glDrawArraysInstanced, (GLenum, GLint, GLsizei, GLsizei))                                          \
    X(void, glDrawElementsInstancedBaseVertex, (GLenum, GLsizei, GLenum, const void *, GLsizei, GLint))        \
    X(void, glDrawArraysInstancedBaseInstance, (GLenum, GLint, GLsizei, GLsizei, GLuint))                      \
    X(void, glDrawElementsInstancedBaseVertexBaseInstance, (GLenum, GLsizei, GLenum, const void *, GLsizei, GLint, GLuint)) \
    X(void, glFlush, (void))                                                                                   \
    X(void, glFinish, (void))                                                                                  \
    X(GLsync, glFenceSync, (GLenum, GLbitfield))                                                               \
    X(GLenum, glClientWaitSync, (GLsync, GLbitfield, GLuint64))                                                \
    X(void, glWaitSync, (GLsync, GLbitfield, GLuint64))                                                        \
    X(void, glDeleteSync, (GLsync))                                                                            \
    X(void, glCopyImageSubData,                                                                                \
      (GLuint, GLenum, GLint, GLint, GLint, GLint, GLuint, GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei)) \
    X(void, glDispatchCompute, (GLuint, GLuint, GLuint))                                                       \
    X(void, glBindImageTexture, (GLuint, GLuint, GLint, GLboolean, GLint, GLenum, GLenum))                     \
    X(void, glMemoryBarrier, (GLbitfield))                                                                     \
    X(void, glGenQueries, (GLsizei, GLuint *))                                                                 \
    X(void, glDeleteQueries, (GLsizei, const GLuint *))                                                        \
    X(void, glBeginQuery, (GLenum, GLuint))                                                                    \
    X(void, glEndQuery, (GLenum))                                                                              \
    X(void, glGetQueryObjectuiv, (GLuint, GLenum, GLuint *))                                                   \
    X(void, glDebugMessageCallback, (void *, const void *))

#define D3DGL_DECLARE(ret, name, params) extern ret(GLAPIENTRY *d3d_##name) params;
D3DGL_FUNCS(D3DGL_DECLARE)
#undef D3DGL_DECLARE
#define GL(name) d3d_##name

// Constantes OpenGL employées.
enum {
    GL_NO_ERROR = 0, GL_POINTS = 0, GL_LINES = 1, GL_LINE_STRIP = 3, GL_TRIANGLES = 4, GL_TRIANGLE_STRIP = 5,
    GL_LINES_ADJACENCY = 0xA, GL_LINE_STRIP_ADJACENCY = 0xB, GL_TRIANGLES_ADJACENCY = 0xC, GL_TRIANGLE_STRIP_ADJACENCY = 0xD,
    GL_PATCHES = 0xE, GL_NEVER = 0x200, GL_LESS, GL_EQUAL, GL_LEQUAL, GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_ALWAYS,
    GL_ZERO = 0, GL_ONE = 1, GL_SRC_COLOR = 0x300, GL_ONE_MINUS_SRC_COLOR, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
    GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA, GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR, GL_SRC_ALPHA_SATURATE,
    GL_CONSTANT_COLOR = 0x8001, GL_ONE_MINUS_CONSTANT_COLOR, GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA,
    GL_SRC1_ALPHA = 0x8589, GL_SRC1_COLOR = 0x88F9, GL_ONE_MINUS_SRC1_COLOR = 0x88FA, GL_ONE_MINUS_SRC1_ALPHA = 0x88FB,
    GL_FUNC_ADD = 0x8006, GL_MIN = 0x8007, GL_MAX = 0x8008, GL_FUNC_SUBTRACT = 0x800A, GL_FUNC_REVERSE_SUBTRACT = 0x800B,
    GL_FRONT = 0x404, GL_BACK = 0x405, GL_FRONT_AND_BACK = 0x408, GL_CW = 0x900, GL_CCW = 0x901,
    GL_CULL_FACE = 0xB44, GL_DEPTH_TEST = 0xB71, GL_STENCIL_TEST = 0xB90, GL_BLEND = 0xBE2, GL_SCISSOR_TEST = 0xC11,
    GL_POLYGON_OFFSET_FILL = 0x8037, GL_SAMPLE_ALPHA_TO_COVERAGE = 0x809E, GL_DEPTH_CLAMP = 0x864F,
    GL_FRAMEBUFFER_SRGB = 0x8DB9, GL_TEXTURE_CUBE_MAP_SEAMLESS = 0x884F, GL_PRIMITIVE_RESTART_FIXED_INDEX = 0x8D69,
    GL_KEEP = 0x1E00, GL_REPLACE = 0x1E01, GL_INCR = 0x1E02, GL_DECR = 0x1E03, GL_INVERT = 0x150A,
    GL_INCR_WRAP = 0x8507, GL_DECR_WRAP = 0x8508, GL_LINE = 0x1B01, GL_FILL = 0x1B02,
    GL_BYTE = 0x1400, GL_UNSIGNED_BYTE, GL_SHORT, GL_UNSIGNED_SHORT, GL_INT, GL_UNSIGNED_INT, GL_FLOAT,
    GL_HALF_FLOAT = 0x140B, GL_UNSIGNED_INT_2_10_10_10_REV = 0x8368, GL_INT_2_10_10_10_REV = 0x8D9F,
    GL_UNSIGNED_INT_10F_11F_11F_REV = 0x8C3B, GL_UNSIGNED_INT_5_9_9_9_REV = 0x8C3E, GL_UNSIGNED_SHORT_5_6_5 = 0x8363,
    GL_UNSIGNED_SHORT_5_5_5_1 = 0x8034, GL_UNSIGNED_SHORT_4_4_4_4 = 0x8033, GL_UNSIGNED_INT_24_8 = 0x84FA,
    GL_FLOAT_32_UNSIGNED_INT_24_8_REV = 0x8DAD,
    GL_RED = 0x1903, GL_RGB = 0x1907, GL_RGBA = 0x1908, GL_RG = 0x8227, GL_RED_INTEGER = 0x8D94, GL_RG_INTEGER = 0x8228,
    GL_RGB_INTEGER = 0x8D98, GL_RGBA_INTEGER = 0x8D99, GL_DEPTH_COMPONENT = 0x1902, GL_DEPTH_STENCIL = 0x84F9,
    GL_BGRA = 0x80E1,
    GL_ARRAY_BUFFER = 0x8892, GL_ELEMENT_ARRAY_BUFFER = 0x8893, GL_UNIFORM_BUFFER = 0x8A11, GL_COPY_READ_BUFFER = 0x8F36,
    GL_COPY_WRITE_BUFFER = 0x8F37, GL_TEXTURE_BUFFER = 0x8C2A, GL_SHADER_STORAGE_BUFFER = 0x90D2,
    GL_PIXEL_PACK_BUFFER = 0x88EB, GL_PIXEL_UNPACK_BUFFER = 0x88EC, GL_DRAW_INDIRECT_BUFFER = 0x8F3F,
    GL_STATIC_DRAW = 0x88E4, GL_DYNAMIC_DRAW = 0x88E8, GL_STREAM_DRAW = 0x88E0, GL_STREAM_READ = 0x88E1,
    GL_MAP_READ_BIT = 1, GL_MAP_WRITE_BIT = 2, GL_MAP_INVALIDATE_RANGE_BIT = 4, GL_MAP_INVALIDATE_BUFFER_BIT = 8,
    GL_MAP_UNSYNCHRONIZED_BIT = 0x20,
    GL_TEXTURE_2D = 0xDE1, GL_TEXTURE_3D = 0x806F, GL_TEXTURE_2D_ARRAY = 0x8C1A, GL_TEXTURE_CUBE_MAP = 0x8513,
    GL_TEXTURE_CUBE_MAP_POSITIVE_X = 0x8515, GL_TEXTURE_CUBE_MAP_ARRAY = 0x9009, GL_TEXTURE_2D_MULTISAMPLE = 0x9100,
    GL_TEXTURE0 = 0x84C0, GL_TEXTURE_MIN_FILTER = 0x2801, GL_TEXTURE_MAG_FILTER = 0x2800, GL_TEXTURE_WRAP_S = 0x2802,
    GL_TEXTURE_WRAP_T = 0x2803, GL_TEXTURE_WRAP_R = 0x8072, GL_TEXTURE_MIN_LOD = 0x813A, GL_TEXTURE_MAX_LOD = 0x813B,
    GL_TEXTURE_BASE_LEVEL = 0x813C, GL_TEXTURE_MAX_LEVEL = 0x813D, GL_TEXTURE_LOD_BIAS = 0x8501,
    GL_TEXTURE_COMPARE_MODE = 0x884C, GL_TEXTURE_COMPARE_FUNC = 0x884D, GL_COMPARE_REF_TO_TEXTURE = 0x884E,
    GL_TEXTURE_BORDER_COLOR = 0x1004, GL_TEXTURE_MAX_ANISOTROPY = 0x84FE, GL_TEXTURE_SWIZZLE_R = 0x8E42,
    GL_TEXTURE_SWIZZLE_G = 0x8E43, GL_TEXTURE_SWIZZLE_B = 0x8E44, GL_TEXTURE_SWIZZLE_A = 0x8E45,
    GL_DEPTH_STENCIL_TEXTURE_MODE = 0x90EA, GL_STENCIL_INDEX = 0x1901,
    GL_NEAREST = 0x2600, GL_LINEAR = 0x2601, GL_NEAREST_MIPMAP_NEAREST = 0x2700, GL_LINEAR_MIPMAP_NEAREST = 0x2701,
    GL_NEAREST_MIPMAP_LINEAR = 0x2702, GL_LINEAR_MIPMAP_LINEAR = 0x2703, GL_REPEAT = 0x2901, GL_CLAMP_TO_EDGE = 0x812F,
    GL_MIRRORED_REPEAT = 0x8370, GL_CLAMP_TO_BORDER = 0x812D, GL_MIRROR_CLAMP_TO_EDGE = 0x8743,
    GL_FRAMEBUFFER = 0x8D40, GL_READ_FRAMEBUFFER = 0x8CA8, GL_DRAW_FRAMEBUFFER = 0x8CA9, GL_COLOR_ATTACHMENT0 = 0x8CE0,
    GL_DEPTH_ATTACHMENT = 0x8D00, GL_STENCIL_ATTACHMENT = 0x8D20, GL_DEPTH_STENCIL_ATTACHMENT = 0x821A,
    GL_FRAMEBUFFER_COMPLETE = 0x8CD5, GL_NONE = 0, GL_COLOR = 0x1800, GL_DEPTH = 0x1801, GL_STENCIL = 0x1802,
    GL_COLOR_BUFFER_BIT = 0x4000, GL_DEPTH_BUFFER_BIT = 0x100, GL_STENCIL_BUFFER_BIT = 0x400,
    GL_PACK_ALIGNMENT = 0xD05, GL_UNPACK_ALIGNMENT = 0xCF5, GL_UNPACK_ROW_LENGTH = 0xCF2, GL_UNPACK_IMAGE_HEIGHT = 0x806E,
    GL_PACK_ROW_LENGTH = 0xD02,
    GL_VERTEX_SHADER = 0x8B31, GL_FRAGMENT_SHADER = 0x8B30, GL_GEOMETRY_SHADER = 0x8DD9, GL_COMPUTE_SHADER = 0x91B9,
    GL_COMPILE_STATUS = 0x8B81, GL_LINK_STATUS = 0x8B82, GL_INFO_LOG_LENGTH = 0x8B84, GL_INVALID_INDEX = 0xFFFFFFFFu,
    GL_VERSION = 0x1F02, GL_RENDERER = 0x1F01, GL_VENDOR = 0x1F00, GL_EXTENSIONS = 0x1F03, GL_NUM_EXTENSIONS = 0x821D,
    GL_SYNC_GPU_COMMANDS_COMPLETE = 0x9117, GL_SYNC_FLUSH_COMMANDS_BIT = 1, GL_TIMEOUT_IGNORED_LOW = 0xFFFFFFFFu,
    GL_SAMPLES_PASSED = 0x8914, GL_ANY_SAMPLES_PASSED = 0x8C2F, GL_TIME_ELAPSED = 0x88BF, GL_QUERY_RESULT = 0x8866,
    GL_QUERY_RESULT_AVAILABLE = 0x8867, GL_ALL_BARRIER_BITS = 0xFFFFFFFFu, GL_READ_WRITE = 0x88BA,
    GL_UNSIGNED_INT_8_8_8_8_REV = 0x8367,
    // Formats internes.
    GL_R8 = 0x8229, GL_R16 = 0x822A, GL_RG8 = 0x822B, GL_RG16 = 0x822C, GL_R16F = 0x822D, GL_R32F = 0x822E,
    GL_RG16F = 0x822F, GL_RG32F = 0x8230, GL_R8I = 0x8231, GL_R8UI = 0x8232, GL_R16I = 0x8233, GL_R16UI = 0x8234,
    GL_R32I = 0x8235, GL_R32UI = 0x8236, GL_RG8I = 0x8237, GL_RG8UI = 0x8238, GL_RG16I = 0x8239, GL_RG16UI = 0x823A,
    GL_RG32I = 0x823B, GL_RG32UI = 0x823C, GL_RGBA8 = 0x8058, GL_RGB10_A2 = 0x8059, GL_RGBA16 = 0x805B,
    GL_RGBA32F = 0x8814, GL_RGB32F = 0x8815, GL_RGBA16F = 0x881A, GL_RGB16F = 0x881B, GL_RGBA32UI = 0x8D70,
    GL_RGB32UI = 0x8D71, GL_RGBA16UI = 0x8D76, GL_RGBA8UI = 0x8D7C, GL_RGBA32I = 0x8D82, GL_RGB32I = 0x8D83,
    GL_RGBA16I = 0x8D88, GL_RGBA8I = 0x8D8E, GL_RGB10_A2UI = 0x906F, GL_R11F_G11F_B10F = 0x8C3A, GL_RGB9_E5 = 0x8C3D,
    GL_SRGB8_ALPHA8 = 0x8C43, GL_R8_SNORM = 0x8F94, GL_RG8_SNORM = 0x8F95, GL_RGBA8_SNORM = 0x8F97,
    GL_R16_SNORM = 0x8F98, GL_RG16_SNORM = 0x8F99, GL_RGBA16_SNORM = 0x8F9B, GL_RGB565 = 0x8D62, GL_RGB5_A1 = 0x8057,
    GL_RGBA4 = 0x8056, GL_DEPTH_COMPONENT16 = 0x81A5, GL_DEPTH_COMPONENT24 = 0x81A6, GL_DEPTH_COMPONENT32F = 0x8CAC,
    GL_DEPTH24_STENCIL8 = 0x88F0, GL_DEPTH32F_STENCIL8 = 0x8CAD,
    GL_COMPRESSED_RGBA_S3TC_DXT1_EXT = 0x83F1, GL_COMPRESSED_RGBA_S3TC_DXT3_EXT = 0x83F2,
    GL_COMPRESSED_RGBA_S3TC_DXT5_EXT = 0x83F3, GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT = 0x8C4D,
    GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT = 0x8C4E, GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT = 0x8C4F,
    GL_COMPRESSED_RED_RGTC1 = 0x8DBB, GL_COMPRESSED_SIGNED_RED_RGTC1 = 0x8DBC, GL_COMPRESSED_RG_RGTC2 = 0x8DBD,
    GL_COMPRESSED_SIGNED_RG_RGTC2 = 0x8DBE, GL_COMPRESSED_RGBA_BPTC_UNORM = 0x8E8C,
    GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM = 0x8E8D, GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT = 0x8E8E,
    GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT = 0x8E8F,
};

// Description d'un format DXGI pour OpenGL.
typedef struct {
    GLenum internal, format, type; // 0 : format inconnu ou sans équivalent
    u8 bytes;                      // octets par pixel, ou par bloc de 4 × 4 (formats compressés)
    u8 block;                      // 1 : compressé par blocs de 4 × 4
    u8 depth;                      // 1 : profondeur ; 2 : profondeur et stencil
    u8 swap_rb;                    // 1 : octets B, G, R, A échangés à l'envoi et à la relecture (B8G8R8A8…)
    u8 integer;                    // 1 : entier non normalisé (attributs : glVertexAttribIPointer)
    u8 components;                 // composantes (attributs de sommets)
    u8 normalized;                 // attributs : entiers normalisés
    u8 srgb;
} D3dFormat;
const D3dFormat *d3d_format(u32 dxgi);
// Format typé d'un format sans type (TYPELESS) de profondeur, pour une texture : celui de stockage.
u32 d3d_storage_format(u32 dxgi, int depth_bound);
// Format OpenGL d'une texture de ce format DXGI (compressé remplacé par RGBA 8 bits si le GPU ne le lit pas).
const D3dFormat *d3d_texture_format(u32 dxgi, int depth_bound);
// Ce format compressé est-il décompressé à l'envoi ?
int d3d_bcn_decoded(u32 dxgi);

// --- Objets COM ---

typedef enum {
    K_FACTORY, K_ADAPTER, K_OUTPUT, K_SWAPCHAIN, K_DXGIDEVICE, K_SURFACE, K_DEVICE, K_CONTEXT, K_BUFFER, K_TEX1D,
    K_TEX2D, K_TEX3D, K_SRV, K_RTV, K_DSV, K_UAV, K_VS, K_PS, K_GS, K_HS, K_DS, K_CS, K_LAYOUT, K_BLEND, K_DEPTH,
    K_RASTER, K_SAMPLER, K_QUERY, K_MULTITHREAD, K_ANNOTATION, K_LINKAGE, K_COUNT
} D3dKind;

typedef struct D3dPriv {
    struct D3dPriv *next;
    u8 guid[16];
    u32 size;
    u64 iface; // interface (SetPrivateDataInterface), sinon données qui suivent
    u8 data[];
} D3dPriv;

typedef struct D3dObj D3dObj;
struct D3dObj {
    u64 vtable;              // vu par le jeu
    volatile u32 refs;
    u32 kind;
    struct D3dDevice *device;
    D3dObj *parent;          // GetParent (DXGI) ; référence tenue
    D3dPriv *priv;
    void (*destroy)(D3dObj *o); // à la dernière référence (ressources OpenGL), puis free
    D3dObj *alias;           // objet compagnon d'une autre interface (IDXGISurface d'une texture…)
};

// Méthode fournie : nom dans la table virtuelle et fonction.
typedef struct {
    const char *name;
    RcnFn fn;
} D3dMethod;

// Nouvel objet de `size` octets (D3dObj en tête), une référence. Méthodes : `methods` puis les
// communes (IUnknown, données privées, GetDevice, GetParent) ; les autres nomment l'appel.
void *d3d_new(RcnCtx *ctx, D3dKind kind, size_t size, const D3dMethod *methods);
void d3d_addref(D3dObj *o);
void d3d_release(D3dObj *o);
#define OBJ(v) ((D3dObj *)PTR(v))
#define THIS(type) ((type *)PTR(A0))
// Écrit un objet dans une case du jeu (avec une référence), si la case existe.
void d3d_out(u64 slot, D3dObj *o);
// Interface demandée (IID du jeu) reconnue pour cet objet ?
int d3d_has_iface(D3dObj *o, u64 iid);
const char *d3d_iid_name(u64 iid);

enum {
    S_OK = 0, S_FALSE = 1, E_NOTIMPL = 0x80004001u, E_NOINTERFACE = 0x80004002u, E_POINTER = 0x80004003u,
    E_FAIL = 0x80004005u, E_INVALIDARG = 0x80070057u, E_OUTOFMEMORY = 0x8007000Eu,
    DXGI_ERROR_NOT_FOUND = 0x887A0002u, DXGI_ERROR_MORE_DATA = 0x887A0003u, DXGI_ERROR_UNSUPPORTED = 0x887A0004u,
    DXGI_ERROR_WAS_STILL_DRAWING = 0x887A000Au, DXGI_ERROR_INVALID_CALL = 0x887A0001u,
};

// --- Ressources et vues ---

enum { DIM_BUFFER = 1, DIM_TEX1D, DIM_TEX2D, DIM_TEX3D };
enum {
    BIND_VERTEX = 1, BIND_INDEX = 2, BIND_CONSTANT = 4, BIND_SHADER_RESOURCE = 8, BIND_STREAM_OUTPUT = 0x10,
    BIND_RENDER_TARGET = 0x20, BIND_DEPTH_STENCIL = 0x40, BIND_UNORDERED_ACCESS = 0x80,
    USAGE_DEFAULT = 0, USAGE_IMMUTABLE = 1, USAGE_DYNAMIC = 2, USAGE_STAGING = 3,
    MISC_GENERATE_MIPS = 1, MISC_TEXTURECUBE = 4, MISC_BUFFER_ALLOW_RAW = 0x20, MISC_BUFFER_STRUCTURED = 0x40,
};

typedef struct D3dRes {
    D3dObj obj;
    u32 dim, usage, bind, cpu, misc;
    u32 width, height, depth, mips, layers, format, samples; // textures (layers : tableau × 6 pour un cube)
    u32 size, stride;                                       // tampons : octets, pas d'un élément structuré
    GLuint gl;                                              // tampon ou texture OpenGL
    GLenum target;
    GLuint tbo;                                             // tampon : texture tampon (vue de nuanceur)
    u8 *mem;           // copie du processeur : tampons (tous), textures de transfert (STAGING)
    size_t mem_size;
    u32 *sub_offset;   // textures de transfert : position de chaque sous-ressource dans mem
    int gpu_newer;     // texture de transfert : la copie du GPU est plus récente (CopyResource vers elle)
    u32 dirty_lo, dirty_hi; // tampons : octets modifiés depuis le dernier envoi
    u32 version;       // augmente à chaque modification (caches)
} D3dRes;

typedef struct D3dView {
    D3dObj obj;
    D3dRes *res;      // référence tenue
    u32 format, dim;  // dim : D3D11_*_DIMENSION de la vue
    u32 mip, mips, layer, layers; // sous-ressources vues
    u32 first, count; // tampons : éléments
    u32 flags;        // DSV : lecture seule
} D3dView;

// --- Nuanceurs ---

typedef struct D3dShader D3dShader;
typedef struct D3dProgram D3dProgram;
struct D3dShader {
    D3dObj obj;
    u32 stage;       // 0 sommets, 1 pixels, 2 géométrie, 3 coque, 4 domaine, 5 calcul
    u8 *code;        // DXBC du jeu (copie)
    size_t size;
    u64 hash;
    struct DxbcShader *dx; // traduction (rcn_dxbc.c)
    GLuint gl;       // nuanceur OpenGL compilé (0 : pas encore, ~0 : échec)
};

typedef struct {
    char semantic[32];
    u32 index, format, slot, offset, instanced, step;
} D3dElement;
typedef struct {
    D3dObj obj;
    u32 count;
    D3dElement el[32];
} D3dLayout;

typedef struct {
    D3dObj obj;
    u32 alpha_to_coverage, independent;
    struct { u32 enable, src, dst, op, src_a, dst_a, op_a, mask; } rt[8];
} D3dBlend;
typedef struct {
    D3dObj obj;
    u32 depth_enable, depth_write, depth_func, stencil_enable, read_mask, write_mask;
    u32 front[4], back[4]; // échec, échec de profondeur, succès, fonction
} D3dDepth;
typedef struct {
    D3dObj obj;
    u32 fill, cull, front_ccw, depth_clip, scissor, multisample, line_aa;
    i32 bias;
    float bias_clamp, slope_bias;
} D3dRaster;
typedef struct {
    D3dObj obj;
    GLuint gl;
    u32 filter, address[3], max_aniso, compare;
    float lod_bias, border[4], min_lod, max_lod;
} D3dSampler;
typedef struct {
    D3dObj obj;
    u32 type, misc;
    GLuint gl;
    GLsync fence;
    int active, ended;
} D3dQuery;

// --- Appareil et contexte ---

#define D3D_STAGES 6
#define D3D_SRV_SLOTS 128
#define D3D_SAMPLER_SLOTS 16
#define D3D_CB_SLOTS 14
#define D3D_UAV_SLOTS 8

typedef struct {
    D3dShader *shader;
    D3dRes *cb[D3D_CB_SLOTS];
    u32 cb_first[D3D_CB_SLOTS], cb_count[D3D_CB_SLOTS]; // en constantes de 16 octets (ContextX1)
    D3dView *srv[D3D_SRV_SLOTS];
    D3dSampler *sampler[D3D_SAMPLER_SLOTS];
} D3dStageState;

typedef struct D3dContext {
    D3dObj obj;
    struct D3dDevice *device;
    pthread_mutex_t lock;
    // État lié (références non tenues : le jeu garde ses objets en vie ; voir ClearState).
    D3dStageState st[D3D_STAGES];
    D3dLayout *layout;
    D3dRes *vb[32];
    u32 vb_stride[32], vb_offset[32];
    D3dRes *ib;
    u32 ib_format, ib_offset;
    u32 topology;
    D3dView *rtv[8];
    D3dView *dsv;
    D3dView *uav[D3D_UAV_SLOTS];
    D3dView *cs_uav[D3D_UAV_SLOTS];
    D3dBlend *blend;
    float blend_factor[4];
    u32 sample_mask;
    D3dDepth *depth;
    u32 stencil_ref;
    D3dRaster *raster;
    float viewport[16][6];
    u32 viewports;
    i32 scissor[16][4];
    u32 scissors;
    D3dQuery *predicate;
} D3dContext;

typedef struct D3dDevice {
    D3dObj obj;
    D3dContext *immediate;
    D3dObj *dxgi_device;   // IDXGIDevice compagnon
    D3dObj *adapter;
    u32 feature_level, flags;
    D3dObj *multithread;
} D3dDevice;

typedef struct {
    u32 draws, no_shader, no_program, topology, clears;
} D3dStats;
extern D3dStats d3d_stats;

// --- rcn_d3d11_gl.c ---

typedef struct {
    int es;               // OpenGL ES (Android)
    int version;          // 10 × majeur + mineur
    int has_s3tc, has_rgtc, has_bptc, has_bgra, has_base_instance, has_copy_image, has_clip_control;
    int max_anisotropy;
} D3dGlCaps;
extern D3dGlCaps d3d_caps;

// Rend courant sur le thread appelant un contexte OpenGL qui partage les objets des autres (créé au
// premier appel du thread). 0 si OpenGL est indisponible.
int d3d_gl_current(RcnCtx *ctx);
// Contexte propre au thread : objets non partagés (framebuffers, tableaux de sommets).
typedef struct D3dThreadGl {
    GLuint vao;
    GLuint read_fb, draw_fb;  // copies
    struct { u64 key[9]; GLuint fb; } fbs[64]; // framebuffers par jeu de cibles
    u32 fb_next;
    u32 fb_epoch;
} D3dThreadGl;
D3dThreadGl *d3d_thread_gl(void);
// Présente la texture `tex` (width × height, ligne 0 en haut) à l'écran de l'hôte ; `wait` : cadence.
void d3d_gl_present(RcnCtx *ctx, D3dRes *backbuffer, int interval);
// Les autres contextes verront ce qui vient d'être fait sur celui-ci (création de ressources hors du
// thread de dessin).
void d3d_gl_publish(void);
// Le thread appelant dessine (ses objets n'ont pas à être publiés).
void d3d_gl_mark_draw(void);
// Journal (RCN_D3D_LOG) et erreurs OpenGL.
extern int d3d_log_level;
void d3d_log(const char *fmt, ...);
void d3d_gl_check(const char *where);

// --- rcn_d3d11.c ---
D3dDevice *d3d_device_create(RcnCtx *ctx, D3dObj *adapter, u32 flags, u32 level);
D3dRes *d3d_texture_new(RcnCtx *ctx, D3dDevice *device, u32 dim, const u32 *desc, u64 init);
void d3d_res_upload(D3dRes *r, u32 sub, const u32 *box, const u8 *data, u32 row_pitch, u32 slice_pitch);
// Envoie au GPU les octets modifiés d'un tampon.
void d3d_buffer_flush(D3dRes *r);
u32 d3d_mip_size(u32 size, u32 mip);
u32 d3d_sub_count(const D3dRes *r);
GLenum d3d_cube_face_target(u32 face);

// --- rcn_d3d11_ctx.c ---
D3dContext *d3d_context_create(RcnCtx *ctx, D3dDevice *device);
// Framebuffer OpenGL des cibles données (vues), sur le contexte du thread.
GLuint d3d_framebuffer(D3dView *const *rtv, u32 count, D3dView *dsv);
void d3d_fb_forget(D3dView *view);

// --- rcn_dxgi.c ---
D3dObj *d3d_adapter_default(RcnCtx *ctx);
D3dObj *d3d_dxgi_device_new(RcnCtx *ctx, D3dDevice *device);
D3dObj *d3d_surface_new(RcnCtx *ctx, D3dRes *texture);
u32 d3d_dxgi_create_swapchain(RcnCtx *ctx, D3dDevice *device, u64 desc, u64 out);
// Objets compagnons d'autres interfaces (IDXGIDevice d'un appareil, IDXGISurface d'une texture…).
D3dObj *d3d_query_extra(RcnCtx *ctx, D3dObj *o, u64 iid);

// Bibliothèques fournies au jeu.
extern const RcnShim rcn_d3d11_exports[], rcn_dxgi_exports[];
extern const size_t rcn_d3d11_exports_count, rcn_dxgi_exports_count;

#endif
