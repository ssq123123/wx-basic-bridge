set_xmakever("2.9.1")
set_project("wx_basic_bridge")
set_languages("c11")
set_runtimes("MT")
add_defines("WIN32_LEAN_AND_MEAN", "_CRT_SECURE_NO_WARNINGS")

if is_plat("windows") then
    set_toolchains("msvc")
    add_cflags("-O2", "/utf-8")
    add_ldflags("-nologo", "-DLL")
    add_links("kernel32", "user32")
end

target("wx_hook_bridge")
    set_kind("shared")
    set_targetdir("dist")
    set_basename("wx_hook_bridge")
    add_includedirs("native/wx_hook_bridge/src/include")
    add_files("native/wx_hook_bridge/src/wx_hook_bridge.c")
    add_files("native/wx_hook_bridge/src/modules/*.c")
