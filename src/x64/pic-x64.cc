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

#include "pic.h"
#include "code-space.h"  // CodeSpace
#include "stubs.h"  // Stubs
#include "macroassembler.h"  // Masm

namespace candor {
namespace internal {

#define __ masm->

void PIC::Generate(Masm* masm) {
  __ push(rbp);
  __ mov(rbp, rsp);

  // Place for spills
  __ pushb(Immediate(Heap::kTagNil));
  __ pushb(Immediate(Heap::kTagNil));

  Label miss, end;
  Operand rdx_op(rdx, 0);
  Operand proto_op(rax, HObject::kProtoOffset);
  Operand rax_s(rbp, -16), rbx_s(rbp, -24);

  __ mov(rax_s, rax);
  __ mov(rbx_s, rbx);

  if (size_ != 0) {
    // Fast-case non-object
    __ IsNil(rax, NULL, &miss);
    __ IsUnboxed(rax, NULL, &miss);
    __ IsHeapObject(Heap::kTagObject, rax, &miss, NULL);

    // Load proto
    __ mov(rdx, proto_op);
    __ cmpq(rdx, Immediate(Heap::kICDisabledValue));
    __ jmp(kEq, &miss);
  }

  for (int i = size_ - 1; i >= 0; i--) {
    Label local_miss;

    __ mov(rbx, Immediate(reinterpret_cast<intptr_t>(protos_[i])));
    proto_offsets_[i] = reinterpret_cast<char**>(static_cast<intptr_t>(
          masm->offset() - 8));
    __ cmpq(rdx, rbx);
    __ jmp(kNe, &local_miss);
    __ mov(rax, Immediate(results_[i]));
    __ xorq(rbx, rbx);
    __ mov(rsp, rbp);
    __ pop(rbp);
    __ ret(0);
    __ bind(&local_miss);
  }

  // Cache failed - call runtime
  __ bind(&miss);

  if (size_ != 0) {
    __ mov(rbx, rbx_s);
    __ mov(rax, rax_s);
  }
  __ Call(space_->stubs()->GetLookupPropertyStub());

  // Miss(this, object, result, ip)
  Operand caller_ip(rbp, 8);
  __ push(caller_ip);
  __ push(rax);
  __ push(rax_s);
  __ mov(scratch, Immediate(reinterpret_cast<intptr_t>(this)));
  __ push(scratch);
  __ Call(space_->stubs()->GetPICMissStub());

  // Return value
  __ bind(&end);
  __ xorq(rbx, rbx);
  __ mov(rsp, rbp);
  __ pop(rbp);
  __ ret(0);
}

}  // namespace internal
}  // namespace candor
