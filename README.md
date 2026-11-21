# AMD-MVR-Adapter

A replacement `nvapi64.dll` that lets Direct3D 11 applications written for NVIDIA Single Pass Stereo (SPS) and Multi-View Rendering (MVR) run on AMD GPUs, using AMD's view broadcasting through the AMD GPU Services (AGS) library. Applications do not need to be recompiled. The primary target is iRacing in VR.

I coined this idea in April 2024 in an [iRacing forum post](https://forums.iracing.com/discussion/comment/522573/#Comment_522573), but never had the time to push it through. With the help of Claude AI, something that was weeks of coding before, is now possible in a much shorter amount of time.

## Implementation

NVIDIA and AMD expose the same capability (rendering several views with a single draw call) through different models. There is very little documentation on how it works on AMD, but a (legendary) blog post [AMD LiquidVR™ MultiView Rendering in Serious Sam VR](https://gpuopen.com/learn/amd-liquidvr-multiview-rendering-in-serious-sam-vr/) explains it all.

The table below summarizes the correspondences between NVIDIA and AMD details:

| | NVIDIA SPS/MVR | AMD view broadcasting |
|---|---|---|
| Enabling | `NvAPI_D3D_SetSinglePassStereoMode()` / `NvAPI_D3D_SetMultiViewMode()` | `agsDriverExtensionsDX11_SetViewBroadcastMasks()` |
| Vertex shader | Runs once, outputs one position per view (`SV_Position` + `NV_X_RIGHT` or `NV_POSITION_VIEW_n`) | Runs once per view, outputs the position of the current view |
| View routing | Viewport mask output by a "fast" geometry shader | Viewport mask given to AGS |
| View index in shaders | `SV_ViewportArrayIndex` from the fast GS | `AmdDxExtShaderIntrinsics_GetViewportIndex()` |

Translating calls on the CPU is the easy part of the process. Rewriting the vertex shaders is the complex part, and requires a DXBC parser and writer. An NVIDIA SPS/MVR vertex shader runs once and outputs the position of each view through custom semantics (`NV_X_RIGHT`, `NV_XYZW_RIGHT`, or `NV_POSITION` matching `NV_POSITION_VIEW_n`), while an AMD vertex shader with view broadcasting runs once per view. So the shader is "un-looped": it still computes the positions of all views, then uses the AMD intrinsic `AmdDxExtShaderIntrinsics_GetViewportIndex()` to select the one for the view it runs for, and writes it to `SV_Position`. The view index is also written to a new `SV_ViewportArrayIndex` output, which pixel shaders written for SPS/MVR read to select per-eye data (on NVIDIA, the fast geometry shader provided it).

## Limitations

- Only planar projection: no Lens Matched Shading (Modified W, fast GS culling) or Multi-Res Shading.
- One render target slice per view cannot be expressed: AGS replicates draws to every combination of viewport and slice in its masks.
- View broadcasting is applied to the immediate context of the AGS device, regardless of the device or context passed by the application.
