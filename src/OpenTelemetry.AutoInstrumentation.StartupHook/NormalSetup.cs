// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

using System.Reflection;
using OpenTelemetry.AutoInstrumentation.Util;

namespace OpenTelemetry.AutoInstrumentation;

/// <summary>
/// Normal initialization for deployments with the native profiler.
/// Loads the Loader assembly directly via Assembly.LoadFrom.
/// </summary>
internal sealed class NormalSetup : InitializationSetup
{
    protected override string ModeName => "Normal";

    protected override void Initialize(string instrumentationHomePath)
    {
        // With Native profiler, we load the Loader from instrumentation home path,
        // create an instance of OpenTelemetry.AutoInstrumentation.Loader.Loader
        // which will setup assembly resolution and initialize Instrumentation.
        // The Loader assembly may live in the runtime-version-specific subfolder
        // (e.g. net6.0) rather than directly under instrumentationHomePath, so use
        // the same version-aware resolution as the rest of the agent assemblies.
        var loaderFilePath = ManagedProfilerLocationHelper.GetAssemblyPath(LoaderAssemblyName)
            ?? throw new InvalidOperationException($"Could not resolve {LoaderAssemblyName}.dll under {instrumentationHomePath}");
        var loaderAssembly = Assembly.LoadFrom(loaderFilePath)
            ?? throw new InvalidOperationException("Failed to load Loader assembly");
        _ = loaderAssembly.CreateInstance(LoaderTypeName)
            ?? throw new InvalidOperationException("Failed to create an instance of the Loader");
    }
}
