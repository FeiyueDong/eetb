set_xmakever("3.0.0")
set_project("GMTools")
set_version("0.1.0")
set_languages("cxx20")
set_encodings("utf-8")
set_warnings("all", "extra")
set_defaultmode("release")
add_rules("mode.debug", "mode.release")
if is_plat("windows") then
    -- Match Qt's MSVC runtime throughout the static-library/executable graph.
    set_runtimes(is_mode("debug") and "MDd" or "MD")
end

option("build_gui")
    set_default(true)
    set_showmenu(true)
    set_description("Build the Qt 6 Widgets application")
option_end()

option("enable_mseed")
    set_default(false)
    set_showmenu(true)
    set_description("Enable the experimental libmseed 3 adapter")
option_end()

option("mseed_sdk")
    set_showmenu(true)
    set_description("Installed libmseed prefix (include/ and lib/ or lib64/)")
option_end()

add_requires("nlohmann_json >=3.10.0 <4.0.0")

-- libmseed is supplied by the user, as in the previous build. A specified SDK
-- is authoritative; do not silently fall back to a different system library.
if has_config("enable_mseed") then
    package("libmseed")
        set_kind("library")
        set_description("Installed EarthScope libmseed 3")
        set_license("Apache-2.0")
        add_configs("sdk", {description = "Installation prefix", type = "string"})
        on_fetch(function (package, opt)
            if not opt.system then
                return
            end
            import("core.base.semver")
            local sdk = package:config("sdk")
            local result
            if sdk then
                import("lib.detect.find_library")
                local header = path.join(sdk, "include", "libmseed.h")
                assert(os.isfile(header), "libmseed.h not found under --mseed_sdk=" .. sdk)
                local version = io.readfile(header):match('#define%s+LIBMSEED_VERSION%s+"([^"]+)"')
                local library = find_library("mseed", {path.join(sdk, "lib"), path.join(sdk, "lib64")},
                                             {plat = package:plat()})
                assert(library, "mseed library not found under --mseed_sdk=" .. sdk)
                result = {includedirs = {path.join(sdk, "include")}, linkdirs = {library.linkdir},
                          links = {"mseed"}, libfiles = {path.join(library.linkdir, library.filename)},
                          version = version}
            else
                result = package:find_package("pkgconfig::mseed")
                assert(result, "libmseed not found; set --mseed_sdk=/path/to/libmseed or expose mseed.pc via PKG_CONFIG_PATH")
            end
            assert(result.version and semver.satisfies(result.version, ">=3.5.3 <4.0.0"),
                   "libmseed 3.5.3 or newer 3.x is required (found " .. (result.version or "unknown") .. ")")
            if package:is_plat("linux", "macosx", "bsd") then
                result.syslinks = table.join(result.syslinks or {}, "m")
            end
            return result
        end)
    package_end()
    local sdk = get_config("mseed_sdk") or nil
    if sdk then
        sdk = path.absolute(sdk, os.projectdir())
    end
    add_requires("libmseed >=3.5.3 <4.0.0", {system = true, configs = {sdk = sdk}})
end

target("gmtools_core")
    set_kind("static")
    set_default(false)
    add_includedirs("include", {public = true})
    add_packages("nlohmann_json")
    add_files("src/core/data.cpp", "src/core/metadata.cpp", "src/core/files.cpp", "src/core/profile.cpp",
              "src/formats/registry.cpp", "src/formats/text.cpp", "src/formats/peer.cpp",
              "src/formats/modified_mseed.cpp", "src/formats/knet.cpp", "src/formats/sac.cpp")
    if has_config("enable_mseed") then
        add_files("src/formats/mseed.cpp")
        add_defines("GMTOOLS_HAVE_LIBMSEED")
        add_packages("libmseed", {public = true})
    end
    -- This is an internal implementation library, not an installed SDK.
    on_install(function () end)
    on_uninstall(function () end)
target_end()

target("gmtools")
    set_kind("binary")
    set_rundir("$(projectdir)")
    add_deps("gmtools_core")
    add_files("src/cli/main.cpp")
    if is_plat("mingw") then
        add_ldflags("-municode")
    end
target_end()

if has_config("build_gui") then
    target("gmtools_gui")
        add_rules("qt.widgetapp")
        set_rundir("$(projectdir)")
        add_deps("gmtools_core")
        add_frameworks("QtConcurrent")
        add_files("src/gui/main.cpp", "src/gui/export_options.cpp", "src/gui/main_window.cpp", "src/gui/parse_dialog.cpp",
                  "src/gui/plot.cpp", "src/gui/theme.cpp", "src/gui/ui_helpers.cpp")
        on_config(function (target)
            import("core.base.semver")
            local qt = target:data("qt")
            assert(qt and qt.sdkver and semver.satisfies(qt.sdkver, ">=6.2.0 <7.0.0"),
                   "Qt 6.2 or newer 6.x is required; select a Qt 6 SDK with --qt=/path/to/Qt")
        end)
    target_end()
end
