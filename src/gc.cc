#include "gc.h"
#include "heap.h"
#include "heap-inl.h"

#include <sys/types.h> // off_t
#include <stdlib.h> // NULL
#include <assert.h> // assert

namespace candor {
namespace internal {

void GC::GCValue::Relocate(char* address) {
  if (slot_ != NULL) {
    *slot_ = address;
  }
  if (!value()->IsGCMarked()) value()->SetGCMark(address);
}


void GC::CollectGarbage(char* current_frame) {
  assert(grey_items()->length() == 0);
  assert(black_items()->length() == 0);

  // __$gc() isn't setting needs_gc() attribute
  if (heap()->needs_gc() == Heap::kGCNone) {
    heap()->needs_gc(Heap::kGCNewSpace);
  }

  // Select space to GC
  Space* space = heap()->needs_gc() == Heap::kGCNewSpace ?
      heap()->new_space()
      :
      heap()->old_space();

  // Temporary space which will contain copies of all visited objects
  tmp_space(new Space(heap(), space->page_size()));

  // Add referenced in C++ land values to the grey list
  ColourPersistentHandles();

  // Colour on-stack registers
  ColourFrames(current_frame);

  // Reset marks for items from external space
  while (black_items()->length() != 0) {
    GCValue* value = black_items()->Shift();
    assert(value->value()->IsSoftGCMarked());
    value->value()->ResetSoftGCMark();
  }

  RelocateNormalHandles();

  // Visit all weak references and call callbacks if some of them are dead
  HandleWeakReferences();

  space->Swap(tmp_space());
  delete tmp_space();

  // Reset GC flag
  heap()->needs_gc(Heap::kGCNone);
}


void GC::ColourPersistentHandles() {
  HValueRefList::Item* item = heap()->references()->head();
  while (item != NULL) {
    HValueReference* ref = item->value();
    if (ref->is_persistent()) {
      push_grey(ref->value(), reinterpret_cast<char**>(ref->reference()));
      push_grey(ref->value(), reinterpret_cast<char**>(ref->valueptr()));
      ProcessGrey();
    }

    item = item->next();
  }
}


void GC::RelocateNormalHandles() {
  HValueRefList::Item* item = heap()->references()->head();
  while (item != NULL) {
    HValueReference* ref = item->value();
    if (ref->is_normal()) {
      GCValue* v;
      v = new GCValue(ref->value(), reinterpret_cast<char**>(ref->reference()));
      if (v->value()->IsGCMarked()) v->Relocate(v->value()->GetGCMark());

      v = new GCValue(ref->value(), reinterpret_cast<char**>(ref->valueptr()));
      if (v->value()->IsGCMarked()) v->Relocate(v->value()->GetGCMark());
    }

    item = item->next();
  }
}


void GC::ColourFrames(char* current_frame) {
  // Go through the frames
  char** frame = reinterpret_cast<char**>(current_frame);
  while (true) {
    uint32_t slots = (*reinterpret_cast<uint32_t*>(frame - 1)) >> 3;

    //
    // Frame layout
    // ... [previous frame] [on-stack vars (and spills) count] [...vars...]
    // or
    // [previous frame] [xFEEDBEEF] [return addr] [rbp] ....
    //
    char** next = frame;
    while (next != NULL &&
           *reinterpret_cast<uint32_t*>(next + 2) == 0xFEEDBEEF) {
      next = *reinterpret_cast<char***>(next + 3);
    }

    if (next == frame) next = reinterpret_cast<char**>(*next);

    for (uint32_t i = 0; i < slots; i++) {
      char* value = *(frame - 2 - i);

      // Skip nil, non-pointer values and rbp pushes
      if (value == HNil::New() || HValue::IsUnboxed(value)) {
        continue;
      }

      push_grey(HValue::Cast(value), frame - 2 - i);
      ProcessGrey();
    }

    if (next == NULL) break;

    frame = next;
  }
}


void GC::HandleWeakReferences() {
  HValueWeakRefList::Item* item = heap()->weak_references()->head();
  while (item != NULL) {
    HValueWeakRef* ref = item->value();
    if (!ref->value()->IsGCMarked()) {
      if (IsInCurrentSpace(ref->value())) {
        // Value is in GC space and wasn't marked
        // call callback as it was GCed
        ref->callback()(ref->value());
        HValueWeakRefList::Item* current = item;
        item = item->next();
        heap()->weak_references()->Remove(current);
        continue;
      }
    } else {
      // Value wasn't GCed, but was moved
      ref->value(reinterpret_cast<HValue*>(ref->value()->GetGCMark()));
    }

    item = item->next();
  }
}


void GC::ProcessGrey() {
  while (grey_items()->length() != 0) {
    GCValue* value = grey_items()->Shift();

    // Skip unboxed address
    if (value->value() == HValue::Cast(HNil::New()) ||
        HValue::IsUnboxed(value->value()->addr())) {
      continue;
    }

    if (!value->value()->IsGCMarked()) {
      // Object is in not in current space, don't move it
      if (!IsInCurrentSpace(value->value())) {
        if (!value->value()->IsSoftGCMarked()) {
          // Set soft mark and add item to black list to reset mark later
          value->value()->SetSoftGCMark();
          black_items()->Push(value);

          GC::VisitValue(value->value());
        }
        continue;
      }

      HValue* hvalue;

      if (heap()->needs_gc() == Heap::kGCNewSpace) {
        // New space GC
        hvalue = value->value()->CopyTo(heap()->old_space(), tmp_space());
      } else {
        // Old space GC
        hvalue = value->value()->CopyTo(tmp_space(), heap()->new_space());
      }

      value->Relocate(hvalue->addr());
      GC::VisitValue(hvalue);
    } else {
      value->Relocate(value->value()->GetGCMark());
    }
  }
}


bool GC::IsInCurrentSpace(HValue* value) {
  return (heap()->needs_gc() == Heap::kGCOldSpace &&
         value->Generation() >= Heap::kMinOldSpaceGeneration) ||
         (heap()->needs_gc() == Heap::kGCNewSpace &&
         value->Generation() < Heap::kMinOldSpaceGeneration);
}


void GC::VisitValue(HValue* value) {
  switch (value->tag()) {
   case Heap::kTagContext:
    return VisitContext(value->As<HContext>());
   case Heap::kTagFunction:
    return VisitFunction(value->As<HFunction>());
   case Heap::kTagObject:
    return VisitObject(value->As<HObject>());
   case Heap::kTagArray:
    return VisitArray(value->As<HArray>());
   case Heap::kTagMap:
    return VisitMap(value->As<HMap>());

   // String and numbers ain't referencing anyone
   case Heap::kTagString:
   case Heap::kTagNumber:
   case Heap::kTagBoolean:
   case Heap::kTagCData:
    return;
   default:
    UNEXPECTED
  }
}


void GC::VisitContext(HContext* context) {
  if (context->has_parent()) {
    push_grey(HValue::Cast(context->parent()), context->parent_slot());
  }

  for (uint32_t i = 0; i < context->slots(); i++) {
    if (!context->HasSlot(i)) continue;

    push_grey(context->GetSlot(i), context->GetSlotAddress(i));
  }
}


void GC::VisitFunction(HFunction* fn) {
  if (fn->parent_slot() != NULL &&
      fn->parent() != reinterpret_cast<char*>(Heap::kBindingContextTag)) {
    push_grey(HValue::Cast(fn->parent()), fn->parent_slot());
  }
  if (fn->root_slot() != NULL) {
    push_grey(HValue::Cast(fn->root()), fn->root_slot());
  }
}


void GC::VisitObject(HObject* obj) {
  push_grey(HValue::Cast(obj->map()), obj->map_slot());
}


void GC::VisitArray(HArray* arr) {
  push_grey(HValue::Cast(arr->map()), arr->map_slot());
}


void GC::VisitMap(HMap* map) {
  uint32_t size = map->size() << 1;
  for (uint32_t i = 0; i < size; i++) {
    if (map->IsEmptySlot(i)) continue;

    push_grey(map->GetSlot(i), map->GetSlotAddress(i));
  }
}

} // namespace internal
} // namespace candor
