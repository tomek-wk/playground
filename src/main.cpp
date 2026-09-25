#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <limits>
#include <random>
#include <vector>

namespace {
constexpr int WORLD_SIZE = 100;
constexpr float MAX_REACH = 5.0f;
constexpr float MOVE_SPEED = 10.0f;
constexpr float MOUSE_SENSITIVITY = 0.09f;

std::array<std::uint8_t, WORLD_SIZE * WORLD_SIZE * WORLD_SIZE> gWorld{};
int gSelectedBlock = 1;
float gYaw = -90.0f;
float gPitch = 0.0f;
bool gFirstMouse = true;
double gLastMouseX = 0.0;
double gLastMouseY = 0.0;

struct Instance {
    float x;
    float y;
    float z;
    std::uint32_t type;
};

struct RayHit {
    bool hit = false;
    glm::ivec3 cell{0};
    glm::ivec3 normal{0};
};

int worldIndex(int x, int y, int z) {
    return x + WORLD_SIZE * (y + WORLD_SIZE * z);
}

bool inBounds(const glm::ivec3& p) {
    return p.x >= 0 && p.y >= 0 && p.z >= 0 &&
           p.x < WORLD_SIZE && p.y < WORLD_SIZE && p.z < WORLD_SIZE;
}

glm::vec3 cameraForward() {
    const float yaw = glm::radians(gYaw);
    const float pitch = glm::radians(gPitch);
    return glm::normalize(glm::vec3(
        std::cos(yaw) * std::cos(pitch),
        std::sin(pitch),
        std::sin(yaw) * std::cos(pitch)));
}

void mouseCallback(GLFWwindow*, double xpos, double ypos) {
    if (gFirstMouse) {
        gLastMouseX = xpos;
        gLastMouseY = ypos;
        gFirstMouse = false;
        return;
    }

    const double dx = xpos - gLastMouseX;
    const double dy = gLastMouseY - ypos;
    gLastMouseX = xpos;
    gLastMouseY = ypos;

    gYaw += static_cast<float>(dx) * MOUSE_SENSITIVITY;
    gPitch += static_cast<float>(dy) * MOUSE_SENSITIVITY;
    gPitch = std::clamp(gPitch, -89.0f, 89.0f);
}

void scrollCallback(GLFWwindow*, double, double yoffset) {
    if (yoffset > 0.0) {
        --gSelectedBlock;
        if (gSelectedBlock < 1) gSelectedBlock = 3;
    } else if (yoffset < 0.0) {
        ++gSelectedBlock;
        if (gSelectedBlock > 3) gSelectedBlock = 1;
    }
}

GLuint compileShader(GLenum type, const char* source) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
        GLint len = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(static_cast<std::size_t>(std::max(len, 1)));
        glGetShaderInfoLog(shader, len, nullptr, log.data());
        std::cerr << "Shader compile failed:\n" << log.data() << '\n';
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint createProgram() {
    static const char* vertexSource = R"GLSL(
#version 430 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 iPos;
layout(location = 3) in uint iType;

uniform mat4 uViewProj;

out vec3 vNormal;
flat out uint vType;

void main() {
    vec3 worldPos = aPos + iPos;
    gl_Position = uViewProj * vec4(worldPos, 1.0);
    vNormal = aNormal;
    vType = iType;
}
)GLSL";

    static const char* fragmentSource = R"GLSL(
#version 430 core
in vec3 vNormal;
flat in uint vType;

uniform vec3 uLightDir;
out vec4 FragColor;

void main() {
    vec3 baseColor = vec3(1.0, 0.1, 0.1);
    if (vType == 2u) baseColor = vec3(0.1, 1.0, 0.1);
    if (vType == 3u) baseColor = vec3(0.1, 0.25, 1.0);

    float diffuse = max(dot(normalize(vNormal), normalize(-uLightDir)), 0.0);
    float light = 0.28 + 0.72 * diffuse;
    FragColor = vec4(baseColor * light, 1.0);
}
)GLSL";

    const GLuint vs = compileShader(GL_VERTEX_SHADER, vertexSource);
    const GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (!vs || !fs) return 0;

    const GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE) {
        GLint len = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(static_cast<std::size_t>(std::max(len, 1)));
        glGetProgramInfoLog(program, len, nullptr, log.data());
        std::cerr << "Program link failed:\n" << log.data() << '\n';
        glDeleteProgram(program);
        return 0;
    }

    return program;
}

