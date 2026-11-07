// OpenGL compatibility for the macOS port.
//
// macOS exposes two kinds of context to Minecraft:
//   * Minecraft 1.16 and older request a "2.0 / any profile" context, which macOS satisfies with its
//     legacy OpenGL 2.1 context (GLSL 1.20, plus ARB_framebuffer_object, ARB_sync,
//     APPLE_vertex_array_object, ARB_instanced_arrays, EXT_gpu_shader4, ...).
//   * Minecraft 1.17+ request a 3.2 core context and receive OpenGL 4.1 core (GLSL 4.10).
// Toolscreen targets GL 3.3 core. For the legacy context this file translates shaders to GLSL 1.20 and
// points GLEW's core entry points at their extension equivalents; for both contexts it supplies
// fallbacks for the few GL 4.2+ entry points macOS never provides.
#include "common/gl_shader_compat.h"
#include "common/utils.h"

#include <OpenGL/OpenGL.h>

#include <cctype>
#include <cstring>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace {

bool IsLegacyContext() {
    const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    return version && version[0] >= '1' && version[0] <= '2' && version[1] == '.';
}

// ----- Fallback entry points --------------------------------------------------------------------
void APIENTRY NoopBindSampler(GLuint, GLuint) {}
void APIENTRY NoopMemoryBarrier(GLbitfield) {}

// GL_ARB_map_buffer_range is absent from the legacy context. Toolscreen only maps whole PBOs for
// reading or writing, which glMapBuffer covers.
void* APIENTRY MapBufferRangeFallback(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access) {
    (void)length;
    if (offset != 0) return nullptr;
    GLenum mapAccess = GL_READ_WRITE;
    const bool read = (access & GL_MAP_READ_BIT) != 0;
    const bool write = (access & GL_MAP_WRITE_BIT) != 0;
    if (read && !write) mapAccess = GL_READ_ONLY;
    else if (write && !read) mapAccess = GL_WRITE_ONLY;
    return glMapBuffer(target, mapAccess);
}

void APIENTRY TexStorage2DFallback(GLenum target, GLsizei levels, GLenum internalFormat, GLsizei width, GLsizei height) {
    GLenum format = GL_RGBA;
    GLenum type = GL_UNSIGNED_BYTE;
    switch (internalFormat) {
    case GL_R8: format = GL_RED; break;
    case GL_RG8: format = GL_RG; break;
    case GL_RGB8: format = GL_RGB; break;
    case GL_DEPTH_COMPONENT24:
    case GL_DEPTH_COMPONENT32F:
        format = GL_DEPTH_COMPONENT;
        type = GL_FLOAT;
        break;
    case GL_DEPTH24_STENCIL8:
        format = GL_DEPTH_STENCIL;
        type = GL_UNSIGNED_INT_24_8;
        break;
    default: break;
    }
    for (GLsizei level = 0; level < levels; ++level) {
        glTexImage2D(target, level, static_cast<GLint>(internalFormat), width, height, 0, format, type, nullptr);
        width = width > 1 ? width / 2 : 1;
        height = height > 1 ? height / 2 : 1;
    }
}

// ----- GLSL 3.30 -> 1.20 ------------------------------------------------------------------------
std::string ReplaceWord(const std::string& text, const std::string& word, const std::string& replacement) {
    std::string out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        if (text.compare(i, word.size(), word) == 0) {
            const bool startOk = i == 0 || !(std::isalnum(static_cast<unsigned char>(text[i - 1])) || text[i - 1] == '_');
            const size_t end = i + word.size();
            const bool endOk = end >= text.size() || !(std::isalnum(static_cast<unsigned char>(text[end])) || text[end] == '_');
            if (startOk && endOk) {
                out += replacement;
                i = end;
                continue;
            }
        }
        out.push_back(text[i++]);
    }
    return out;
}

std::string TranslateToGlsl120(GLenum type, const std::string& source) {
    const bool vertex = type == GL_VERTEX_SHADER;
    std::istringstream in(source);
    std::ostringstream body;
    std::vector<std::string> fragOutputs;
    std::string line;
    bool sawVersion = false;

    static const std::regex kLayoutIn(R"(^\s*layout\s*\(\s*location\s*=\s*\d+\s*\)\s*in\s+)");
    static const std::regex kIn(R"(^(\s*)(flat\s+|smooth\s+|noperspective\s+)?in\s+)");
    static const std::regex kOut(R"(^(\s*)(flat\s+|smooth\s+|noperspective\s+)?out\s+)");
    static const std::regex kFragOut(R"(^\s*out\s+vec4\s+([A-Za-z_][A-Za-z0-9_]*)\s*;)");

    while (std::getline(in, line)) {
        std::string trimmed = line;
        trimmed.erase(0, trimmed.find_first_not_of(" \t"));
        if (!sawVersion && trimmed.rfind("#version", 0) == 0) {
            sawVersion = true;
            continue;
        }
        std::smatch m;
        if (vertex) {
            if (std::regex_search(line, kLayoutIn)) {
                line = std::regex_replace(line, kLayoutIn, "attribute ");
            } else if (std::regex_search(line, m, kIn)) {
                line = std::regex_replace(line, kIn, "$1attribute ");
            } else if (std::regex_search(line, m, kOut)) {
                line = std::regex_replace(line, kOut, "$1$2varying ");
            }
        } else {
            if (std::regex_search(line, m, kFragOut)) {
                fragOutputs.push_back(m[1].str());
                continue;
            }
            if (std::regex_search(line, m, kIn)) line = std::regex_replace(line, kIn, "$1$2varying ");
        }
        body << line << '\n';
    }

    std::string translated = body.str();
    translated = ReplaceWord(translated, "texture", "texture2D");
    translated = ReplaceWord(translated, "textureLod", "texture2DLod");
    translated = ReplaceWord(translated, "texelFetch", "texelFetch2D");
    translated = ReplaceWord(translated, "textureSize", "textureSize2D");

    std::ostringstream out;
    out << "#version 120\n#extension GL_EXT_gpu_shader4 : enable\n";
    for (const std::string& name : fragOutputs) out << "#define " << name << " gl_FragColor\n";
    out << translated;
    return out.str();
}

} // namespace

