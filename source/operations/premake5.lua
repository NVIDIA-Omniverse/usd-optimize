
so_build = require("tools/premake/scene-optimizer-public")

plugin_author = "NVIDIA Corporation"


so_build.link_operations_dir()


-- Discover and link python operations
project "python_operations"
    kind "Utility"
    local python_ops = os.matchfiles("**__init__.py")
    for _, py_file in ipairs(python_ops) do
        local dir = path.getdirectory(py_file)
        local name = path.getname(dir)
        so_build.operation_plugin_python(dir, name)
    end


-- Recursive check for any premakes under operations
local plugin_premakes = os.matchfiles("**premake5.lua")
for _, premake in ipairs(plugin_premakes) do
    -- skip this premake file
    if premake ~= "premake5.lua" then
        print("Found plugin: "..premake)
        include(premake)
    end
end
