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
#include "DFGSpeculativeJIT.h"

#if ENABLE(DFG_JIT)

namespace JSC { namespace DFG {

template<bool strict>
GPRReg SpeculativeJIT::fillSpeculateIntInternal(NodeIndex nodeIndex, DataFormat& returnFormat)
{
    Node& node = m_jit.graph()[nodeIndex];
    VirtualRegister virtualRegister = node.virtualRegister;
    GenerationInfo& info = m_generationInfo[virtualRegister];

    switch (info.registerFormat()) {
    case DataFormatNone: {
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
            m_jit.move(constantAsJSValueAsImmPtr(nodeIndex), reg);
        } else if (node.isArgument()) {
            m_gprs.retain(gpr, virtualRegister, SpillOrderArgument);
            m_jit.loadPtr(m_jit.addressForArgument(m_jit.graph()[nodeIndex].argumentNumber()), reg);
        } else {
            DataFormat spillFormat = info.spillFormat();
            ASSERT(spillFormat & DataFormatJS);

            m_gprs.retain(gpr, virtualRegister, SpillOrderSpilled);

            if (spillFormat == DataFormatJSInteger) {
                // If we know this was spilled as an integer we can fill without checking.
                if (strict) {
                    m_jit.load32(JITCompiler::addressFor(virtualRegister), reg);
                    info.fillInteger(gpr);
                    returnFormat = DataFormatInteger;
                    return gpr;
                }
                m_jit.loadPtr(JITCompiler::addressFor(virtualRegister), reg);
                info.fillJSValue(gpr, DataFormatJSInteger);
                returnFormat = DataFormatJSInteger;
                return gpr;
            }
            m_jit.loadPtr(JITCompiler::addressFor(virtualRegister), reg);
        }

        // Fill as JSValue, and fall through.
        info.fillJSValue(gpr, DataFormatJSInteger);
        m_gprs.unlock(gpr);
    }

    case DataFormatJS: {
        // Check the value is an integer.
        GPRReg gpr = info.gpr();
        m_gprs.lock(gpr);
        JITCompiler::RegisterID reg = JITCompiler::gprToRegisterID(gpr);
        speculationCheck(m_jit.branchPtr(MacroAssembler::Below, reg, JITCompiler::tagTypeNumberRegister));
        info.fillJSValue(gpr, DataFormatJSInteger);
        // If !strict we're done, return.
        if (!strict) {
            returnFormat = DataFormatJSInteger;
            return gpr;
        }
        // else fall through & handle as DataFormatJSInteger.
        m_gprs.unlock(gpr);
    }

    case DataFormatJSInteger: {
        // In a strict fill we need to strip off the value tag.
        if (strict) {
            GPRReg gpr = info.gpr();
            GPRReg result;
            // If the register has already been locked we need to take a copy.
            // If not, we'll zero extend in place, so mark on the info that this is now type DataFormatInteger, not DataFormatJSInteger.
            if (m_gprs.isLocked(gpr))
                result = allocate();
            else {
                m_gprs.lock(gpr);
                info.fillInteger(gpr);
                result = gpr;
            }
            m_jit.zeroExtend32ToPtr(JITCompiler::gprToRegisterID(gpr), JITCompiler::gprToRegisterID(result));
            returnFormat = DataFormatInteger;
            return result;
        }

        GPRReg gpr = info.gpr();
        m_gprs.lock(gpr);
        returnFormat = DataFormatJSInteger;
        return gpr;
    }

    case DataFormatInteger: {
        GPRReg gpr = info.gpr();
        m_gprs.lock(gpr);
        returnFormat = DataFormatInteger;
        return gpr;
    }

    case DataFormatDouble:
    case DataFormatCell:
    case DataFormatJSDouble:
    case DataFormatJSCell: {
        terminateSpeculativeExecution();
        returnFormat = DataFormatInteger;
        return allocate();
    }
    }

