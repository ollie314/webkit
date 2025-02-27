#! /usr/bin/python

# Copyright (C) 2016 Apple Inc. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
# 1.  Redistributions of source code must retain the above copyright
#     notice, this list of conditions and the following disclaimer.
# 2.  Redistributions in binary form must reproduce the above copyright
#     notice, this list of conditions and the following disclaimer in the
#     documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
# THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# This tool has a couple of helpful macros to process Wasm files from the wasm.json.

from generateWasm import *
import optparse
import sys

parser = optparse.OptionParser(usage="usage: %prog <wasm.json> <WasmOps.h>")
(options, args) = parser.parse_args(sys.argv[0:])
if len(args) != 3:
    parser.error(parser.usage)

wasm = Wasm(args[0], args[1])
opcodes = wasm.opcodes
wasmOpsHFile = open(args[2], "w")


def cppMacro(wasmOpcode, value, b3Opcode):
    return " \\\n    macro(" + wasm.toCpp(wasmOpcode) + ", " + hex(int(value)) + ", " + b3Opcode + ")"


def opcodeMacroizer(filter):
    for op in wasm.opcodeIterator(filter):
        b3op = "Oops"
        if isSimple(op["opcode"]):
            b3op = op["opcode"]["b3op"]
        yield cppMacro(op["name"], op["opcode"]["value"], b3op)

defines = ["#define FOR_EACH_WASM_SPECIAL_OP(macro)"]
defines.extend([op for op in opcodeMacroizer(lambda op: op["category"] == "special" or op["category"] == "call")])
defines.append("\n\n#define FOR_EACH_WASM_CONTROL_FLOW_OP(macro)")
defines.extend([op for op in opcodeMacroizer(lambda op: op["category"] == "control")])
defines.append("\n\n#define FOR_EACH_WASM_SIMPLE_UNARY_OP(macro)")
defines.extend([op for op in opcodeMacroizer(lambda op: isUnary(op) and isSimple(op))])
defines.append("\n\n#define FOR_EACH_WASM_UNARY_OP(macro) \\\n    FOR_EACH_WASM_SIMPLE_UNARY_OP(macro)")
defines.extend([op for op in opcodeMacroizer(lambda op: isUnary(op) and not (isSimple(op)))])
defines.append("\n\n#define FOR_EACH_WASM_SIMPLE_BINARY_OP(macro)")
defines.extend([op for op in opcodeMacroizer(lambda op: isBinary(op) and isSimple(op))])
defines.append("\n\n#define FOR_EACH_WASM_BINARY_OP(macro) \\\n    FOR_EACH_WASM_SIMPLE_BINARY_OP(macro)")
defines.extend([op for op in opcodeMacroizer(lambda op: isBinary(op) and not (isSimple(op)))])
defines.append("\n\n#define FOR_EACH_WASM_MEMORY_LOAD_OP(macro)")
defines.extend([op for op in opcodeMacroizer(lambda op: (op["category"] == "memory" and len(op["return"]) == 1))])
defines.append("\n\n#define FOR_EACH_WASM_MEMORY_STORE_OP(macro)")
defines.extend([op for op in opcodeMacroizer(lambda op: (op["category"] == "memory" and len(op["return"]) == 0))])
defines.append("\n\n")

defines = "".join(defines)

opValueSet = set([op for op in wasm.opcodeIterator(lambda op: True, lambda op: opcodes[op]["value"])])
maxOpValue = max(opValueSet)


# Luckily, python does floor division rather than trunc division so this works.
def ceilDiv(a, b):
    return -(-a // b)


def bitSet():
    v = ""
    for i in range(ceilDiv(maxOpValue, 8)):
        entry = 0
        for j in range(8):
            if i * 8 + j in opValueSet:
                entry |= 1 << j
        v += (", " if i else "") + hex(entry)
    return v

validOps = bitSet()

contents = wasm.header + """

#pragma once

#if ENABLE(WEBASSEMBLY)

#include <cstdint>

namespace JSC { namespace Wasm {

""" + defines + """
#define FOR_EACH_WASM_OP(macro) \\
    FOR_EACH_WASM_SPECIAL_OP(macro) \\
    FOR_EACH_WASM_CONTROL_FLOW_OP(macro) \\
    FOR_EACH_WASM_UNARY_OP(macro) \\
    FOR_EACH_WASM_BINARY_OP(macro) \\
    FOR_EACH_WASM_MEMORY_LOAD_OP(macro) \\
    FOR_EACH_WASM_MEMORY_STORE_OP(macro)

#define CREATE_ENUM_VALUE(name, id, b3op) name = id,

enum OpType : uint8_t {
    FOR_EACH_WASM_OP(CREATE_ENUM_VALUE)
};

template<typename Int>
inline bool isValidOpType(Int i)
{
    // Bitset of valid ops.
    static const uint8_t valid[] = { """ + validOps + """ };
    return 0 <= i && i <= """ + str(maxOpValue) + """ && (valid[i / 8] & (1 << (i % 8)));
}

enum class BinaryOpType : uint8_t {
    FOR_EACH_WASM_BINARY_OP(CREATE_ENUM_VALUE)
};

enum class UnaryOpType : uint8_t {
    FOR_EACH_WASM_UNARY_OP(CREATE_ENUM_VALUE)
};

enum class LoadOpType : uint8_t {
    FOR_EACH_WASM_MEMORY_LOAD_OP(CREATE_ENUM_VALUE)
};

enum class StoreOpType : uint8_t {
    FOR_EACH_WASM_MEMORY_STORE_OP(CREATE_ENUM_VALUE)
};

#undef CREATE_ENUM_VALUE

inline bool isControlOp(OpType op)
{
    switch (op) {
#define CREATE_CASE(name, id, b3op) case OpType::name:
    FOR_EACH_WASM_CONTROL_FLOW_OP(CREATE_CASE)
        return true;
#undef CREATE_CASE
    default:
        break;
    }
    return false;
}

inline bool isSimple(UnaryOpType op)
{
    switch (op) {
#define CREATE_CASE(name, id, b3op) case UnaryOpType::name:
    FOR_EACH_WASM_SIMPLE_UNARY_OP(CREATE_CASE)
        return true;
#undef CREATE_CASE
    default:
        break;
    }
    return false;
}

inline bool isSimple(BinaryOpType op)
{
    switch (op) {
#define CREATE_CASE(name, id, b3op) case BinaryOpType::name:
    FOR_EACH_WASM_SIMPLE_BINARY_OP(CREATE_CASE)
        return true;
#undef CREATE_CASE
    default:
        break;
    }
    return false;
}

} } // namespace JSC::Wasm

#endif // ENABLE(WEBASSEMBLY)

"""

wasmOpsHFile.write(contents)
wasmOpsHFile.close()
