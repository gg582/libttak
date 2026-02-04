## LibTTAK

![Mascot](./mascot.png)

*LibTTAK's Mascot, Memuh the sea rabbit*
*Memuh consumes the memory leftovers when the lifetime expires.*

Gentle.
Predictable.
Explicit.

A small and friendly C systems collection for safer memory
and enjoyable programming.

LibTTAK provides safer C development by tracking memory lifetimes.
All dynamically allocated objects created through libttak
are associated with an explicit lifetime.

When a lifetime expires, the associated memory can be cleaned
by calling a user-controlled cleanup function.
This is not garbage collection.
No memory is freed unless explicitly requested by the user.

- Memory is reclaimed only when you decide to clean it
- No stop-the-world behavior
- Every allocation belongs to a well-defined lifetime

## How is it used?

See the tutorials directory for step-by-step examples.
The apps directory contains complete, working applications
built on top of libttak.

LibTTAK allows grouping temporary allocations under a single lifetime.
This makes it possible to write complex or long-running programs
without manually tracking every individual free path.

Intermediate objects can be allocated freely
and released together at a well-defined point.

------------------------------------------------------------

## C is good, if you stay safe

LibTTAK prioritizes safety over raw performance.

It does not claim to be faster than plain C.
Its performance characteristics are closer to modern systems languages,
while preserving a traditional C programming model.

The library follows conventional C style.
There are no templates, iterators, or hidden abstractions.
Most behavior is explicit and visible in the source code.

------------------------------------------------------------

## What libttak provides

LibTTAK consists of several modules designed
around a lifetime-based memory model.

- Memory
  Lifetime-tracked allocation with deterministic cleanup

- Everything as a type
  Functions, variables, and memory contexts are represented
  explicitly as typed objects rather than implicit global state

- Ownership
  Explicit ownership rules to enforce clear code structure

- Thread
  Thread abstractions built on atomics and synchronization primitives

- Async
  A lightweight asynchronous layer based on threads and timing

- Data structures
  Containers designed to operate on lifetime-managed memory

- Math
  Math utilities that rely on controlled allocation lifetimes

- Scheduling
  Priority-based scheduling helpers

Low-level wrappers for atomics, synchronization,
and timing are provided to support these modules.

------------------------------------------------------------

## Memory lifetime model

Every allocation performed through libttak
is associated with a lifetime.

A lifetime represents a scope or time span defined by the user.
When a lifetime ends, all allocations associated with it
can be released safely in a single operation.

There is no automatic memory reclamation.
Cleanup occurs only when explicitly requested.

------------------------------------------------------------

## Forced dependency

An owner constrains its member components.
Each component follows a defined sequence of steps
to participate in program execution.

This design deliberately limits certain degrees of freedom
to reduce ambiguity and enforce consistent structure.

- Reproducible program behavior
- No uncontrolled memory growth
- Clear owner-member relationships

------------------------------------------------------------

## Notes

LibTTAK provides a constrained programming model
designed to reduce common sources of error in C.

When dangerous patterns are avoided
and ownership is made explicit,
C remains a viable and effective systems language.

LibTTAK exists to support that style of development.
