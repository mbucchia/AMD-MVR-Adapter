# Single Pass Stereo (SPS) and Multi-View Rendering (MVR) for AMD

A replacement `nvapi64.dll` that lets Direct3D 11 applications written for NVIDIA Single Pass Stereo (SPS) and Multi-View Rendering (MVR) run on AMD GPUs, using AMD's view broadcasting through the AMD GPU Services (AGS) library. Applications do not need to be recompiled. **The primary target is iRacing in VR** (but someone could expand it to work with other games).

I coined this idea in April 2024 in an [iRacing forum post](https://forums.iracing.com/discussion/comment/522573/#Comment_522573), but never had the time to push it through. With the help of Claude AI, something that was weeks of coding before, is now possible in a much shorter amount of time.

DISCLAIMER: This software is distributed as-is, without any warranties or conditions of any kind. Use at your own risks.

## Installing

See the [Release page](https://github.com/mbucchia/AMD-MVR-Adapter/releases/latest) for a download link.

Extract `dxgi.dll` and `nvapi64.dll` into the game's installation folder. The extracted files should appear next to iRacing's `iRacingSim64DX11.exe`. **You can remove the mod later by deleting these two files.**

**You will still need to enable SPS or MVR foveated rendering in iRacing's display settings.**

Go to 'Settings' > 'Display' > 'VR' and change 'VR Mode'.

In my testing, this required restarting the session.

### Triple screens

The same mod also enables iRacing's **Simultaneous Multi-Projection** (SMP) for triple screens, which renders the 3 projections with multi-view instead of 3 separate passes. This reduces CPU usage, which is usually the bottleneck with 3 projections.

Go to 'Settings' > 'Display', turn on 'Render Scene Using 3 Projections', then 'Nvidia Simultaneous Multi-Projection'. This also requires restarting the session.

## Troubleshooting

There are log files under `C:\Users\<username>\AppData\Local\AMD-MVR-Adapter`. If this folder is not created or is empty, then it means the installation above failed.

If your GPU or GPU driver does not support what this tool needs, you will see the following message in `nvapi_adapter.log`:

> ERROR: Your GPU or driver does not support multi-view (AMD view broadcasting): Single Pass Stereo and Multi-View Rendering will not be available.

Make sure you have the latest driver for your GPU, or try a full re-install (factory reset) of the driver if you think this statement is incorrect.

## Implementation

NVIDIA and AMD expose the same capability (rendering several views with a single draw call) through different models. There is very little documentation on how it works on AMD, but a (legendary) blog post [AMD LiquidVR™ MultiView Rendering in Serious Sam VR](https://gpuopen.com/learn/amd-liquidvr-multiview-rendering-in-serious-sam-vr/) explains it all.

The table below summarizes the correspondences between NVIDIA and AMD details:

| | NVIDIA SPS/MVR | AMD view broadcasting |
|---|---|---|
| Enabling | `NvAPI_D3D_SetSinglePassStereoMode()` / `NvAPI_D3D_SetMultiViewMode()` | `agsDriverExtensionsDX11_SetViewBroadcastMasks()` |
| Vertex shader | Runs once, outputs one position per view (`SV_Position` + `NV_X_RIGHT` or `NV_POSITION_VIEW_n`) | Runs once per view, outputs the position of the current view |
| View routing | Viewport mask output by a "fast" geometry shader | Viewport mask given to AGS |
| View index in shaders | `SV_ViewportArrayIndex` from the fast GS | `AmdDxExtShaderIntrinsics_GetViewportIndex()` |

Translating calls on the CPU is the easy part of the process. Rewriting the vertex shaders is the complex part, and requires a DXBC parser and writer. An NVIDIA SPS/MVR vertex/geometry shader runs once and outputs the position of each view through custom semantics (`NV_X_RIGHT`, `NV_XYZW_RIGHT`, or `NV_POSITION` matching `NV_POSITION_VIEW_n`), while the same shaders with AMD view broadcasting runs once per view. So the shader is "un-looped": it still computes the positions of all views, then uses the AMD intrinsic `AmdDxExtShaderIntrinsics_GetViewportIndex()` to select the one for the view it runs for, and writes it to `SV_Position`. The view index is also written to a new `SV_ViewportArrayIndex` output, which pixel shaders written for SPS/MVR read to select per-eye data (on NVIDIA, the fast geometry shader provided it).

Games like iRacing may also gate exposing SPS/MVR to the user after detecting that the GPU is NVIDIA (as opposed to simply querying NVAPI). The GPU vendor ID is spoofed to return NVIDIA, and other NVAPI functions (such as DRS in the case of iRacing) are mocked up.

## Building

The Visual Studio solution is self-contained, but be sure to initialize Git submodules after checking out the repository! Output is placed in the `bin/` folder. The VRWorks SDK demo app (`demo_dx11` project) can be used to quickly iterate changes, however beware that it is not representative of the complexity of a game like iRacing and will not exercise all the logic.

## Limitations

These do not seem to apply to iRacing, but may apply to other games if you try porting the code.

- Only planar projection: no Lens Matched Shading (Modified W, fast GS culling) or Multi-Res Shading. Modified W is reported as supported, because iRacing requires it to enable SMP for triple screens (without using it), but only the identity (all coefficients 0) is accepted: anything else fails with `NVAPI_NOT_SUPPORTED` and a line in the log.
- One render target slice per view cannot be expressed: AGS replicates draws to every combination of viewport and slice in its masks.
- View broadcasting is applied to the immediate context of the device passed by the application, even when it passes a deferred context.
