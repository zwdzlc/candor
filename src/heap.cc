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

#include "heap.h"

#include <stdint.h>  // uint32_t, intptr_t
#include <stdlib.h>  // NULL
#include <string.h>  // memcpy
#include <zone.h>  // Zone::Allocate
#include <assert.h>  // assert

#include "heap-inl.h"
#include "runtime.h"  // RuntimeLookupProperty

namespace candor {
namespace internal {

Heap* Heap::current_ = NULL;

Space::Space(Heap* heap, uint32_t page_size) : heap_(heap),
                                               root_(NULL),
                                               page_size_(page_size),
                                               size_(0) {
  // Create the first page
  pages_.Push(new Page(page_size));

  select(pages_.head()->value());

  compute_size_limit();
}


void Space::select(Page* page) {
  top_ = &page->top_;
  limit_ = &page->limit_;
}


void Space::AddPage(uint32_t size) {
  uint32_t real_size = RoundUp(size, page_size());
  Page* page = new Page(real_size);
  pages_.Push(page);
  size_ += real_size;

  select(page);
}


char* Space::Allocate(uint32_t bytes) {
  // If current page was exhausted - run GC
  uint32_t even_bytes = bytes + (bytes & 0x01);
  bool place_in_current = *top_ + even_bytes <= *limit_;

  if (!place_in_current) {
    // Go through all pages to find gap
    List<Page*, EmptyClass>::Item* item = pages_.head();
    for (;*top_ + even_bytes > *limit_ && item != NULL; item = item->next()) {
      select(item->value());
    }

    // No gap was found - allocate new page
    if (item == NULL) {
      if (size() > size_limit()) {
        heap()->needs_gc(this == heap()->new_space() ?
            Heap::kGCNewSpace
            :
            Heap::kGCOldSpace);
      }

      // Including tagging byte offset
      AddPage(even_bytes + 1);
    }
  }

  char* result = *top_;
  *top_ += even_bytes;

  return result;
}


void Space::Swap(Space* space) {
  // Remove self pages
  Clear();

  while (space->pages_.length() != 0) {
    pages_.Push(space->pages_.Shift());
    size_ += pages_.tail()->value()->size_;
  }

  select(pages_.head()->value());
  compute_size_limit();
}


void Space::Clear() {
  size_ = 0;
  while (pages_.length() != 0) {
    delete pages_.Shift();
  }
}


Heap::Heap(uint32_t page_size) : new_space_(this, page_size),
                                 old_space_(this, page_size),
                                 last_stack_(NULL),
                                 last_frame_(NULL),
                                 pending_exception_(NULL),
                                 needs_gc_(kGCNone),
                                 gc_(this),
                                 code_space_(NULL) {
  current_ = this;
  factory_ = HValue::Cast(HObject::NewEmpty(this, kMinFactorySize));
  Reference(Heap::kRefPersistent, &factory_, factory_);
}


char* Heap::ToFactory(char* key) {
  char** slot = HObject::LookupProperty(this,
                                        reinterpret_cast<char*>(factory_),
                                        key,
                                        1);
  if (*slot == HNil::New()) {
    *slot = key;
  } else {
    key = *slot;
  }

  return key;
}


char* Heap::CreateString(const char* key, uint32_t size) {
  return ToFactory(HString::New(this, Heap::kTenureOld, key, size));
}


char* Heap::CreateNumber(double num) {
  return ToFactory(HNumber::New(this, Heap::kTenureOld, num));
}


char* Heap::CreateBoolean(bool value) {
  return ToFactory(HBoolean::New(this, Heap::kTenureOld, value));
}


const char* Heap::ErrorToString(Error err) {
  switch (err) {
    case kErrorNone:
      return NULL;
    case kErrorIncorrectLhs:
      return "Incorrect left-hand side";
    case kErrorCallWithoutVariable:
      return "Call without variable";
    case kErrorExpectedLoop:
      return "Expected loop";
  }

  return NULL;
}


char* Heap::AllocateTagged(HeapTag tag, TenureType tenure, uint32_t bytes) {
  char* result = space(tenure)->Allocate(bytes + 8);
  intptr_t qtag = tag;
  if (tenure == kTenureOld) {
    int bit_offset = (HValue::kGenerationOffset -
                      HValue::interior_offset(0)) << 3;
    qtag = qtag | (kMinOldSpaceGeneration << bit_offset);
  }
  *reinterpret_cast<intptr_t*>(result + HValue::kTagOffset) = qtag;

  return result;
}


HValueReference* Heap::Reference(ReferenceType type,
                                 HValue** reference,
                                 HValue* value) {
  HValueReference* ref = new HValueReference(type, reference, value);
  references()->Set(NumberKey::New(reference), ref);

  return ref;
}


void Heap::Dereference(HValue** reference, HValue* value) {
  references()->RemoveOne(NumberKey::New(reference));
}


void Heap::AddWeak(HValue* value, WeakCallback callback) {
  weak_references()->Set(NumberKey::New(value),
                         new HValueWeakRef(value, callback));
}


void Heap::RemoveWeak(HValue* value) {
  weak_references()->RemoveOne(NumberKey::New(value));
}


HValue* HValue::CopyTo(Space* old_space, Space* new_space) {
  assert(!IsUnboxed(addr()));

  uint32_t size = kPointerSize;
  switch (tag()) {
    case Heap::kTagContext:
      // parent + slots
      size += (2 + As<HContext>()->slots()) * kPointerSize;
      break;
    case Heap::kTagFunction:
      // parent + body + root + argc
      size += 4 * kPointerSize;
      break;
    case Heap::kTagNumber:
      size += HNumber::kDoubleSize;
      break;
    case Heap::kTagBoolean:
      size += kPointerSize;
      break;
    case Heap::kTagString:
      // hash + length
      size += 2 * kPointerSize;
      switch (GetRepresentation<HString::Representation>(addr())) {
        case HString::kNormal:
          // + bytes
          size += As<HString>()->length();
          break;
        case HString::kCons:
          // + lhs + rhs + scratch_slot (for traversing)
          size += 2 * kPointerSize;
          break;
        default:
          UNEXPECTED
          break;
      }
      break;
    case Heap::kTagObject:
      // mask + map + proto
      size += 3 * kPointerSize;
      break;
    case Heap::kTagArray:
      // mask + map + proto + length
      size += 4 * kPointerSize;
      break;
    case Heap::kTagMap:
      // size + space ( keys + values )
      size += (1 + (As<HMap>()->size() << 1)) * kPointerSize;
      break;
    case Heap::kTagCData:
      // size + data
      size += kPointerSize + As<HCData>()->size();
      break;
    default:
      UNEXPECTED
  }

  IncrementGeneration();
  char* result;
  if (Generation() >= Heap::kMinOldSpaceGeneration) {
    result = old_space->Allocate(size);
  } else {
    result = new_space->Allocate(size);
  }

  memcpy(result + interior_offset(0), addr() + interior_offset(0), size);

  return HValue::Cast(result);
}


char* HContext::New(Heap* heap,
                    ZoneList<char*>* values) {
  char* result = heap->AllocateTagged(Heap::kTagContext,
                                      Heap::kTenureOld,
                                      (2 + values->length()) * kPointerSize);

  // Zero parent
  *reinterpret_cast<char**>(result + kParentOffset) = HNil::New();

  // Put size
  *reinterpret_cast<intptr_t*>(result + kSlotsOffset) = values->length();

  // Put all values
  char** slot = reinterpret_cast<char**>(result + GetIndexDisp(0));
  while (values->length() != 0) {
    *slot = values->Shift();
    slot++;
  }

  return result;
}


char* HNumber::New(Heap* heap, int64_t value) {
  return reinterpret_cast<char*>(Tag(value));
}


char* HNumber::New(Heap* heap, Heap::TenureType tenure, double value) {
  char* result = heap->AllocateTagged(Heap::kTagNumber, tenure, 8);
  *reinterpret_cast<double*>(result + kValueOffset) = value;
  return result;
}


char* HBoolean::New(Heap* heap, Heap::TenureType tenure, bool value) {
  char* result = heap->AllocateTagged(Heap::kTagBoolean, tenure, kPointerSize);
  *reinterpret_cast<int8_t*>(result + kValueOffset) = value ? 1 : 0;

  return result;
}


char* HString::New(Heap* heap,
                   Heap::TenureType tenure,
                   uint32_t length) {
  char* result = heap->AllocateTagged(Heap::kTagString,
                                      tenure,
                                      length + 3 * kPointerSize);

  // Zero hash
  *reinterpret_cast<intptr_t*>(result + kHashOffset) = 0;
  // Set length
  *reinterpret_cast<intptr_t*>(result + kLengthOffset) = length;

  return result;
}


char* HString::New(Heap* heap,
                   Heap::TenureType tenure,
                   const char* value,
                   uint32_t length) {
  char* result = New(heap, tenure, length);

  // Copy value
  memcpy(result + kValueOffset, value, length);

  return result;
}


char* HString::NewCons(Heap* heap,
                       Heap::TenureType tenure,
                       uint32_t length,
                       char* left,
                       char* right) {
  char* result = New(heap, tenure, 2 * kPointerSize);

  // Set representation
  SetRepresentation<Representation>(result, kCons);

  // Set length
  *reinterpret_cast<uint32_t*>(result + kLengthOffset) = length;

  // Set lhs and rhs
  *LeftConsSlot(result) = left;
  *RightConsSlot(result) = right;

  return result;
}


char* HString::FlattenCons(char* addr, char* buffer) {
  while (addr != NULL) {
    switch (GetRepresentation<Representation>(addr)) {
      case kNormal:
        {
          uint32_t len = HString::Length(addr);
          memcpy(buffer, addr + kValueOffset, len);
          return buffer + len;
        }
      case kCons:
        {
          char* left = LeftCons(addr);
          char* right = RightCons(addr);

          if (right == HNil::New()) {
            addr = left;
          } else {
            // Iterate through bigger string, recurse through smaller
            if (HString::Length(left) > HString::Length(right)) {
              FlattenCons(right, buffer + HString::Length(left));
              addr = left;
            } else {
              buffer = FlattenCons(left, buffer);
              addr = right;
            }
          }
        }
        break;
      default:
        UNEXPECTED
        return buffer;
    }
  }

  return buffer;
}


char* HString::Value(Heap* heap, char* addr) {
  switch (GetRepresentation<Representation>(addr)) {
    case kNormal:
      return addr + kValueOffset;
    case kCons:
      if (RightCons(addr) == HNil::New()) {
        // Return cached left if right is null
        return HString::Value(heap, LeftCons(addr));
      } else {
        // Concatenate strings and put them into left slot
        char* result = HString::New(heap,
            Heap::kTenureNew,
            HString::Length(addr));
        char* value = HString::Value(heap, result);

        // Traverse cons tree and put strings in
        HString::FlattenCons(addr, value);

        *RightConsSlot(addr) = HNil::New();
        *LeftConsSlot(addr) = result;

        return value;
      }
    default:
      UNEXPECTED
      return NULL;
  }
}


uint32_t HString::Hash(Heap* heap, char* addr) {
  uint32_t* hash_addr = reinterpret_cast<uint32_t*>(addr + kHashOffset);
  uint32_t hash = *hash_addr;
  if (hash == 0) {
    hash = ComputeHash(Value(heap, addr), Length(addr));
    *hash_addr = hash;
  }
  return hash;
}


char* HObject::NewEmpty(Heap* heap, uint32_t size) {
  char* obj = heap->AllocateTagged(Heap::kTagObject,
                                   Heap::kTenureNew,
                                   3 * kPointerSize);
  HObject::Init(heap, obj, size);

  return obj;
}


void HObject::Init(Heap* heap, char* obj, uint32_t size) {
  // Set mask
  *reinterpret_cast<intptr_t*>(obj + kMaskOffset) = (size - 1) * kPointerSize;
  // Set map
  char* map = HMap::NewEmpty(heap, size);
  *reinterpret_cast<char**>(obj + kMapOffset) = map;
  // Set proto
  *reinterpret_cast<char**>(obj + kProtoOffset) = map;
}


char** HObject::LookupProperty(Heap* heap, char* addr, char* key, int insert) {
  intptr_t offset = RuntimeLookupProperty(heap, addr, key, insert);
  return reinterpret_cast<char**>(HObject::Map(addr) + offset);
}


char* HArray::NewEmpty(Heap* heap) {
  char* obj = heap->AllocateTagged(Heap::kTagArray,
                                   Heap::kTenureNew,
                                   4 * kPointerSize);

  HObject::Init(heap, obj, 16);

  // Set length
  SetLength(obj, 0);

  return obj;
}


int64_t HArray::Length(char* obj, bool shrink) {
  int64_t result = *reinterpret_cast<intptr_t*>(obj + kLengthOffset);

  if (shrink) {
    // Lookup property at [length - 1]
    // Shrink if it's nil
    //
    // NOTE: passing NULL as heap is completely safe here,
    // as we ain't going to allocate or change anything
    int64_t shrinked = result;
    char* shrinkedptr;
    char** slot;
    do {
      if (shrinked < 0) break;
      shrinked--;
      shrinkedptr = reinterpret_cast<char*>(HNumber::Tag(shrinked));
      slot = HObject::LookupProperty(NULL, obj, shrinkedptr, 0);
    } while (*slot == HNil::New());

    // If array was shrinked - change length
    if (result != (shrinked + 1)) {
      result = shrinked + 1;
      SetLength(obj, result);
    }
  }

  return result;
}


char* HMap::NewEmpty(Heap* heap, uint32_t size) {
  char* map = heap->AllocateTagged(Heap::kTagMap,
                                   Heap::kTenureNew,
                                   ((size << 1) + 1) * kPointerSize);

  // Set map's size
  *reinterpret_cast<intptr_t*>(map + kSizeOffset) = size;

  // Nullify all map's slots (both keys and values)
  size = (size << 1) * kPointerSize;
  memset(map + kSpaceOffset, 0x00, size);
  for (uint32_t i = 0; i < size; i += kPointerSize) {
    map[i + kSpaceOffset] = Heap::kTagNil;
  }

  return map;
}


char* HFunction::New(Heap* heap, char* parent, char* addr, char* root) {
  char* fn = heap->AllocateTagged(Heap::kTagFunction,
                                  Heap::kTenureOld,
                                  4 * kPointerSize);

  // Set parent context
  *reinterpret_cast<char**>(fn + kParentOffset) = parent;

  // Set pointer to code
  *reinterpret_cast<char**>(fn + kCodeOffset) = addr;

  // Set root context
  *reinterpret_cast<char**>(fn + kRootOffset) = root;

  // Set argc
  *reinterpret_cast<char**>(fn + kArgcOffset) = NULL;

  return fn;
}


char* HFunction::NewBinding(Heap* heap, char* addr, char* root) {
  return New(heap,
             reinterpret_cast<char*>(Heap::kBindingContextTag),
             addr,
             root);
}


char* HCData::New(Heap* heap, size_t size) {
  char* d = heap->AllocateTagged(Heap::kTagCData,
                                 Heap::kTenureNew,
                                 kPointerSize + size);

  *reinterpret_cast<uint32_t*>(d + kSizeOffset) = size;

  return d;
}

}  // namespace internal
}  // namespace candor
