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

namespace {

    constexpr uint32_t kDXBC = dxbc::MakeFourCC('D', 'X', 'B', 'C');
    constexpr uint32_t kISGN = dxbc::MakeFourCC('I', 'S', 'G', 'N');
    constexpr uint32_t kOSGN = dxbc::MakeFourCC('O', 'S', 'G', 'N');
    constexpr uint32_t kPCSG = dxbc::MakeFourCC('P', 'C', 'S', 'G');
    constexpr uint32_t kOSG5 = dxbc::MakeFourCC('O', 'S', 'G', '5');
    constexpr uint32_t kISG1 = dxbc::MakeFourCC('I', 'S', 'G', '1');
    constexpr uint32_t kOSG1 = dxbc::MakeFourCC('O', 'S', 'G', '1');
    constexpr uint32_t kPSG1 = dxbc::MakeFourCC('P', 'S', 'G', '1');
    constexpr uint32_t kSHDR = dxbc::MakeFourCC('S', 'H', 'D', 'R');
    constexpr uint32_t kSHEX = dxbc::MakeFourCC('S', 'H', 'E', 'X');

    // Container header: "DXBC", checksum (16 bytes), version (1), total size, chunk count, then the chunk offsets.
    constexpr size_t kHeaderSize = 32;
    constexpr size_t kChecksumEnd = 20;

    // D3D10_SB_OPCODE_CUSTOMDATA: the only instruction whose length is in the second token.
    constexpr uint32_t kOpcodeCustomData = 53;

    uint32_t ReadU32(const uint8_t* p) {
        uint32_t value;
        memcpy(&value, p, sizeof(value));
        return value;
    }

    void AppendU32(std::vector<uint8_t>& buffer, uint32_t value) {
        const auto p = reinterpret_cast<const uint8_t*>(&value);
        buffer.insert(buffer.end(), p, p + sizeof(value));
    }

    void WriteU32(std::vector<uint8_t>& buffer, size_t offset, uint32_t value) {
        memcpy(buffer.data() + offset, &value, sizeof(value));
    }

    // MD5 compression function (RFC 1321), on one 64-byte block.
    void MD5Transform(uint32_t state[4], const uint8_t block[64]) {
        static constexpr uint32_t K[64] = {
            0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
            0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
            0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
            0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
            0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
            0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
            0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
            0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
        };
        static constexpr uint32_t S[64] = {
            7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9,  14, 20, 5, 9,  14, 20,
            5, 9,  14, 20, 5, 9,  14, 20, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
            6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
        };

        uint32_t M[16];
        memcpy(M, block, sizeof(M));

        uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
        for (uint32_t i = 0; i < 64; i++) {
            uint32_t f, g;
            if (i < 16) {
                f = (b & c) | (~b & d);
                g = i;
            } else if (i < 32) {
                f = (d & b) | (~d & c);
                g = (5 * i + 1) % 16;
            } else if (i < 48) {
                f = b ^ c ^ d;
                g = (3 * i + 5) % 16;
            } else {
                f = c ^ (b | ~d);
                g = (7 * i) % 16;
            }
            const uint32_t temp = d;
            d = c;
            c = b;
            b = b + _rotl(a + f + K[i] + M[g], S[i]);
            a = temp;
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
    }

} // namespace

namespace dxbc {

    std::string FourCCToString(uint32_t fourCC) {
        return std::string(reinterpret_cast<const char*>(&fourCC), 4);
    }