void fillWorld() {
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> color(1, 3);
    for (auto& voxel : gWorld) {
        voxel = static_cast<std::uint8_t>(color(rng));
    }
}

void rebuildInstanceBuffer(GLuint instanceVbo, std::vector<Instance>& instances) {
    instances.clear();
    instances.reserve(gWorld.size());

    for (int z = 0; z < WORLD_SIZE; ++z) {
        for (int y = 0; y < WORLD_SIZE; ++y) {
            for (int x = 0; x < WORLD_SIZE; ++x) {
                const std::uint8_t type = gWorld[worldIndex(x, y, z)];
                if (type == 0) continue;
                instances.push_back(Instance{
                    static_cast<float>(x),
                    static_cast<float>(y),
                    static_cast<float>(z),
                    static_cast<std::uint32_t>(type)});
            }
        }
    }

    glBindBuffer(GL_ARRAY_BUFFER, instanceVbo);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(instances.size() * sizeof(Instance)),
        instances.empty() ? nullptr : instances.data(),
        GL_DYNAMIC_DRAW);
}

RayHit raycast(const glm::vec3& origin, glm::vec3 direction, float maxDistance) {
    direction = glm::normalize(direction);
    glm::ivec3 cell(
        static_cast<int>(std::floor(origin.x)),
        static_cast<int>(std::floor(origin.y)),
        static_cast<int>(std::floor(origin.z)));

    glm::ivec3 step(0);
    glm::vec3 tMax(0.0f);
    glm::vec3 tDelta(0.0f);
    const float inf = std::numeric_limits<float>::infinity();

    auto setupAxis = [&](float o, float d, int c, int& s, float& tm, float& td) {
        if (d > 0.0f) {
            s = 1;
            td = 1.0f / d;
            tm = (static_cast<float>(c + 1) - o) / d;
        } else if (d < 0.0f) {
            s = -1;
            td = -1.0f / d;
            tm = (o - static_cast<float>(c)) / -d;
        } else {
            s = 0;
            td = inf;
            tm = inf;
        }
    };

    setupAxis(origin.x, direction.x, cell.x, step.x, tMax.x, tDelta.x);
    setupAxis(origin.y, direction.y, cell.y, step.y, tMax.y, tDelta.y);
    setupAxis(origin.z, direction.z, cell.z, step.z, tMax.z, tDelta.z);

    float t = 0.0f;
    glm::ivec3 enterNormal(0);

    while (t <= maxDistance) {
        if (inBounds(cell) && gWorld[worldIndex(cell.x, cell.y, cell.z)] != 0) {
            return RayHit{true, cell, enterNormal};
        }

        if (tMax.x <= tMax.y && tMax.x <= tMax.z) {
            t = tMax.x;
            if (t > maxDistance) break;
            cell.x += step.x;
            enterNormal = glm::ivec3(-step.x, 0, 0);
            tMax.x += tDelta.x;
        } else if (tMax.y <= tMax.z) {
            t = tMax.y;
            if (t > maxDistance) break;
            cell.y += step.y;
            enterNormal = glm::ivec3(0, -step.y, 0);
            tMax.y += tDelta.y;
        } else {
            t = tMax.z;
            if (t > maxDistance) break;
            cell.z += step.z;
            enterNormal = glm::ivec3(0, 0, -step.z);
            tMax.z += tDelta.z;
        }
    }

    return {};
}

