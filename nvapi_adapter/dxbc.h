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

    // D3D10_SB_OPCODE_TYPE / D3D11_SB_OPCODE_TYPE values we use.
    namespace opcode {
        constexpr uint32_t Call = 4;
        constexpr uint32_t CallC = 5;
        constexpr uint32_t Ieq = 32;
        constexpr uint32_t Label = 44;
        constexpr uint32_t CustomData = 53;
        constexpr uint32_t Mov = 54;
        constexpr uint32_t Movc = 55;
        constexpr uint32_t Ret = 62;
        constexpr uint32_t RetC = 63;
        constexpr uint32_t SampleL = 72;
        constexpr uint32_t DclResource = 88;
        constexpr uint32_t DclSampler = 90;
        constexpr uint32_t DclInput = 95;
        constexpr uint32_t DclOutput = 101;
        constexpr uint32_t DclOutputSgv = 102;
        constexpr uint32_t DclOutputSiv = 103;
        constexpr uint32_t DclTemps = 104;
        constexpr uint32_t DclGlobalFlags = 106;
        constexpr uint32_t InterfaceCall = 120;
        constexpr uint32_t DclResourceRaw = 161;
        constexpr uint32_t DclResourceStructured = 162;
    } // namespace opcode

    // D3D10_SB_OPERAND_TYPE values we use.
    namespace operand_type {
        constexpr uint32_t Temp = 0;
        constexpr uint32_t Input = 1;
        constexpr uint32_t Output = 2;
        constexpr uint32_t Immediate32 = 4;
        constexpr uint32_t Sampler = 6;
        constexpr uint32_t Resource = 7;
    } // namespace operand_type

    // D3D10_SB_NAME value for SV_ViewportArrayIndex (in declarations and signatures).
    constexpr uint32_t kNameViewportArrayIndex = 5;
    // D3D_NAME value for SV_Position in signatures.
    constexpr uint32_t kNamePosition = 1;
    // D3D_REGISTER_COMPONENT_TYPE value for uint in signatures.
    constexpr uint32_t kComponentTypeUInt32 = 1;

    // Whether the opcode is a declaration (dcl_*), whose operands have a declaration-specific layout.
    bool IsDeclaration(uint32_t opcode);

    // Operand token fields.
    inline uint32_t OperandType(uint32_t token) {
        return (token >> 12) & 0xff;
    }
    inline uint32_t OperandIndexDimension(uint32_t token) {
        return (token >> 20) & 0x3;
    }
    inline uint32_t OperandIndexRepresentation(uint32_t token, uint32_t dimension) {
        return (token >> (22 + 3 * dimension)) & 0x7;
    }
    inline uint32_t SetOperandType(uint32_t token, uint32_t type) {
        return (token & ~(0xffu << 12)) | (type << 12);
    }

    // A top-level operand of an instruction: the position of its operand token, and of its first index token (right
    // after the operand token and its extended operand tokens).
    struct OperandLocation {
        size_t token = 0;
        size_t firstIndex = 0;
    };

    // Encodes a 4-component swizzle, components 0 to 3 for x to w.
    constexpr uint32_t Swizzle(uint32_t x, uint32_t y, uint32_t z, uint32_t w) {
        return x | (y << 2) | (z << 4) | (w << 6);
    }
    constexpr uint32_t kSwizzleXYZW = Swizzle(0, 1, 2, 3);

    // Builders for simple operands (register with a 1D immediate index, or a scalar immediate).
    std::vector<uint32_t> DestinationOperand(uint32_t type, uint32_t index, uint8_t writeMask);
    std::vector<uint32_t> SourceOperand(uint32_t type, uint32_t index, uint32_t swizzle);
    std::vector<uint32_t> ImmediateOperand(uint32_t value);

    // One instruction (or declaration) of the shader program, as raw tokens. The first token is the opcode token.
    struct Instruction {
        std::vector<uint32_t> tokens;

        // D3D10_SB_OPCODE_TYPE / D3D11_SB_OPCODE_TYPE.
        uint32_t Opcode() const {
            return tokens[0] & 0x7ff;
        }

        // Builds a non-declaration instruction from its operands, with no extended opcode token.
        static Instruction Make(uint32_t opcode, std::initializer_list<std::vector<uint32_t>> operands);

        // Locates the top-level operands of a non-declaration instruction. Returns false if the instruction cannot be
        // decoded.
        bool GetOperands(std::vector<OperandLocation>& operands) const;
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
