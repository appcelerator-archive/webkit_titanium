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
#include "DFGNonSpeculativeJIT.h"

#include "DFGSpeculativeJIT.h"

#if ENABLE(DFG_JIT)

namespace JSC { namespace DFG {

const double twoToThe32 = (double)0x100000000ull;

EntryLocation::EntryLocation(MacroAssembler::Label entry, NonSpeculativeJIT* jit)
    : m_entry(entry)
    , m_nodeIndex(jit->m_compileIndex)
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

void NonSpeculativeJIT::valueToNumber(JSValueOperand& operand, FPRReg fpr)
{
    GPRReg jsValueGpr = operand.gpr();
    GPRReg tempGpr = allocate(); // FIXME: can we skip this allocation on the last use of the virtual register?

    JITCompiler::RegisterID jsValueReg = JITCompiler::gprToRegisterID(jsValueGpr);
    JITCompiler::FPRegisterID fpReg = JITCompiler::fprToRegisterID(fpr);
    JITCompiler::RegisterID tempReg = JITCompiler::gprToRegisterID(tempGpr);

    JITCompiler::Jump isInteger = m_jit.branchPtr(MacroAssembler::AboveOrEqual, jsValueReg, JITCompiler::tagTypeNumberRegister);
    JITCompiler::Jump nonNumeric = m_jit.branchTestPtr(MacroAssembler::Zero, jsValueReg, JITCompiler::tagTypeNumberRegister);

    // First, if we get here we have a double encoded as a JSValue
    m_jit.move(jsValueReg, tempReg);
    m_jit.addPtr(JITCompiler::tagTypeNumberRegister, tempReg);
    m_jit.movePtrToDouble(tempReg, fpReg);
    JITCompiler::Jump hasUnboxedDouble = m_jit.jump();

    // Next handle cells (& other JS immediates)
    nonNumeric.link(&m_jit);
    silentSpillAllRegisters(jsValueGpr);
    m_jit.move(jsValueReg, JITCompiler::argumentRegister1);
    m_jit.move(JITCompiler::callFrameRegister, JITCompiler::argumentRegister0);
    appendCallWithExceptionCheck(dfgConvertJSValueToNumber);
    m_jit.moveDouble(JITCompiler::fpReturnValueRegister, fpReg);
    silentFillAllRegisters(fpr);
    JITCompiler::Jump hasCalledToNumber = m_jit.jump();
    
    // Finally, handle integers.
    isInteger.link(&m_jit);
    m_jit.convertInt32ToDouble(jsValueReg, fpReg);
    hasUnboxedDouble.link(&m_jit);
    hasCalledToNumber.link(&m_jit);

    m_gprs.unlock(tempGpr);
}

void NonSpeculativeJIT::valueToInt32(JSValueOperand& operand, GPRReg result)
{
    GPRReg jsValueGpr = operand.gpr();

    JITCompiler::RegisterID jsValueReg = JITCompiler::gprToRegisterID(jsValueGpr);
    JITCompiler::RegisterID resultReg = JITCompiler::gprToRegisterID(result);

    JITCompiler::Jump isInteger = m_jit.branchPtr(MacroAssembler::AboveOrEqual, jsValueReg, JITCompiler::tagTypeNumberRegister);

    // First handle non-integers
    silentSpillAllRegisters(jsValueGpr);
    m_jit.move(jsValueReg, JITCompiler::argumentRegister1);
    m_jit.move(JITCompiler::callFrameRegister, JITCompiler::argumentRegister0);
    appendCallWithExceptionCheck(dfgConvertJSValueToInt32);
    m_jit.zeroExtend32ToPtr(JITCompiler::returnValueRegister, resultReg);
    silentFillAllRegisters(result);
    JITCompiler::Jump hasCalledToInt32 = m_jit.jump();
    
    // Then handle integers.
    isInteger.link(&m_jit);
    m_jit.zeroExtend32ToPtr(jsValueReg, resultReg);
    hasCalledToInt32.link(&m_jit);
}

void NonSpeculativeJIT::numberToInt32(FPRReg fpr, GPRReg gpr)
{
    JITCompiler::FPRegisterID fpReg = JITCompiler::fprToRegisterID(fpr);
    JITCompiler::RegisterID reg = JITCompiler::gprToRegisterID(gpr);

    JITCompiler::Jump truncatedToInteger = m_jit.branchTruncateDoubleToInt32(fpReg, reg, JITCompiler::BranchIfTruncateSuccessful);

    silentSpillAllRegisters(gpr); // don't really care!

    m_jit.moveDouble(fpReg, JITCompiler::fpArgumentRegister0);
    appendCallWithExceptionCheck(toInt32);
    m_jit.zeroExtend32ToPtr(JITCompiler::returnValueRegister, reg);

    silentFillAllRegisters(gpr);

    truncatedToInteger.link(&m_jit);
}

void NonSpeculativeJIT::compile(SpeculationCheckIndexIterator& checkIterator, Node& node)
{
    // ...
    if (checkIterator.hasCheckAtIndex(m_compileIndex))
        trackEntry(m_jit.label());

    checkConsistency();

    NodeType op = node.op;

    switch (op) {
    case ConvertThis: {
        JSValueOperand thisValue(this, node.child1);
        GPRReg thisGPR = thisValue.gpr();
        flushRegisters();

        GPRResult result(this);
        callOperation(operationConvertThis, result.gpr(), thisGPR);
        cellResult(result.gpr(), m_compileIndex);
        break;
    }

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
            IntegerOperand op2(this, node.child2);
            GPRTemporary result(this, op2);

            bitOp(op, valueOfInt32Constant(node.child1), op2.registerID(), result.registerID());

            integerResult(result.gpr(), m_compileIndex);
        } else if (isInt32Constant(node.child2)) {
            IntegerOperand op1(this, node.child1);
            GPRTemporary result(this, op1);

            bitOp(op, valueOfInt32Constant(node.child2), op1.registerID(), result.registerID());

            integerResult(result.gpr(), m_compileIndex);
        } else {
            IntegerOperand op1(this, node.child1);
            IntegerOperand op2(this, node.child2);
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
            IntegerOperand op1(this, node.child1);
            GPRTemporary result(this, op1);

            int shiftAmount = valueOfInt32Constant(node.child2) & 0x1f;
            // Shifts by zero should have been optimized out of the graph!
            ASSERT(shiftAmount);
            shiftOp(op, op1.registerID(), shiftAmount, result.registerID());

            integerResult(result.gpr(), m_compileIndex);
        } else {
            // Do not allow shift amount to be used as the result, MacroAssembler does not permit this.
            IntegerOperand op1(this, node.child1);
            IntegerOperand op2(this, node.child2);
            GPRTemporary result(this, op1);

            MacroAssembler::RegisterID reg1 = op1.registerID();
            MacroAssembler::RegisterID reg2 = op2.registerID();
            shiftOp(op, reg1, reg2, result.registerID());

            integerResult(result.gpr(), m_compileIndex);
        }
        break;

    case UInt32ToNumber: {
        IntegerOperand op1(this, node.child1);
        FPRTemporary result(this);
        m_jit.convertInt32ToDouble(op1.registerID(), result.registerID());

        MacroAssembler::Jump positive = m_jit.branch32(MacroAssembler::GreaterThanOrEqual, op1.registerID(), TrustedImm32(0));
        m_jit.addDouble(JITCompiler::AbsoluteAddress(&twoToThe32), result.registerID());
        positive.link(&m_jit);

        doubleResult(result.fpr(), m_compileIndex);
        break;
    }

    case Int32ToNumber: {
        IntegerOperand op1(this, node.child1);
        FPRTemporary result(this);
        m_jit.convertInt32ToDouble(op1.registerID(), result.registerID());
        doubleResult(result.fpr(), m_compileIndex);
        break;
    }

    case NumberToInt32:
    case ValueToInt32: {
        ASSERT(!isInt32Constant(node.child1));
        GenerationInfo& operandInfo = m_generationInfo[m_jit.graph()[node.child1].virtualRegister];

        switch (operandInfo.registerFormat()) {
        case DataFormatInteger: {
            IntegerOperand op1(this, node.child1);
            GPRTemporary result(this, op1);
            m_jit.move(op1.registerID(), result.registerID());
            integerResult(result.gpr(), m_compileIndex);
            break;
        }

        case DataFormatDouble: {
            DoubleOperand op1(this, node.child1);
            GPRTemporary result(this);
            numberToInt32(op1.fpr(), result.gpr());
            integerResult(result.gpr(), m_compileIndex);
            break;
        }

        default: {
            JSValueOperand op1(this, node.child1);
            GPRTemporary result(this, op1);
            op1.gpr(); // force op1 to be filled!
            result.gpr(); // force result to be allocated!
            
            switch (operandInfo.registerFormat()) {
            case DataFormatNone:
            case DataFormatInteger:
            case DataFormatDouble:
                // The operand has been filled as a JSValue; it cannot be in a !DataFormatJS state.
                CRASH();

            case DataFormatCell:
            case DataFormatJS:
            case DataFormatJSCell: {
                if (op == NumberToInt32) {
                    FPRTemporary fpTemp(this);
                    FPRReg fpr = fpTemp.fpr();

                    JITCompiler::Jump isInteger = m_jit.branchPtr(MacroAssembler::AboveOrEqual, op1.registerID(), JITCompiler::tagTypeNumberRegister);

                    m_jit.move(op1.registerID(), result.registerID());
                    m_jit.addPtr(JITCompiler::tagTypeNumberRegister, result.registerID());
                    m_jit.movePtrToDouble(result.registerID(), fpTemp.registerID());
                    numberToInt32(fpr, result.gpr());
                    JITCompiler::Jump wasDouble = m_jit.jump();
                    
                    isInteger.link(&m_jit);
                    m_jit.zeroExtend32ToPtr(op1.registerID(), result.registerID());

                    wasDouble.link(&m_jit);
                } else
                    valueToInt32(op1, result.gpr());
                integerResult(result.gpr(), m_compileIndex);
                break;
            }

            case DataFormatJSDouble: {
                FPRTemporary fpTemp(this);
                m_jit.move(op1.registerID(), result.registerID());
                m_jit.addPtr(JITCompiler::tagTypeNumberRegister, result.registerID());
                m_jit.movePtrToDouble(result.registerID(), fpTemp.registerID());
                numberToInt32(fpTemp.fpr(), result.gpr());
                integerResult(result.gpr(), m_compileIndex);
                break;
            }

            case DataFormatJSInteger: {
                m_jit.move(op1.registerID(), result.registerID());
                jsValueResult(result.gpr(), m_compileIndex, DataFormatJSInteger);
                break;
            }
            }
        }

        }
        break;
    }

    case ValueToNumber: {
        ASSERT(!isInt32Constant(node.child1));
        ASSERT(!isDoubleConstant(node.child1));
        GenerationInfo& operandInfo = m_generationInfo[m_jit.graph()[node.child1].virtualRegister];
        switch (operandInfo.registerFormat()) {
        case DataFormatNone:
        case DataFormatCell:
        case DataFormatJS:
        case DataFormatJSCell: {
            JSValueOperand op1(this, node.child1);
            FPRTemporary result(this);
            valueToNumber(op1, result.fpr());
            doubleResult(result.fpr(), m_compileIndex);
            break;
        }

        case DataFormatJSDouble:
        case DataFormatDouble: {
            DoubleOperand op1(this, node.child1);
            FPRTemporary result(this, op1);
            m_jit.moveDouble(op1.registerID(), result.registerID());
            doubleResult(result.fpr(), m_compileIndex);
            break;
        }

        case DataFormatJSInteger:
        case DataFormatInteger: {
            IntegerOperand op1(this, node.child1);
            FPRTemporary result(this);
            m_jit.convertInt32ToDouble(op1.registerID(), result.registerID());
            doubleResult(result.fpr(), m_compileIndex);
            break;
        }
        }
        break;
    }

    case ValueAdd: {
        JSValueOperand arg1(this, node.child1);
        JSValueOperand arg2(this, node.child2);
        GPRReg arg1GPR = arg1.gpr();
        GPRReg arg2GPR = arg2.gpr();
        flushRegisters();

        GPRResult result(this);
        callOperation(operationValueAdd, result.gpr(), arg1GPR, arg2GPR);

        jsValueResult(result.gpr(), m_compileIndex);
        break;
    }
        
    case ArithAdd: {
        DoubleOperand op1(this, node.child1);
        DoubleOperand op2(this, node.child2);
        FPRTemporary result(this, op1, op2);

        MacroAssembler::FPRegisterID reg1 = op1.registerID();
        MacroAssembler::FPRegisterID reg2 = op2.registerID();
        m_jit.addDouble(reg1, reg2, result.registerID());

        doubleResult(result.fpr(), m_compileIndex);
        break;
    }

    case ArithSub: {
        DoubleOperand op1(this, node.child1);
        DoubleOperand op2(this, node.child2);
        FPRTemporary result(this, op1);

        MacroAssembler::FPRegisterID reg1 = op1.registerID();
        MacroAssembler::FPRegisterID reg2 = op2.registerID();
        m_jit.subDouble(reg1, reg2, result.registerID());

        doubleResult(result.fpr(), m_compileIndex);
        break;
    }

    case ArithMul: {
        DoubleOperand op1(this, node.child1);
        DoubleOperand op2(this, node.child2);
        FPRTemporary result(this, op1, op2);

        MacroAssembler::FPRegisterID reg1 = op1.registerID();
        MacroAssembler::FPRegisterID reg2 = op2.registerID();
        m_jit.mulDouble(reg1, reg2, result.registerID());

        doubleResult(result.fpr(), m_compileIndex);
        break;
    }

    case ArithDiv: {
        DoubleOperand op1(this, node.child1);
        DoubleOperand op2(this, node.child2);
        FPRTemporary result(this, op1);

        MacroAssembler::FPRegisterID reg1 = op1.registerID();
        MacroAssembler::FPRegisterID reg2 = op2.registerID();
        m_jit.divDouble(reg1, reg2, result.registerID());

        doubleResult(result.fpr(), m_compileIndex);
        break;
    }

    case ArithMod: {
        DoubleOperand arg1(this, node.child1);
        DoubleOperand arg2(this, node.child2);
        FPRReg arg1FPR = arg1.fpr();
        FPRReg arg2FPR = arg2.fpr();
        flushRegisters();

        FPRResult result(this);
        callOperation(fmod, result.fpr(), arg1FPR, arg2FPR);

        doubleResult(result.fpr(), m_compileIndex);
        break;
    }

    case GetByVal: {
        JSValueOperand arg1(this, node.child1);
        JSValueOperand arg2(this, node.child2);
        GPRReg arg1GPR = arg1.gpr();
        GPRReg arg2GPR = arg2.gpr();
        flushRegisters();

        GPRResult result(this);
        callOperation(operationGetByVal, result.gpr(), arg1GPR, arg2GPR);

        jsValueResult(result.gpr(), m_compileIndex);
        break;
    }

    case PutByVal:
    case PutByValAlias: {
        JSValueOperand arg1(this, node.child1);
        JSValueOperand arg2(this, node.child2);
        JSValueOperand arg3(this, node.child3);
        GPRReg arg1GPR = arg1.gpr();
        GPRReg arg2GPR = arg2.gpr();
        GPRReg arg3GPR = arg3.gpr();
        flushRegisters();

        GPRResult result(this);
        callOperation(m_jit.codeBlock()->isStrictMode() ? operationPutByValStrict : operationPutByValNonStrict, arg1GPR, arg2GPR, arg3GPR);

        noResult(m_compileIndex);
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
    }

    if (node.mustGenerate())
        use(m_compileIndex);

    checkConsistency();
}

void NonSpeculativeJIT::compile(SpeculationCheckIndexIterator& checkIterator)
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
        compile(checkIterator, node);
    }
}

} } // namespace JSC::DFG

#endif
