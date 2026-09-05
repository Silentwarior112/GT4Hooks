"""
gen_vsproject.py - Regenerate the Visual Studio project and filters from the
tree, so they cover every game without being hand-maintained.

The project is a Makefile-type project: Visual Studio does not compile anything
itself, it shells out to build.bat. Its value is browsing and IntelliSense, so
what matters is that the file list is complete and the include paths and defines
match what the real build passes.

Configurations are named after what they actually build - gt4o-US, tt-US, tt-EU,
tt-JP - rather than the meaningless Debug/Release x Win32/x64 grid the project
carried before.

Run this after adding a game, a region, or a source file.

Usage:  python tools/gen_vsproject.py
"""

import os
import re
import sys
import uuid

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC = os.path.join(ROOT, "source")

PROJECT_GUID = "{2C95429D-8256-4051-B257-4025D63BC70F}"   # kept from the original
SOLUTION_GUID = "{5793181A-25AF-4BF6-82E5-F0DBE0FFFCA2}"


def discover_targets():
    """[(config name, game, region, build id)] from each game.mk."""
    out = []
    games_dir = os.path.join(SRC, "games")
    for game in sorted(os.listdir(games_dir)):
        mk = os.path.join(games_dir, game, "game.mk")
        if not os.path.exists(mk):
            continue
        text = open(mk, encoding="utf-8").read()
        m = re.search(r"^REGIONS\s*:?=\s*(.+)$", text, re.M)
        regions = m.group(1).split() if m else []
        for r in regions:
            b = re.search(r"^BUILD_%s\s*:?=\s*(\S+)" % r, text, re.M)
            out.append(("%s-%s" % (game, r), game, r, b.group(1) if b else ""))
    return out


def collect(ext):
    """Repo-relative, backslash-separated source paths."""
    found = []
    for base in ("source", "mk", "tools"):
        for root, _d, files in os.walk(os.path.join(ROOT, base)):
            if "cache" in root or "__pycache__" in root:
                continue
            for fn in sorted(files):
                if fn.endswith(ext):
                    p = os.path.relpath(os.path.join(root, fn), ROOT)
                    found.append(p.replace("/", "\\"))
    return sorted(found)


def filter_for(path):
    """Which virtual folder a file belongs in."""
    p = path.replace("\\", "/")
    if p.startswith("source/core/"):
        sub = os.path.dirname(p[len("source/core/"):])
        return "Core" + ("\\" + sub.replace("/", "\\") if sub else "")
    if p.startswith("source/games/"):
        rest = p[len("source/games/"):]
        game = rest.split("/")[0]
        sub = os.path.dirname(rest[len(game) + 1:])
        return "Game %s%s" % (game, "\\" + sub.replace("/", "\\") if sub else "")
    if p.startswith("tools/"):
        return "Tools"
    if p.startswith("mk/"):
        return "Build"
    return "Build"


