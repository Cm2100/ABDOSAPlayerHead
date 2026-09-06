set_project("ABDOSAPlayerHead")
set_version("0.1.0")
set_license("MIT")
set_languages("c++23")
set_warnings("allextra")

add_rules("mode.debug", "mode.releasedbg")
add_requires("minhook")

-- CI clones CommonLibF4 into this path before configuring.
includes("lib/commonlibf4")

target("ABDOSAPlayerHead")
    add_rules("commonlibf4.plugin", {
        name = "ABDOSAPlayerHead",
        author = "Cm2100",
        description = "Player-only Fallout 4 female head mesh redirect without custom HDPT records"
    })
    add_packages("minhook")
    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    set_pcxxheader("src/pch.h")