std::string ToolscreenPrepareShaderSource(GLenum shaderType, const char* source) {
    if (!source) return {};
    if (!IsLegacyContext()) return source;
    return TranslateToGlsl120(shaderType, source);
}

void ToolscreenBindShaderAttribLocations(GLuint program, const char* vertexSource) {
    if (!program || !vertexSource) return;
    static const std::regex kLayout(R"(layout\s*\(\s*location\s*=\s*(\d+)\s*\)\s*in\s+\w+\s+([A-Za-z_][A-Za-z0-9_]*)\s*;)");
    const std::string src = vertexSource;
    for (auto it = std::sregex_iterator(src.begin(), src.end(), kLayout); it != std::sregex_iterator(); ++it) {
        glBindAttribLocation(program, static_cast<GLuint>(std::stoi((*it)[1].str())), (*it)[2].str().c_str());
    }
}

const char* ToolscreenImGuiGlslVersion() { return IsLegacyContext() ? "#version 120" : "#version 330"; }

void ToolscreenOnGlewInitialized() {
    const bool legacy = IsLegacyContext();
    const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    LogCategory("init", std::string("[RENDER][macOS] OpenGL ") + (version ? version : "?") + " on " + (renderer ? renderer : "?") +
                            (legacy ? " (legacy 2.1 context: translating shaders to GLSL 1.20)" : ""));

    // OpenGL.framework exports every GL 3.x/4.x entry point whatever the context supports, so GLEW
    // resolves them all to non-null pointers. In a context that lacks the feature (the legacy 2.1
    // context lacks everything past 2.1; the 4.1 core context lacks 4.2+) the call does nothing but
    // raise GL_INVALID_OPERATION. Select by what the context reports instead of by pointer.
    if (legacy) {
        // Vertex array objects exist only as APPLE_vertex_array_object here. Without this, every
        // glGenVertexArrays returns 0 and Toolscreen's (and Dear ImGui's) vertex attribute setup lands
        // in Minecraft's default vertex array.
        if (glGenVertexArraysAPPLE && glBindVertexArrayAPPLE && glDeleteVertexArraysAPPLE) {
            glGenVertexArrays = reinterpret_cast<PFNGLGENVERTEXARRAYSPROC>(glGenVertexArraysAPPLE);
            glBindVertexArray = reinterpret_cast<PFNGLBINDVERTEXARRAYPROC>(glBindVertexArrayAPPLE);
            glDeleteVertexArrays = reinterpret_cast<PFNGLDELETEVERTEXARRAYSPROC>(glDeleteVertexArraysAPPLE);
            glIsVertexArray = reinterpret_cast<PFNGLISVERTEXARRAYPROC>(glIsVertexArrayAPPLE);
        } else {
            LogCategory("init", "[RENDER][macOS] WARNING: APPLE_vertex_array_object is unavailable.");
        }
        if (glVertexAttribDivisorARB) glVertexAttribDivisor = reinterpret_cast<PFNGLVERTEXATTRIBDIVISORPROC>(glVertexAttribDivisorARB);
        if (glDrawArraysInstancedARB) glDrawArraysInstanced = reinterpret_cast<PFNGLDRAWARRAYSINSTANCEDPROC>(glDrawArraysInstancedARB);
        if (glDrawElementsInstancedARB) glDrawElementsInstanced = reinterpret_cast<PFNGLDRAWELEMENTSINSTANCEDPROC>(glDrawElementsInstancedARB);
        if (!GLEW_ARB_map_buffer_range) glMapBufferRange = MapBufferRangeFallback;
    }
    if (!glBindSampler || !(GLEW_VERSION_3_3 || GLEW_ARB_sampler_objects)) glBindSampler = NoopBindSampler;
    if (!glMemoryBarrier || !(GLEW_VERSION_4_2 || GLEW_ARB_shader_image_load_store)) glMemoryBarrier = NoopMemoryBarrier;
    if (!glTexStorage2D || !(GLEW_VERSION_4_2 || GLEW_ARB_texture_storage)) glTexStorage2D = TexStorage2DFallback;
}