void drawHud(int width, int height) {
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const ImVec2 center(width * 0.5f, height * 0.5f);
    const ImU32 white = IM_COL32(255, 255, 255, 255);

    char fpsText[32];
    std::snprintf(fpsText, sizeof(fpsText), "FPS: %.1f", ImGui::GetIO().Framerate);
    draw->AddText(ImVec2(12.0f, 12.0f), white, fpsText);

    draw->AddLine(ImVec2(center.x - 7.0f, center.y), ImVec2(center.x + 7.0f, center.y), white, 2.0f);
    draw->AddLine(ImVec2(center.x, center.y - 7.0f), ImVec2(center.x, center.y + 7.0f), white, 2.0f);

    constexpr float size = 42.0f;
    constexpr float gap = 8.0f;
    const float total = 3.0f * size + 2.0f * gap;
    const float startX = (width - total) * 0.5f;
    const float y = static_cast<float>(height) - size - 24.0f;

    const ImU32 colors[3] = {
        IM_COL32(255, 25, 25, 255),
        IM_COL32(25, 255, 25, 255),
        IM_COL32(25, 65, 255, 255)};

    for (int i = 0; i < 3; ++i) {
        const ImVec2 a(startX + i * (size + gap), y);
        const ImVec2 b(a.x + size, a.y + size);
        draw->AddRectFilled(a, b, colors[i]);
        const bool selected = gSelectedBlock == i + 1;
        draw->AddRect(a, b,
                      selected ? IM_COL32(255, 255, 255, 255) : IM_COL32(90, 90, 90, 255),
                      0.0f, 0, selected ? 4.0f : 2.0f);
    }
}

} // namespace