def main():
    targets = discover_targets()
    sources = collect(".c")
    headers = collect(".h")
    others = [p.replace("/", "\\") for p in ("makefile", "build.bat", "README.md")
              if os.path.exists(os.path.join(ROOT, p))]
    others += collect(".mk") + collect(".py") + [
        p.replace("/", "\\") for p in ("mk/linkfile", "mk/eemakefile.eeglobal")
        if os.path.exists(os.path.join(ROOT, p))]

    # ---------------- vcxproj ----------------
    x = []
    x.append('<?xml version="1.0" encoding="utf-8"?>')
    x.append('<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">')
    x.append('  <ItemGroup Label="ProjectConfigurations">')
    for cfg, _s, _r, _b in targets:
        x.append('    <ProjectConfiguration Include="%s|x64">' % cfg)
        x.append("      <Configuration>%s</Configuration>" % cfg)
        x.append("      <Platform>x64</Platform>")
        x.append("    </ProjectConfiguration>")
    x.append("  </ItemGroup>")

    x.append("  <ItemGroup>")
    for p in sources:
        x.append('    <ClCompile Include="%s" />' % p)
    x.append("  </ItemGroup>")
    x.append("  <ItemGroup>")
    for p in headers:
        x.append('    <ClInclude Include="%s" />' % p)
    x.append("  </ItemGroup>")
    x.append("  <ItemGroup>")
    for p in sorted(set(others)):
        x.append('    <None Include="%s" />' % p)
    x.append("  </ItemGroup>")

    x.append('  <PropertyGroup Label="Globals">')
    x.append("    <VCProjectVersion>17.0</VCProjectVersion>")
    x.append("    <ProjectGuid>%s</ProjectGuid>" % PROJECT_GUID)
    x.append("    <Keyword>MakeFileProj</Keyword>")
    x.append("  </PropertyGroup>")
    x.append('  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.Default.props" />')

    for cfg, _s, _r, _b in targets:
        x.append("  <PropertyGroup Condition=\"'$(Configuration)|$(Platform)'=='%s|x64'\" Label=\"Configuration\">" % cfg)
        x.append("    <ConfigurationType>Makefile</ConfigurationType>")
        x.append("    <UseDebugLibraries>false</UseDebugLibraries>")
        x.append("    <PlatformToolset>v143</PlatformToolset>")
        x.append("  </PropertyGroup>")

    x.append('  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.props" />')
    x.append('  <ImportGroup Label="ExtensionSettings" />')
    x.append('  <ImportGroup Label="Shared" />')
    for cfg, _s, _r, _b in targets:
        x.append("  <ImportGroup Label=\"PropertySheets\" Condition=\"'$(Configuration)|$(Platform)'=='%s|x64'\">" % cfg)
        x.append('    <Import Project="$(UserRootDir)\\Microsoft.Cpp.$(Platform).user.props" '
                 'Condition="exists(\'$(UserRootDir)\\Microsoft.Cpp.$(Platform).user.props\')" '
                 'Label="LocalAppDataPlatform" />')
        x.append("  </ImportGroup>")

    # NMake settings: build.bat does the work; the include path and defines are
    # only so IntelliSense resolves the same headers the real build does.
    for cfg, game, region, build in targets:
        x.append("  <PropertyGroup Condition=\"'$(Configuration)|$(Platform)'=='%s|x64'\">" % cfg)
        x.append("    <NMakeBuildCommandLine>build.bat %s %s</NMakeBuildCommandLine>" % (game, region))
        x.append("    <NMakeReBuildCommandLine>build.bat clean %s %s &amp;&amp; build.bat %s %s</NMakeReBuildCommandLine>"
                 % (game, region, game, region))
        x.append("    <NMakeCleanCommandLine>build.bat clean %s %s</NMakeCleanCommandLine>" % (game, region))
        x.append("    <NMakeOutput>..\\GT4Hooks-work\\out\\%s\\%s\\plugin.elf</NMakeOutput>" % (game, build))
        x.append("    <NMakePreprocessorDefinitions>_EE;HOSTFS_PRINT=1;TARGET_HEADER=\"%s.h\";$(NMakePreprocessorDefinitions)</NMakePreprocessorDefinitions>" % build)
        x.append("    <NMakeIncludeSearchPath>$(ProjectDir)source;$(ProjectDir)source\\games\\%s\\Targets;"
                 "$(ProjectDir)..\\ps2sdk-main\\ps2sdk\\ee\\include;"
                 "$(ProjectDir)..\\ps2sdk-main\\ps2sdk\\common\\include;"
                 "$(NMakeIncludeSearchPath)</NMakeIncludeSearchPath>" % game)
        x.append("  </PropertyGroup>")

    x.append('  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.targets" />')
    x.append('  <ImportGroup Label="ExtensionTargets" />')
    x.append("</Project>")

    with open(os.path.join(ROOT, "GT4Hooks.vcxproj"), "w", encoding="utf-8-sig", newline="\r\n") as f:
        f.write("\n".join(x) + "\n")

    # ---------------- filters ----------------
    folders = set()
    for p in sources + headers + others:
        f = filter_for(p)
        parts = f.split("\\")
        for i in range(len(parts)):
            folders.add("\\".join(parts[: i + 1]))

    y = []
    y.append('<?xml version="1.0" encoding="utf-8"?>')
    y.append('<Project ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">')
    y.append("  <ItemGroup>")
    for f in sorted(folders):
        y.append('    <Filter Include="%s">' % f)
        y.append("      <UniqueIdentifier>{%s}</UniqueIdentifier>"
                 % uuid.uuid5(uuid.NAMESPACE_DNS, "gt4hooks:" + f))
        y.append("    </Filter>")
    y.append("  </ItemGroup>")
    for tag, items in (("ClCompile", sources), ("ClInclude", headers), ("None", sorted(set(others)))):
        y.append("  <ItemGroup>")
        for p in items:
            y.append('    <%s Include="%s">' % (tag, p))
            y.append("      <Filter>%s</Filter>" % filter_for(p))
            y.append("    </%s>" % tag)
        y.append("  </ItemGroup>")
    y.append("</Project>")

    with open(os.path.join(ROOT, "GT4Hooks.vcxproj.filters"), "w", encoding="utf-8-sig", newline="\r\n") as f:
        f.write("\n".join(y) + "\n")

    # ---------------- solution ----------------
    s = []
    s.append("")
    s.append("Microsoft Visual Studio Solution File, Format Version 12.00")
    s.append("# Visual Studio Version 17")
    s.append('Project("{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}") = "GT4Hooks", "GT4Hooks.vcxproj", "%s"' % PROJECT_GUID)
    s.append("EndProject")
    s.append("Global")
    s.append("\tGlobalSection(SolutionConfigurationPlatforms) = preSolution")
    for cfg, _sp, _r, _b in targets:
        s.append("\t\t%s|x64 = %s|x64" % (cfg, cfg))
    s.append("\tEndGlobalSection")
    s.append("\tGlobalSection(ProjectConfigurationPlatforms) = postSolution")
    for cfg, _sp, _r, _b in targets:
        s.append("\t\t%s.%s|x64.ActiveCfg = %s|x64" % (PROJECT_GUID, cfg, cfg))
        s.append("\t\t%s.%s|x64.Build.0 = %s|x64" % (PROJECT_GUID, cfg, cfg))
    s.append("\tEndGlobalSection")
    s.append("\tGlobalSection(SolutionProperties) = preSolution")
    s.append("\t\tHideSolutionNode = FALSE")
    s.append("\tEndGlobalSection")
    s.append("\tGlobalSection(ExtensibilityGlobals) = postSolution")
    s.append("\t\tSolutionGuid = %s" % SOLUTION_GUID)
    s.append("\tEndGlobalSection")
    s.append("EndGlobal")
    with open(os.path.join(ROOT, "GT4Hooks.sln"), "w", encoding="utf-8-sig", newline="\r\n") as f:
        f.write("\n".join(s) + "\n")

    print("configurations : %s" % ", ".join(c for c, _s, _r, _b in targets))
    print("ClCompile      : %d" % len(sources))
    print("ClInclude      : %d" % len(headers))
    print("None           : %d" % len(set(others)))
    print("filters        : %d" % len(folders))
    print("\nwrote GT4Hooks.vcxproj, GT4Hooks.vcxproj.filters, GT4Hooks.sln")
    return 0


if __name__ == "__main__":
    sys.exit(main())
