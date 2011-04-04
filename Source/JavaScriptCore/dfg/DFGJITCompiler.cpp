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
#include "DFGJITCompiler.h"

#if ENABLE(DFG_JIT)

#include "CodeBlock.h"
#include "DFGJITCodeGenerator.h"
#include "DFGNonSpeculativeJIT.h"
#include "DFGOperations.h"
#include "DFGRegisterBank.h"
#include "DFGSpeculativeJIT.h"
#include "JSGlobalData.h"
#include "LinkBuffer.h"

namespace JSC { namespace DFG {

// This method used to fill a numeric value to a FPR when linking speculative -> non-speculative.
void JITCompiler::fillNumericToDouble(NodeIndex nodeIndex, FPRReg fpr, GPRReg temporary)
{
    Node& node = graph()[nodeIndex];
    MacroAssembler::RegisterID tempReg = gprToRegisterID(temporary);

    // Arguments can't be know to be double, would need to have been a ValueToNumber node in the way!
    ASSERT(!node.isArgument());

    if (node.isConstant()) {
        ASSERT(node.op == DoubleConstant);
        move(MacroAssembler::ImmPtr(reinterpret_cast<void*>(reinterpretDoubleToIntptr(valueOfDoubleConstant(nodeIndex)))), tempReg);
        movePtrToDouble(tempReg, fprToRegisterID(fpr));
    } else {
        loadPtr(addressFor(node.virtualRegister), tempReg);
        Jump isInteger = branchPtr(MacroAssembler::AboveOrEqual, tempReg, tagTypeNumberRegister);
        jitAssertIsJSDouble(gpr0);
        addPtr(tagTypeNumberRegister, tempReg);
        movePtrToDouble(tempReg, fprToRegisterID(fpr));
        Jump hasUnboxedDouble = jump();
        isInteger.link(this);
        convertInt32ToDouble(tempReg, fprToRegisterID(fpr));
        hasUnboxedDouble.link(this);
    }
}

// This method used to fill an integer value to a GPR when linking speculative -> non-speculative.
void JITCompiler::fillInt32ToInteger(NodeIndex nodeIndex, GPRReg gpr)
{
    Node& node = graph()[nodeIndex];

    // Arguments can't be know to be int32, would need to have been a ValueToInt32 node in the way!
    ASSERT(!node.isArgument());

    if (node.isConstant()) {
        ASSERT(node.op == Int32Constant);
        move(MacroAssembler::Imm32(valueOfInt32Constant(nodeIndex)), gprToRegisterID(gpr));
    } else {
#if DFG_JIT_ASSERT
        // Redundant load, just so we can check the tag!
        loadPtr(addressFor(node.virtualRegister), gprToRegisterID(gpr));
        jitAssertIsJSInt32(gpr);
#endif
        load32(addressFor(node.virtualRegister), gprToRegisterID(gpr));
    }
}

// This method used to fill a JSValue to a GPR when linking speculative -> non-speculative.
void JITCompiler::fillToJS(NodeIndex nodeIndex, GPRReg gpr)
{
    Node& node = graph()[nodeIndex];

    if (node.isArgument()) {
        loadPtr(addressForArgument(node.argumentNumber()), gprToRegisterID(gpr));
        return;
    }

    if (node.isConstant()) {
        if (isInt32Constant(nodeIndex)) {
            JSValue jsValue = jsNumber(valueOfInt32Constant(nodeIndex));
            move(MacroAssembler::ImmPtr(JSValue::encode(jsValue)), gprToRegisterID(gpr));
        } else if (isDoubleConstant(nodeIndex)) {
            JSValue jsValue(JSValue::EncodeAsDouble, valueOfDoubleConstant(nodeIndex));
            move(MacroAssembler::ImmPtr(JSValue::encode(jsValue)), gprToRegisterID(gpr));
        } else {
            ASSERT(isJSConstant(nodeIndex));
            JSValue jsValue = valueOfJSConstant(nodeIndex);
            move(MacroAssembler::ImmPtr(JSValue::encode(jsValue)), gprToRegisterID(gpr));
        }
        return;
    }

    loadPtr(addressFor(node.virtualRegister), gprToRegisterID(gpr));
}

void JITCompiler::jumpFromSpeculativeToNonSpeculative(const SpeculationCheck& check, const EntryLocation& entry, SpeculationRecovery* recovery)
{
    ASSERT(check.m_nodeIndex == entry.m_nodeIndex);

    // Link the jump from the Speculative path to here.
    check.m_check.link(this);

    // Does this speculation check require any additional recovery to be performed,
    // to restore any state that has been overwritten before we enter back in to the
    // non-speculative path.
    if (recovery) {
        // The only additional recovery we currently support is for integer add operation
        ASSERT(recovery->type() == SpeculativeAdd);
        // Revert the add.
        sub32(gprToRegisterID(recovery->src()), gprToRegisterID(recovery->dest()));
    }

    // FIXME: - This is hideously inefficient!
    // Where a value is live in a register in the speculative path, and is required in a register
    // on the non-speculative path, we should not need to be spilling it and reloading (we may
    // need to spill anyway, if the value is marked as spilled on the non-speculative path).
    // This may also be spilling values that don't need spilling, e.g. are already spilled,
    // are constants, or are arguments.

    // Spill all GPRs in use by the speculative path.
    for (GPRReg gpr = gpr0; gpr < numberOfGPRs; next(gpr)) {
        NodeIndex nodeIndex = check.m_gprInfo[gpr].nodeIndex;
        if (nodeIndex == NoNode)
            continue;

        DataFormat dataFormat = check.m_gprInfo[gpr].format;
        VirtualRegister virtualRegister = graph()[nodeIndex].virtualRegister;

        ASSERT(dataFormat == DataFormatInteger || DataFormatCell || dataFormat & DataFormatJS);
        if (dataFormat == DataFormatInteger)
            orPtr(tagTypeNumberRegister, gprToRegisterID(gpr));
        storePtr(gprToRegisterID(gpr), addressFor(virtualRegister));
    }

    // Spill all FPRs in use by the speculative path.
    for (FPRReg fpr = fpr0; fpr < numberOfFPRs; next(fpr)) {
        NodeIndex nodeIndex = check.m_fprInfo[fpr];
        if (nodeIndex == NoNode)
            continue;

        VirtualRegister virtualRegister = graph()[nodeIndex].virtualRegister;

        moveDoubleToPtr(fprToRegisterID(fpr), regT0);
        subPtr(tagTypeNumberRegister, regT0);
        storePtr(regT0, addressFor(virtualRegister));
    }

    // Fill all FPRs in use by the non-speculative path.
    for (FPRReg fpr = fpr0; fpr < numberOfFPRs; next(fpr)) {
        NodeIndex nodeIndex = entry.m_fprInfo[fpr];
        if (nodeIndex == NoNode)
            continue;

        fillNumericToDouble(nodeIndex, fpr, gpr0);
    }

    // Fill all GPRs in use by the non-speculative path.
    for (GPRReg gpr = gpr0; gpr < numberOfGPRs; next(gpr)) {
        NodeIndex nodeIndex = entry.m_gprInfo[gpr].nodeIndex;
        if (nodeIndex == NoNode)
            continue;

        DataFormat dataFormat = entry.m_gprInfo[gpr].format;
        if (dataFormat == DataFormatInteger)
            fillInt32ToInteger(nodeIndex, gpr);
        else {
            ASSERT(dataFormat & DataFormatJS || dataFormat == DataFormatCell); // Treat cell as JSValue for now!
            fillToJS(nodeIndex, gpr);
            // FIXME: For subtypes of DataFormatJS, should jitAssert the subtype?
        }
    }

    // Jump into the non-speculative path.
    jump(entry.m_entry);
}

void JITCompiler::linkSpeculationChecks(SpeculativeJIT& speculative, NonSpeculativeJIT& nonSpeculative)
{
    // Iterators to walk over the set of bail outs & corresponding entry points.
    SpeculativeJIT::SpeculationCheckVector::Iterator checksIter = speculative.speculationChecks().begin();
    SpeculativeJIT::SpeculationCheckVector::Iterator checksEnd = speculative.speculationChecks().end();
    NonSpeculativeJIT::EntryLocationVector::Iterator entriesIter = nonSpeculative.entryLocations().begin();
    NonSpeculativeJIT::EntryLocationVector::Iterator entriesEnd = nonSpeculative.entryLocations().end();

    // Iterate over the speculation checks.
    while (checksIter != checksEnd) {
        // For every bail out from the speculative path, we must have provided an entry point
        // into the non-speculative one.
        ASSERT(checksIter->m_nodeIndex == entriesIter->m_nodeIndex);

        // There may be multiple bail outs that map to the same entry point!
        do {
            ASSERT(checksIter != checksEnd);
            ASSERT(entriesIter != entriesEnd);

            // Plant code to link this speculation failure.
            const SpeculationCheck& check = *checksIter;
            const EntryLocation& entry = *entriesIter;
            jumpFromSpeculativeToNonSpeculative(check, entry, speculative.speculationRecovery(check.m_recoveryIndex));
             ++checksIter;
        } while (checksIter != checksEnd && checksIter->m_nodeIndex == entriesIter->m_nodeIndex);
         ++entriesIter;
    }

    // FIXME: https://bugs.webkit.org/show_bug.cgi?id=56289
    ASSERT(!(checksIter != checksEnd));
    ASSERT(!(entriesIter != entriesEnd));
}

void JITCompiler::compileFunction(JITCode& entry, MacroAssemblerCodePtr& entryWithArityCheck)
{
    // === Stage 1 - Function header code generation ===
    //
    // This code currently matches the old JIT. In the function header we need to
    // pop the return address (since we do not allow any recursion on the machine
    // stack), and perform a fast register file check.

    // This is the main entry point, without performing an arity check.
    // FIXME: https://bugs.webkit.org/show_bug.cgi?id=56292
    // We'll need to convert the remaining cti_ style calls (specifically the register file
    // check) which will be dependent on stack layout. (We'd need to account for this in
    // both normal return code and when jumping to an exception handler).
    preserveReturnAddressAfterCall(regT2);
    emitPutToCallFrameHeader(regT2, RegisterFile::ReturnPC);
    // If we needed to perform an arity check we will already have moved the return address,
    // so enter after this.
    Label fromArityCheck(this);

    // Setup a pointer to the codeblock in the CallFrameHeader.
    emitPutImmediateToCallFrameHeader(m_codeBlock, RegisterFile::CodeBlock);

    // Plant a check that sufficient space is available in the RegisterFile.
    // FIXME: https://bugs.webkit.org/show_bug.cgi?id=56291
    addPtr(Imm32(m_codeBlock->m_numCalleeRegisters * sizeof(Register)), callFrameRegister, regT1);
    Jump registerFileCheck = branchPtr(Below, AbsoluteAddress(m_globalData->interpreter->registerFile().addressOfEnd()), regT1);
    // Return here after register file check.
    Label fromRegisterFileCheck = label();


    // === Stage 2 - Function body code generation ===
    //
    // We generate the speculative code path, followed by the non-speculative
    // code for the function. Next we need to link the two together, making
    // bail-outs from the speculative path jump to the corresponding point on
    // the non-speculative one (and generating any code necessary to juggle
    // register values around, rebox values, and ensure spilled, to match the
    // non-speculative path's requirements).

#if DFG_JIT_BREAK_ON_ENTRY
    // Handy debug tool!
    breakpoint();
#endif

    // First generate the speculative path.
    SpeculativeJIT speculative(*this);
    speculative.compile();

    // Next, generate the non-speculative path. We pass this a SpeculationCheckIndexIterator
    // to allow it to check which nodes in the graph may bail out, and may need to reenter the
    // non-speculative path.
    SpeculationCheckIndexIterator checkIterator(speculative);
    NonSpeculativeJIT nonSpeculative(*this);
    nonSpeculative.compile(checkIterator);

    // Link the bail-outs from the speculative path to the corresponding entry points into the non-speculative one.
    linkSpeculationChecks(speculative, nonSpeculative);


    // === Stage 3 - Function footer code generation ===
    //
    // Generate code to lookup and jump to exception handlers, to perform the slow
    // register file check (if the fast one in the function header fails), and
    // generate the entry point with arity check.

    // Iterate over the m_calls vector, checking for exception checks,
    // and linking them to here.
    unsigned exceptionCheckCount = 0;
    for (unsigned i = 0; i < m_calls.size(); ++i) {
        Jump& exceptionCheck = m_calls[i].m_exceptionCheck;
        if (exceptionCheck.isSet()) {
            exceptionCheck.link(this);
            ++exceptionCheckCount;
        }
    }
    // If any exception checks were linked, generate code to lookup a handler.
    if (exceptionCheckCount) {
        // lookupExceptionHandler is passed two arguments, exec (the CallFrame*), and
        // an identifier for the operation that threw the exception, which we can use
        // to look up handler information. The identifier we use is the return address
        // of the call out from JIT code that threw the exception; this is still
        // available on the stack, just below the stack pointer!
        move(callFrameRegister, argumentRegister0);
        peek(argumentRegister1, -1);
        m_calls.append(CallRecord(call(), lookupExceptionHandler));
        // lookupExceptionHandler leaves the handler CallFrame* in the returnValueRegister,
        // and the address of the handler in returnValueRegister2.
        jump(returnValueRegister2);
    }

    // Generate the register file check; if the fast check in the function head fails,
    // we need to call out to a helper function to check whether more space is available.
    // FIXME: change this from a cti call to a DFG style operation (normal C calling conventions).
    registerFileCheck.link(this);
    move(stackPointerRegister, argumentRegister0);
    poke(callFrameRegister, OBJECT_OFFSETOF(struct JITStackFrame, callFrame) / sizeof(void*));
    Call callRegisterFileCheck = call();
    jump(fromRegisterFileCheck);

    // The fast entry point into a function does not check the correct number of arguments
    // have been passed to the call (we only use the fast entry point where we can statically
    // determine the correct number of arguments have been passed, or have already checked).
    // In cases where an arity check is necessary, we enter here.
    // FIXME: change this from a cti call to a DFG style operation (normal C calling conventions).
    Label arityCheck = label();
    preserveReturnAddressAfterCall(regT2);
    emitPutToCallFrameHeader(regT2, RegisterFile::ReturnPC);
    branch32(Equal, regT1, Imm32(m_codeBlock->m_numParameters)).linkTo(fromArityCheck, this);
    move(stackPointerRegister, argumentRegister0);
    poke(callFrameRegister, OBJECT_OFFSETOF(struct JITStackFrame, callFrame) / sizeof(void*));
    Call callArityCheck = call();
    move(regT0, callFrameRegister);
    jump(fromArityCheck);


    // === Stage 4 - Link ===
    //
    // Link the code, populate data in CodeBlock data structures.

    LinkBuffer linkBuffer(this, m_globalData->executableAllocator.poolForSize(m_assembler.size()), 0);

    // Link all calls out from the JIT code to their respective functions.
    for (unsigned i = 0; i < m_calls.size(); ++i)
        linkBuffer.link(m_calls[i].m_call, m_calls[i].m_function);

    if (m_codeBlock->needsCallReturnIndices()) {
        m_codeBlock->callReturnIndexVector().reserveCapacity(exceptionCheckCount);
        for (unsigned i = 0; i < m_calls.size(); ++i) {
            if (m_calls[i].m_exceptionCheck.isSet()) {
                unsigned returnAddressOffset = linkBuffer.returnAddressOffset(m_calls[i].m_call);
                unsigned exceptionInfo = m_calls[i].m_exceptionInfo;
                m_codeBlock->callReturnIndexVector().append(CallReturnOffsetToBytecodeOffset(returnAddressOffset, exceptionInfo));
            }
        }
    }

    // FIXME: switch the register file check & arity check over to DFGOpertaion style calls, not JIT stubs.
    linkBuffer.link(callRegisterFileCheck, cti_register_file_check);
    linkBuffer.link(callArityCheck, m_codeBlock->m_isConstructor ? cti_op_construct_arityCheck : cti_op_call_arityCheck);

    entryWithArityCheck = linkBuffer.locationOf(arityCheck);
    entry = linkBuffer.finalizeCode();
}

#if DFG_JIT_ASSERT
void JITCompiler::jitAssertIsInt32(GPRReg gpr)
{
#if CPU(X86_64)
    Jump checkInt32 = branchPtr(BelowOrEqual, gprToRegisterID(gpr), TrustedImmPtr(reinterpret_cast<void*>(static_cast<uintptr_t>(0xFFFFFFFFu))));
    breakpoint();
    checkInt32.link(this);
#else
    UNUSED_PARAM(gpr);
#endif
}

void JITCompiler::jitAssertIsJSInt32(GPRReg gpr)
{
    Jump checkJSInt32 = branchPtr(AboveOrEqual, gprToRegisterID(gpr), tagTypeNumberRegister);
    breakpoint();
    checkJSInt32.link(this);
}

void JITCompiler::jitAssertIsJSNumber(GPRReg gpr)
{
    Jump checkJSNumber = branchTestPtr(MacroAssembler::NonZero, gprToRegisterID(gpr), tagTypeNumberRegister);
    breakpoint();
    checkJSNumber.link(this);
}

void JITCompiler::jitAssertIsJSDouble(GPRReg gpr)
{
    Jump checkJSInt32 = branchPtr(AboveOrEqual, gprToRegisterID(gpr), tagTypeNumberRegister);
    Jump checkJSNumber = branchTestPtr(MacroAssembler::NonZero, gprToRegisterID(gpr), tagTypeNumberRegister);
    checkJSInt32.link(this);
    breakpoint();
    checkJSNumber.link(this);
}
#endif

#if ENABLE(SAMPLING_COUNTERS) && CPU(X86_64) // Or any other 64-bit platform!
void JITCompiler::emitCount(AbstractSamplingCounter& counter, uint32_t increment)
{
    addPtr(TrustedImm32(increment), AbsoluteAddress(counter.addressOfCounter()));
}
#endif

#if ENABLE(SAMPLING_COUNTERS) && CPU(X86) // Or any other little-endian 32-bit platform!
void JITCompiler::emitCount(AbstractSamplingCounter& counter, uint32_t increment)
{
    intptr_t hiWord = reinterpret_cast<intptr_t>(counter.addressOfCounter()) + sizeof(int32_t);
    add32(TrustedImm32(increment), AbsoluteAddress(counter.addressOfCounter()));
    addWithCarry32(TrustedImm32(0), AbsoluteAddress(reinterpret_cast<void*>(hiWord)));
}
#endif

} } // namespace JSC::DFG

#endif
