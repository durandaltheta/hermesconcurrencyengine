//SPDX-License-Identifier: MIT
//Author: Blayne Dennis 
#ifndef __HERMES_COROUTINE_ENGINE_LIFECYCLE__
#define __HERMES_COROUTINE_ENGINE_LIFECYCLE__

#include "opaque_pointer.hpp"
#include "coroutine.hpp"

namespace hce {
namespace lifecycle {

/**
 @brief stash a type erased opaque pointer till process exit

 This mechanism guarantees that a given allocated pointer will stay in memory 
 until all registered awaitables are awaited and the framework cleaned up, after 
 which the pointers will be destructed (triggering the destructors of the 
 allocated objects).
 */
void register_opaque_pointer(opaque_pointer&&);

/**
 @brief register an awaitable interface for the process to eventually join 

 The user can complete all their operations, and even exit main(), and 
 awaitables passed to this mechanism will still be awaited successfully before 
 this framework halts. 

 WARNING: interfaces registered with this mechanism are only 
 guaranteed to be joined *on process exit*. It is important that 
 awaitable_done is called when the operation is complete to free memory. The 
 reason for this design is if awaitables were awaited immediately any 
 long-running detached coroutine would create a memory leak.

 @param i an awaitable::interface that needs to be awaited
 @return a key to pass to awaitable_done()
 */
size_t register_awaitable(awaitable::interface* i);

/**
 @brief notify the process a registered awaitable interface is ready to join

 It is important for completed awaitable interfaces which previously were 
 registered with register_await() to call this method to inform the process that 
 they can be joined and cleaned up immediately. 

 WARNING: Without calling this interface will *eventually* be joined at process 
 exit, but the process will otherwise hold onto the pointer until then, which is 
 effectively a memory leak.

 @param key the key returned from register_await()
 */
void awaitable_done(size_t key);

}
}

#endif
