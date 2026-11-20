-- set minimum xmake version
set_xmakever("3.0.0")

-- CommonLibF4 (git submodule)
includes("lib/commonlibf4")

-- project
set_project("PowerGridDoctor")
set_version("0.2.0")
set_license("GPL-3.0")

set_languages("c++23")
set_warnings("allextra")
set_encodings("utf-8")

add_rules("mode.debug", "mode.releasedbg")
set_defaultmode("releasedbg")

target("PowerGridDoctor", function()
    add_rules("commonlibf4.plugin", {
        name = "PowerGridDoctor",
        author = "PowerGridDoctor contributors",
        description = "Diagnoses (and optionally repairs) dangling references in workshop power grids"
    })

    add_files("src/**.cpp")
    add_includedirs("include")
    add_headerfiles("include/**.h")
    add_extrafiles("dist/F4SE/Plugins/PowerGridDoctor.ini")
    add_installfiles("dist/F4SE/Plugins/PowerGridDoctor.ini", { prefixdir = "F4SE/Plugins" })
end)
