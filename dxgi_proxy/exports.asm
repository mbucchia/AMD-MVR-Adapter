; Stubs for the exports of dxgi.dll, jumping to the functions of the system dxgi.dll.
;
; A jump leaves the stack and the argument registers untouched, so the stubs work for any signature, including the
; undocumented functions.
;
; The functions are resolved on their first call, not in DllMain(): other code may call our exports before our
; DllMain() has run (for example a DLL initialized before us, that looks up dxgi.dll itself). Each entry of
; g_systemFunctions initially points to a resolver, which resolves the function, stores it, and jumps to it.

extern ResolveExport:proc

; The list of the exports, in the order of their indices (must match kExportNames in dllmain.cpp).
EXPORT_LIST macro
    EXPORT ApplyCompatResolutionQuirking, 0
    EXPORT CompatString, 1
    EXPORT CompatValue, 2
    EXPORT DXGIDumpJournal, 3
    EXPORT PIXBeginCapture, 4
    EXPORT PIXEndCapture, 5
    EXPORT PIXGetCaptureState, 6
    EXPORT SetAppCompatStringPointer, 7
    EXPORT UpdateHMDEmulationStatus, 8
    EXPORT CreateDXGIFactory, 9
    EXPORT CreateDXGIFactory1, 10
    EXPORT CreateDXGIFactory2, 11
    EXPORT DXGID3D10CreateDevice, 12
    EXPORT DXGID3D10CreateLayeredDevice, 13
    EXPORT DXGID3D10GetLayeredDeviceSize, 14
    EXPORT DXGID3D10RegisterLayers, 15
    EXPORT DXGIDeclareAdapterRemovalSupport, 16
    EXPORT DXGIDisableVBlankVirtualization, 17
    EXPORT DXGIGetDebugInterface1, 18
    EXPORT DXGIReportAdapterConfiguration, 19
endm

.code

; Resolves the function of index eax, then jumps to it with the original arguments.
ResolveAndJump proc
    ; Save the argument registers. On entry, rsp is 8 mod 16 (the return address of the call to the stub).
    push rcx
    push rdx
    push r8
    push r9
    ; 32 bytes of shadow space, 64 bytes for xmm0-3, and 8 bytes to align rsp on 16 bytes for the call.
    sub rsp, 104
    movdqu xmmword ptr [rsp + 32], xmm0
    movdqu xmmword ptr [rsp + 48], xmm1
    movdqu xmmword ptr [rsp + 64], xmm2
    movdqu xmmword ptr [rsp + 80], xmm3

    mov ecx, eax
    call ResolveExport

    movdqu xmm0, xmmword ptr [rsp + 32]
    movdqu xmm1, xmmword ptr [rsp + 48]
    movdqu xmm2, xmmword ptr [rsp + 64]
    movdqu xmm3, xmmword ptr [rsp + 80]
    add rsp, 104
    pop r9
    pop r8
    pop rdx
    pop rcx
    jmp rax
ResolveAndJump endp

; The stubs (exported through dxgi.def), and the resolvers they initially jump to.
EXPORT macro name, index
public name&_Stub
name&_Stub proc
    jmp qword ptr [g_systemFunctions + index * 8]
name&_Stub endp

name&_Resolve proc
    mov eax, index
    jmp ResolveAndJump
name&_Resolve endp
endm

EXPORT_LIST

.data

; The functions the stubs jump to, initially the resolvers.
EXPORT macro name, index
    dq name&_Resolve
endm

public g_systemFunctions
g_systemFunctions label qword
EXPORT_LIST

end
