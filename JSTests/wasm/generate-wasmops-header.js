// Use the JSON description of WebAssembly to generate the JavaScriptCore's WASMOps.h.

const jsonFile = 'wasm.json';
const wasm = JSON.parse(read(jsonFile));
const opcodes = wasm.opcode;

// Iterate through opcodes which match filter, and on each iteration yield ret.
const opcodeIterator = (filter, ret = op => { return {name: op, opcode: opcodes[op]}; }) => {
    return function*() {
        for (let op in opcodes)
            if (filter(opcodes[op]))
                yield ret(op);
    };
};

// WebAssembly opcode name to C++ name suitable for JSC.
const toCpp = name => {
    const camelCase = name.replace(/([^a-z0-9].)/g, c => c[1].toUpperCase());
    const CamelCase = camelCase.charAt(0).toUpperCase() + camelCase.slice(1);
    return CamelCase;
};

const cppMacro = (wasmOpcode, value, b3Opcode) => " \\\n    macro(" + toCpp(wasmOpcode) + ", 0x" + parseInt(value).toString(16) + ", " + b3Opcode + ")";

function* opcodeMacroizer(filter) {
    for (let op of opcodeIterator(filter)())
        yield cppMacro(op.name, op.opcode.value, op.opcode.b3op || "Oops");
}

const defines = [
    "#define FOR_EACH_WASM_SPECIAL_OP(macro)",
    ...opcodeMacroizer(op => op.category === "special" || op.category === "call"),
    "\n\n#define FOR_EACH_WASM_CONTROL_FLOW_OP(macro)",
    ...opcodeMacroizer(op => op.category === "control"),
    "\n\n#define FOR_EACH_WASM_UNARY_OP(macro)",
    ...opcodeMacroizer(op => op.category === "arithmetic" && op.parameter.length === 1),
    "\n\n#define FOR_EACH_WASM_BINARY_OP(macro)",
    ...opcodeMacroizer(op => (op.category === "arithmetic" || op.category === "comparison") && op.parameter.length === 2),
    "\n\n#define FOR_EACH_WASM_MEMORY_LOAD_OP(macro)",
    ...opcodeMacroizer(op => (op.category === "memory" && op.return.length === 1)),
    "\n\n#define FOR_EACH_WASM_MEMORY_STORE_OP(macro)",
    ...opcodeMacroizer(op => (op.category === "memory" && op.return.length === 0)),
    "\n\n"].join("");

const opValueSet = new Set(opcodeIterator(op => true, op => opcodes[op].value)());
const maxOpValue = Math.max(...opValueSet);
const validOps = (() => {
    // Create a bitset of valid ops.
    let v = "";
    for (let i = 0; i < maxOpValue / 8; ++i) {
        let entry = 0;
        for (let j = 0; j < 8; ++j)
            if (opValueSet.has(i * 8 + j))
                entry |= 1 << j;
        v += (i ? ", " : "") + "0x" + entry.toString(16);
    }
    return v;
})();

const template = `/*
 * Copyright (C) 2016 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. \`\`AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

// This file is auto-generated using ${jsonFile}.

#pragma once

#if ENABLE(WEBASSEMBLY)

#include <cstdint>

namespace JSC { namespace WASM {

${defines}

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
    static const uint8_t valid[] = { ${validOps} };
    return 0 <= i && i <= ${maxOpValue} && (valid[i / 8] & (1 << (i % 8)));
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

} } // namespace JSC::WASM

#endif // ENABLE(WEBASSEMBLY)
`;

print(template);