int main() {
    if (!glfwInit()) {
        std::cerr << "GLFW init failed\n";
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    if (!monitor) {
        std::cerr << "No primary monitor\n";
        glfwTerminate();
        return 1;
    }
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    if (!mode) {
        std::cerr << "No primary video mode\n";
        glfwTerminate();
        return 1;
    }

    glfwWindowHint(GLFW_RED_BITS, mode->redBits);
    glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
    glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
    glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);

    GLFWwindow* window = glfwCreateWindow(mode->width, mode->height, "playground", monitor, nullptr);
    if (!window) {
        std::cerr << "OpenGL 4.3 fullscreen window creation failed\n";
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(0);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwSetCursorPosCallback(window, mouseCallback);
    glfwSetScrollCallback(window, scrollCallback);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::cerr << "GLEW init failed\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    glGetError();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 430 core");

    const GLuint program = createProgram();
    if (!program) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    const float cubeVertices[] = {
        // +Z
        0,0,1,  0,0,1,   1,0,1,  0,0,1,   1,1,1,  0,0,1,   0,1,1,  0,0,1,
        // -Z
        1,0,0,  0,0,-1,  0,0,0,  0,0,-1,  0,1,0,  0,0,-1,  1,1,0,  0,0,-1,
        // -X
        0,0,0, -1,0,0,   0,0,1, -1,0,0,   0,1,1, -1,0,0,   0,1,0, -1,0,0,
        // +X
        1,0,1,  1,0,0,   1,0,0,  1,0,0,   1,1,0,  1,0,0,   1,1,1,  1,0,0,
        // +Y
        0,1,1,  0,1,0,   1,1,1,  0,1,0,   1,1,0,  0,1,0,   0,1,0,  0,1,0,
        // -Y
        0,0,0,  0,-1,0,  1,0,0,  0,-1,0,  1,0,1,  0,-1,0,  0,0,1,  0,-1,0,
    };

    const std::uint32_t cubeIndices[] = {
         0, 1, 2,  2, 3, 0,
         4, 5, 6,  6, 7, 4,
         8, 9,10, 10,11, 8,
        12,13,14, 14,15,12,
        16,17,18, 18,19,16,
        20,21,22, 22,23,20,
    };

    GLuint vao = 0;
    GLuint vertexVbo = 0;
    GLuint indexEbo = 0;
    GLuint instanceVbo = 0;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vertexVbo);
    glGenBuffers(1, &indexEbo);
    glGenBuffers(1, &instanceVbo);

    glBindVertexArray(vao);

    glBindBuffer(GL_ARRAY_BUFFER, vertexVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVertices), cubeVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexEbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(cubeIndices), cubeIndices, GL_STATIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, instanceVbo);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Instance), nullptr);
    glVertexAttribDivisor(2, 1);
    glEnableVertexAttribArray(3);
    glVertexAttribIPointer(3, 1, GL_UNSIGNED_INT, sizeof(Instance), reinterpret_cast<void*>(3 * sizeof(float)));
    glVertexAttribDivisor(3, 1);

    fillWorld();
    std::vector<Instance> instances;
    rebuildInstanceBuffer(instanceVbo, instances);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    glm::vec3 cameraPos(50.0f, 50.0f, 104.0f);
    double previousTime = glfwGetTime();
    bool previousLmb = false;
    bool previousRmb = false;

    const GLint viewProjLoc = glGetUniformLocation(program, "uViewProj");
    const GLint lightDirLoc = glGetUniformLocation(program, "uLightDir");

    while (!glfwWindowShouldClose(window)) {
        const double now = glfwGetTime();
        float dt = static_cast<float>(now - previousTime);
        previousTime = now;
        dt = std::min(dt, 0.1f);

        glfwPollEvents();

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

        const glm::vec3 forward = cameraForward();
        glm::vec3 right = glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f));
        if (glm::dot(right, right) > 0.0001f) right = glm::normalize(right);

        const float step = MOVE_SPEED * dt;
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) cameraPos += forward * step;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) cameraPos -= forward * step;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) cameraPos -= right * step;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) cameraPos += right * step;

        const bool lmb = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        const bool rmb = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;

        if ((lmb && !previousLmb) || (rmb && !previousRmb)) {
            const RayHit hit = raycast(cameraPos, forward, MAX_REACH);
            bool changed = false;

            if (hit.hit && lmb && !previousLmb) {
                gWorld[worldIndex(hit.cell.x, hit.cell.y, hit.cell.z)] = 0;
                changed = true;
            } else if (hit.hit && rmb && !previousRmb) {
                const glm::ivec3 target = hit.cell + hit.normal;
                if (inBounds(target) && gWorld[worldIndex(target.x, target.y, target.z)] == 0) {
                    gWorld[worldIndex(target.x, target.y, target.z)] = static_cast<std::uint8_t>(gSelectedBlock);
                    changed = true;
                }
            }

            if (changed) rebuildInstanceBuffer(instanceVbo, instances);
        }

        previousLmb = lmb;
        previousRmb = rmb;

        int fbWidth = 0;
        int fbHeight = 0;
        glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
        if (fbHeight <= 0) fbHeight = 1;

        const glm::mat4 projection = glm::perspective(
            glm::radians(70.0f),
            static_cast<float>(fbWidth) / static_cast<float>(fbHeight),
            0.05f,
            300.0f);
        const glm::mat4 view = glm::lookAt(cameraPos, cameraPos + forward, glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 viewProj = projection * view;

        glViewport(0, 0, fbWidth, fbHeight);
        glClearColor(0.08f, 0.10f, 0.14f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(program);
        glUniformMatrix4fv(viewProjLoc, 1, GL_FALSE, glm::value_ptr(viewProj));
        glUniform3f(lightDirLoc, -0.45f, -1.0f, -0.35f);
        glBindVertexArray(vao);
        glDrawElementsInstanced(
            GL_TRIANGLES,
            36,
            GL_UNSIGNED_INT,
            nullptr,
            static_cast<GLsizei>(instances.size()));

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        drawHud(fbWidth, fbHeight);
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    glDeleteBuffers(1, &instanceVbo);
    glDeleteBuffers(1, &indexEbo);
    glDeleteBuffers(1, &vertexVbo);
    glDeleteVertexArrays(1, &vao);
    glDeleteProgram(program);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}