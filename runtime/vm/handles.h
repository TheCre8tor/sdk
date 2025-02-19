// Copyright (c) 2011, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef RUNTIME_VM_HANDLES_H_
#define RUNTIME_VM_HANDLES_H_

#include "vm/allocation.h"
#include "vm/flags.h"
#include "vm/os.h"

namespace dart {

// Handles are used in the Dart Virtual Machine to ensure that access
// to dart objects in the virtual machine code is done in a
// Garbage Collection safe manner.
//
// The class Handles is the basic type that implements creation of handles and
// manages their life cycle (allocated either in the current zone or
// current handle scope).
// The two forms of handle allocation are:
// - allocation of handles in the current zone (Handle::AllocateZoneHandle).
//   Handles allocated in this manner are destroyed when the zone is destroyed.
// - allocation of handles in a scoped manner (Handle::AllocateHandle).
//   A new scope can be started using HANDLESCOPE(thread).
//   Handles allocated in this manner are destroyed when the HandleScope
//   object is destroyed.
// Code that uses scoped handles typically looks as follows:
//   {
//     HANDLESCOPE(thread);
//     const String& str = String::Handle(String::New("abc"));
//     .....
//     .....
//   }
// Code that uses zone handles typically looks as follows:
//   const String& str = String::ZoneHandle(String::New("abc"));
//   .....
//   .....
//
//   The Handle function for each object type internally uses the
//   Handles::AllocateHandle() function for creating handles. The Handle
//   function of the object type is the only way to create scoped handles
//   in the dart VM.
//   The ZoneHandle function for each object type internally uses the
//   Handles::AllocateZoneHandle() function for creating zone handles.
//   The ZoneHandle function of the object type is the only way to create
//   zone handles in the dart VM.

// Forward declarations.
class ObjectPointerVisitor;
class HandleVisitor;

template <int kHandleSizeInWords, int kHandlesPerChunk, int kOffsetOfRawPtr>
class Handles {
 public:
  Handles()
      : zone_blocks_(NULL),
        first_scoped_block_(NULL),
        scoped_blocks_(&first_scoped_block_) {}
  ~Handles() { DeleteAll(); }

  // Visit all object pointers stored in the various handles.
  void VisitObjectPointers(ObjectPointerVisitor* visitor);

  // Visit all the scoped handles.
  void VisitScopedHandles(ObjectPointerVisitor* visitor);

  // Visit all blocks that have been added since the last time
  // this method was called.
  // Be careful with this, since multiple users of this method could
  // interfere with eachother.
  // Currently only used by GC trace facility.
  void VisitUnvisitedScopedHandles(ObjectPointerVisitor* visitor);

  // Visit all of the various handles.
  void Visit(HandleVisitor* visitor);

  // Reset the handles so that we can reuse.
  void Reset();

  // Allocates a handle in the current handle scope. This handle is valid only
  // in the current handle scope and is destroyed when the current handle
  // scope ends.
  static uword AllocateHandle(Zone* zone);

  // Allocates a handle in the current zone. This handle will be destroyed
  // when the current zone is destroyed.
  static uword AllocateZoneHandle(Zone* zone);

#if defined(DEBUG)
  // Returns true if specified handle is a zone handle.
  static bool IsZoneHandle(uword handle);
#endif

  // Allocates space for a scoped handle.
  uword AllocateScopedHandle() {
    if (scoped_blocks_->IsFull()) {
      SetupNextScopeBlock();
    }
    return scoped_blocks_->AllocateHandle();
  }

  bool IsEmpty() const {
    if (zone_blocks_ != nullptr) return false;
    if (first_scoped_block_.HandleCount() != 0) return false;
    if (scoped_blocks_ != &first_scoped_block_) return false;
    return true;
  }

  intptr_t ZoneHandlesCapacityInBytes() const {
    intptr_t capacity = 0;
    for (HandlesBlock* block = zone_blocks_; block != nullptr;
         block = block->next_block()) {
      capacity += sizeof(*block);
    }
    return capacity;
  }

  intptr_t ScopedHandlesCapacityInBytes() const {
    intptr_t capacity = 0;
    for (HandlesBlock* block = scoped_blocks_; block != nullptr;
         block = block->next_block()) {
      capacity += sizeof(*block);
    }
    return capacity;
  }

 protected:
  // Returns a count of active handles (used for testing purposes).
  int CountScopedHandles() const;
  int CountZoneHandles() const;

  // Returns true if passed in handle is a valid zone handle.
  bool IsValidScopedHandle(uword handle) const;
  bool IsValidZoneHandle(uword handle) const;

 private:
  // Base structure for managing blocks of handles.
  // Handles are allocated in Chunks (each chunk holds kHandlesPerChunk
  // handles). The chunk is uninitialized, subsequent requests for handles
  // is allocated from the chunk until we run out space in the chunk,
  // at this point another chunk is allocated. These chunks are chained
  // together.
  class HandlesBlock : public MallocAllocated {
   public:
    explicit HandlesBlock(HandlesBlock* next)
        : next_block_(next), next_handle_slot_(0) {}
    ~HandlesBlock();

    // Reinitializes handle block for reuse.
    void ReInit();

    // Returns true if the handle block is full.
    bool IsFull() const {
      return next_handle_slot_ >= (kHandleSizeInWords * kHandlesPerChunk);
    }

    // Returns true if passed in handle belongs to this block.
    bool IsValidHandle(uword handle) const {
      uword start = reinterpret_cast<uword>(data_);
      uword end = start + (kHandleSizeInWords * kWordSize * kHandlesPerChunk);
      return (start <= handle && handle < end);
    }

