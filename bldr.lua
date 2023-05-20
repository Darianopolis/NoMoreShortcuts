if Project "nms" then
    Compile "src/**"
    Include "src"
    Import {
        "glfw",
        "glad",
        "imgui-opengl",
        "imgui-glfw",
        "stb",
    }
    Artifact { "out/main", type = "Console" }
end