    void ComputeChecksum(const uint8_t* container, size_t length, uint32_t checksum[4]) {
        // The hashed data starts after the checksum field. It is processed like MD5, except for the final block(s):
        // the bit count is stored at the start of the last block rather than at the end, and the last dword holds
        // (bit count / 4) | 1.
        const uint8_t* data = container + kChecksumEnd;
        const size_t size = length - kChecksumEnd;
        const uint32_t numBits = uint32_t(size * 8);
        const uint32_t numBitsPart2 = (numBits >> 2) | 1;
        const size_t leftOver = size % 64;
        const size_t fullBlocksSize = size - leftOver;

        uint32_t state[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
        for (size_t offset = 0; offset < fullBlocksSize; offset += 64) {
            MD5Transform(state, data + offset);
        }

        uint8_t block[64] = {};
        if (leftOver >= 56) {
            // No room for the bit count: pad the leftover in its own block.
            memcpy(block, data + fullBlocksSize, leftOver);
            block[leftOver] = 0x80;
            MD5Transform(state, block);

            memset(block, 0, sizeof(block));
            memcpy(block, &numBits, 4);
            memcpy(block + 60, &numBitsPart2, 4);
            MD5Transform(state, block);
        } else {
            memcpy(block, &numBits, 4);
            memcpy(block + 4, data + fullBlocksSize, leftOver);
            block[4 + leftOver] = 0x80;
            memcpy(block + 60, &numBitsPart2, 4);
            MD5Transform(state, block);
        }

        memcpy(checksum, state, sizeof(state));
    }

    bool Container::Parse(const void* bytecode, size_t length) {
        chunks.clear();
        const auto bytes = static_cast<const uint8_t*>(bytecode);
        if (!bytes || length < kHeaderSize || ReadU32(bytes) != kDXBC) {
            return false;
        }
        const uint32_t totalSize = ReadU32(bytes + 24);
        const uint32_t chunkCount = ReadU32(bytes + 28);
        if (totalSize > length || kHeaderSize + size_t(chunkCount) * 4 > totalSize) {
            return false;
        }

        for (uint32_t i = 0; i < chunkCount; i++) {
            const uint32_t offset = ReadU32(bytes + kHeaderSize + i * 4);
            if (size_t(offset) + 8 > totalSize) {
                return false;
            }
            const uint32_t chunkSize = ReadU32(bytes + offset + 4);
            if (size_t(offset) + 8 + chunkSize > totalSize) {
                return false;
            }
            Chunk chunk;
            chunk.fourCC = ReadU32(bytes + offset);
            chunk.data.assign(bytes + offset + 8, bytes + offset + 8 + chunkSize);
            chunks.push_back(std::move(chunk));
        }
        return true;
    }

    std::vector<uint8_t> Container::Serialize() const {
        std::vector<uint8_t> output;
        AppendU32(output, kDXBC);
        output.resize(kChecksumEnd); // Checksum, filled in last.
        AppendU32(output, 1);        // Version.
        AppendU32(output, 0);        // Total size, filled in below.
        AppendU32(output, uint32_t(chunks.size()));

        const size_t offsetsStart = output.size();
        output.resize(offsetsStart + chunks.size() * 4);
        for (size_t i = 0; i < chunks.size(); i++) {
            WriteU32(output, offsetsStart + i * 4, uint32_t(output.size()));
            AppendU32(output, chunks[i].fourCC);
            AppendU32(output, uint32_t(chunks[i].data.size()));
            output.insert(output.end(), chunks[i].data.begin(), chunks[i].data.end());
        }
        WriteU32(output, 24, uint32_t(output.size()));

        uint32_t checksum[4];
        ComputeChecksum(output.data(), output.size(), checksum);
        memcpy(output.data() + 4, checksum, sizeof(checksum));
        return output;
    }

    Chunk* Container::FindChunk(uint32_t fourCC) {
        for (auto& chunk : chunks) {
            if (chunk.fourCC == fourCC) {
                return &chunk;
            }
        }
        return nullptr;
    }

    const Chunk* Container::FindChunk(uint32_t fourCC) const {
        return const_cast<Container*>(this)->FindChunk(fourCC);
    }

    bool Signature::IsSignatureChunk(uint32_t fourCC) {
        return fourCC == kISGN || fourCC == kOSGN || fourCC == kPCSG || fourCC == kOSG5 || fourCC == kISG1 ||
               fourCC == kOSG1 || fourCC == kPSG1;
    }

    namespace {

        bool HasStream(uint32_t fourCC) {
            return fourCC == kOSG5 || fourCC == kISG1 || fourCC == kOSG1 || fourCC == kPSG1;
        }

        bool HasMinPrecision(uint32_t fourCC) {
            return fourCC == kISG1 || fourCC == kOSG1 || fourCC == kPSG1;
        }

        size_t ElementSize(uint32_t fourCC) {
            return 24 + (HasStream(fourCC) ? 4 : 0) + (HasMinPrecision(fourCC) ? 4 : 0);
        }

    } // namespace

    bool Signature::Parse(const Chunk& chunk) {
        fourCC = chunk.fourCC;
        elements.clear();
        if (!IsSignatureChunk(fourCC) || chunk.data.size() < 8) {
            return false;
        }
        const uint8_t* const data = chunk.data.data();
        const size_t size = chunk.data.size();
        const uint32_t count = ReadU32(data);
        const uint32_t elementsOffset = ReadU32(data + 4);
        const size_t elementSize = ElementSize(fourCC);
        if (elementsOffset + count * elementSize > size) {
            return false;
        }

        for (uint32_t i = 0; i < count; i++) {
            const uint8_t* p = data + elementsOffset + i * elementSize;
            SignatureElement element;
            if (HasStream(fourCC)) {
                element.stream = ReadU32(p);
                p += 4;
            }
            const uint32_t nameOffset = ReadU32(p);
            if (nameOffset >= size || !memchr(data + nameOffset, 0, size - nameOffset)) {
                return false;
            }
            element.semanticName = reinterpret_cast<const char*>(data + nameOffset);
            element.semanticIndex = ReadU32(p + 4);
            element.systemValue = ReadU32(p + 8);
            element.componentType = ReadU32(p + 12);
            element.registerIndex = ReadU32(p + 16);
            element.mask = p[20];
            element.readWriteMask = p[21];
            if (HasMinPrecision(fourCC)) {
                element.minPrecision = ReadU32(p + 24);
            }
            elements.push_back(std::move(element));
        }
        return true;
    }

    Chunk Signature::Serialize() const {
        Chunk chunk;
        chunk.fourCC = fourCC;
        auto& output = chunk.data;
        AppendU32(output, uint32_t(elements.size()));
        AppendU32(output, 8);

        const size_t elementSize = ElementSize(fourCC);
        const size_t elementsStart = output.size();
        output.resize(elementsStart + elements.size() * elementSize);

        // The names follow the elements. fxc stores each distinct name once, and pads the string table to a 4-byte
        // boundary with 0xAB.
        std::vector<std::pair<std::string, uint32_t>> nameOffsets;
        for (size_t i = 0; i < elements.size(); i++) {
            const SignatureElement& element = elements[i];

            uint32_t nameOffset = 0;
            const auto it = std::find_if(nameOffsets.begin(), nameOffsets.end(), [&](const auto& entry) {
                return entry.first == element.semanticName;
            });
            if (it != nameOffsets.end()) {
                nameOffset = it->second;
            } else {
                nameOffset = uint32_t(output.size());
                output.insert(output.end(), element.semanticName.begin(), element.semanticName.end());
                output.push_back(0);
                nameOffsets.emplace_back(element.semanticName, nameOffset);
            }

            size_t offset = elementsStart + i * elementSize;
            if (HasStream(fourCC)) {
                WriteU32(output, offset, element.stream);
                offset += 4;
            }
            WriteU32(output, offset, nameOffset);
            WriteU32(output, offset + 4, element.semanticIndex);
            WriteU32(output, offset + 8, element.systemValue);
            WriteU32(output, offset + 12, element.componentType);
            WriteU32(output, offset + 16, element.registerIndex);
            output[offset + 20] = element.mask;
            output[offset + 21] = element.readWriteMask;
            output[offset + 22] = 0;
            output[offset + 23] = 0;
            if (HasMinPrecision(fourCC)) {
                WriteU32(output, offset + 24, element.minPrecision);
            }
        }
        while (output.size() % 4) {
            output.push_back(0xab);
        }
        return chunk;
    }

    bool Program::IsProgramChunk(uint32_t fourCC) {
        return fourCC == kSHDR || fourCC == kSHEX;
    }

    bool Program::Parse(const Chunk& chunk) {
        fourCC = chunk.fourCC;
        instructions.clear();
        if (!IsProgramChunk(fourCC) || chunk.data.size() < 8 || chunk.data.size() % 4) {
            return false;
        }
        const auto tokens = reinterpret_cast<const uint32_t*>(chunk.data.data());
        const size_t tokenCount = chunk.data.size() / 4;
        versionToken = tokens[0];
        const uint32_t programLength = tokens[1];
        if (programLength < 2 || programLength > tokenCount) {
            return false;
        }

        size_t offset = 2;
        while (offset < programLength) {
            const uint32_t opcode = tokens[offset] & 0x7ff;
            uint32_t length;
            if (opcode == kOpcodeCustomData) {
                if (offset + 1 >= programLength) {
                    return false;
                }
                length = tokens[offset + 1];
            } else {
                length = (tokens[offset] >> 24) & 0x7f;
            }
            if (length == 0 || offset + length > programLength) {
                return false;
            }
            Instruction instruction;
            instruction.tokens.assign(tokens + offset, tokens + offset + length);
            instructions.push_back(std::move(instruction));
            offset += length;
        }
        return true;
    }

    Chunk Program::Serialize() const {
        Chunk chunk;
        chunk.fourCC = fourCC;
        size_t length = 2;
        for (const auto& instruction : instructions) {
            length += instruction.tokens.size();
        }
        auto& output = chunk.data;
        output.reserve(length * 4);
        AppendU32(output, versionToken);
        AppendU32(output, uint32_t(length));
        for (const auto& instruction : instructions) {
            for (const uint32_t token : instruction.tokens) {
                AppendU32(output, token);
            }
        }
        return chunk;
    }

} // namespace dxbc
