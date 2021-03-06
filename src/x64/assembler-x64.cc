/**
 * Copyright (c) 2012, Fedor Indutny.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#define __STDC_LIMIT_MACROS
#include <stdint.h>  // UINT32_MAX

#include "assembler.h"
#include "assembler-inl.h"
#include "heap.h"
#include "heap-inl.h"

namespace candor {
namespace internal {

void RelocationInfo::Relocate(Heap* heap, char* buffer) {
  uint64_t addr = 0;

  if (type_ == kAbsolute) {
    addr = reinterpret_cast<uint64_t>(buffer) + target_;
  } else if (type_ == kValue) {
    addr = target_;
  } else {
    addr = target_ - offset_;

    // Add offset
    switch (size_) {
      case kByte: addr -= 1; break;
      case kWord: addr -= 2; break;
      case kLong: addr -= 4; break;
      case kQuad: addr -= 8; break;
      default: UNEXPECTED break;
    }
  }

  switch (size_) {
    case kByte:
      *reinterpret_cast<uint8_t*>(buffer + offset_) = addr;
      break;
    case kWord:
      *reinterpret_cast<uint16_t*>(buffer + offset_) = addr;
      break;
    case kLong:
      *reinterpret_cast<uint32_t*>(buffer + offset_) = addr;
      break;
    case kQuad:
      *reinterpret_cast<uint64_t*>(buffer + offset_) = addr;
      break;
    default:
      UNEXPECTED
      break;
  }

  if (notify_gc_) {
    assert(type_ == kAbsolute);
    heap->Reference(Heap::kRefWeak,
                    reinterpret_cast<HValue**>(addr),
                    *reinterpret_cast<HValue**>(addr));
  }
}


void Assembler::Relocate(Heap* heap, char* buffer) {
  ZoneList<RelocationInfo*>::Item* item = relocation_info_.head();
  while (item != NULL) {
    item->value()->Relocate(heap, buffer);
    item = item->next();
  }
}


void Assembler::Grow() {
  if (offset_ + 32 < length_) return;

  char* new_buffer = new char[length_ << 1];
  memcpy(new_buffer, buffer_, length_);
  memset(new_buffer + length_, 0xCC, length_);

  delete[] buffer_;
  buffer_ = new_buffer;
  length_ <<= 1;
}


void Assembler::nop() {
  emitb(0x90);
}


void Assembler::cpuid() {
  emitb(0x0F);
  emitb(0xA2);
}


void Assembler::push(Register src) {
  emit_rex_if_high(src);
  emitb(0x50 | src.low());
}


void Assembler::push(const Immediate imm) {
  emitb(0x68);
  emitl(imm.value());
}


void Assembler::pushb(const Immediate imm) {
  emitb(0x6A);
  emitb(imm.value());
}


void Assembler::push(const Operand& src) {
  emit_rexw(rax, src);
  emitb(0xFF);
  emit_modrm(src, 6);
}


void Assembler::pop(Register dst) {
  emit_rex_if_high(dst);
  emitb(0x58 | dst.low());
}


void Assembler::ret(uint16_t imm) {
  if (imm == 0) {
    emitb(0xC3);
  } else {
    emitb(0xC2);
    emitw(imm);
  }
  Grow();
}


void Assembler::bind(Label* label) {
  label->relocate(offset());
}


void Assembler::cmpq(Register dst, Register src) {
  emit_rexw(dst, src);
  emitb(0x3B);
  emit_modrm(dst, src);
}


void Assembler::cmpq(Register dst, const Operand& src) {
  emit_rexw(dst, src);
  emitb(0x3B);
  emit_modrm(dst, src);
}


void Assembler::cmpq(Register dst, const Immediate src) {
  emit_rexw(rax, dst);
  emitb(0x81);
  emit_modrm(dst, 7);
  emitl(src.value());
}


void Assembler::cmpqb(Register dst, const Immediate src) {
  emit_rexw(rax, dst);
  emitb(0x83);
  emit_modrm(dst, 7);
  emitb(src.value());
}


void Assembler::cmpq(const Operand& dst, const Immediate src) {
  emit_rexw(rax, dst);
  emitb(0x81);
  emit_modrm(dst, 7);
  emitl(src.value());
}


void Assembler::cmpb(Register dst, const Operand& src) {
  emit_rexw(dst, src);
  emitb(0x3A);
  emit_modrm(dst, src);
}


void Assembler::cmpb(Register dst, const Immediate src) {
  emit_rexw(rax, dst);
  emitb(0x80);
  emit_modrm(dst, 7);
  emitb(src.value());
}


void Assembler::cmpb(const Operand& dst, const Immediate src) {
  emit_rexw(rax, dst);
  emitb(0x80);
  emit_modrm(dst, 7);
  emitb(src.value());
}


void Assembler::testb(Register dst, const Immediate src) {
  emit_rexw(rax, dst);
  emitb(0xF6);
  emit_modrm(dst, 0);
  emitb(src.value());
}


void Assembler::testl(Register dst, const Immediate src) {
  emit_rexw(rax, dst);
  emitb(0xF7);
  emit_modrm(dst, 0);
  emitl(src.value());
}


void Assembler::jmp(Label* label) {
  emitb(0xE9);
  emitl(0x11111111);
  if (label != NULL) label->use(this, offset() - 4);
}


void Assembler::jmp(Condition cond, Label* label) {
  emitb(0x0F);
  switch (cond) {
    case kEq: emitb(0x84); break;
    case kNe: emitb(0x85); break;
    case kLt: emitb(0x8C); break;
    case kLe: emitb(0x8E); break;
    case kGt: emitb(0x8F); break;
    case kGe: emitb(0x8D); break;
    case kBelow: emitb(0x82); break;
    case kBe: emitb(0x86); break;
    case kAbove: emitb(0x87); break;
    case kAe: emitb(0x83); break;
    case kCarry: emitb(0x82); break;
    case kOverflow: emitb(0x80); break;
    case kNoOverflow: emitb(0x81); break;
    default:
      UNEXPECTED
  }
  emitl(0x11111111);
  if (label != NULL) label->use(this, offset() - 4);
}


void Assembler::mov(Register dst, Register src) {
  emit_rexw(dst, src);
  emitb(0x8B);
  emit_modrm(dst, src);
}


void Assembler::mov(Register dst, const Operand& src) {
  emit_rexw(dst, src);
  emitb(0x8B);
  emit_modrm(dst, src);
}


void Assembler::mov(const Operand& dst, Register src) {
  emit_rexw(src, dst);
  emitb(0x89);
  emit_modrm(src, dst);
}


void Assembler::mov(Register dst, const Immediate src) {
  emit_rexw(rax, dst);
  emitb(0xB8 | dst.low());
  emitq(src.value());
}


void Assembler::mov(const Operand& dst, const Immediate src) {
  // One can't just load 64bit value into the operand
  if (src.value() >= UINT32_MAX) {
    push(scratch);
    mov(scratch, src);
    mov(dst, scratch);
    pop(scratch);
    return;
  }

  emit_rexw(rax, dst);
  emitb(0xC7);
  emit_modrm(dst);
  emitl(src.value());
}


void Assembler::movl(const Operand& dst, const Immediate src) {
  emitb(0xC7);
  emit_modrm(dst);
  emitl(src.value());
}


void Assembler::movl(Register dst, const Immediate src) {
  emit_rexw(dst);
  emitb(0xC7);
  emit_modrm(dst, 0);
  emitl(src.value());
}


void Assembler::movb(Register dst, const Immediate src) {
  emit_rexw(dst);
  emitb(0xC6);
  emit_modrm(dst, 0);
  emitb(src.value());
}


void Assembler::movb(const Operand& dst, const Immediate src) {
  emit_rexw(dst);
  emitb(0xC6);
  emit_modrm(dst);
  emitb(src.value());
}


void Assembler::movb(const Operand& dst, Register src) {
  emit_rexw(src, dst);
  emitb(0x88);
  emit_modrm(src, dst);
}


void Assembler::movzxb(Register dst, const Operand& src) {
  emit_rexw(dst, src);
  emitb(0x0F);
  emitb(0xB6);
  emit_modrm(dst, src);
}


void Assembler::xchg(Register dst, Register src) {
  emit_rexw(dst, src);
  emitb(0x87);
  emit_modrm(dst, src);
}


void Assembler::addq(Register dst, Register src) {
  emit_rexw(dst, src);
  emitb(0x03);
  emit_modrm(dst, src);
}


void Assembler::addl(Register dst, Register src) {
  emitb(0x03);
  emit_modrm(dst, src);
}


void Assembler::addq(Register dst, const Operand& src) {
  emit_rexw(dst, src);
  emitb(0x03);
  emit_modrm(dst, src);
}


void Assembler::addq(Register dst, const Immediate imm) {
  emit_rexw(rax, dst);
  emitb(0x81);
  emit_modrm(dst, 0);
  emitl(imm.value());
}


void Assembler::addqb(Register dst, const Immediate imm) {
  emit_rexw(rax, dst);
  emitb(0x83);
  emit_modrm(dst, 0);
  emitb(imm.value());
}


void Assembler::subq(Register dst, Register src) {
  emit_rexw(dst, src);
  emitb(0x2B);
  emit_modrm(dst, src);
}


void Assembler::subq(Register dst, const Immediate src) {
  emit_rexw(rax, dst);
  emitb(0x81);
  emit_modrm(dst, 0x05);
  emitl(src.value());
}


void Assembler::subqb(Register dst, const Immediate src) {
  emit_rexw(rax, dst);
  emitb(0x81);
  emit_modrm(dst, 0x05);
  emitl(src.value());
}


void Assembler::imulq(Register src) {
  emit_rexw(rax, src);
  emitb(0xF7);
  emit_modrm(src, 0x05);
}


void Assembler::idivq(Register src) {
  emit_rexw(rax, src);
  emitb(0xF7);
  emit_modrm(src, 0x07);
}


void Assembler::andq(Register dst, Register src) {
  emit_rexw(dst, src);
  emitb(0x23);
  emit_modrm(dst, src);
}


void Assembler::orq(Register dst, Register src) {
  emit_rexw(dst, src);
  emitb(0x0B);
  emit_modrm(dst, src);
}


void Assembler::orqb(Register dst, const Immediate src) {
  emit_rexw(rax, dst);
  emitb(0x83);
  emit_modrm(dst, 0x01);
  emitb(src.value());
}


void Assembler::xorq(Register dst, Register src) {
  emit_rexw(dst, src);
  emitb(0x33);
  emit_modrm(dst, src);
}


void Assembler::xorl(Register dst, Register src) {
  emitb(0x33);
  emit_modrm(dst, src);
}


void Assembler::inc(Register dst) {
  emit_rexw(rax, dst);
  emitb(0xFF);
  emit_modrm(dst, 0x00);
}


void Assembler::dec(Register dst) {
  emit_rexw(rax, dst);
  emitb(0xFF);
  emit_modrm(dst, 0x01);
}


void Assembler::shl(Register dst, const Immediate src) {
  emit_rexw(rax, dst);
  emitb(0xC1);
  emit_modrm(dst, 0x04);
  emitb(src.value());
}


void Assembler::shr(Register dst, const Immediate src) {
  emit_rexw(rax, dst);
  emitb(0xC1);
  emit_modrm(dst, 0x05);
  emitb(src.value());
}


void Assembler::shll(Register dst, const Immediate src) {
  emitb(0xC1);
  emit_modrm(dst, 0x04);
  emitb(src.value());
}


void Assembler::shrl(Register dst, const Immediate src) {
  emitb(0xC1);
  emit_modrm(dst, 0x05);
  emitb(src.value());
}


void Assembler::shl(Register dst) {
  emit_rexw(rcx, dst);
  emitb(0xD3);
  emit_modrm(dst, 0x04);
}


void Assembler::shr(Register dst) {
  emit_rexw(rcx, dst);
  emitb(0xD3);
  emit_modrm(dst, 0x05);
}


void Assembler::sal(Register dst, const Immediate src) {
  emit_rexw(rax, dst);
  emitb(0xC1);
  emit_modrm(dst, 0x04);
  emitb(src.value());
}


void Assembler::sar(Register dst, const Immediate src) {
  emit_rexw(rax, dst);
  emitb(0xC1);
  emit_modrm(dst, 0x07);
  emitb(src.value());
}


void Assembler::sal(Register dst) {
  emit_rexw(rcx, dst);
  emitb(0xD3);
  emit_modrm(dst, 0x04);
}


void Assembler::sar(Register dst) {
  emit_rexw(rcx, dst);
  emitb(0xD3);
  emit_modrm(dst, 0x07);
}


void Assembler::callq(Register dst) {
  emit_rexw(rax, dst);
  emitb(0xFF);
  emit_modrm(dst, 2);
}


void Assembler::callq(const Operand& dst) {
  emit_rexw(rax, dst);
  emitb(0xFF);
  emit_modrm(dst, 2);
}


// Floating point instructions


void Assembler::movd(DoubleRegister dst, Register src) {
  emitb(0x66);
  emit_rexw(dst, src);
  emitb(0x0F);
  emitb(0x6E);
  emit_modrm(dst, src);
}


void Assembler::movd(DoubleRegister dst, const Operand& src) {
  emitb(0x66);
  emit_rexw(dst, src);
  emitb(0x0F);
  emitb(0x6E);
  emit_modrm(dst, src);
}


void Assembler::movd(Register dst, DoubleRegister src) {
  emitb(0x66);
  emit_rexw(src, dst);
  emitb(0x0F);
  emitb(0x7E);
  emit_modrm(src, dst);
}


void Assembler::movd(const Operand& dst, DoubleRegister src) {
  emitb(0x66);
  emit_rexw(src, dst);
  emitb(0x0F);
  emitb(0x7E);
  emit_modrm(dst, src);
}


void Assembler::addqd(DoubleRegister dst, DoubleRegister src) {
  emitb(0xF2);
  emitb(0x0F);
  emitb(0x58);
  emit_modrm(dst, src);
}


void Assembler::subqd(DoubleRegister dst, DoubleRegister src) {
  emitb(0xF2);
  emitb(0x0F);
  emitb(0x5C);
  emit_modrm(dst, src);
}


void Assembler::mulqd(DoubleRegister dst, DoubleRegister src) {
  emitb(0xF2);
  emitb(0x0F);
  emitb(0x59);
  emit_modrm(dst, src);
}


void Assembler::divqd(DoubleRegister dst, DoubleRegister src) {
  emitb(0xF2);
  emitb(0x0F);
  emitb(0x5E);
  emit_modrm(dst, src);
}


void Assembler::xorqd(DoubleRegister dst, DoubleRegister src) {
  emitb(0x66);
  emitb(0x0F);
  emitb(0x57);
  emit_modrm(dst, src);
}


void Assembler::cvtsi2sd(DoubleRegister dst, Register src) {
  emitb(0xF2);
  emit_rexw(dst, src);
  emitb(0x0F);
  emitb(0x2A);
  emit_modrm(dst, src);
}


void Assembler::cvttsd2si(Register dst, DoubleRegister src) {
  emitb(0xF2);
  emit_rexw(dst, src);
  emitb(0x0F);
  emitb(0x2C);
  emit_modrm(dst, src);
}


void Assembler::roundsd(DoubleRegister dst,
                        DoubleRegister src,
                        RoundMode mode) {
  emitb(0x66);
  emit_rexw(dst, src);
  emitb(0x0F);
  emitb(0x3A);
  emitb(0x0B);
  emit_modrm(dst, src);

  // Exception handling mask
  emitb(static_cast<uint8_t>(mode) | 0x08);
}


void Assembler::ucomisd(DoubleRegister dst, DoubleRegister src) {
  emitb(0x66);
  emitb(0x0F);
  emitb(0x2E);
  emit_modrm(dst, src);
}

}  // namespace internal
}  // namespace candor