    ASSERT_NOT_REACHED();
    return InvalidGPRReg;
}

SpeculationCheck::SpeculationCheck(MacroAssembler::Jump check, SpeculativeJIT* jit, unsigned recoveryIndex)
    : m_check(check)
    , m_nodeIndex(jit->m_compileIndex)
    , m_recoveryIndex(recoveryIndex)
{
    for (GPRReg gpr = gpr0; gpr < numberOfGPRs; next(gpr)) {
        VirtualRegister virtualRegister = jit->m_gprs.name(gpr);
        if (virtualRegister != InvalidVirtualRegister) {
            GenerationInfo& info =  jit->m_generationInfo[virtualRegister];
            m_gprInfo[gpr].nodeIndex = info.nodeIndex();
            m_gprInfo[gpr].format = info.registerFormat();
        } else
            m_gprInfo[gpr].nodeIndex = NoNode;
    }
    for (FPRReg fpr = fpr0; fpr < numberOfFPRs; next(fpr)) {
        VirtualRegister virtualRegister = jit->m_fprs.name(fpr);
        if (virtualRegister != InvalidVirtualRegister) {
            GenerationInfo& info =  jit->m_generationInfo[virtualRegister];
            ASSERT(info.registerFormat() == DataFormatDouble);
            m_fprInfo[fpr] = info.nodeIndex();
        } else
            m_fprInfo[fpr] = NoNode;
    }
}

GPRReg SpeculativeJIT::fillSpeculateInt(NodeIndex nodeIndex, DataFormat& returnFormat)
{
    return fillSpeculateIntInternal<false>(nodeIndex, returnFormat);
}

GPRReg SpeculativeJIT::fillSpeculateIntStrict(NodeIndex nodeIndex)
{
    DataFormat mustBeDataFormatInteger;
    GPRReg result = fillSpeculateIntInternal<true>(nodeIndex, mustBeDataFormatInteger);
    ASSERT(mustBeDataFormatInteger == DataFormatInteger);
    return result;
}

GPRReg SpeculativeJIT::fillSpeculateCell(NodeIndex nodeIndex)
{
    Node& node = m_jit.graph()[nodeIndex];
    VirtualRegister virtualRegister = node.virtualRegister;
    GenerationInfo& info = m_generationInfo[virtualRegister];

    switch (info.registerFormat()) {
    case DataFormatNone: {
        GPRReg gpr = allocate();
        JITCompiler::RegisterID reg = JITCompiler::gprToRegisterID(gpr);

        if (node.isConstant()) {
            m_gprs.retain(gpr, virtualRegister, SpillOrderConstant);
            JSValue jsValue = constantAsJSValue(nodeIndex);
            if (jsValue.isCell()) {
                m_jit.move(MacroAssembler::TrustedImmPtr(jsValue.asCell()), reg);
                info.fillJSValue(gpr, DataFormatJSCell);
                return gpr;
            }
            terminateSpeculativeExecution();
            return gpr;
        }
        if (node.isArgument()) {
            m_gprs.retain(gpr, virtualRegister, SpillOrderArgument);
            m_jit.loadPtr(m_jit.addressForArgument(m_jit.graph()[nodeIndex].argumentNumber()), reg);
            speculationCheck(m_jit.branchTestPtr(MacroAssembler::NonZero, reg, JITCompiler::tagMaskRegister));
            info.fillJSValue(gpr, DataFormatJSCell);
            return gpr;
        }
        ASSERT(info.spillFormat() & DataFormatJS);
        m_gprs.retain(gpr, virtualRegister, SpillOrderSpilled);
        m_jit.loadPtr(JITCompiler::addressFor(virtualRegister), reg);

        if (info.spillFormat() != DataFormatJSCell)
            speculationCheck(m_jit.branchTestPtr(MacroAssembler::NonZero, reg, JITCompiler::tagMaskRegister));
        info.fillJSValue(gpr, DataFormatJSCell);
        return gpr;
    }

    case DataFormatCell:
    case DataFormatJSCell: {
        GPRReg gpr = info.gpr();
        m_gprs.lock(gpr);
        return gpr;
    }

    case DataFormatJS: {
        GPRReg gpr = info.gpr();
        m_gprs.lock(gpr);
        JITCompiler::RegisterID reg = JITCompiler::gprToRegisterID(gpr);
        speculationCheck(m_jit.branchTestPtr(MacroAssembler::NonZero, reg, JITCompiler::tagMaskRegister));
        info.fillJSValue(gpr, DataFormatJSCell);
        return gpr;
    }

    case DataFormatJSInteger:
    case DataFormatInteger:
    case DataFormatJSDouble:
    case DataFormatDouble: {
        terminateSpeculativeExecution();
        return allocate();
    }
    }

    ASSERT_NOT_REACHED();
    return InvalidGPRReg;
}

bool SpeculativeJIT::compile(Node& node)
{
    checkConsistency();

    NodeType op = node.op;

    switch (op) {
    case Int32Constant:
    case DoubleConstant:
    case JSConstant:
        initConstantInfo(m_compileIndex);
        break;
    
    case Argument:
        initArgumentInfo(m_compileIndex);
        break;

    case BitAnd:
    case BitOr:
    case BitXor:
        if (isInt32Constant(node.child1)) {
            SpeculateIntegerOperand op2(this, node.child2);
            GPRTemporary result(this, op2);

            bitOp(op, valueOfInt32Constant(node.child1), op2.registerID(), result.registerID());

            integerResult(result.gpr(), m_compileIndex);
        } else if (isInt32Constant(node.child2)) {
            SpeculateIntegerOperand op1(this, node.child1);
            GPRTemporary result(this, op1);

            bitOp(op, valueOfInt32Constant(node.child2), op1.registerID(), result.registerID());

            integerResult(result.gpr(), m_compileIndex);
        } else {
            SpeculateIntegerOperand op1(this, node.child1);
            SpeculateIntegerOperand op2(this, node.child2);
            GPRTemporary result(this, op1, op2);

            MacroAssembler::RegisterID reg1 = op1.registerID();
            MacroAssembler::RegisterID reg2 = op2.registerID();
            bitOp(op, reg1, reg2, result.registerID());

            integerResult(result.gpr(), m_compileIndex);
        }
        break;

    case BitRShift:
    case BitLShift:
    case BitURShift:
        if (isInt32Constant(node.child2)) {
            SpeculateIntegerOperand op1(this, node.child1);
            GPRTemporary result(this, op1);

            shiftOp(op, op1.registerID(), valueOfInt32Constant(node.child2) & 0x1f, result.registerID());

            integerResult(result.gpr(), m_compileIndex);
        } else {
            // Do not allow shift amount to be used as the result, MacroAssembler does not permit this.
            SpeculateIntegerOperand op1(this, node.child1);
            SpeculateIntegerOperand op2(this, node.child2);
            GPRTemporary result(this, op1);

            MacroAssembler::RegisterID reg1 = op1.registerID();
            MacroAssembler::RegisterID reg2 = op2.registerID();
            shiftOp(op, reg1, reg2, result.registerID());

            integerResult(result.gpr(), m_compileIndex);
        }
        break;

    case UInt32ToNumber: {
        IntegerOperand op1(this, node.child1);
        GPRTemporary result(this, op1);

        // Test the operand is positive.
        speculationCheck(m_jit.branch32(MacroAssembler::LessThan, op1.registerID(), TrustedImm32(0)));

        m_jit.move(op1.registerID(), result.registerID());
        integerResult(result.gpr(), m_compileIndex, op1.format());
        break;
    }

    case NumberToInt32: {
        SpeculateIntegerOperand op1(this, node.child1);
        GPRTemporary result(this, op1);
        m_jit.move(op1.registerID(), result.registerID());
        integerResult(result.gpr(), m_compileIndex, op1.format());
        break;
    }

    case Int32ToNumber: {
        SpeculateIntegerOperand op1(this, node.child1);
        GPRTemporary result(this, op1);
        m_jit.move(op1.registerID(), result.registerID());
        integerResult(result.gpr(), m_compileIndex, op1.format());
        break;
    }
    case ValueToInt32: {
        SpeculateIntegerOperand op1(this, node.child1);
        GPRTemporary result(this, op1);
        m_jit.move(op1.registerID(), result.registerID());
        integerResult(result.gpr(), m_compileIndex, op1.format());
        break;
    }

    case ValueToNumber: {
        SpeculateIntegerOperand op1(this, node.child1);
        GPRTemporary result(this, op1);
        m_jit.move(op1.registerID(), result.registerID());
        integerResult(result.gpr(), m_compileIndex, op1.format());
        break;
    }

    case ValueAdd:
    case ArithAdd: {
        SpeculateIntegerOperand op1(this, node.child1);
        SpeculateIntegerOperand op2(this, node.child2);
        GPRTemporary result(this, op1, op2);

        GPRReg gpr1 = op1.gpr();
        GPRReg gpr2 = op2.gpr();
        GPRReg gprResult = result.gpr();
        MacroAssembler::Jump check = m_jit.branchAdd32(MacroAssembler::Overflow, JITCompiler::gprToRegisterID(gpr1), JITCompiler::gprToRegisterID(gpr2), JITCompiler::gprToRegisterID(gprResult));

        if (gpr1 == gprResult)
            speculationCheck(check, SpeculationRecovery(SpeculativeAdd, gprResult, gpr2));
        else if (gpr2 == gprResult)
            speculationCheck(check, SpeculationRecovery(SpeculativeAdd, gprResult, gpr1));
        else
            speculationCheck(check);

        integerResult(gprResult, m_compileIndex);
        break;
    }

    case ArithSub: {
        SpeculateIntegerOperand op1(this, node.child1);
        SpeculateIntegerOperand op2(this, node.child2);
        GPRTemporary result(this);

        MacroAssembler::RegisterID reg1 = op1.registerID();
        MacroAssembler::RegisterID reg2 = op2.registerID();
        speculationCheck(m_jit.branchSub32(MacroAssembler::Overflow, reg1, reg2, result.registerID()));

        integerResult(result.gpr(), m_compileIndex);
        break;
    }

    case ArithMul: {
        SpeculateIntegerOperand op1(this, node.child1);
        SpeculateIntegerOperand op2(this, node.child2);
        GPRTemporary result(this);

        MacroAssembler::RegisterID reg1 = op1.registerID();
        MacroAssembler::RegisterID reg2 = op2.registerID();
        speculationCheck(m_jit.branchMul32(MacroAssembler::Overflow, reg1, reg2, result.registerID()));
        speculationCheck(m_jit.branchTest32(MacroAssembler::Zero, result.registerID()));

        integerResult(result.gpr(), m_compileIndex);
        break;
    }

    case ArithDiv: {
        SpeculateIntegerOperand op1(this, node.child1);
        SpeculateIntegerOperand op2(this, node.child2);
        GPRTemporary result(this, op1, op2);

        terminateSpeculativeExecution();

        integerResult(result.gpr(), m_compileIndex);
        break;
    }

    case ArithMod: {
        SpeculateIntegerOperand op1(this, node.child1);
        SpeculateIntegerOperand op2(this, node.child2);
        GPRTemporary result(this, op1, op2);

        terminateSpeculativeExecution();

        integerResult(result.gpr(), m_compileIndex);
        break;
    }

    case GetByVal: {
        NodeIndex alias = node.child3;
        if (alias != NoNode) {
            // FIXME: result should be able to reuse child1, child2. Should have an 'UnusedOperand' type.
            JSValueOperand aliasedValue(this, node.child3);
            GPRTemporary result(this, aliasedValue);
            m_jit.move(aliasedValue.registerID(), result.registerID());
            jsValueResult(result.gpr(), m_compileIndex);
            break;
        }

        SpeculateCellOperand base(this, node.child1);
        SpeculateStrictInt32Operand property(this, node.child2);
        GPRTemporary storage(this);

        MacroAssembler::RegisterID baseReg = base.registerID();
        MacroAssembler::RegisterID propertyReg = property.registerID();
        MacroAssembler::RegisterID storageReg = storage.registerID();

        // Get the array storage. We haven't yet checked this is a JSArray, so this is only safe if
        // an access with offset JSArray::storageOffset() is valid for all JSCells!
        m_jit.loadPtr(MacroAssembler::Address(baseReg, JSArray::storageOffset()), storageReg);

        // Check that base is an array, and that property is contained within m_vector (< m_vectorLength).
        speculationCheck(m_jit.branchPtr(MacroAssembler::NotEqual, MacroAssembler::Address(baseReg), MacroAssembler::TrustedImmPtr(m_jit.globalData()->jsArrayVPtr)));
        speculationCheck(m_jit.branch32(MacroAssembler::AboveOrEqual, propertyReg, MacroAssembler::Address(baseReg, JSArray::vectorLengthOffset())));

        // FIXME: In cases where there are subsequent by_val accesses to the same base it might help to cache
        // the storage pointer - especially if there happens to be another register free right now. If we do so,
        // then we'll need to allocate a new temporary for result.
        GPRTemporary& result = storage;
        m_jit.loadPtr(MacroAssembler::BaseIndex(storageReg, propertyReg, MacroAssembler::ScalePtr, OBJECT_OFFSETOF(ArrayStorage, m_vector[0])), result.registerID());
        speculationCheck(m_jit.branchTestPtr(MacroAssembler::Zero, result.registerID()));

        jsValueResult(result.gpr(), m_compileIndex);
        break;
    }

    case PutByVal:
    case PutByValAlias: {
        SpeculateStrictInt32Operand property(this, node.child2);
        GPRTemporary storage(this);

        MacroAssembler::RegisterID propertyReg;
        MacroAssembler::RegisterID storageReg;

        // This block also defines the scope for base, and all bails to the non-speculative path.
        // At the end of this scope base will be release, and as such may be reused by for 'value'.
        //
        // If we've already read from this location on the speculative pass, then it cannot be beyond array bounds, or a hole.
        if (op == PutByValAlias) {
            SpeculateCellOperand base(this, node.child1);

            // Map base & property into registers, allocate a register for storage.
            propertyReg = property.registerID();
            storageReg = storage.registerID();
            MacroAssembler::RegisterID baseReg = base.registerID();

            // Get the array storage.
            m_jit.loadPtr(MacroAssembler::Address(baseReg, JSArray::storageOffset()), storageReg);
        } else {
            SpeculateCellOperand base(this, node.child1);

            // Map base & property into registers, allocate a register for storage.
            propertyReg = property.registerID();
            storageReg = storage.registerID();
            MacroAssembler::RegisterID baseReg = base.registerID();

            // Check that base is an array, and that property is contained within m_vector (< m_vectorLength).
            speculationCheck(m_jit.branchPtr(MacroAssembler::NotEqual, MacroAssembler::Address(baseReg), MacroAssembler::TrustedImmPtr(m_jit.globalData()->jsArrayVPtr)));
            speculationCheck(m_jit.branch32(MacroAssembler::AboveOrEqual, propertyReg, MacroAssembler::Address(baseReg, JSArray::vectorLengthOffset())));

            // Get the array storage.
            m_jit.loadPtr(MacroAssembler::Address(baseReg, JSArray::storageOffset()), storageReg);

            // Check if we're writing to a hole; if so increment m_numValuesInVector.
            MacroAssembler::Jump notHoleValue = m_jit.branchTestPtr(MacroAssembler::NonZero, MacroAssembler::BaseIndex(storageReg, propertyReg, MacroAssembler::ScalePtr, OBJECT_OFFSETOF(ArrayStorage, m_vector[0])));
            m_jit.add32(TrustedImm32(1), MacroAssembler::Address(storageReg, OBJECT_OFFSETOF(ArrayStorage, m_numValuesInVector)));

            // If we're writing to a hole we might be growing the array; 
            MacroAssembler::Jump lengthDoesNotNeedUpdate = m_jit.branch32(MacroAssembler::Below, propertyReg, MacroAssembler::Address(storageReg, OBJECT_OFFSETOF(ArrayStorage, m_length)));
            m_jit.add32(TrustedImm32(1), propertyReg);
            m_jit.store32(propertyReg, MacroAssembler::Address(storageReg, OBJECT_OFFSETOF(ArrayStorage, m_length)));
            m_jit.sub32(TrustedImm32(1), propertyReg);

            lengthDoesNotNeedUpdate.link(&m_jit);
            notHoleValue.link(&m_jit);
        }
        // After this point base goes out of scope. This may free the register.
        // As such, after this point we'd better not have any bails out to the non-speculative path!

        // Store the value to the array.
        JSValueOperand value(this, node.child3);
        MacroAssembler::RegisterID valueReg = value.registerID();
        m_jit.storePtr(valueReg, MacroAssembler::BaseIndex(storageReg, propertyReg, MacroAssembler::ScalePtr, OBJECT_OFFSETOF(ArrayStorage, m_vector[0])));

        noResult(m_compileIndex);
        break;
    }

    case Return: {
        ASSERT(JITCompiler::callFrameRegister != JITCompiler::regT1);
        ASSERT(JITCompiler::regT1 != JITCompiler::returnValueRegister);
        ASSERT(JITCompiler::returnValueRegister != JITCompiler::callFrameRegister);

        // Return the result in returnValueRegister.
        JSValueOperand op1(this, node.child1);
        m_jit.move(op1.registerID(), JITCompiler::returnValueRegister);

        // Grab the return address.
        m_jit.emitGetFromCallFrameHeaderPtr(RegisterFile::ReturnPC, JITCompiler::regT1);
        // Restore our caller's "r".
        m_jit.emitGetFromCallFrameHeaderPtr(RegisterFile::CallerFrame, JITCompiler::callFrameRegister);
        // Return.
        m_jit.restoreReturnAddressBeforeReturn(JITCompiler::regT1);
        m_jit.ret();
        
        noResult(m_compileIndex);
        break;
    }

    case ConvertThis: {
        SpeculateCellOperand thisValue(this, node.child1);
        GPRTemporary temp(this);

        m_jit.loadPtr(JITCompiler::Address(thisValue.registerID(), JSCell::structureOffset()), temp.registerID());
        speculationCheck(m_jit.branchTest8(JITCompiler::NonZero, JITCompiler::Address(temp.registerID(), Structure::typeInfoFlagsOffset()), JITCompiler::TrustedImm32(NeedsThisConversion)));

        cellResult(thisValue.gpr(), m_compileIndex);
        break;
    }

    case GetById: {
        JSValueOperand base(this, node.child1);
        GPRReg baseGPR = base.gpr();
        flushRegisters();

        GPRResult result(this);
        callOperation(operationGetById, result.gpr(), baseGPR, identifier(node.identifierNumber()));
        jsValueResult(result.gpr(), m_compileIndex);
        break;
    }

    case PutById: {
        JSValueOperand base(this, node.child1);
        JSValueOperand value(this, node.child2);
        GPRReg valueGPR = value.gpr();
        GPRReg baseGPR = base.gpr();
        flushRegisters();

        callOperation(m_jit.codeBlock()->isStrictMode() ? operationPutByIdStrict : operationPutByIdNonStrict, valueGPR, baseGPR, identifier(node.identifierNumber()));
        noResult(m_compileIndex);
        break;
    }

    case PutByIdDirect: {
        JSValueOperand base(this, node.child1);
        JSValueOperand value(this, node.child2);
        GPRReg valueGPR = value.gpr();
        GPRReg baseGPR = base.gpr();
        flushRegisters();

        callOperation(m_jit.codeBlock()->isStrictMode() ? operationPutByIdDirectStrict : operationPutByIdDirectNonStrict, valueGPR, baseGPR, identifier(node.identifierNumber()));
        noResult(m_compileIndex);
        break;
    }

    case GetGlobalVar: {
        GPRTemporary result(this);

        JSVariableObject* globalObject = m_jit.codeBlock()->globalObject();
        m_jit.loadPtr(globalObject->addressOfRegisters(), result.registerID());
        m_jit.loadPtr(JITCompiler::addressForGlobalVar(result.registerID(), node.varNumber()), result.registerID());

        jsValueResult(result.gpr(), m_compileIndex);
        break;
    }

    case PutGlobalVar: {
        JSValueOperand value(this, node.child1);
        GPRTemporary temp(this);

        JSVariableObject* globalObject = m_jit.codeBlock()->globalObject();
        m_jit.loadPtr(globalObject->addressOfRegisters(), temp.registerID());
        m_jit.storePtr(value.registerID(), JITCompiler::addressForGlobalVar(temp.registerID(), node.varNumber()));

        noResult(m_compileIndex);
        break;
    }
    }

    // Check if generation for the speculative path has failed catastrophically. :-)
    // In the future, we may want to throw away the code we've generated in this case.
    // For now, there is no point generating any further code, return immediately.
    if (m_didTerminate)
        return false;

    if (node.mustGenerate())
        use(m_compileIndex);

    checkConsistency();

    return true;
}

bool SpeculativeJIT::compile()
{
    ASSERT(!m_compileIndex);
    Node* nodes = m_jit.graph().begin();

    for (; m_compileIndex < m_jit.graph().size(); ++m_compileIndex) {
#if DFG_DEBUG_VERBOSE
        fprintf(stderr, "index(%d)\n", (int)m_compileIndex);
#endif

        Node& node = nodes[m_compileIndex];
        if (!node.refCount)
            continue;
        if (!compile(node))
            return false;
    }
    return true;
}

} } // namespace JSC::DFG

#endif
