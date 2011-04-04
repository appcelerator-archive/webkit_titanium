/*
 * Copyright (C) 2011 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
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

#include "config.h"
#include "DFGJITCodeGenerator.h"

#if ENABLE(DFG_JIT)

#include "DFGNonSpeculativeJIT.h"
#include "DFGSpeculativeJIT.h"
#include "LinkBuffer.h"

namespace JSC { namespace DFG {

GPRReg JITCodeGenerator::fillInteger(NodeIndex nodeIndex, DataFormat& returnFormat)
{
    Node& node = m_jit.graph()[nodeIndex];
    VirtualRegister virtualRegister = node.virtualRegister;
    GenerationInfo& info = m_generationInfo[virtualRegister];

    if (info.registerFormat() == DataFormatNone) {
        GPRReg gpr = allocate();
        JITCompiler::RegisterID reg = JITCompiler::gprToRegisterID(gpr);

        if (node.isConstant()) {
            m_gprs.retain(gpr, virtualRegister, SpillOrderConstant);
            if (isInt32Constant(nodeIndex)) {
                m_jit.move(MacroAssembler::Imm32(valueOfInt32Constant(nodeIndex)), reg);
                info.fillInteger(gpr);
                returnFormat = DataFormatInteger;
                return gpr;
            }
            if (isDoubleConstant(nodeIndex)) {
                JSValue jsValue = jsNumber(valueOfDoubleConstant(nodeIndex));
                m_jit.move(MacroAssembler::ImmPtr(JSValue::encode(jsValue)), reg);
            } else {
                ASSERT(isJSConstant(nodeIndex));
                JSValue jsValue = valueOfJSConstant(nodeIndex);
                m_jit.move(MacroAssembler::ImmPtr(JSValue::encode(jsValue)), reg);
            }
        } else if (node.isArgument()) {
            m_gprs.retain(gpr, virtualRegister, SpillOrderArgument);
            m_jit.loadPtr(m_jit.addressForArgument(m_jit.graph()[nodeIndex].argumentNumber()), reg);
        } else {
            ASSERT(info.spillFormat() == DataFormatJS || info.spillFormat() == DataFormatJSInteger);
            m_gprs.retain(gpr, virtualRegister, SpillOrderSpilled);
            m_jit.loadPtr(JITCompiler::addressFor(virtualRegister), reg);
        }

        // Since we statically know that we're filling an integer, and values
        // in the RegisterFile are boxed, this must be DataFormatJSInteger.
        // We will check this with a jitAssert below.
        info.fillJSValue(gpr, DataFormatJSInteger);
        unlock(gpr);
    }

    switch (info.registerFormat()) {
    case DataFormatNone:
        // Should have filled, above.
    case DataFormatJSDouble:
    case DataFormatDouble:
    case DataFormatJS:
    case DataFormatCell:
    case DataFormatJSCell:
        // Should only be calling this function if we know this operand to be integer.
        ASSERT_NOT_REACHED();

    case DataFormatJSInteger: {
        GPRReg gpr = info.gpr();
        m_gprs.lock(gpr);
        m_jit.jitAssertIsJSInt32(gpr);
        returnFormat = DataFormatJSInteger;
        return gpr;
    }

    case DataFormatInteger: {
        GPRReg gpr = info.gpr();
        m_gprs.lock(gpr);
        m_jit.jitAssertIsInt32(gpr);
        returnFormat = DataFormatInteger;
        return gpr;
    }
    }

    ASSERT_NOT_REACHED();
    return InvalidGPRReg;
}

FPRReg JITCodeGenerator::fillDouble(NodeIndex nodeIndex)
{
    Node& node = m_jit.graph()[nodeIndex];
    VirtualRegister virtualRegister = node.virtualRegister;
    GenerationInfo& info = m_generationInfo[virtualRegister];

    if (info.registerFormat() == DataFormatNone) {
        GPRReg gpr = allocate();
        JITCompiler::RegisterID reg = JITCompiler::gprToRegisterID(gpr);

        if (node.isConstant()) {
            if (isInt32Constant(nodeIndex)) {
                // FIXME: should not be reachable?
                m_jit.move(MacroAssembler::Imm32(valueOfInt32Constant(nodeIndex)), reg);
                m_gprs.retain(gpr, virtualRegister, SpillOrderConstant);
                info.fillInteger(gpr);
                unlock(gpr);
            } else if (isDoubleConstant(nodeIndex)) {
                FPRReg fpr = fprAllocate();
                m_jit.move(MacroAssembler::ImmPtr(reinterpret_cast<void*>(reinterpretDoubleToIntptr(valueOfDoubleConstant(nodeIndex)))), reg);
                m_jit.movePtrToDouble(reg, JITCompiler::fprToRegisterID(fpr));
                unlock(gpr);

                m_fprs.retain(fpr, virtualRegister, SpillOrderDouble);
                info.fillDouble(fpr);
                return fpr;
            } else {
                // FIXME: should not be reachable?
                ASSERT(isJSConstant(nodeIndex));
                JSValue jsValue = valueOfJSConstant(nodeIndex);
                m_jit.move(MacroAssembler::ImmPtr(JSValue::encode(jsValue)), reg);
                m_gprs.retain(gpr, virtualRegister, SpillOrderConstant);
                info.fillJSValue(gpr, DataFormatJS);
                unlock(gpr);
            }
        } else if (node.isArgument()) {
            m_gprs.retain(gpr, virtualRegister, SpillOrderArgument);
            m_jit.loadPtr(m_jit.addressForArgument(m_jit.graph()[nodeIndex].argumentNumber()), reg);
            info.fillJSValue(gpr, DataFormatJS);
            unlock(gpr);
        } else {
            DataFormat spillFormat = info.spillFormat();
            ASSERT(spillFormat & DataFormatJS);
            m_gprs.retain(gpr, virtualRegister, SpillOrderSpilled);
            m_jit.loadPtr(JITCompiler::addressFor(virtualRegister), reg);
            info.fillJSValue(gpr, m_isSpeculative ? spillFormat : DataFormatJS);
            unlock(gpr);
        }
    }

    switch (info.registerFormat()) {
    case DataFormatNone:
        // Should have filled, above.
    case DataFormatCell:
    case DataFormatJSCell:
        // Should only be calling this function if we know this operand to be numeric.
        ASSERT_NOT_REACHED();

    case DataFormatJS: {
        GPRReg jsValueGpr = info.gpr();
        m_gprs.lock(jsValueGpr);
        FPRReg fpr = fprAllocate();
        GPRReg tempGpr = allocate(); // FIXME: can we skip this allocation on the last use of the virtual register?

        JITCompiler::RegisterID jsValueReg = JITCompiler::gprToRegisterID(jsValueGpr);
        JITCompiler::FPRegisterID fpReg = JITCompiler::fprToRegisterID(fpr);
        JITCompiler::RegisterID tempReg = JITCompiler::gprToRegisterID(tempGpr);

        JITCompiler::Jump isInteger = m_jit.branchPtr(MacroAssembler::AboveOrEqual, jsValueReg, JITCompiler::tagTypeNumberRegister);

        m_jit.jitAssertIsJSDouble(jsValueGpr);

        // First, if we get here we have a double encoded as a JSValue
        m_jit.move(jsValueReg, tempReg);
        m_jit.addPtr(JITCompiler::tagTypeNumberRegister, tempReg);
        m_jit.movePtrToDouble(tempReg, fpReg);
        JITCompiler::Jump hasUnboxedDouble = m_jit.jump();

        // Finally, handle integers.
        isInteger.link(&m_jit);
        m_jit.convertInt32ToDouble(jsValueReg, fpReg);
        hasUnboxedDouble.link(&m_jit);

        m_gprs.release(jsValueGpr);
        m_gprs.unlock(jsValueGpr);
        m_gprs.unlock(tempGpr);
        m_fprs.retain(fpr, virtualRegister, SpillOrderDouble);
        info.fillDouble(fpr);
        return fpr;
    }

    case DataFormatJSInteger:
    case DataFormatInteger: {
        FPRReg fpr = fprAllocate();
        GPRReg gpr = info.gpr();
        m_gprs.lock(gpr);
        JITCompiler::RegisterID reg = JITCompiler::gprToRegisterID(gpr);
        JITCompiler::FPRegisterID fpReg = JITCompiler::fprToRegisterID(fpr);

        m_jit.convertInt32ToDouble(reg, fpReg);

        m_gprs.release(gpr);
        m_gprs.unlock(gpr);
        m_fprs.retain(fpr, virtualRegister, SpillOrderDouble);
        info.fillDouble(fpr);
        return fpr;
    }

    // Unbox the double
    case DataFormatJSDouble: {
        GPRReg gpr = info.gpr();
        FPRReg fpr = unboxDouble(gpr);

        m_gprs.release(gpr);
        m_fprs.retain(fpr, virtualRegister, SpillOrderDouble);

        info.fillDouble(fpr);
        return fpr;
    }

    case DataFormatDouble: {
        FPRReg fpr = info.fpr();
        m_fprs.lock(fpr);
        return fpr;
    }
    }

    ASSERT_NOT_REACHED();
    return InvalidFPRReg;
}

GPRReg JITCodeGenerator::fillJSValue(NodeIndex nodeIndex)
{
    Node& node = m_jit.graph()[nodeIndex];
    VirtualRegister virtualRegister = node.virtualRegister;
    GenerationInfo& info = m_generationInfo[virtualRegister];

    switch (info.registerFormat()) {
    case DataFormatNone: {
        GPRReg gpr = allocate();
        JITCompiler::RegisterID reg = JITCompiler::gprToRegisterID(gpr);

        if (node.isConstant()) {
            if (isInt32Constant(nodeIndex)) {
                info.fillJSValue(gpr, DataFormatJSInteger);
                JSValue jsValue = jsNumber(valueOfInt32Constant(nodeIndex));
                m_jit.move(MacroAssembler::ImmPtr(JSValue::encode(jsValue)), reg);
            } else if (isDoubleConstant(nodeIndex)) {
                info.fillJSValue(gpr, DataFormatJSDouble);
                JSValue jsValue(JSValue::EncodeAsDouble, valueOfDoubleConstant(nodeIndex));
                m_jit.move(MacroAssembler::ImmPtr(JSValue::encode(jsValue)), reg);
            } else {
                ASSERT(isJSConstant(nodeIndex));
                JSValue jsValue = valueOfJSConstant(nodeIndex);
                m_jit.move(MacroAssembler::ImmPtr(JSValue::encode(jsValue)), reg);
                info.fillJSValue(gpr, DataFormatJS);
            }

            m_gprs.retain(gpr, virtualRegister, SpillOrderConstant);
        } else if (node.isArgument()) {
            m_gprs.retain(gpr, virtualRegister, SpillOrderArgument);
            m_jit.loadPtr(m_jit.addressForArgument(m_jit.graph()[nodeIndex].argumentNumber()), reg);
            info.fillJSValue(gpr, DataFormatJS);
        } else {
            DataFormat spillFormat = info.spillFormat();
            ASSERT(spillFormat & DataFormatJS);
            m_gprs.retain(gpr, virtualRegister, SpillOrderSpilled);
            m_jit.loadPtr(JITCompiler::addressFor(virtualRegister), reg);
            info.fillJSValue(gpr, m_isSpeculative ? spillFormat : DataFormatJS);
        }
        return gpr;
    }

    case DataFormatInteger: {
        GPRReg gpr = info.gpr();
        m_gprs.lock(gpr);
        m_jit.orPtr(JITCompiler::tagTypeNumberRegister, JITCompiler::gprToRegisterID(gpr));
        info.fillJSValue(gpr, DataFormatJSInteger);
        return gpr;
    }

    case DataFormatDouble: {
        FPRReg fpr = info.fpr();
        GPRReg gpr = boxDouble(fpr);

        // Update all info
        info.fillJSValue(gpr, DataFormatJSDouble);
        m_fprs.release(fpr);
        m_gprs.retain(gpr, virtualRegister, SpillOrderJS);

        return gpr;
    }

    case DataFormatCell:
        // No retag required on JSVALUE64!
    case DataFormatJS:
    case DataFormatJSInteger:
    case DataFormatJSDouble:
    case DataFormatJSCell: {
        GPRReg gpr = info.gpr();
        m_gprs.lock(gpr);
        return gpr;
    }
    }

    ASSERT_NOT_REACHED();
    return InvalidGPRReg;
}

void JITCodeGenerator::useChildren(Node& node)
{
    NodeIndex child1 = node.child1;
    if (child1 == NoNode) {
        ASSERT(node.child2 == NoNode && node.child3 == NoNode);
        return;
    }
    use(child1);

    NodeIndex child2 = node.child2;
    if (child2 == NoNode) {
        ASSERT(node.child3 == NoNode);
        return;
    }
    use(child2);

    NodeIndex child3 = node.child3;
    if (child3 == NoNode)
        return;
    use(child3);
}

#ifndef NDEBUG
static const char* dataFormatString(DataFormat format)
{
    // These values correspond to the DataFormat enum.
    const char* strings[] = {
        "[  ]",
        "[ i]",
        "[ d]",
        "[ c]",
        "Err!",
        "Err!",
        "Err!",
        "Err!",
        "[J ]",
        "[Ji]",
        "[Jd]",
        "[Jc]",
        "Err!",
        "Err!",
        "Err!",
        "Err!",
    };
    return strings[format];
}

void JITCodeGenerator::dump(const char* label)
{
    if (label)
        fprintf(stderr, "<%s>\n", label);

    fprintf(stderr, "  gprs:\n");
    m_gprs.dump();
    fprintf(stderr, "  fprs:\n");
    m_fprs.dump();
    fprintf(stderr, "  VirtualRegisters:\n");
    for (unsigned i = 0; i < m_generationInfo.size(); ++i) {
        GenerationInfo& info = m_generationInfo[i];
        if (info.alive())
            fprintf(stderr, "    % 3d:%s%s\n", i, dataFormatString(info.registerFormat()), dataFormatString(info.spillFormat()));
        else
            fprintf(stderr, "    % 3d:[__][__]\n", i);
    }
    if (label)
        fprintf(stderr, "</%s>\n", label);
}
#endif


#if DFG_CONSISTENCY_CHECK
void JITCodeGenerator::checkConsistency()
{
    VirtualRegister grpContents[numberOfGPRs];
    VirtualRegister frpContents[numberOfFPRs];

    for (unsigned i = 0; i < numberOfGPRs; ++i)
        grpContents[i] = InvalidVirtualRegister;
    for (unsigned i = 0; i < numberOfFPRs; ++i)
        frpContents[i] = InvalidVirtualRegister;
    for (unsigned i = 0; i < m_generationInfo.size(); ++i) {
        GenerationInfo& info = m_generationInfo[i];
        if (!info.alive())
            continue;
        switch (info.registerFormat()) {
        case DataFormatNone:
            break;
        case DataFormatInteger:
        case DataFormatCell:
        case DataFormatJS:
        case DataFormatJSInteger:
        case DataFormatJSDouble:
        case DataFormatJSCell: {
            GPRReg gpr = info.gpr();
            ASSERT(gpr != InvalidGPRReg);
            grpContents[gpr] = (VirtualRegister)i;
            break;
        }
        case DataFormatDouble: {
            FPRReg fpr = info.fpr();
            ASSERT(fpr != InvalidFPRReg);
            frpContents[fpr] = (VirtualRegister)i;
            break;
        }
        }
    }

    for (GPRReg i = gpr0; i < numberOfGPRs; next(i)) {
        if (m_gprs.isLocked(i) || m_gprs.name(i) != grpContents[i]) {
            dump();
            CRASH();
        }
    }
    for (FPRReg i = fpr0; i < numberOfFPRs; next(i)) {
        if (m_fprs.isLocked(i) || m_fprs.name(i) != frpContents[i]) {
            dump();
            CRASH();
        }
    }
}
#endif

GPRTemporary::GPRTemporary(JITCodeGenerator* jit)
    : m_jit(jit)
    , m_gpr(InvalidGPRReg)
{
    m_gpr = m_jit->allocate();
}

GPRTemporary::GPRTemporary(JITCodeGenerator* jit, SpeculateIntegerOperand& op1)
    : m_jit(jit)
    , m_gpr(InvalidGPRReg)
{
    // locking into a register may free for reuse!
    op1.gpr();
    if (m_jit->canReuse(op1.index()))
        m_gpr = m_jit->reuse(op1.gpr());
    else
        m_gpr = m_jit->allocate();
}

GPRTemporary::GPRTemporary(JITCodeGenerator* jit, SpeculateIntegerOperand& op1, SpeculateIntegerOperand& op2)
    : m_jit(jit)
    , m_gpr(InvalidGPRReg)
{
    // locking into a register may free for reuse!
    op1.gpr();
    op2.gpr();
    if (m_jit->canReuse(op1.index()))
        m_gpr = m_jit->reuse(op1.gpr());
    else if (m_jit->canReuse(op2.index()))
        m_gpr = m_jit->reuse(op2.gpr());
    else
        m_gpr = m_jit->allocate();
}

GPRTemporary::GPRTemporary(JITCodeGenerator* jit, IntegerOperand& op1)
    : m_jit(jit)
    , m_gpr(InvalidGPRReg)
{
    // locking into a register may free for reuse!
    op1.gpr();
    if (m_jit->canReuse(op1.index()))
        m_gpr = m_jit->reuse(op1.gpr());
    else
        m_gpr = m_jit->allocate();
}

GPRTemporary::GPRTemporary(JITCodeGenerator* jit, IntegerOperand& op1, IntegerOperand& op2)
    : m_jit(jit)
    , m_gpr(InvalidGPRReg)
{
    // locking into a register may free for reuse!
    op1.gpr();
    op2.gpr();
    if (m_jit->canReuse(op1.index()))
        m_gpr = m_jit->reuse(op1.gpr());
    else if (m_jit->canReuse(op2.index()))
        m_gpr = m_jit->reuse(op2.gpr());
    else
        m_gpr = m_jit->allocate();
}

GPRTemporary::GPRTemporary(JITCodeGenerator* jit, SpeculateCellOperand& op1)
    : m_jit(jit)
    , m_gpr(InvalidGPRReg)
{
    // locking into a register may free for reuse!
    op1.gpr();
    if (m_jit->canReuse(op1.index()))
        m_gpr = m_jit->reuse(op1.gpr());
    else
        m_gpr = m_jit->allocate();
}

GPRTemporary::GPRTemporary(JITCodeGenerator* jit, JSValueOperand& op1)
    : m_jit(jit)
    , m_gpr(InvalidGPRReg)
{
    // locking into a register may free for reuse!
    op1.gpr();
    if (m_jit->canReuse(op1.index()))
        m_gpr = m_jit->reuse(op1.gpr());
    else
        m_gpr = m_jit->allocate();
}

FPRTemporary::FPRTemporary(JITCodeGenerator* jit)
    : m_jit(jit)
    , m_fpr(InvalidFPRReg)
{
    m_fpr = m_jit->fprAllocate();
}

FPRTemporary::FPRTemporary(JITCodeGenerator* jit, DoubleOperand& op1)
    : m_jit(jit)
    , m_fpr(InvalidFPRReg)
{
    // locking into a register may free for reuse!
    op1.fpr();
    if (m_jit->canReuse(op1.index()))
        m_fpr = m_jit->reuse(op1.fpr());
    else
        m_fpr = m_jit->fprAllocate();
}

FPRTemporary::FPRTemporary(JITCodeGenerator* jit, DoubleOperand& op1, DoubleOperand& op2)
    : m_jit(jit)
    , m_fpr(InvalidFPRReg)
{
    // locking into a register may free for reuse!
    op1.fpr();
    op2.fpr();
    if (m_jit->canReuse(op1.index()))
        m_fpr = m_jit->reuse(op1.fpr());
    else if (m_jit->canReuse(op2.index()))
        m_fpr = m_jit->reuse(op2.fpr());
    else
        m_fpr = m_jit->fprAllocate();
}

} } // namespace JSC::DFG

#endif
