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

#pragma once

// Reader/writer for DXBC shader containers (Shader Model 4/5 bytecode, as produced by fxc), enough to patch shaders:
// - the container itself (a list of chunks) and its checksum,
// - the input/output signature chunks (ISGN/OSGN, OSG5, ISG1/OSG1),
// - the shader program chunk (SHDR/SHEX), as a list of instructions (raw tokens).
// Other chunks (RDEF, STAT, SFI0...) are carried over untouched.

namespace dxbc {

    constexpr uint32_t MakeFourCC(char a, char b, char c, char d) {
        return uint32_t(uint8_t(a)) | (uint32_t(uint8_t(b)) << 8) | (uint32_t(uint8_t(c)) << 16) |
               (uint32_t(uint8_t(d)) << 24);
    }

    std::string FourCCToString(uint32_t fourCC);

    struct Chunk {
        uint32_t fourCC = 0;
        std::vector<uint8_t> data;
    };

    struct Container {
        std::vector<Chunk> chunks;

        // Returns false if the bytecode is not a valid DXBC container.
        bool Parse(const void* bytecode, size_t length);

        // Serializes the container and computes its checksum.
        std::vector<uint8_t> Serialize() const;

        Chunk* FindChunk(uint32_t fourCC);
        const Chunk* FindChunk(uint32_t fourCC) const;
    };

    // The DXBC checksum: a variant of MD5 over the container, starting after the checksum field.
    void ComputeChecksum(const uint8_t* container, size_t length, uint32_t checksum[4]);

    // One element of an input/output signature. Field values are D3D_NAME (systemValue),
    // D3D_REGISTER_COMPONENT_TYPE (componentType) and D3D_MIN_PRECISION (minPrecision).
    struct SignatureElement {
        std::string semanticName;
        uint32_t semanticIndex = 0;
        uint32_t systemValue = 0;
        uint32_t componentType = 0;
        uint32_t registerIndex = 0;
        uint8_t mask = 0;
        uint8_t readWriteMask = 0; // Components never written (outputs) or always read (inputs).
        uint32_t stream = 0;
        uint32_t minPrecision = 0;
    };

    struct Signature {
        // The chunk type determines which fields are serialized (stream for OSG5, stream and min precision for
        // ISG1/OSG1).
        uint32_t fourCC = 0;
        std::vector<SignatureElement> elements;

        bool Parse(const Chunk& chunk);
        Chunk Serialize() const;

        static bool IsSignatureChunk(uint32_t fourCC);
    };

    // One instruction (or declaration) of the shader program, as raw tokens. The first token is the opcode token.
    struct Instruction {
        std::vector<uint32_t> tokens;

        // D3D10_SB_OPCODE_TYPE / D3D11_SB_OPCODE_TYPE.
        uint32_t Opcode() const {
            return tokens[0] & 0x7ff;
        }
    };

    struct Program {
        uint32_t fourCC = 0; // SHDR or SHEX
        uint32_t versionToken = 0;
        std::vector<Instruction> instructions;

        // D3D10_SB_TOKENIZED_PROGRAM_TYPE (0 = pixel, 1 = vertex, 2 = geometry, 3 = hull, 4 = domain, 5 = compute).
        uint32_t ProgramType() const {
            return versionToken >> 16;
        }
        uint32_t MajorVersion() const {
            return (versionToken >> 4) & 0xf;
        }
        uint32_t MinorVersion() const {
            return versionToken & 0xf;
        }

        bool Parse(const Chunk& chunk);
        Chunk Serialize() const;

        static bool IsProgramChunk(uint32_t fourCC);
    };

} // namespace dxbc