    // Allocates space for a handle in the data area.
    uword AllocateHandle() {
      ASSERT(!IsFull());
      uword handle_address = reinterpret_cast<uword>(data_ + next_handle_slot_);
      next_handle_slot_ += kHandleSizeInWords;
      return handle_address;
    }

    // Visit all object pointers in the handle block.
    void VisitObjectPointers(ObjectPointerVisitor* visitor);

    // Visit all of the handles in the handle block.
    void Visit(HandleVisitor* visitor);

#if defined(DEBUG)
    // Zaps the free handle area to an uninitialized value.
    void ZapFreeHandles();
#endif

    // Returns number of active handles in the handle block.
    int HandleCount() const;

    // Accessors.
    intptr_t next_handle_slot() const { return next_handle_slot_; }
    void set_next_handle_slot(intptr_t next_handle_slot) {
      next_handle_slot_ = next_handle_slot;
    }
    HandlesBlock* next_block() const { return next_block_; }
    void set_next_block(HandlesBlock* next) { next_block_ = next; }

   private:
    HandlesBlock* next_block_;   // Link to next block of handles.
    intptr_t next_handle_slot_;  // Next slot for allocation in current block.
    uword data_[kHandleSizeInWords * kHandlesPerChunk];  // Handles area.

    DISALLOW_COPY_AND_ASSIGN(HandlesBlock);
  };

  // Deletes all the allocated handle blocks.
  void DeleteAll();
  void DeleteHandleBlocks(HandlesBlock* blocks);

  // Sets up the next handle block (allocates a new one if needed).
  void SetupNextScopeBlock();

  // Allocates space for a zone handle.
  uword AllocateHandleInZone() {
    if (zone_blocks_ == NULL || zone_blocks_->IsFull()) {
      SetupNextZoneBlock();
    }
    return zone_blocks_->AllocateHandle();
  }

  // Allocates a new handle block and links it up.
  void SetupNextZoneBlock();

#if defined(DEBUG)
  // Verifies consistency of handle blocks after a scope is destroyed.
  void VerifyScopedHandleState();

  // Zaps the free scoped handles to an uninitialized value.
  void ZapFreeScopedHandles();
#endif

  HandlesBlock* zone_blocks_;        // List of zone handles.
  HandlesBlock first_scoped_block_;  // First block of scoped handles.
  HandlesBlock* scoped_blocks_;      // List of scoped handles.

  friend class HandleScope;
  friend class Dart;
  friend class IsolateObjectStore;
  friend class ObjectStore;
  friend class ThreadState;
  DISALLOW_ALLOCATION();
  DISALLOW_COPY_AND_ASSIGN(Handles);
};

#if defined(DEBUG)
static const int kVMHandleSizeInWords = 3;
static const int kOffsetOfIsZoneHandle = 2;
#else
static const int kVMHandleSizeInWords = 2;
#endif
static const int kVMHandlesPerChunk = 63;
static const int kOffsetOfRawPtr = kWordSize;
class VMHandles : public Handles<kVMHandleSizeInWords,
                                 kVMHandlesPerChunk,
                                 kOffsetOfRawPtr> {
 public:
  static const int kOffsetOfRawPtrInHandle = kOffsetOfRawPtr;

  VMHandles()
      : Handles<kVMHandleSizeInWords, kVMHandlesPerChunk, kOffsetOfRawPtr>() {
#if defined(DEBUG)
    if (FLAG_trace_handles) {
      OS::PrintErr("*** Starting a new VM handle block 0x%" Px "\n",
                   reinterpret_cast<intptr_t>(this));
    }
#endif
  }
  ~VMHandles() {
#if defined(DEBUG)
    if (FLAG_trace_handles) {
      OS::PrintErr("***   Handle Counts for 0x(%" Px
                   "):Zone = %d,Scoped = %d\n",
                   reinterpret_cast<intptr_t>(this), CountZoneHandles(),
                   CountScopedHandles());
      OS::PrintErr("*** Deleting VM handle block 0x%" Px "\n",
                   reinterpret_cast<intptr_t>(this));
    }
#endif
  }

  // Visit all object pointers stored in the various handles.
  void VisitObjectPointers(ObjectPointerVisitor* visitor);

  // Allocates a handle in the current handle scope of 'zone', which must be
  // the current zone. This handle is valid only in the current handle scope
  // and is destroyed when the current handle scope ends.
  static uword AllocateHandle(Zone* zone);

  // Allocates a handle in 'zone', which must be the current zone. This handle
  // will be destroyed when the current zone is destroyed.
  static uword AllocateZoneHandle(Zone* zone);

#if defined(DEBUG)
  // Returns true if specified handle is a zone handle.
  static bool IsZoneHandle(uword handle);
#endif

  // Returns number of handles, these functions are used for testing purposes.
  static int ScopedHandleCount();
  static int ZoneHandleCount();

  friend class ApiZone;
  friend class ApiNativeScope;
};

// The class HandleScope is used to start a new handles scope in the code.
// It is used as follows:
// {
//   HANDLESCOPE(thread);
//   ....
//   .....
//   code that creates some scoped handles.
//   ....
// }
class HandleScope : public StackResource {
 public:
  explicit HandleScope(ThreadState* thread);
  ~HandleScope();

 private:
  void Initialize();

  VMHandles::HandlesBlock* saved_handle_block_;  // Handle block at prev scope.
  uword saved_handle_slot_;  // Next available handle slot at previous scope.
#if defined(DEBUG)
  HandleScope* link_;  // Link to previous scope.
#endif
  DISALLOW_IMPLICIT_CONSTRUCTORS(HandleScope);
};

// Macro to start a new Handle scope.
#define HANDLESCOPE(thread)                                                    \
  dart::HandleScope vm_internal_handles_scope_(thread);

}  // namespace dart

#endif  // RUNTIME_VM_HANDLES_H_
