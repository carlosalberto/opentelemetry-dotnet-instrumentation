/*
 * Copyright The OpenTelemetry Authors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef OTEL_CLR_PROFILER_MANAGED_PROFILER_LOCATION_HELPER_H_
#define OTEL_CLR_PROFILER_MANAGED_PROFILER_LOCATION_HELPER_H_

#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>
#include <vector>

#include "file_utils.h"
#include "logger.h"
#include "string_utils.h" // NOLINT

namespace trace
{

namespace detail
{
const WSTRING net_subdir   = WStr("net");
const WSTRING netfx_subdir = WStr("netfx");
} // namespace detail

// Represents the result of a managed assembly search.
struct AssemblyLocation
{
    std::filesystem::path path;
    bool                  is_standalone = false; // true if found in a standalone (net/netfx) layout

    bool empty() const
    {
        return path.empty();
    }
};

// Reads a ".link" file (as produced by the build's OptimizeTracerHomeAssemblies step) and
// returns the sibling runtime-version directory name it points to (e.g. "net8.0"), or an
// empty string if the link file doesn't exist or can't be read.
inline WSTRING ResolveVersionedAssemblyLink(const std::filesystem::path& link_path)
{
    std::error_code ec;
    if (!std::filesystem::exists(link_path, ec) || ec)
    {
        return EmptyWStr;
    }

    std::ifstream link_file(link_path);
    if (!link_file.is_open())
    {
        return EmptyWStr;
    }

    std::stringstream buffer;
    buffer << link_file.rdbuf();
    std::string target_dir_name = buffer.str();

    // Trim any trailing whitespace/newline the build step may have written.
    while (!target_dir_name.empty() &&
          (target_dir_name.back() == '\r' || target_dir_name.back() == '\n' || target_dir_name.back() == ' '))
    {
        target_dir_name.pop_back();
    }

    if (target_dir_name.empty())
    {
        return EmptyWStr;
    }

    return ToWSTRING(target_dir_name);
}

// Finds the first existing path for a named managed assembly, and reports whether
// the match is from a standalone (net/netfx subdirectory) or NuGet-based layout.
// Priority order:
//   1. tracer_home/<net|netfx>/<version_subdir>/<filename>        (standalone, runtime-version-specific)
//   1b. ... or, if only a "<filename>.link" exists there, the directory it points to
//   2. tracer_home/<net|netfx>/<filename>        (standalone via OTEL_DOTNET_AUTO_HOME, common/hoisted)
//   3. tracer_home/<filename>                    (NuGet via OTEL_DOTNET_AUTO_HOME)
//   4. parent(native_dir)/<net|netfx>/<version_subdir>/<filename> (standalone, profiler-relative)
//   4b. ... or the ".link" equivalent of the above
//   5. parent(native_dir)/<net|netfx>/<filename> (standalone, profiler-relative, common/hoisted)
//   6. native_dir/<filename>                     (NuGet, platform-dependent)
//   7. grandparent(native_dir)/<filename>        (NuGet, platform-independent)
// The runtime subdirectory is "netfx" when is_netfx is true (.NET Framework, Windows only),
// and "net" otherwise (.NET Core/5+).
// version_subdir, when non-empty (e.g. "net8.0"), is the runtime-version-specific directory
// that PublishManagedProfiler/OptimizeTracerHomeAssemblies may place this assembly under
// instead of hoisting it to the common runtime_subdir root - this happens whenever the
// assembly differs between target frameworks (e.g. our own compiled assemblies once more
// than one framework is built). Pass EmptyWStr to skip this check (e.g. for assemblies that
// are always common across all target frameworks, such as the StartupHook).
// Skips tracer_home candidates when tracer_home is empty, and skips profiler-relative
// candidates when profiler_path is empty.
inline AssemblyLocation FindManagedAssembly(const WSTRING& filename,
                                            const WSTRING& profiler_path,
                                            const WSTRING& tracer_home,
                                            [[maybe_unused]] bool is_netfx,
                                            const WSTRING& version_subdir = EmptyWStr)
{
#ifdef _WIN32
    const auto& runtime_subdir = is_netfx ? detail::netfx_subdir : detail::net_subdir;
#else
    const auto& runtime_subdir = detail::net_subdir;
#endif

    std::vector<std::pair<std::filesystem::path, bool>> candidates;

    // Adds the runtime-version-specific candidate(s) for a given runtime_subdir root:
    // the direct file first, then (if not found) the ".link" file's resolved target.
    auto add_versioned_candidates = [&](const std::filesystem::path& runtime_root)
    {
        if (version_subdir == EmptyWStr)
        {
            return;
        }

        candidates.emplace_back(runtime_root / version_subdir / filename, true);

        const auto link_path = runtime_root / version_subdir / (filename + WStr(".link"));
        const auto link_target = ResolveVersionedAssemblyLink(link_path);
        if (link_target != EmptyWStr)
        {
            candidates.emplace_back(runtime_root / link_target / filename, true);
        }
    };

    if (tracer_home != EmptyWStr)
    {
        const auto home        = std::filesystem::path(tracer_home);
        const auto runtime_root = home / runtime_subdir;
        add_versioned_candidates(runtime_root);
        candidates.emplace_back(runtime_root / filename, true);
        candidates.emplace_back(home / filename, false);
    }

    if (profiler_path != EmptyWStr)
    {
        const auto profiler_dir    = std::filesystem::path(profiler_path).parent_path();
        const auto parent_dir      = profiler_dir.parent_path();
        const auto grandparent_dir = parent_dir.parent_path();
        const auto runtime_root    = parent_dir / runtime_subdir;
        add_versioned_candidates(runtime_root);
        candidates.emplace_back(runtime_root / filename, true);
        candidates.emplace_back(profiler_dir / filename, false);
        candidates.emplace_back(grandparent_dir / filename, false);
    }

    for (const auto& [path, is_standalone] : candidates)
    {
        std::error_code ec;
        const bool      exists = std::filesystem::exists(path, ec);
        if (ec)
        {
            Logger::Warn("Failed to check path '", ToString(PATH_TO_WSTRING(path)), "': ", ec.message());
        }
        if (exists)
        {
            return {path, is_standalone};
        }
    }
    return {};
}

} // namespace trace

#endif // OTEL_CLR_PROFILER_MANAGED_PROFILER_LOCATION_HELPER_H_
