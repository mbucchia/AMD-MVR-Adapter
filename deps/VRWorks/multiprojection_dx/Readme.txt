NVIDIA Multi-Projection SDK
----------------------------------------------------------

This package contains the following:
- demo/dx11                         DX11 API demo app showing MRS [*], LMS and SPS on screen, or on Oculus or OpenVR headsets
- demo/dx11_smp_assist              SMP Assist DX11 NVAPI demo app showing MRS [*], LMS and SPS on screen, or on Oculus or OpenVR headsets
- demo/dx12                         DX12 demo app showing MRS [*], LMS and SPS on screen, or on Oculus or OpenVR headsets
- doc/                              Explanation of how MRS, LMS and SPS work and documentation for the SDK.
- nvapi/                            NVAPI headers and libs.
- lib/                              Multi-projection SDK include and source files to add to your project.
- multiprojection_sdk_sample.sln    Visual Studio 2015 solution for the demo app.
- Readme.txt                        This file.

[*] Abbreviations:
- MRS is for Multi-Resolution Shading
- LMS is for Lens Matched Shading
- SPS is for Single Pass Stereo

System requirements:
- Windows 7 or later
- NVIDIA GP10x or later GPU
- NVIDIA graphics driver 368.12 or later
- NVIDIA graphics driver r396 series or later (for SMP Assist)
- NVIDIA graphics driver 411.63 or higher (for Turing Feature Multi-View Rendering)
- Visual Studio 2015 (any edition) is needed to build the demo app

Contact Info
------------
Please send any questions, comments or bug reports to vrsupport@nvidia.com. If submitting a bug
report, please note your OS, GPU model, and driver version.
