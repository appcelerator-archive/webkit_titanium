/*
 * Copyright (C) 2009 Apple Inc. All rights reserved.
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
#include "YarrJIT.h"

#include "ASCIICType.h"
#include "LinkBuffer.h"
#include "Yarr.h"

#if ENABLE(YARR_JIT)

using namespace WTF;

namespace JSC { namespace Yarr {

class YarrGenerator : private MacroAssembler {
    friend void jitCompile(JSGlobalData*, YarrCodeBlock& jitObject, const UString& pattern, unsigned& numSubpatterns, const char*& error, bool ignoreCase, bool multiline);

#if CPU(ARM)
    static const RegisterID input = ARMRegisters::r0;
    static const RegisterID index = ARMRegisters::r1;
    static const RegisterID length = ARMRegisters::r2;
    static const RegisterID output = ARMRegisters::r4;

    static const RegisterID regT0 = ARMRegisters::r5;
    static const RegisterID regT1 = ARMRegisters::r6;

    static const RegisterID returnRegister = ARMRegisters::r0;
#elif CPU(MIPS)
    static const RegisterID input = MIPSRegisters::a0;
    static const RegisterID index = MIPSRegisters::a1;
    static const RegisterID length = MIPSRegisters::a2;
    static const RegisterID output = MIPSRegisters::a3;

    static const RegisterID regT0 = MIPSRegisters::t4;
    static const RegisterID regT1 = MIPSRegisters::t5;

    static const RegisterID returnRegister = MIPSRegisters::v0;
#elif CPU(SH4)
    static const RegisterID input = SH4Registers::r4;
    static const RegisterID index = SH4Registers::r5;
    static const RegisterID length = SH4Registers::r6;
    static const RegisterID output = SH4Registers::r7;

    static const RegisterID regT0 = SH4Registers::r0;
    static const RegisterID regT1 = SH4Registers::r1;

    static const RegisterID returnRegister = SH4Registers::r0;
#elif CPU(X86)
    static const RegisterID input = X86Registers::eax;
    static const RegisterID index = X86Registers::edx;
    static const RegisterID length = X86Registers::ecx;
    static const RegisterID output = X86Registers::edi;

    static const RegisterID regT0 = X86Registers::ebx;
    static const RegisterID regT1 = X86Registers::esi;

    static const RegisterID returnRegister = X86Registers::eax;
#elif CPU(X86_64)
    static const RegisterID input = X86Registers::edi;
    static const RegisterID index = X86Registers::esi;
    static const RegisterID length = X86Registers::edx;
    static const RegisterID output = X86Registers::ecx;

    static const RegisterID regT0 = X86Registers::eax;
    static const RegisterID regT1 = X86Registers::ebx;

    static const RegisterID returnRegister = X86Registers::eax;
#endif

    void optimizeAlternative(PatternAlternative* alternative)
    {
        if (!alternative->m_terms.size())
            return;

        for (unsigned i = 0; i < alternative->m_terms.size() - 1; ++i) {
            PatternTerm& term = alternative->m_terms[i];
            PatternTerm& nextTerm = alternative->m_terms[i + 1];

            if ((term.type == PatternTerm::TypeCharacterClass)
                && (term.quantityType == QuantifierFixedCount)
                && (nextTerm.type == PatternTerm::TypePatternCharacter)
                && (nextTerm.quantityType == QuantifierFixedCount)) {
                PatternTerm termCopy = term;
                alternative->m_terms[i] = nextTerm;
                alternative->m_terms[i + 1] = termCopy;
            }
        }
    }

    void matchCharacterClassRange(RegisterID character, JumpList& failures, JumpList& matchDest, const CharacterRange* ranges, unsigned count, unsigned* matchIndex, const UChar* matches, unsigned matchCount)
    {
        do {
            // pick which range we're going to generate
            int which = count >> 1;
            char lo = ranges[which].begin;
            char hi = ranges[which].end;

            // check if there are any ranges or matches below lo.  If not, just jl to failure -
            // if there is anything else to check, check that first, if it falls through jmp to failure.
            if ((*matchIndex < matchCount) && (matches[*matchIndex] < lo)) {
                Jump loOrAbove = branch32(GreaterThanOrEqual, character, Imm32((unsigned short)lo));

                // generate code for all ranges before this one
                if (which)
                    matchCharacterClassRange(character, failures, matchDest, ranges, which, matchIndex, matches, matchCount);

                while ((*matchIndex < matchCount) && (matches[*matchIndex] < lo)) {
                    matchDest.append(branch32(Equal, character, Imm32((unsigned short)matches[*matchIndex])));
                    ++*matchIndex;
                }
                failures.append(jump());

                loOrAbove.link(this);
            } else if (which) {
                Jump loOrAbove = branch32(GreaterThanOrEqual, character, Imm32((unsigned short)lo));

                matchCharacterClassRange(character, failures, matchDest, ranges, which, matchIndex, matches, matchCount);
                failures.append(jump());

                loOrAbove.link(this);
            } else
                failures.append(branch32(LessThan, character, Imm32((unsigned short)lo)));

            while ((*matchIndex < matchCount) && (matches[*matchIndex] <= hi))
                ++*matchIndex;

            matchDest.append(branch32(LessThanOrEqual, character, Imm32((unsigned short)hi)));
            // fall through to here, the value is above hi.

            // shuffle along & loop around if there are any more matches to handle.
            unsigned next = which + 1;
            ranges += next;
            count -= next;
        } while (count);
    }

    void matchCharacterClass(RegisterID character, JumpList& matchDest, const CharacterClass* charClass)
    {
        if (charClass->m_table) {
            ExtendedAddress tableEntry(character, reinterpret_cast<intptr_t>(charClass->m_table->m_table));
            matchDest.append(branchTest8(charClass->m_table->m_inverted ? Zero : NonZero, tableEntry));
            return;
        }
        Jump unicodeFail;
        if (charClass->m_matchesUnicode.size() || charClass->m_rangesUnicode.size()) {
            Jump isAscii = branch32(LessThanOrEqual, character, TrustedImm32(0x7f));

            if (charClass->m_matchesUnicode.size()) {
                for (unsigned i = 0; i < charClass->m_matchesUnicode.size(); ++i) {
                    UChar ch = charClass->m_matchesUnicode[i];
                    matchDest.append(branch32(Equal, character, Imm32(ch)));
                }
            }

            if (charClass->m_rangesUnicode.size()) {
                for (unsigned i = 0; i < charClass->m_rangesUnicode.size(); ++i) {
                    UChar lo = charClass->m_rangesUnicode[i].begin;
                    UChar hi = charClass->m_rangesUnicode[i].end;

                    Jump below = branch32(LessThan, character, Imm32(lo));
                    matchDest.append(branch32(LessThanOrEqual, character, Imm32(hi)));
                    below.link(this);
                }
            }

            unicodeFail = jump();
            isAscii.link(this);
        }

        if (charClass->m_ranges.size()) {
            unsigned matchIndex = 0;
            JumpList failures;
            matchCharacterClassRange(character, failures, matchDest, charClass->m_ranges.begin(), charClass->m_ranges.size(), &matchIndex, charClass->m_matches.begin(), charClass->m_matches.size());
            while (matchIndex < charClass->m_matches.size())
                matchDest.append(branch32(Equal, character, Imm32((unsigned short)charClass->m_matches[matchIndex++])));

            failures.link(this);
        } else if (charClass->m_matches.size()) {
            // optimization: gather 'a','A' etc back together, can mask & test once.
            Vector<char> matchesAZaz;

            for (unsigned i = 0; i < charClass->m_matches.size(); ++i) {
                char ch = charClass->m_matches[i];
                if (m_pattern.m_ignoreCase) {
                    if (isASCIILower(ch)) {
                        matchesAZaz.append(ch);
                        continue;
                    }
                    if (isASCIIUpper(ch))
                        continue;
                }
                matchDest.append(branch32(Equal, character, Imm32((unsigned short)ch)));
            }

            if (unsigned countAZaz = matchesAZaz.size()) {
                or32(TrustedImm32(32), character);
                for (unsigned i = 0; i < countAZaz; ++i)
                    matchDest.append(branch32(Equal, character, TrustedImm32(matchesAZaz[i])));
            }
        }

        if (charClass->m_matchesUnicode.size() || charClass->m_rangesUnicode.size())
            unicodeFail.link(this);
    }

    // Jumps if input not available; will have (incorrectly) incremented already!
    Jump jumpIfNoAvailableInput(unsigned countToCheck)
    {
        add32(Imm32(countToCheck), index);
        return branch32(Above, index, length);
    }

    Jump jumpIfAvailableInput(unsigned countToCheck)
    {
        add32(Imm32(countToCheck), index);
        return branch32(BelowOrEqual, index, length);
    }

    Jump checkInput()
    {
        return branch32(BelowOrEqual, index, length);
    }

    Jump atEndOfInput()
    {
        return branch32(Equal, index, length);
    }

    Jump notAtEndOfInput()
    {
        return branch32(NotEqual, index, length);
    }

    Jump jumpIfCharEquals(UChar ch, int inputPosition)
    {
        return branch16(Equal, BaseIndex(input, index, TimesTwo, inputPosition * sizeof(UChar)), Imm32(ch));
    }

    Jump jumpIfCharNotEquals(UChar ch, int inputPosition)
    {
        return branch16(NotEqual, BaseIndex(input, index, TimesTwo, inputPosition * sizeof(UChar)), Imm32(ch));
    }

    void readCharacter(int inputPosition, RegisterID reg)
    {
        load16(BaseIndex(input, index, TimesTwo, inputPosition * sizeof(UChar)), reg);
    }

    void storeToFrame(RegisterID reg, unsigned frameLocation)
    {
        poke(reg, frameLocation);
    }

    void storeToFrame(TrustedImm32 imm, unsigned frameLocation)
    {
        poke(imm, frameLocation);
    }

    DataLabelPtr storeToFrameWithPatch(unsigned frameLocation)
    {
        return storePtrWithPatch(TrustedImmPtr(0), Address(stackPointerRegister, frameLocation * sizeof(void*)));
    }

    void loadFromFrame(unsigned frameLocation, RegisterID reg)
    {
        peek(reg, frameLocation);
    }

    void loadFromFrameAndJump(unsigned frameLocation)
    {
        jump(Address(stackPointerRegister, frameLocation * sizeof(void*)));
    }

    struct IndirectJumpEntry {
        IndirectJumpEntry(int32_t stackOffset)
            : m_stackOffset(stackOffset)
        {
        }

        IndirectJumpEntry(int32_t stackOffset, Jump jump)
            : m_stackOffset(stackOffset)
        {
            addJump(jump);
        }

        IndirectJumpEntry(int32_t stackOffset, DataLabelPtr dataLabel)
        : m_stackOffset(stackOffset)
        {
            addDataLabel(dataLabel);
        }

        void addJump(Jump jump)
        {
            m_relJumps.append(jump);
        }
        
        void addDataLabel(DataLabelPtr dataLabel)
        {
            m_dataLabelPtrVector.append(dataLabel);
        }

        int32_t m_stackOffset;
        JumpList m_relJumps;
        Vector<DataLabelPtr, 16> m_dataLabelPtrVector;
    };

    struct AlternativeBacktrackRecord {
        DataLabelPtr dataLabel;
        Label backtrackLocation;

        AlternativeBacktrackRecord(DataLabelPtr dataLabel, Label backtrackLocation)
            : dataLabel(dataLabel)
            , backtrackLocation(backtrackLocation)
        {
        }
    };

    struct ParenthesesTail;
    struct TermGenerationState;

    struct GenerationState {
        typedef HashMap<int, IndirectJumpEntry*, WTF::IntHash<uint32_t>, UnsignedWithZeroKeyHashTraits<uint32_t> > IndirectJumpHashMap;

        GenerationState()
            : m_parenNestingLevel(0)
        {
        }

        void addIndirectJumpEntry(int32_t stackOffset, Jump jump)
        {
            IndirectJumpHashMap::iterator result = m_indirectJumpMap.find(stackOffset);

            ASSERT(stackOffset >= 0);

            uint32_t offset = static_cast<uint32_t>(stackOffset);

            if (result == m_indirectJumpMap.end())
                m_indirectJumpMap.add(offset, new IndirectJumpEntry(stackOffset, jump));
            else
                result->second->addJump(jump);
        }

        void addIndirectJumpEntry(int32_t stackOffset, JumpList jumps)
        {
            JumpList::JumpVector jumpVector = jumps.jumps();
            size_t size = jumpVector.size();
            for (size_t i = 0; i < size; ++i)
                addIndirectJumpEntry(stackOffset, jumpVector[i]);

            jumps.empty();
        }

        void addIndirectJumpEntry(int32_t stackOffset, DataLabelPtr dataLabel)
        {
            IndirectJumpHashMap::iterator result = m_indirectJumpMap.find(stackOffset);

            ASSERT(stackOffset >= 0);

            uint32_t offset = static_cast<uint32_t>(stackOffset);

            if (result == m_indirectJumpMap.end())
                m_indirectJumpMap.add(offset, new IndirectJumpEntry(stackOffset, dataLabel));
            else
                result->second->addDataLabel(dataLabel);
        }

        void emitIndirectJumpTable(MacroAssembler* masm)
        {
            for (IndirectJumpHashMap::iterator iter = m_indirectJumpMap.begin(); iter != m_indirectJumpMap.end(); ++iter) {
                IndirectJumpEntry* indJumpEntry = iter->second;
                size_t size = indJumpEntry->m_dataLabelPtrVector.size();
                if (size) {
                    // Link any associated DataLabelPtr's with indirect jump via label
                    Label hereLabel = masm->label();
                    for (size_t i = 0; i < size; ++i)
                        m_backtrackRecords.append(AlternativeBacktrackRecord(indJumpEntry->m_dataLabelPtrVector[i], hereLabel));
                }
                indJumpEntry->m_relJumps.link(masm);
                masm->jump(Address(stackPointerRegister, indJumpEntry->m_stackOffset));
                delete indJumpEntry;
            }
        }

        void incrementParenNestingLevel()
        {
            ++m_parenNestingLevel;
        }

        void decrementParenNestingLevel()
        {
            --m_parenNestingLevel;
        }

        ParenthesesTail* addParenthesesTail(PatternTerm& term, JumpList* jumpListToPriorParen)
        {
            ParenthesesTail* parenthesesTail = new ParenthesesTail(term, m_parenNestingLevel, jumpListToPriorParen);
            m_parenTails.append(parenthesesTail);
            m_parenTailsForIteration.append(parenthesesTail);

            return parenthesesTail;
        }

        void emitParenthesesTail(YarrGenerator* generator)
        {
            unsigned vectorSize = m_parenTails.size();
            bool priorBacktrackFallThrough = false;

            // Emit in reverse order so parentTail N can fall through to N-1
            for (unsigned index = vectorSize; index > 0; --index) {
                JumpList jumpsToNext;
                priorBacktrackFallThrough = m_parenTails[index-1].get()->generateCode(generator, jumpsToNext, priorBacktrackFallThrough, index > 1);
                if (index > 1)
                    jumpsToNext.linkTo(generator->label(), generator);
                else
                    addJumpsToNextInteration(jumpsToNext);
            }
            m_parenTails.clear();
        }

        void addJumpToNextInteration(Jump jump)
        {
            m_jumpsToNextInteration.append(jump);
        }

        void addJumpsToNextInteration(JumpList jumps)
        {
            m_jumpsToNextInteration.append(jumps);
        }

        void addDataLabelToNextIteration(DataLabelPtr dataLabel)
        {
            m_dataPtrsToNextIteration.append(dataLabel);
        }

        void linkToNextIteration(Label label)
        {
            m_nextIteration = label;

            for (unsigned i = 0; i < m_dataPtrsToNextIteration.size(); ++i)
                m_backtrackRecords.append(AlternativeBacktrackRecord(m_dataPtrsToNextIteration[i], m_nextIteration));

            m_dataPtrsToNextIteration.clear();

            for (unsigned i = 0; i < m_parenTailsForIteration.size(); ++i)
                m_parenTailsForIteration[i]->setNextIteration(m_nextIteration);

            m_parenTailsForIteration.clear();
        }

        void linkToNextIteration(YarrGenerator* generator)
        {
            m_jumpsToNextInteration.linkTo(m_nextIteration, generator);
        }

        int m_parenNestingLevel;
        Vector<AlternativeBacktrackRecord> m_backtrackRecords;
        IndirectJumpHashMap m_indirectJumpMap;
        Label m_nextIteration;
        Vector<OwnPtr<ParenthesesTail> > m_parenTails;
        JumpList m_jumpsToNextInteration;
        Vector<DataLabelPtr> m_dataPtrsToNextIteration;
        Vector<ParenthesesTail*> m_parenTailsForIteration;
    };

    struct BacktrackDestination {
        typedef enum {
            NoBacktrack,
            BacktrackLabel,
            BacktrackStackOffset,
            BacktrackJumpList,
            BacktrackLinked
        } BacktrackType;

        BacktrackDestination()
            : m_backtrackType(NoBacktrack)
            , m_backtrackToLabel(0)
            , m_subDataLabelPtr(0)
            , m_nextBacktrack(0)
            , m_backtrackSourceLabel(0)
            , m_backtrackSourceJumps(0)
        {
        }

        BacktrackDestination(int32_t stackOffset)
            : m_backtrackType(BacktrackStackOffset)
            , m_backtrackStackOffset(stackOffset)
            , m_backtrackToLabel(0)
            , m_subDataLabelPtr(0)
            , m_nextBacktrack(0)
            , m_backtrackSourceLabel(0)
            , m_backtrackSourceJumps(0)
        {
        }

        BacktrackDestination(Label label)
            : m_backtrackType(BacktrackLabel)
            , m_backtrackLabel(label)
            , m_backtrackToLabel(0)
            , m_subDataLabelPtr(0)
            , m_nextBacktrack(0)
            , m_backtrackSourceLabel(0)
            , m_backtrackSourceJumps(0)
        {
        }

        void clear(bool doDataLabelClear = true)
        {
            m_backtrackType = NoBacktrack;
            if (doDataLabelClear)
                clearDataLabel();
            m_nextBacktrack = 0;
        }

        void clearDataLabel()
        {
            m_dataLabelPtr = DataLabelPtr();
        }

        bool hasDestination()
        {
            return (m_backtrackType != NoBacktrack);
        }

        bool isStackOffset()
        {
            return (m_backtrackType == BacktrackStackOffset);
        }

        bool isLabel()
        {
            return (m_backtrackType == BacktrackLabel);
        }

        bool isJumpList()
        {
            return (m_backtrackType == BacktrackJumpList);
        }

        bool hasDataLabel()
        {
            return m_dataLabelPtr.isSet();
        }

        void copyTarget(BacktrackDestination& rhs, bool copyDataLabel = true)
        {
            m_backtrackType = rhs.m_backtrackType;
            if (m_backtrackType == BacktrackStackOffset)
                m_backtrackStackOffset = rhs.m_backtrackStackOffset;
            else if (m_backtrackType == BacktrackLabel)
                m_backtrackLabel = rhs.m_backtrackLabel;
            if (copyDataLabel)
                m_dataLabelPtr = rhs.m_dataLabelPtr;
            m_backtrackSourceJumps = rhs.m_backtrackSourceJumps;
            m_backtrackSourceLabel = rhs.m_backtrackSourceLabel;
        }

        void copyTo(BacktrackDestination& lhs)
        {
            lhs.m_backtrackType = m_backtrackType;
            if (m_backtrackType == BacktrackStackOffset)
                lhs.m_backtrackStackOffset = m_backtrackStackOffset;
            else if (m_backtrackType == BacktrackLabel)
                lhs.m_backtrackLabel = m_backtrackLabel;
            lhs.m_backtrackSourceJumps = m_backtrackSourceJumps;
            lhs.m_backtrackSourceLabel = m_backtrackSourceLabel;
            lhs.m_dataLabelPtr = m_dataLabelPtr;
            lhs.m_backTrackJumps = m_backTrackJumps;
        }

        void addBacktrackJump(Jump jump)
        {
            m_backTrackJumps.append(jump);
        }

        void setStackOffset(int32_t stackOffset)
        {
            m_backtrackType = BacktrackStackOffset;
            m_backtrackStackOffset = stackOffset;
        }

        void setLabel(Label label)
        {
            m_backtrackType = BacktrackLabel;
            m_backtrackLabel = label;
        }

        void setNextBacktrackLabel(Label label)
        {
            if (m_nextBacktrack)
                m_nextBacktrack->setLabel(label);
        }

        void propagateBacktrackToLabel(const BacktrackDestination& rhs)
        {
            if (!m_backtrackToLabel && rhs.m_backtrackToLabel)
                m_backtrackToLabel = rhs.m_backtrackToLabel;
        }

        void setBacktrackToLabel(Label* backtrackToLabel)
        {
            if (!m_backtrackToLabel)
                m_backtrackToLabel = backtrackToLabel;
        }

        bool hasBacktrackToLabel()
        {
            return m_backtrackToLabel;
        }

        void setBacktrackJumpList(JumpList* jumpList)
        {
            m_backtrackType = BacktrackJumpList;
            m_backtrackSourceJumps = jumpList;
        }

        void setBacktrackSourceLabel(Label* backtrackSourceLabel)
        {
            m_backtrackSourceLabel = backtrackSourceLabel;
        }

        void setDataLabel(DataLabelPtr dp)
        {
            if (m_subDataLabelPtr) {
                *m_subDataLabelPtr = dp;
                m_subDataLabelPtr = 0;
            } else {
                ASSERT(!hasDataLabel());
                m_dataLabelPtr = dp;
            }
        }

        void clearSubDataLabelPtr()
        {
            m_subDataLabelPtr = 0;
        }

        void setSubDataLabelPtr(DataLabelPtr* subDataLabelPtr)
        {
            m_subDataLabelPtr = subDataLabelPtr;
        }

        void linkToNextBacktrack(BacktrackDestination* nextBacktrack)
        {
            m_nextBacktrack = nextBacktrack;
        }

        int32_t getStackOffset()
        {
            ASSERT(m_backtrackType == BacktrackStackOffset);
            return m_backtrackStackOffset;
        }

        Label getLabel()
        {
            ASSERT(m_backtrackType == BacktrackLabel);
            return m_backtrackLabel;
        }

        JumpList& getBacktrackJumps()
        {
            return m_backTrackJumps;
        }

        DataLabelPtr& getDataLabel()
        {
            return m_dataLabelPtr;
        }

        void jumpToBacktrack(MacroAssembler* masm)
        {
            if (isJumpList()) {
                if (m_backtrackSourceLabel && (m_backtrackSourceLabel->isSet()))
                    masm->jump().linkTo(*m_backtrackSourceLabel, masm);
                else
                    m_backtrackSourceJumps->append(masm->jump());
            } else if (isStackOffset())
                masm->jump(Address(stackPointerRegister, m_backtrackStackOffset));
            else if (isLabel())
                masm->jump().linkTo(m_backtrackLabel, masm);
            else
                m_backTrackJumps.append(masm->jump());
        }

        void jumpToBacktrack(YarrGenerator* generator, Jump jump)
        {
            if (isJumpList()) {
                if (m_backtrackSourceLabel && (m_backtrackSourceLabel->isSet()))
                    jump.linkTo(*m_backtrackSourceLabel, generator);
                else
                    m_backtrackSourceJumps->append(jump);
            } else if (isStackOffset())
                generator->m_expressionState.addIndirectJumpEntry(getStackOffset(), jump);
            else if (isLabel())
                jump.linkTo(getLabel(), generator);
            else
                m_backTrackJumps.append(jump);
        }

        void jumpToBacktrack(YarrGenerator* generator, JumpList& jumps)
        {
            if (isJumpList()) {
                if (m_backtrackSourceLabel && (m_backtrackSourceLabel->isSet()))
                    jumps.linkTo(*m_backtrackSourceLabel, generator);
                else
                    m_backtrackSourceJumps->append(jumps);
            } else if (isStackOffset())
                generator->m_expressionState.addIndirectJumpEntry(getStackOffset(), jumps);
            else if (isLabel())
                jumps.linkTo(getLabel(), generator);
            else
                m_backTrackJumps.append(jumps);
        }

        bool plantJumpToBacktrackIfExists(YarrGenerator* generator)
        {
            if (isJumpList()) {
                if (m_backtrackSourceLabel && (m_backtrackSourceLabel->isSet()))
                    generator->jump(*m_backtrackSourceLabel);
                else
                    m_backtrackSourceJumps->append(generator->jump());

                return true;
            }

            if (isStackOffset()) {
                generator->jump(Address(stackPointerRegister, getStackOffset()));
                return true;
            }

            if (isLabel()) {
                generator->jump(getLabel());
                if (hasDataLabel()) {
                    generator->m_expressionState.m_backtrackRecords.append(AlternativeBacktrackRecord(getDataLabel(), getLabel()));
                    clearDataLabel();
                }
                return true;
            }

            return false;
        }

        void linkBacktrackToLabel(Label backtrackLabel)
        {
            if (m_backtrackToLabel)
                *m_backtrackToLabel = backtrackLabel;
        }

        void linkAlternativeBacktracks(YarrGenerator* generator, bool nextIteration = false)
        {
            Label hereLabel = generator->label();

            if (m_backtrackToLabel) {
                *m_backtrackToLabel = hereLabel;
                m_backtrackToLabel = 0;
            }

            m_backTrackJumps.link(generator);

            if (nextIteration)
                generator->m_expressionState.linkToNextIteration(hereLabel);

            if (hasDataLabel()) {
                generator->m_expressionState.m_backtrackRecords.append(AlternativeBacktrackRecord(getDataLabel(), hereLabel));
                // data label cleared as a result of the clear() below
            }

            clear();
        }

        void linkAlternativeBacktracksTo(YarrGenerator* generator, Label label, bool nextIteration = false)
        {
            m_backTrackJumps.linkTo(label, generator);

            if (nextIteration)
                generator->m_expressionState.linkToNextIteration(label);

            if (hasDataLabel()) {
                generator->m_expressionState.m_backtrackRecords.append(AlternativeBacktrackRecord(getDataLabel(), label));
                clearDataLabel();
            }
        }

    private:
        BacktrackType m_backtrackType;
        int32_t m_backtrackStackOffset;
        Label m_backtrackLabel;
        DataLabelPtr m_dataLabelPtr;
        Label* m_backtrackToLabel;
        DataLabelPtr* m_subDataLabelPtr;
        BacktrackDestination* m_nextBacktrack;
        Label* m_backtrackSourceLabel;
        JumpList* m_backtrackSourceJumps;
        JumpList m_backTrackJumps;
    };

    struct TermGenerationState {
        TermGenerationState(PatternDisjunction* disjunction, unsigned checkedTotal)
            : disjunction(disjunction)
            , checkedTotal(checkedTotal)
            , m_subParenNum(0)
            , m_linkedBacktrack(0)
            , m_jumpList(0)
        {
        }

        void resetAlternative()
        {
            m_backtrack.clear();
            alt = 0;
        }
        bool alternativeValid()
        {
            return alt < disjunction->m_alternatives.size();
        }
        void nextAlternative()
        {
            ++alt;
        }
        PatternAlternative* alternative()
        {
            return disjunction->m_alternatives[alt];
        }
        bool isLastAlternative()
        {
            return (alt + 1) == disjunction->m_alternatives.size();
        }

        void resetTerm()
        {
            ASSERT(alternativeValid());
            t = 0;
            m_subParenNum = 0;
        }
        bool termValid()
        {
            ASSERT(alternativeValid());
            return t < alternative()->m_terms.size();
        }
        void nextTerm()
        {
            ASSERT(alternativeValid());
            ++t;
        }
        PatternTerm& term()
        {
            ASSERT(alternativeValid());
            return alternative()->m_terms[t];
        }
        bool isLastTerm()
        {
            ASSERT(alternativeValid());
            return (t + 1) == alternative()->m_terms.size();
        }
        unsigned getSubParenNum()
        {
            return m_subParenNum++;
        }
        bool isMainDisjunction()
        {
            return !disjunction->m_parent;
        }

        void setJumpListToPriorParen(JumpList* jumpList)
        {
            m_jumpList = jumpList;
        }

        JumpList* getJumpListToPriorParen()
        {
            return m_jumpList;
        }

        PatternTerm& lookaheadTerm()
        {
            ASSERT(alternativeValid());
            ASSERT((t + 1) < alternative()->m_terms.size());
            return alternative()->m_terms[t + 1];
        }
        bool isSinglePatternCharacterLookaheadTerm()
        {
            ASSERT(alternativeValid());
            return ((t + 1) < alternative()->m_terms.size())
                && (lookaheadTerm().type == PatternTerm::TypePatternCharacter)
                && (lookaheadTerm().quantityType == QuantifierFixedCount)
                && (lookaheadTerm().quantityCount == 1);
        }

        int inputOffset()
        {
            return term().inputPosition - checkedTotal;
        }

        void clearBacktrack()
        {
            m_backtrack.clear(false);
            m_linkedBacktrack = 0;
        }

        void jumpToBacktrack(MacroAssembler* masm)
        {
            m_backtrack.jumpToBacktrack(masm);
        }

        void jumpToBacktrack(YarrGenerator* generator, Jump jump)
        {
            m_backtrack.jumpToBacktrack(generator, jump);
        }

        void jumpToBacktrack(YarrGenerator* generator, JumpList& jumps)
        {
            m_backtrack.jumpToBacktrack(generator, jumps);
        }

        bool plantJumpToBacktrackIfExists(YarrGenerator* generator)
        {
            return m_backtrack.plantJumpToBacktrackIfExists(generator);
        }

        void linkDataLabelToBacktrackIfExists(YarrGenerator* generator, DataLabelPtr dataLabel)
        {
            // If we have a stack offset backtrack destination, use it directly
            if (m_backtrack.isStackOffset()) {
                generator->m_expressionState.addIndirectJumpEntry(m_backtrack.getStackOffset(), dataLabel);
                m_backtrack.clearSubDataLabelPtr();
            } else {
                // If we have a backtrack label, connect the datalabel to it directly.
                if (m_backtrack.isLabel())
                    generator->m_expressionState.m_backtrackRecords.append(AlternativeBacktrackRecord(dataLabel, m_backtrack.getLabel()));
                else
                    setBacktrackDataLabel(dataLabel);
            }
        }

        void addBacktrackJump(Jump jump)
        {
            m_backtrack.addBacktrackJump(jump);
        }

        void setBacktrackDataLabel(DataLabelPtr dp)
        {
            m_backtrack.setDataLabel(dp);
        }

        void setBackTrackStackOffset(int32_t stackOffset)
        {
            m_backtrack.setStackOffset(stackOffset);
        }

        void setBacktrackLabel(Label label)
        {
            m_backtrack.setLabel(label);
        }

        void linkAlternativeBacktracks(YarrGenerator* generator, bool nextIteration = false)
        {
            m_backtrack.linkAlternativeBacktracks(generator, nextIteration);
            m_linkedBacktrack = 0;
        }

        void linkAlternativeBacktracksTo(YarrGenerator* generator, Label label, bool nextIteration = false)
        {
            m_backtrack.linkAlternativeBacktracksTo(generator, label, nextIteration);
        }

        void setBacktrackLink(BacktrackDestination* linkedBacktrack)
        {
            m_linkedBacktrack = linkedBacktrack;
        }

        void chainBacktracks(BacktrackDestination* followonBacktrack)
        {
            if (m_linkedBacktrack)
                m_linkedBacktrack->linkToNextBacktrack(followonBacktrack);
        }

        BacktrackDestination& getBacktrackDestination()
        {
            return m_backtrack;
        }

        void propagateBacktrackingFrom(YarrGenerator* generator, BacktrackDestination& backtrack, bool doJump = true)
        {
            if (doJump)
                m_backtrack.jumpToBacktrack(generator, backtrack.getBacktrackJumps());

            if (m_backtrack.isLabel() && backtrack.hasBacktrackToLabel())
                backtrack.linkBacktrackToLabel(m_backtrack.getLabel());

            if (backtrack.hasDestination()) {
                if (m_backtrack.hasDataLabel())
                    generator->m_expressionState.addDataLabelToNextIteration(m_backtrack.getDataLabel());

                m_backtrack.copyTarget(backtrack, doJump);
            }
        }

        PatternDisjunction* disjunction;
        int checkedTotal;
    private:
        unsigned alt;
        unsigned t;
        unsigned m_subParenNum;
        BacktrackDestination m_backtrack;
        BacktrackDestination* m_linkedBacktrack;
        JumpList* m_jumpList;
    };

    struct ParenthesesTail {
        ParenthesesTail(PatternTerm& term, int nestingLevel, JumpList* jumpListToPriorParen)
            : m_term(term)
            , m_nestingLevel(nestingLevel)
            , m_subParenIndex(0)
            , m_jumpListToPriorParen(jumpListToPriorParen)
        {
        }

        void processBacktracks(YarrGenerator* generator, TermGenerationState& state, TermGenerationState& parenthesesState, Label nonGreedyTryParentheses, Label fallThrough)
        {
            m_nonGreedyTryParentheses = nonGreedyTryParentheses;
            m_fallThrough = fallThrough;

            m_subParenIndex = state.getSubParenNum();
            parenthesesState.getBacktrackDestination().copyTo(m_parenBacktrack);
            state.chainBacktracks(&m_backtrack);
            BacktrackDestination& stateBacktrack = state.getBacktrackDestination();
            stateBacktrack.copyTo(m_backtrack);
            stateBacktrack.setBacktrackToLabel(&m_backtrackToLabel);
            state.setBacktrackLink(&m_backtrack);
            stateBacktrack.setSubDataLabelPtr(&m_dataAfterLabelPtr);

            m_doDirectBacktrack = m_parenBacktrack.hasDestination();

            if ((m_term.quantityType == QuantifierGreedy) || (m_term.quantityType == QuantifierNonGreedy))
                m_doDirectBacktrack = false;

            if (m_doDirectBacktrack)
                state.propagateBacktrackingFrom(generator, m_parenBacktrack, false);
            else {
                stateBacktrack.setBacktrackJumpList(&m_afterBacktrackJumps);
                stateBacktrack.setBacktrackSourceLabel(&m_backtrackFromAfterParens);
            }
        }

        void setNextIteration(Label nextIteration)
        {
            if (!m_nestingLevel && !m_backtrackToLabel.isSet())
                m_backtrackToLabel = nextIteration;
        }

        void addAfterParenJump(Jump jump)
        {
            m_afterBacktrackJumps.append(jump);
        }

        bool generateCode(YarrGenerator* generator, JumpList& jumpsToNext, bool priorBackTrackFallThrough, bool nextBacktrackFallThrough)
        {
            const RegisterID indexTemporary = regT0;
            unsigned parenthesesFrameLocation = m_term.frameLocation;
            Jump fromPriorBacktrack;
            bool needJumpForPriorParenTail = false;

            if (priorBackTrackFallThrough
                && ((m_term.quantityType == QuantifierGreedy)
                 || (m_term.quantityType == QuantifierNonGreedy)
                 || (!m_doDirectBacktrack && m_parenBacktrack.hasDestination()))) {
                // If the prior paren tail code assumed that it could fall through,
                // but we need to generate after paren backtrack code, then provide
                // a jump around that code for the prior paren tail code.
                // A regular expressing like ((xxx)...)? needs this.
                fromPriorBacktrack = generator->jump();
                needJumpForPriorParenTail = true;
            }

            if (!m_backtrack.hasDestination()) {
                if (m_backtrackToLabel.isSet()) {
                    m_backtrack.setLabel(m_backtrackToLabel);
                    nextBacktrackFallThrough = false;
                } else if (m_jumpListToPriorParen) {
                    // If we don't have a destination, go back to either the prior paren or the next outer paren.
                    m_backtrack.setBacktrackJumpList(m_jumpListToPriorParen);
                    nextBacktrackFallThrough = false;
                } else
                    m_backtrack.setBacktrackJumpList(&jumpsToNext);
            } else
                nextBacktrackFallThrough = false;

            // A failure AFTER the parens jumps here - Backtrack to this paren
            m_backtrackFromAfterParens = generator->label();

            if (m_dataAfterLabelPtr.isSet())
                generator->m_expressionState.m_backtrackRecords.append(AlternativeBacktrackRecord(m_dataAfterLabelPtr, m_backtrackFromAfterParens));

            m_afterBacktrackJumps.link(generator);

            if (m_term.quantityType == QuantifierGreedy) {
                // If this is -1 we have now tested with both with and without the parens.
                generator->loadFromFrame(parenthesesFrameLocation, indexTemporary);
                m_backtrack.jumpToBacktrack(generator, generator->branch32(Equal, indexTemporary, TrustedImm32(-1)));
            } else if (m_term.quantityType == QuantifierNonGreedy) {
                // If this is -1 we have now tested with both with and without the parens.
                generator->loadFromFrame(parenthesesFrameLocation, indexTemporary);
                generator->branch32(Equal, indexTemporary, TrustedImm32(-1)).linkTo(m_nonGreedyTryParentheses, generator);
            }

            if (!m_doDirectBacktrack)
                m_parenBacktrack.plantJumpToBacktrackIfExists(generator);

            // A failure WITHIN the parens jumps here
            if (needJumpForPriorParenTail)
                fromPriorBacktrack.link(generator);
            m_parenBacktrack.linkAlternativeBacktracks(generator);
            m_withinBacktrackJumps.link(generator);

            if (m_term.capture())
                generator->store32(TrustedImm32(-1), Address(output, (m_term.parentheses.subpatternId << 1) * sizeof(int)));

            if (m_term.quantityType == QuantifierGreedy) {
                generator->storeToFrame(TrustedImm32(-1), parenthesesFrameLocation);
                generator->jump().linkTo(m_fallThrough, generator);
                nextBacktrackFallThrough = false;
            } else if (!nextBacktrackFallThrough)
                m_backtrack.jumpToBacktrack(generator);

            if (!m_doDirectBacktrack)
                m_backtrack.setNextBacktrackLabel(m_backtrackFromAfterParens);

            return nextBacktrackFallThrough;
        }

        PatternTerm& m_term;
        int m_nestingLevel;
        unsigned m_subParenIndex;
        JumpList* m_jumpListToPriorParen;
        Label m_nonGreedyTryParentheses;
        Label m_fallThrough;
        Label m_backtrackToLabel;
        Label m_backtrackFromAfterParens;
        DataLabelPtr m_dataAfterLabelPtr;
        JumpList m_withinBacktrackJumps;
        JumpList m_afterBacktrackJumps;
        BacktrackDestination m_parenBacktrack;
        BacktrackDestination m_backtrack;
        bool m_doDirectBacktrack;
    };

    void generateAssertionBOL(TermGenerationState& state)
    {
        PatternTerm& term = state.term();

        if (m_pattern.m_multiline) {
            const RegisterID character = regT0;

            JumpList matchDest;
            if (!term.inputPosition)
                matchDest.append(branch32(Equal, index, Imm32(state.checkedTotal)));

            readCharacter(state.inputOffset() - 1, character);
            matchCharacterClass(character, matchDest, m_pattern.newlineCharacterClass());
            state.jumpToBacktrack(this);

            matchDest.link(this);
        } else {
            // Erk, really should poison out these alternatives early. :-/
            if (term.inputPosition)
                state.jumpToBacktrack(this);
            else
                state.jumpToBacktrack(this, branch32(NotEqual, index, Imm32(state.checkedTotal)));
        }
    }

    void generateAssertionEOL(TermGenerationState& state)
    {
        PatternTerm& term = state.term();

        if (m_pattern.m_multiline) {
            const RegisterID character = regT0;

            JumpList matchDest;
            if (term.inputPosition == state.checkedTotal)
                matchDest.append(atEndOfInput());

            readCharacter(state.inputOffset(), character);
            matchCharacterClass(character, matchDest, m_pattern.newlineCharacterClass());
            state.jumpToBacktrack(this);

            matchDest.link(this);
        } else {
            if (term.inputPosition == state.checkedTotal)
                state.jumpToBacktrack(this, notAtEndOfInput());
            // Erk, really should poison out these alternatives early. :-/
            else
                state.jumpToBacktrack(this);
        }
    }

    // Also falls though on nextIsNotWordChar.
    void matchAssertionWordchar(TermGenerationState& state, JumpList& nextIsWordChar, JumpList& nextIsNotWordChar)
    {
        const RegisterID character = regT0;
        PatternTerm& term = state.term();

        if (term.inputPosition == state.checkedTotal)
            nextIsNotWordChar.append(atEndOfInput());

        readCharacter(state.inputOffset(), character);
        matchCharacterClass(character, nextIsWordChar, m_pattern.wordcharCharacterClass());
    }

    void generateAssertionWordBoundary(TermGenerationState& state)
    {
        const RegisterID character = regT0;
        PatternTerm& term = state.term();

        Jump atBegin;
        JumpList matchDest;
        if (!term.inputPosition)
            atBegin = branch32(Equal, index, Imm32(state.checkedTotal));
        readCharacter(state.inputOffset() - 1, character);
        matchCharacterClass(character, matchDest, m_pattern.wordcharCharacterClass());
        if (!term.inputPosition)
            atBegin.link(this);

        // We fall through to here if the last character was not a wordchar.
        JumpList nonWordCharThenWordChar;
        JumpList nonWordCharThenNonWordChar;
        if (term.invert()) {
            matchAssertionWordchar(state, nonWordCharThenNonWordChar, nonWordCharThenWordChar);
            nonWordCharThenWordChar.append(jump());
        } else {
            matchAssertionWordchar(state, nonWordCharThenWordChar, nonWordCharThenNonWordChar);
            nonWordCharThenNonWordChar.append(jump());
        }
        state.jumpToBacktrack(this, nonWordCharThenNonWordChar);

        // We jump here if the last character was a wordchar.
        matchDest.link(this);
        JumpList wordCharThenWordChar;
        JumpList wordCharThenNonWordChar;
        if (term.invert()) {
            matchAssertionWordchar(state, wordCharThenNonWordChar, wordCharThenWordChar);
            wordCharThenWordChar.append(jump());
        } else {
            matchAssertionWordchar(state, wordCharThenWordChar, wordCharThenNonWordChar);
            // This can fall-though!
        }

        state.jumpToBacktrack(this, wordCharThenWordChar);

        nonWordCharThenWordChar.link(this);
        wordCharThenNonWordChar.link(this);
    }

    void generatePatternCharacterSingle(TermGenerationState& state)
    {
        const RegisterID character = regT0;
        UChar ch = state.term().patternCharacter;

        if (m_pattern.m_ignoreCase && isASCIIAlpha(ch)) {
            readCharacter(state.inputOffset(), character);
            or32(TrustedImm32(32), character);
            state.jumpToBacktrack(this, branch32(NotEqual, character, Imm32(Unicode::toLower(ch))));
        } else {
            ASSERT(!m_pattern.m_ignoreCase || (Unicode::toLower(ch) == Unicode::toUpper(ch)));
            state.jumpToBacktrack(this, jumpIfCharNotEquals(ch, state.inputOffset()));
        }
    }

    void generatePatternCharacterPair(TermGenerationState& state)
    {
        const RegisterID character = regT0;
        UChar ch1 = state.term().patternCharacter;
        UChar ch2 = state.lookaheadTerm().patternCharacter;

        int mask = 0;
        int chPair = ch1 | (ch2 << 16);

        if (m_pattern.m_ignoreCase) {
            if (isASCIIAlpha(ch1))
                mask |= 32;
            if (isASCIIAlpha(ch2))
                mask |= 32 << 16;
        }

        if (mask) {
            load32WithUnalignedHalfWords(BaseIndex(input, index, TimesTwo, state.inputOffset() * sizeof(UChar)), character);
            or32(Imm32(mask), character);
            state.jumpToBacktrack(this, branch32(NotEqual, character, Imm32(chPair | mask)));
        } else
            state.jumpToBacktrack(this, branch32WithUnalignedHalfWords(NotEqual, BaseIndex(input, index, TimesTwo, state.inputOffset() * sizeof(UChar)), Imm32(chPair)));
    }

    void generatePatternCharacterFixed(TermGenerationState& state)
    {
        const RegisterID character = regT0;
        const RegisterID countRegister = regT1;
        PatternTerm& term = state.term();
        UChar ch = term.patternCharacter;

        move(index, countRegister);
        sub32(Imm32(term.quantityCount), countRegister);

        Label loop(this);
        if (m_pattern.m_ignoreCase && isASCIIAlpha(ch)) {
            load16(BaseIndex(input, countRegister, TimesTwo, (state.inputOffset() + term.quantityCount) * sizeof(UChar)), character);
            or32(TrustedImm32(32), character);
            state.jumpToBacktrack(this, branch32(NotEqual, character, Imm32(Unicode::toLower(ch))));
        } else {
            ASSERT(!m_pattern.m_ignoreCase || (Unicode::toLower(ch) == Unicode::toUpper(ch)));
            state.jumpToBacktrack(this, branch16(NotEqual, BaseIndex(input, countRegister, TimesTwo, (state.inputOffset() + term.quantityCount) * sizeof(UChar)), Imm32(ch)));
        }
        add32(TrustedImm32(1), countRegister);
        branch32(NotEqual, countRegister, index).linkTo(loop, this);
    }

    void generatePatternCharacterGreedy(TermGenerationState& state)
    {
        const RegisterID character = regT0;
        const RegisterID countRegister = regT1;
        PatternTerm& term = state.term();
        UChar ch = term.patternCharacter;

        move(TrustedImm32(0), countRegister);

        JumpList failures;
        Label loop(this);
        failures.append(atEndOfInput());
        if (m_pattern.m_ignoreCase && isASCIIAlpha(ch)) {
            readCharacter(state.inputOffset(), character);
            or32(TrustedImm32(32), character);
            failures.append(branch32(NotEqual, character, Imm32(Unicode::toLower(ch))));
        } else {
            ASSERT(!m_pattern.m_ignoreCase || (Unicode::toLower(ch) == Unicode::toUpper(ch)));
            failures.append(jumpIfCharNotEquals(ch, state.inputOffset()));
        }

        add32(TrustedImm32(1), countRegister);
        add32(TrustedImm32(1), index);
        if (term.quantityCount != quantifyInfinite) {
            branch32(NotEqual, countRegister, Imm32(term.quantityCount)).linkTo(loop, this);
            failures.append(jump());
        } else
            jump(loop);

        Label backtrackBegin(this);
        loadFromFrame(term.frameLocation, countRegister);
        state.jumpToBacktrack(this, branchTest32(Zero, countRegister));
        sub32(TrustedImm32(1), countRegister);
        sub32(TrustedImm32(1), index);

        failures.link(this);

        storeToFrame(countRegister, term.frameLocation);

        state.setBacktrackLabel(backtrackBegin);
    }

    void generatePatternCharacterNonGreedy(TermGenerationState& state)
    {
        const RegisterID character = regT0;
        const RegisterID countRegister = regT1;
        PatternTerm& term = state.term();
        UChar ch = term.patternCharacter;

        move(TrustedImm32(0), countRegister);

        Jump firstTimeDoNothing = jump();

        Label hardFail(this);
        sub32(countRegister, index);
        state.jumpToBacktrack(this);

        Label backtrackBegin(this);
        loadFromFrame(term.frameLocation, countRegister);

        atEndOfInput().linkTo(hardFail, this);
        if (term.quantityCount != quantifyInfinite)
            branch32(Equal, countRegister, Imm32(term.quantityCount), hardFail);
        if (m_pattern.m_ignoreCase && isASCIIAlpha(ch)) {
            readCharacter(state.inputOffset(), character);
            or32(TrustedImm32(32), character);
            branch32(NotEqual, character, Imm32(Unicode::toLower(ch))).linkTo(hardFail, this);
        } else {
            ASSERT(!m_pattern.m_ignoreCase || (Unicode::toLower(ch) == Unicode::toUpper(ch)));
            jumpIfCharNotEquals(ch, state.inputOffset()).linkTo(hardFail, this);
        }

        add32(TrustedImm32(1), countRegister);
        add32(TrustedImm32(1), index);

        firstTimeDoNothing.link(this);
        storeToFrame(countRegister, term.frameLocation);

        state.setBacktrackLabel(backtrackBegin);
    }

    void generateCharacterClassSingle(TermGenerationState& state)
    {
        const RegisterID character = regT0;
        PatternTerm& term = state.term();

        JumpList matchDest;
        readCharacter(state.inputOffset(), character);
        matchCharacterClass(character, matchDest, term.characterClass);

        if (term.invert())
            state.jumpToBacktrack(this, matchDest);
        else {
            state.jumpToBacktrack(this);
            matchDest.link(this);
        }
    }

    void generateCharacterClassFixed(TermGenerationState& state)
    {
        const RegisterID character = regT0;
        const RegisterID countRegister = regT1;
        PatternTerm& term = state.term();

        move(index, countRegister);
        sub32(Imm32(term.quantityCount), countRegister);

        Label loop(this);
        JumpList matchDest;
        load16(BaseIndex(input, countRegister, TimesTwo, (state.inputOffset() + term.quantityCount) * sizeof(UChar)), character);
        matchCharacterClass(character, matchDest, term.characterClass);

        if (term.invert())
            state.jumpToBacktrack(this, matchDest);
        else {
            state.jumpToBacktrack(this);
            matchDest.link(this);
        }

        add32(TrustedImm32(1), countRegister);
        branch32(NotEqual, countRegister, index).linkTo(loop, this);
    }

    void generateCharacterClassGreedy(TermGenerationState& state)
    {
        const RegisterID character = regT0;
        const RegisterID countRegister = regT1;
        PatternTerm& term = state.term();

        move(TrustedImm32(0), countRegister);

        JumpList failures;
        Label loop(this);
        failures.append(atEndOfInput());

        if (term.invert()) {
            readCharacter(state.inputOffset(), character);
            matchCharacterClass(character, failures, term.characterClass);
        } else {
            JumpList matchDest;
            readCharacter(state.inputOffset(), character);
            matchCharacterClass(character, matchDest, term.characterClass);
            failures.append(jump());
            matchDest.link(this);
        }

        add32(TrustedImm32(1), countRegister);
        add32(TrustedImm32(1), index);
        if (term.quantityCount != quantifyInfinite) {
            branch32(NotEqual, countRegister, Imm32(term.quantityCount)).linkTo(loop, this);
            failures.append(jump());
        } else
            jump(loop);

        Label backtrackBegin(this);
        loadFromFrame(term.frameLocation, countRegister);
        state.jumpToBacktrack(this, branchTest32(Zero, countRegister));
        sub32(TrustedImm32(1), countRegister);
        sub32(TrustedImm32(1), index);

        failures.link(this);

        storeToFrame(countRegister, term.frameLocation);

        state.setBacktrackLabel(backtrackBegin);
    }

    void generateCharacterClassNonGreedy(TermGenerationState& state)
    {
        const RegisterID character = regT0;
        const RegisterID countRegister = regT1;
        PatternTerm& term = state.term();

        move(TrustedImm32(0), countRegister);

        Jump firstTimeDoNothing = jump();

        Label hardFail(this);
        sub32(countRegister, index);
        state.jumpToBacktrack(this);

        Label backtrackBegin(this);
        loadFromFrame(term.frameLocation, countRegister);

        atEndOfInput().linkTo(hardFail, this);
        branch32(Equal, countRegister, Imm32(term.quantityCount), hardFail);

        JumpList matchDest;
        readCharacter(state.inputOffset(), character);
        matchCharacterClass(character, matchDest, term.characterClass);

        if (term.invert())
            matchDest.linkTo(hardFail, this);
        else {
            jump(hardFail);
            matchDest.link(this);
        }

        add32(TrustedImm32(1), countRegister);
        add32(TrustedImm32(1), index);

        firstTimeDoNothing.link(this);
        storeToFrame(countRegister, term.frameLocation);

        state.setBacktrackLabel(backtrackBegin);
    }

    void generateParenthesesDisjunction(PatternTerm& parenthesesTerm, TermGenerationState& state, unsigned alternativeFrameLocation)
    {
        ASSERT((parenthesesTerm.type == PatternTerm::TypeParenthesesSubpattern) || (parenthesesTerm.type == PatternTerm::TypeParentheticalAssertion));
        ASSERT(parenthesesTerm.quantityCount == 1);

        PatternDisjunction* disjunction = parenthesesTerm.parentheses.disjunction;
        unsigned preCheckedCount = ((parenthesesTerm.quantityType == QuantifierFixedCount) && (parenthesesTerm.type != PatternTerm::TypeParentheticalAssertion)) ? disjunction->m_minimumSize : 0;

        if (disjunction->m_alternatives.size() == 1) {
            state.resetAlternative();
            ASSERT(state.alternativeValid());
            PatternAlternative* alternative = state.alternative();
            optimizeAlternative(alternative);

            int countToCheck = alternative->m_minimumSize - preCheckedCount;
            if (countToCheck) {
                ASSERT((parenthesesTerm.type == PatternTerm::TypeParentheticalAssertion) || (parenthesesTerm.quantityType != QuantifierFixedCount));

                // FIXME: This is quite horrible.  The call to 'plantJumpToBacktrackIfExists'
                // will be forced to always trampoline into here, just to decrement the index.
                // Ick. 
                Jump skip = jump();

                Label backtrackBegin(this);
                sub32(Imm32(countToCheck), index);
                state.addBacktrackJump(jump());

                skip.link(this);

                state.setBacktrackLabel(backtrackBegin);

                state.jumpToBacktrack(this, jumpIfNoAvailableInput(countToCheck));
                state.checkedTotal += countToCheck;
            }

            for (state.resetTerm(); state.termValid(); state.nextTerm())
                generateTerm(state);

            state.checkedTotal -= countToCheck;
        } else {
            JumpList successes;
            bool propogateBacktrack = false;

            // Save current state's paren jump list for use with each alternative 
            JumpList* outerJumpList = state.getJumpListToPriorParen();

            for (state.resetAlternative(); state.alternativeValid(); state.nextAlternative(), state.setJumpListToPriorParen(outerJumpList)) {
                PatternAlternative* alternative = state.alternative();
                optimizeAlternative(alternative);

                ASSERT(alternative->m_minimumSize >= preCheckedCount);
                int countToCheck = alternative->m_minimumSize - preCheckedCount;
                if (countToCheck) {
                    state.addBacktrackJump(jumpIfNoAvailableInput(countToCheck));
                    state.checkedTotal += countToCheck;
                }

                for (state.resetTerm(); state.termValid(); state.nextTerm())
                    generateTerm(state);

                // Matched an alternative.
                DataLabelPtr dataLabel = storeToFrameWithPatch(alternativeFrameLocation);

                if (!state.isLastAlternative() || countToCheck)
                    successes.append(jump());

                // Alternative did not match.

                // Do we have a backtrack destination?
                //    if so, link the data label to it.
                state.linkDataLabelToBacktrackIfExists(this, dataLabel);

                if (!state.isLastAlternative() || countToCheck)
                    state.linkAlternativeBacktracks(this);

                if (countToCheck) {
                    sub32(Imm32(countToCheck), index);
                    state.checkedTotal -= countToCheck;
                } else if (state.isLastAlternative())
                    propogateBacktrack = true;
            }
            // We fall through to here when the last alternative fails.
            // Add a backtrack out of here for the parenthese handling code to link up.
            if (!propogateBacktrack)
                state.addBacktrackJump(jump());

            // Save address on stack for the parens code to backtrack to, to retry the
            // next alternative.
            state.setBackTrackStackOffset(alternativeFrameLocation * sizeof(void*));

            successes.link(this);
        }
    }

    void generateParenthesesSingle(TermGenerationState& state)
    {
        const RegisterID indexTemporary = regT0;
        PatternTerm& term = state.term();
        PatternDisjunction* disjunction = term.parentheses.disjunction;
        ASSERT(term.quantityCount == 1);

        unsigned preCheckedCount = (term.quantityType == QuantifierFixedCount) ? disjunction->m_minimumSize : 0;

        unsigned parenthesesFrameLocation = term.frameLocation;
        unsigned alternativeFrameLocation = parenthesesFrameLocation;
        if (term.quantityType != QuantifierFixedCount)
            alternativeFrameLocation += YarrStackSpaceForBackTrackInfoParenthesesOnce;

        // optimized case - no capture & no quantifier can be handled in a light-weight manner.
        if (!term.capture() && (term.quantityType == QuantifierFixedCount)) {
            m_expressionState.incrementParenNestingLevel();

            TermGenerationState parenthesesState(disjunction, state.checkedTotal);

            // Use the current state's jump list for the nested parentheses.
            parenthesesState.setJumpListToPriorParen(state.getJumpListToPriorParen());

            generateParenthesesDisjunction(state.term(), parenthesesState, alternativeFrameLocation);
            // this expects that any backtracks back out of the parentheses will be in the
            // parenthesesState's m_backTrackJumps vector, and that if they need backtracking
            // they will have set an entry point on the parenthesesState's m_backtrackLabel.
            BacktrackDestination& parenthesesBacktrack = parenthesesState.getBacktrackDestination();
            BacktrackDestination& stateBacktrack = state.getBacktrackDestination();

            state.propagateBacktrackingFrom(this, parenthesesBacktrack);
            stateBacktrack.propagateBacktrackToLabel(parenthesesBacktrack);

            state.setJumpListToPriorParen(parenthesesState.getJumpListToPriorParen());

            m_expressionState.decrementParenNestingLevel();
        } else {
            Jump nonGreedySkipParentheses;
            Label nonGreedyTryParentheses;
            if (term.quantityType == QuantifierGreedy)
                storeToFrame(index, parenthesesFrameLocation);
            else if (term.quantityType == QuantifierNonGreedy) {
                storeToFrame(TrustedImm32(-1), parenthesesFrameLocation);
                nonGreedySkipParentheses = jump();
                nonGreedyTryParentheses = label();
                storeToFrame(index, parenthesesFrameLocation);
            }

            // store the match start index
            if (term.capture()) {
                int inputOffset = state.inputOffset() - preCheckedCount;
                if (inputOffset) {
                    move(index, indexTemporary);
                    add32(Imm32(inputOffset), indexTemporary);
                    store32(indexTemporary, Address(output, (term.parentheses.subpatternId << 1) * sizeof(int)));
                } else
                    store32(index, Address(output, (term.parentheses.subpatternId << 1) * sizeof(int)));
            }

            ParenthesesTail* parenthesesTail = m_expressionState.addParenthesesTail(term, state.getJumpListToPriorParen());

            m_expressionState.incrementParenNestingLevel();

            TermGenerationState parenthesesState(disjunction, state.checkedTotal);

            // Save the parenthesesTail for backtracking from nested parens to this one.
            parenthesesState.setJumpListToPriorParen(&parenthesesTail->m_withinBacktrackJumps);

            // generate the body of the parentheses
            generateParenthesesDisjunction(state.term(), parenthesesState, alternativeFrameLocation);

            // For non-fixed counts, backtrack if we didn't match anything.
            if (term.quantityType != QuantifierFixedCount)
                parenthesesTail->addAfterParenJump(branch32(Equal, index, Address(stackPointerRegister, (parenthesesFrameLocation * sizeof(void*)))));

            // store the match end index
            if (term.capture()) {
                int inputOffset = state.inputOffset();
                if (inputOffset) {
                    move(index, indexTemporary);
                    add32(Imm32(state.inputOffset()), indexTemporary);
                    store32(indexTemporary, Address(output, ((term.parentheses.subpatternId << 1) + 1) * sizeof(int)));
                } else
                    store32(index, Address(output, ((term.parentheses.subpatternId << 1) + 1) * sizeof(int)));
            }

            m_expressionState.decrementParenNestingLevel();

            parenthesesTail->processBacktracks(this, state, parenthesesState, nonGreedyTryParentheses, label());

            state.setJumpListToPriorParen(&parenthesesTail->m_afterBacktrackJumps);
            
            parenthesesState.getBacktrackDestination().clear();

            if (term.quantityType == QuantifierNonGreedy)
                nonGreedySkipParentheses.link(this);
        }
    }

    void generateParenthesesGreedyNoBacktrack(TermGenerationState& state)
    {
        PatternTerm& parenthesesTerm = state.term();
        PatternDisjunction* disjunction = parenthesesTerm.parentheses.disjunction;
        ASSERT(parenthesesTerm.type == PatternTerm::TypeParenthesesSubpattern);
        ASSERT(parenthesesTerm.quantityCount != 1); // Handled by generateParenthesesSingle.

        TermGenerationState parenthesesState(disjunction, state.checkedTotal);

        Label matchAgain(this);

        storeToFrame(index, parenthesesTerm.frameLocation); // Save the current index to check for zero len matches later.

        for (parenthesesState.resetAlternative(); parenthesesState.alternativeValid(); parenthesesState.nextAlternative()) {

            PatternAlternative* alternative = parenthesesState.alternative();
            optimizeAlternative(alternative);

            int countToCheck = alternative->m_minimumSize;
            if (countToCheck) {
                parenthesesState.addBacktrackJump(jumpIfNoAvailableInput(countToCheck));
                parenthesesState.checkedTotal += countToCheck;
            }

            for (parenthesesState.resetTerm(); parenthesesState.termValid(); parenthesesState.nextTerm())
                generateTerm(parenthesesState);

            // If we get here, we matched! If the index advanced then try to match more since limit isn't supported yet.
            branch32(NotEqual, index, Address(stackPointerRegister, (parenthesesTerm.frameLocation * sizeof(void*))), matchAgain);

            // If we get here we matched, but we matched "" - cannot accept this alternative as is, so either backtrack,
            // or fall through to try the next alternative if no backtrack is available.
            parenthesesState.plantJumpToBacktrackIfExists(this);

            parenthesesState.linkAlternativeBacktracks(this);

            // We get here if the alternative fails to match - fall through to the next iteration, or out of the loop.

            if (countToCheck) {
                sub32(Imm32(countToCheck), index);
                parenthesesState.checkedTotal -= countToCheck;
            }
        }

        // If the last alternative falls through to here, we have a failed match...
        // Which means that we match whatever we have matched up to this point (even if nothing).
    }

    void generateParentheticalAssertion(TermGenerationState& state)
    {
        PatternTerm& term = state.term();
        PatternDisjunction* disjunction = term.parentheses.disjunction;
        ASSERT(term.quantityCount == 1);
        ASSERT(term.quantityType == QuantifierFixedCount);

        unsigned parenthesesFrameLocation = term.frameLocation;
        unsigned alternativeFrameLocation = parenthesesFrameLocation + YarrStackSpaceForBackTrackInfoParentheticalAssertion;

        int countCheckedAfterAssertion = state.checkedTotal - term.inputPosition;

        if (term.invert()) {
            // Inverted case
            storeToFrame(index, parenthesesFrameLocation);

            state.checkedTotal -= countCheckedAfterAssertion;
            if (countCheckedAfterAssertion)
                sub32(Imm32(countCheckedAfterAssertion), index);

            TermGenerationState parenthesesState(disjunction, state.checkedTotal);
            generateParenthesesDisjunction(state.term(), parenthesesState, alternativeFrameLocation);
            // Success! - which means - Fail!
            loadFromFrame(parenthesesFrameLocation, index);
            state.jumpToBacktrack(this);

            // And fail means success.
            parenthesesState.linkAlternativeBacktracks(this);

            loadFromFrame(parenthesesFrameLocation, index);

            state.checkedTotal += countCheckedAfterAssertion;
        } else {
            // Normal case
            storeToFrame(index, parenthesesFrameLocation);

            state.checkedTotal -= countCheckedAfterAssertion;
            if (countCheckedAfterAssertion)
                sub32(Imm32(countCheckedAfterAssertion), index);

            TermGenerationState parenthesesState(disjunction, state.checkedTotal);
            generateParenthesesDisjunction(state.term(), parenthesesState, alternativeFrameLocation);
            // Success! - which means - Success!
            loadFromFrame(parenthesesFrameLocation, index);
            Jump success = jump();

            parenthesesState.linkAlternativeBacktracks(this);

            loadFromFrame(parenthesesFrameLocation, index);
            state.jumpToBacktrack(this);

            success.link(this);

            state.checkedTotal += countCheckedAfterAssertion;
        }
    }

    void generateTerm(TermGenerationState& state)
    {
        PatternTerm& term = state.term();

        switch (term.type) {
        case PatternTerm::TypeAssertionBOL:
            generateAssertionBOL(state);
            break;

        case PatternTerm::TypeAssertionEOL:
            generateAssertionEOL(state);
            break;

        case PatternTerm::TypeAssertionWordBoundary:
            generateAssertionWordBoundary(state);
            break;

        case PatternTerm::TypePatternCharacter:
            switch (term.quantityType) {
            case QuantifierFixedCount:
                if (term.quantityCount == 1) {
                    if (state.isSinglePatternCharacterLookaheadTerm() && (state.lookaheadTerm().inputPosition == (term.inputPosition + 1))) {
                        generatePatternCharacterPair(state);
                        state.nextTerm();
                    } else
                        generatePatternCharacterSingle(state);
                } else
                    generatePatternCharacterFixed(state);
                break;
            case QuantifierGreedy:
                generatePatternCharacterGreedy(state);
                break;
            case QuantifierNonGreedy:
                generatePatternCharacterNonGreedy(state);
                break;
            }
            break;

        case PatternTerm::TypeCharacterClass:
            switch (term.quantityType) {
            case QuantifierFixedCount:
                if (term.quantityCount == 1)
                    generateCharacterClassSingle(state);
                else
                    generateCharacterClassFixed(state);
                break;
            case QuantifierGreedy:
                generateCharacterClassGreedy(state);
                break;
            case QuantifierNonGreedy:
                generateCharacterClassNonGreedy(state);
                break;
            }
            break;

        case PatternTerm::TypeBackReference:
            m_shouldFallBack = true;
            break;

        case PatternTerm::TypeForwardReference:
            break;

        case PatternTerm::TypeParenthesesSubpattern:
            if (term.quantityCount == 1 && !term.parentheses.isCopy)
                generateParenthesesSingle(state);
            else if (term.parentheses.isTerminal)
                generateParenthesesGreedyNoBacktrack(state);
            else
                m_shouldFallBack = true;
            break;

        case PatternTerm::TypeParentheticalAssertion:
            generateParentheticalAssertion(state);
            break;
        }
    }

    void generateDisjunction(PatternDisjunction* disjunction)
    {
        TermGenerationState state(disjunction, 0);
        state.resetAlternative();

        // check availability for the next alternative
        int countCheckedForCurrentAlternative = 0;
        int countToCheckForFirstAlternative = 0;
        bool hasShorterAlternatives = false;
        bool setRepeatAlternativeLabels = false;
        JumpList notEnoughInputForPreviousAlternative;
        Label firstAlternative;
        Label firstAlternativeInputChecked;

        // The label 'firstAlternative' is used to plant a check to see if there is 
        // sufficient input available to run the first repeating alternative.
        // The label 'firstAlternativeInputChecked' will jump directly to matching 
        // the first repeating alternative having skipped this check.

        if (state.alternativeValid()) {
            PatternAlternative* alternative = state.alternative();
            if (!alternative->onceThrough()) {
                firstAlternative = Label(this);
                setRepeatAlternativeLabels = true;
            }
            countToCheckForFirstAlternative = alternative->m_minimumSize;
            state.checkedTotal += countToCheckForFirstAlternative;
            if (countToCheckForFirstAlternative)
                notEnoughInputForPreviousAlternative.append(jumpIfNoAvailableInput(countToCheckForFirstAlternative));
            countCheckedForCurrentAlternative = countToCheckForFirstAlternative;
        }

        if (setRepeatAlternativeLabels)
            firstAlternativeInputChecked = Label(this);

        while (state.alternativeValid()) {
            PatternAlternative* alternative = state.alternative();
            optimizeAlternative(alternative);

            // Track whether any alternatives are shorter than the first one.
            if (!alternative->onceThrough())
                hasShorterAlternatives = hasShorterAlternatives || (countCheckedForCurrentAlternative < countToCheckForFirstAlternative);

            for (state.resetTerm(); state.termValid(); state.nextTerm())
                generateTerm(state);

            // If we get here, the alternative matched.
            if (m_pattern.m_body->m_callFrameSize)
                addPtr(Imm32(m_pattern.m_body->m_callFrameSize * sizeof(void*)), stackPointerRegister);

            ASSERT(index != returnRegister);
            if (m_pattern.m_body->m_hasFixedSize) {
                move(index, returnRegister);
                if (alternative->m_minimumSize)
                    sub32(Imm32(alternative->m_minimumSize), returnRegister);

                store32(returnRegister, output);
            } else
                load32(Address(output), returnRegister);

            store32(index, Address(output, 4));

            generateReturn();

            state.nextAlternative();
            if (alternative->onceThrough() && state.alternativeValid())
                state.clearBacktrack();

            // if there are any more alternatives, plant the check for input before looping.
            if (state.alternativeValid()) {
                state.setJumpListToPriorParen(0);
                PatternAlternative* nextAlternative = state.alternative();
                if (!setRepeatAlternativeLabels && !nextAlternative->onceThrough()) {
                    // We have handled non-repeating alternatives, jump to next iteration 
                    // and loop over repeating alternatives.
                    state.jumpToBacktrack(this);

                    countToCheckForFirstAlternative = nextAlternative->m_minimumSize;

                    // If we get here, there the last input checked failed.
                    notEnoughInputForPreviousAlternative.link(this);

                    state.linkAlternativeBacktracks(this);

                    // Back up to start the looping alternatives.
                    if (countCheckedForCurrentAlternative)
                        sub32(Imm32(countCheckedForCurrentAlternative), index);

                    firstAlternative = Label(this);

                    state.checkedTotal = countToCheckForFirstAlternative;
                    if (countToCheckForFirstAlternative)
                        notEnoughInputForPreviousAlternative.append(jumpIfNoAvailableInput(countToCheckForFirstAlternative));

                    countCheckedForCurrentAlternative = countToCheckForFirstAlternative;

                    firstAlternativeInputChecked = Label(this);

                    setRepeatAlternativeLabels = true;
                } else {
                    int countToCheckForNextAlternative = nextAlternative->m_minimumSize;

                    if (countCheckedForCurrentAlternative > countToCheckForNextAlternative) { // CASE 1: current alternative was longer than the next one.
                        // If we get here, then the last input checked failed.
                        notEnoughInputForPreviousAlternative.link(this);

                        // Check if sufficent input available to run the next alternative 
                        notEnoughInputForPreviousAlternative.append(jumpIfNoAvailableInput(countToCheckForNextAlternative - countCheckedForCurrentAlternative));
                        // We are now in the correct state to enter the next alternative; this add is only required
                        // to mirror and revert operation of the sub32, just below.
                        add32(Imm32(countCheckedForCurrentAlternative - countToCheckForNextAlternative), index);

                        // If we get here, then the last input checked passed.
                        state.linkAlternativeBacktracks(this);

                        // No need to check if we can run the next alternative, since it is shorter -
                        // just update index.
                        sub32(Imm32(countCheckedForCurrentAlternative - countToCheckForNextAlternative), index);
                    } else if (countCheckedForCurrentAlternative < countToCheckForNextAlternative) { // CASE 2: next alternative is longer than the current one.
                        // If we get here, then the last input checked failed.
                        // If there is insufficient input to run the current alternative, and the next alternative is longer,
                        // then there is definitely not enough input to run it - don't even check. Just adjust index, as if
                        // we had checked.
                        notEnoughInputForPreviousAlternative.link(this);
                        add32(Imm32(countToCheckForNextAlternative - countCheckedForCurrentAlternative), index);
                        notEnoughInputForPreviousAlternative.append(jump());

                        // The next alternative is longer than the current one; check the difference.
                        state.linkAlternativeBacktracks(this);

                        notEnoughInputForPreviousAlternative.append(jumpIfNoAvailableInput(countToCheckForNextAlternative - countCheckedForCurrentAlternative));
                    } else { // CASE 3: Both alternatives are the same length.
                        ASSERT(countCheckedForCurrentAlternative == countToCheckForNextAlternative);

                        // If the next alterative is the same length as this one, then no need to check the input -
                        // if there was sufficent input to run the current alternative then there is sufficient
                        // input to run the next one; if not, there isn't.
                        state.linkAlternativeBacktracks(this);
                    }
                    state.checkedTotal -= countCheckedForCurrentAlternative;
                    countCheckedForCurrentAlternative = countToCheckForNextAlternative;
                    state.checkedTotal += countCheckedForCurrentAlternative;
                }
            }
        }

        // If we get here, all Alternatives failed...

        state.checkedTotal -= countCheckedForCurrentAlternative;

        if (!setRepeatAlternativeLabels) {
            // If there are no alternatives that need repeating (all are marked 'onceThrough') then just link
            // the match failures to this point, and fall through to the return below.
            state.linkAlternativeBacktracks(this, true);

            notEnoughInputForPreviousAlternative.link(this);
        } else {
            // How much more input need there be to be able to retry from the first alternative?
            // examples:
            //   /yarr_jit/ or /wrec|pcre/
            //     In these examples we need check for one more input before looping.
            //   /yarr_jit|pcre/
            //     In this case we need check for 5 more input to loop (+4 to allow for the first alterative
            //     being four longer than the last alternative checked, and another +1 to effectively move
            //     the start position along by one).
            //   /yarr|rules/ or /wrec|notsomuch/
            //     In these examples, provided that there was sufficient input to have just been matching for
            //     the second alternative we can loop without checking for available input (since the second
            //     alternative is longer than the first).  In the latter example we need to decrement index
            //     (by 4) so the start position is only progressed by 1 from the last iteration.
            int incrementForNextIter = (countToCheckForFirstAlternative - countCheckedForCurrentAlternative) + 1;

            // First, deal with the cases where there was sufficient input to try the last alternative.
            if (incrementForNextIter > 0) // We need to check for more input anyway, fall through to the checking below.
                state.linkAlternativeBacktracks(this, true);
            else if (m_pattern.m_body->m_hasFixedSize && !incrementForNextIter) // No need to update anything, link these backtracks straight to the to pof the loop!
                state.linkAlternativeBacktracksTo(this, firstAlternativeInputChecked, true);
            else { // no need to check the input, but we do have some bookkeeping to do first.
                state.linkAlternativeBacktracks(this, true);

                // Where necessary update our preserved start position.
                if (!m_pattern.m_body->m_hasFixedSize) {
                    move(index, regT0);
                    sub32(Imm32(countCheckedForCurrentAlternative - 1), regT0);
                    store32(regT0, Address(output));
                }

                // Update index if necessary, and loop (without checking).
                if (incrementForNextIter)
                    add32(Imm32(incrementForNextIter), index);
                jump().linkTo(firstAlternativeInputChecked, this);
            }

            notEnoughInputForPreviousAlternative.link(this);
            // Update our idea of the start position, if we're tracking this.
            if (!m_pattern.m_body->m_hasFixedSize) {
                if (countCheckedForCurrentAlternative - 1) {
                    move(index, regT0);
                    sub32(Imm32(countCheckedForCurrentAlternative - 1), regT0);
                    store32(regT0, Address(output));
                } else
                    store32(index, Address(output));
            }

            // Check if there is sufficent input to run the first alternative again.
            jumpIfAvailableInput(incrementForNextIter).linkTo(firstAlternativeInputChecked, this);
            // No - insufficent input to run the first alteranative, are there any other alternatives we
            // might need to check?  If so, the last check will have left the index incremented by
            // (countToCheckForFirstAlternative + 1), so we need test whether countToCheckForFirstAlternative
            // LESS input is available, to have the effect of just progressing the start position by 1
            // from the last iteration.  If this check passes we can just jump up to the check associated
            // with the first alternative in the loop.  This is a bit sad, since we'll end up trying the
            // first alternative again, and this check will fail (otherwise the check planted just above
            // here would have passed).  This is a bit sad, however it saves trying to do something more
            // complex here in compilation, and in the common case we should end up coallescing the checks.
            //
            // FIXME: a nice improvement here may be to stop trying to match sooner, based on the least
            // of the minimum-alternative-lengths.  E.g. if I have two alternatives of length 200 and 150,
            // and a string of length 100, we'll end up looping index from 0 to 100, checking whether there
            // is sufficient input to run either alternative (constantly failing).  If there had been only
            // one alternative, or if the shorter alternative had come first, we would have terminated
            // immediately. :-/
            if (hasShorterAlternatives)
                jumpIfAvailableInput(-countToCheckForFirstAlternative).linkTo(firstAlternative, this);
            // index will now be a bit garbled (depending on whether 'hasShorterAlternatives' is true,
            // it has either been incremented by 1 or by (countToCheckForFirstAlternative + 1) ... 
            // but since we're about to return a failure this doesn't really matter!)
        }

        if (m_pattern.m_body->m_callFrameSize)
            addPtr(Imm32(m_pattern.m_body->m_callFrameSize * sizeof(void*)), stackPointerRegister);

        move(TrustedImm32(-1), returnRegister);

        generateReturn();

        m_expressionState.emitParenthesesTail(this);
        m_expressionState.emitIndirectJumpTable(this);
        m_expressionState.linkToNextIteration(this);
    }

    void generateEnter()
    {
#if CPU(X86_64)
        push(X86Registers::ebp);
        move(stackPointerRegister, X86Registers::ebp);
        push(X86Registers::ebx);
#elif CPU(X86)
        push(X86Registers::ebp);
        move(stackPointerRegister, X86Registers::ebp);
        // TODO: do we need spill registers to fill the output pointer if there are no sub captures?
        push(X86Registers::ebx);
        push(X86Registers::edi);
        push(X86Registers::esi);
        // load output into edi (2 = saved ebp + return address).
    #if COMPILER(MSVC)
        loadPtr(Address(X86Registers::ebp, 2 * sizeof(void*)), input);
        loadPtr(Address(X86Registers::ebp, 3 * sizeof(void*)), index);
        loadPtr(Address(X86Registers::ebp, 4 * sizeof(void*)), length);
        loadPtr(Address(X86Registers::ebp, 5 * sizeof(void*)), output);
    #else
        loadPtr(Address(X86Registers::ebp, 2 * sizeof(void*)), output);
    #endif
#elif CPU(ARM)
        push(ARMRegisters::r4);
        push(ARMRegisters::r5);
        push(ARMRegisters::r6);
#if CPU(ARM_TRADITIONAL)
        push(ARMRegisters::r8); // scratch register
#endif
        move(ARMRegisters::r3, output);
#elif CPU(SH4)
        push(SH4Registers::r11);
        push(SH4Registers::r13);
#elif CPU(MIPS)
        // Do nothing.
#endif
    }

    void generateReturn()
    {
#if CPU(X86_64)
        pop(X86Registers::ebx);
        pop(X86Registers::ebp);
#elif CPU(X86)
        pop(X86Registers::esi);
        pop(X86Registers::edi);
        pop(X86Registers::ebx);
        pop(X86Registers::ebp);
#elif CPU(ARM)
#if CPU(ARM_TRADITIONAL)
        pop(ARMRegisters::r8); // scratch register
#endif
        pop(ARMRegisters::r6);
        pop(ARMRegisters::r5);
        pop(ARMRegisters::r4);
#elif CPU(SH4)
        pop(SH4Registers::r13);
        pop(SH4Registers::r11);
#elif CPU(MIPS)
        // Do nothing
#endif
        ret();
    }

public:
    YarrGenerator(YarrPattern& pattern)
        : m_pattern(pattern)
        , m_shouldFallBack(false)
    {
    }

    void generate()
    {
        generateEnter();

        if (!m_pattern.m_body->m_hasFixedSize)
            store32(index, Address(output));

        if (m_pattern.m_body->m_callFrameSize)
            subPtr(Imm32(m_pattern.m_body->m_callFrameSize * sizeof(void*)), stackPointerRegister);

        generateDisjunction(m_pattern.m_body);
    }

    void compile(JSGlobalData* globalData, YarrCodeBlock& jitObject)
    {
        generate();

        LinkBuffer patchBuffer(this, globalData->regexAllocator.poolForSize(size()), 0);

        for (unsigned i = 0; i < m_expressionState.m_backtrackRecords.size(); ++i)
            patchBuffer.patch(m_expressionState.m_backtrackRecords[i].dataLabel, patchBuffer.locationOf(m_expressionState.m_backtrackRecords[i].backtrackLocation));

        jitObject.set(patchBuffer.finalizeCode());
        jitObject.setFallBack(m_shouldFallBack);
    }

private:
    YarrPattern& m_pattern;
    bool m_shouldFallBack;
    GenerationState m_expressionState;
};

void jitCompile(YarrPattern& pattern, JSGlobalData* globalData, YarrCodeBlock& jitObject)
{
    YarrGenerator(pattern).compile(globalData, jitObject);
}

int execute(YarrCodeBlock& jitObject, const UChar* input, unsigned start, unsigned length, int* output)
{
    return jitObject.execute(input, start, length, output);
}

}}

#endif
