#include "lir.h"
#include "lir-inl.h"
#include "lir-instructions.h"
#include "lir-instructions-inl.h"
#include "macroassembler.h"
#include "stubs.h" // Stubs
#include <unistd.h> // intptr_t

namespace candor {
namespace internal {

// Masm helpers

Register LUse::ToRegister() {
  assert(is_register());
  return RegisterByIndex(interval()->index());
}


Operand* LUse::ToOperand() {
  assert(is_stackslot());
  return new Operand(rbp, HValue::kPointerSize * (interval()->index() + 2));
}

#define __ masm->

void LEntry::Generate(Masm* masm) {
  __ push(rbp);
  __ mov(rbp, rsp);

  // Allocate spills
  __ AllocateSpills();

  // Allocate context slots
  __ AllocateContext(context_slots_);
}


void LReturn::Generate(Masm* masm) {
  __ mov(rsp, rbp);
  __ pop(rbp);
  __ ret(0);
}


void LNop::Generate(Masm* masm) {
  // No need to generate real nops, they're only clobbering alignment
}


void LMove::Generate(Masm* masm) {
  // Ignore nop moves
  if (result->IsEqual(inputs[0])) return;
  __ Move(result, inputs[0]);
}


void LGap::Generate(Masm* masm) {
  // Resolve loops
  Resolve();

  PairList::Item* head = pairs_.head();
  for (; head != NULL; head = head->next()) {
    Pair* p = head->value();
    __ Move(p->dst_->first_use(), p->src_->last_use());
  }
}


void LNil::Generate(Masm* masm) {
  __ Move(result, Immediate(Heap::kTagNil));
}


void LLiteral::Generate(Masm* masm) {
  if (root_slot_->is_immediate()) {
    __ Move(result,
            Immediate(reinterpret_cast<intptr_t>(root_slot_->value())));
  } else {
    assert(root_slot_->is_context());
    assert(root_slot_->depth() == -2);
    Operand slot(root_reg, HContext::GetIndexDisp(root_slot_->index()));
    __ Move(result, slot);
  }
}


void LAllocateObject::Generate(Masm* masm) {
  // XXX Use correct size here
  __ push(Immediate(HNumber::Tag(16)));
  __ push(Immediate(HNumber::Tag(Heap::kTagObject)));
  __ Call(masm->stubs()->GetAllocateObjectStub());
}


void LAllocateArray::Generate(Masm* masm) {
  // XXX Use correct size here
  __ push(Immediate(HNumber::Tag(16)));
  __ push(Immediate(HNumber::Tag(Heap::kTagArray)));
  __ Call(masm->stubs()->GetAllocateObjectStub());
}


void LFunction::Generate(Masm* masm) {
}


void LLoadContext::Generate(Masm* masm) {
}


void LStoreContext::Generate(Masm* masm) {
}


void LLoadProperty::Generate(Masm* masm) {
}


void LStoreProperty::Generate(Masm* masm) {
}


void LDeleteProperty::Generate(Masm* masm) {
}


void LNot::Generate(Masm* masm) {
}


void LBinOp::Generate(Masm* masm) {
}


void LTypeof::Generate(Masm* masm) {
}


void LSizeof::Generate(Masm* masm) {
}


void LKeysof::Generate(Masm* masm) {
}


void LClone::Generate(Masm* masm) {
}


void LCall::Generate(Masm* masm) {
}


void LCollectGarbage::Generate(Masm* masm) {
}


void LGetStackTrace::Generate(Masm* masm) {
}


void LPhi::Generate(Masm* masm) {
}


void LLabel::Generate(Masm* masm) {
}


void LLoadArg::Generate(Masm* masm) {
}


void LBranch::Generate(Masm* masm) {
}


void LGoto::Generate(Masm* masm) {
}

} // namespace internal
} // namespace candor
