## LibTTAK
Timestamp-tracking memory lifetime, thread and async toolkit

LibTTAK provides safer C development by tracking memory lifetime.
All dynamically allocated objects created through libttak
have an explicit lifetime.

When a lifetime expires, the associated memory can be cleaned
by calling a cleaner function.
This is not garbage collection.
Nothing is freed unless the user explicitly requests it.

- Memory is collected when you decide to clean it
- No stop-the-world GC
- Every allocated block belongs to a lifetime

------------------------------------------------------------

## How cool is it?

See examples under the apps directory.

LibTTAK allows grouping temporary allocations under a single lifetime.
This makes it easier to write complex or long-running programs
without manually tracking every free path.

Intermediate objects can be allocated freely and released together
at a well-defined point.

------------------------------------------------------------

## C is good, if you stay safe

LibTTAK prioritizes safety over performance.

It does not claim to be faster than plain C.
Its performance characteristics are closer to modern systems languages,
but without C++ language features.

The library follows C-style programming.
There are no templates, iterators, or hidden abstractions.
Most behavior is explicit and visible in code.

------------------------------------------------------------

## What libttak provides

LibTTAK consists of several modules designed
to work around the lifetime-based memory model.

- Memory
  Lifetime-tracked allocation and deterministic cleanup

- Thread
  Thread abstraction using atomics and synchronization primitives

- Async
  Lightweight async layer built on threads and timing

- Data structures
  Containers designed to work with lifetime-managed memory

- Math
  Math utilities that rely on controlled allocation lifetimes

- Scheduling
  Priority-based scheduling helpers

Low-level utilities such as atomics, synchronization,
and timing are provided to support these modules.

------------------------------------------------------------

## Memory lifetime model

Every allocation performed through libttak
is associated with a lifetime.

A lifetime represents a scope or time span defined by the user.
When the lifetime ends, all allocations associated with it
can be released safely in one step.

There is no automatic memory reclamation.
Cleanup only happens when explicitly requested.

------------------------------------------------------------

## Example (conceptual)

The intended usage is as follows:

    lifetime_t scope = lifetime_begin();

    int *buf = ttak_alloc(scope, sizeof(int) * 1024);

    /* use buf */

    lifetime_end(scope);

All memory allocated under this lifetime
is released at this point.

------------------------------------------------------------

## Notes

LibTTAK does not attempt to change the nature of C.

If dangerous patterns are avoided and ownership is explicit,
C remains a viable and effective language.

LibTTAK exists to support that style of development.
