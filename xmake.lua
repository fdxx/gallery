target("gallery")
    set_kind("binary")
    set_languages("cxx23")
    set_arch("x64")
    set_toolchains("clang")
    add_includedirs("include")
    add_files("src/gallery.cpp")
    add_cxflags(
        "-Wall", 
        "-Wshadow",
        "-Wno-parentheses-equality",
        "-fvisibility=hidden", 
        "-fvisibility-inlines-hidden", 
        "-O2", "-g3"
    )

    after_build(function (target)
        os.tryrm("release")
        os.mkdir("release")
        os.cp(target:targetfile(), "release")
        os.cp("assets", "release")
    end)

