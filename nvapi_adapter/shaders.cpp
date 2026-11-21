// MIT License
//
// Copyright (c) 2026 Matthieu Bucchianeri
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "pch.h"

#include "dxbc.h"
#include "log.h"
#include "shaders.h"

namespace {

    const char* CustomSemanticTypeName(NV_CUSTOM_SEMANTIC_TYPE type) {
        switch (type) {
        case NV_X_RIGHT_SEMANTIC:
            return "X_RIGHT";
        case NV_VIEWPORT_MASK_SEMANTIC:
            return "VIEWPORT_MASK";
        case NV_XYZW_RIGHT_SEMANTIC:
            return "XYZW_RIGHT";
        case NV_VIEWPORT_MASK_2_SEMANTIC:
            return "VIEWPORT_MASK_2";
        case NV_POSITION_SEMANTIC:
            return "POSITION";
        case NV_CLIP_DISTANCE_0_SEMANTIC:
            return "CLIP_DISTANCE_0";
        case NV_CLIP_DISTANCE_1_SEMANTIC:
            return "CLIP_DISTANCE_1";
        case NV_CULL_DISTANCE_0_SEMANTIC:
            return "CULL_DISTANCE_0";
        case NV_CULL_DISTANCE_1_SEMANTIC:
            return "CULL_DISTANCE_1";
        case NV_GENERIC_ATTRIBUTE_SEMANTIC:
            return "GENERIC_ATTRIBUTE";
        case NV_PACKED_EYE_INDEX_SEMANTIC:
            return "PACKED_EYE_INDEX";
        default:
            return "?";
        }
    }

    std::string MaskToString(uint8_t mask) {
        std::string result;
        for (int i = 0; i < 4; i++) {
            if (mask & (1 << i)) {
                result += "xyzw"[i];
            }
        }
        return result;
    }

    // A short identifier for the shader, from its DXBC checksum.
    std::string ShaderHash(const uint8_t* bytecode) {
        char hash[33];
        for (int i = 0; i < 16; i++) {
            sprintf_s(hash + i * 2, 3, "%02x", bytecode[4 + i]);
        }
        return hash;
    }

    void LogSignature(const dxbc::Signature& signature) {
        logging::Log("  %s:\n", dxbc::FourCCToString(signature.fourCC).c_str());
        for (const auto& element : signature.elements) {
            logging::Log("    %s%u: reg=%u mask=%s sv=%u type=%u stream=%u\n",
                         element.semanticName.c_str(),
                         element.semanticIndex,
                         element.registerIndex,
                         MaskToString(element.mask).c_str(),
                         element.systemValue,
                         element.componentType,
                         element.stream);
        }
    }

    void DumpDisassembly(const std::string& name, const void* bytecode, size_t length) {
        const std::wstring dataDirectory = logging::GetDataDirectory();
        if (dataDirectory.empty()) {
            return;
        }
        const std::wstring directory = dataDirectory + L"\\shaders";
        CreateDirectoryW(directory.c_str(), nullptr);
        const std::wstring path = directory + L"\\" + std::wstring(name.begin(), name.end()) + L".asm";

        Microsoft::WRL::ComPtr<ID3DBlob> disassembly;
        if (FAILED(D3DDisassemble(bytecode, length, 0, nullptr, &disassembly))) {
            logging::Log("  Failed to disassemble the shader\n");
            return;
        }
        FILE* file = nullptr;
        if (_wfopen_s(&file, path.c_str(), L"wb") == 0 && file) {
            fwrite(disassembly->GetBufferPointer(), 1, disassembly->GetBufferSize(), file);
            fclose(file);
        }
    }

} // namespace

namespace shaders {

    bool ProcessShader(const char* stage,
                       const void* bytecode,
                       size_t length,
                       NvU32 numCustomSemantics,
                       const NV_CUSTOM_SEMANTIC* customSemantics,
                       std::vector<uint8_t>& output) {
        dxbc::Container container;
        if (!container.Parse(bytecode, length)) {
            logging::Log("Failed to parse %s shader\n", stage);
            return false;
        }

        const auto bytes = static_cast<const uint8_t*>(bytecode);
        const std::string hash = ShaderHash(bytes);
        std::string chunkList;
        for (const auto& chunk : container.chunks) {
            chunkList += " " + dxbc::FourCCToString(chunk.fourCC);
        }
        logging::Log("%s shader %s (%zu bytes):%s\n", stage, hash.c_str(), length, chunkList.c_str());

        for (NvU32 i = 0; i < numCustomSemantics; i++) {
            const NV_CUSTOM_SEMANTIC& semantic = customSemantics[i];
            logging::Log("  Custom semantic: %s \"%s\"", CustomSemanticTypeName(semantic.NVCustomSemanticType),
                         semantic.NVCustomSemanticNameString);
            if (semantic.RegisterSpecified) {
                logging::Log(" (reg=%u mask=0x%x)", semantic.RegisterNum, semantic.RegisterMask);
            }
            logging::Log("\n");
        }

        // Parse and re-serialize every chunk we understand.
        for (auto& chunk : container.chunks) {
            if (dxbc::Signature::IsSignatureChunk(chunk.fourCC)) {
                dxbc::Signature signature;
                if (!signature.Parse(chunk)) {
                    logging::Log("  Failed to parse the %s chunk\n", dxbc::FourCCToString(chunk.fourCC).c_str());
                    return false;
                }
                LogSignature(signature);
                chunk = signature.Serialize();
            } else if (dxbc::Program::IsProgramChunk(chunk.fourCC)) {
                dxbc::Program program;
                if (!program.Parse(chunk)) {
                    logging::Log("  Failed to parse the %s chunk\n", dxbc::FourCCToString(chunk.fourCC).c_str());
                    return false;
                }
                logging::Log("  %s: shader model %u.%u, %zu instructions\n",
                             dxbc::FourCCToString(chunk.fourCC).c_str(),
                             program.MajorVersion(),
                             program.MinorVersion(),
                             program.instructions.size());
                chunk = program.Serialize();
            }
        }

        output = container.Serialize();

        DumpDisassembly(hash + "." + stage, bytecode, length);

#if 0
        // Parser validation: when nothing was changed, the output must be identical to the input, checksum included.
        if (output.size() != length || memcmp(output.data(), bytecode, length) != 0) {
            logging::Log("  Round-trip mismatch (%zu bytes vs %zu)!\n", output.size(), length);
            dxbc::Container original;
            original.Parse(bytecode, length);
            dxbc::Container rebuilt;
            rebuilt.Parse(output.data(), output.size());
            for (size_t i = 0; i < original.chunks.size() && i < rebuilt.chunks.size(); i++) {
                if (original.chunks[i].data != rebuilt.chunks[i].data) {
                    logging::Log("  Chunk %s differs\n", dxbc::FourCCToString(original.chunks[i].fourCC).c_str());
                }
            }
            if (memcmp(output.data() + 4, bytes + 4, 16) != 0) {
                logging::Log("  Checksum differs\n");
            }
            return false;
        }
#endif
        return true;
    }

} // namespace shaders
