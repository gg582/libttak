## LibTTAK

![Mascot](./mascot.png)

*LibTTAK's Mascot, Memuh the sea rabbit*

*Memuh consumes the memory leftovers when the lifetime expires.*

**Gentle.**

**Predictable.**

**Explicit.**

[Docs](https://gg582.github.io/libttak)

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

## Why LibTTAK?

LibTTAK exists because defensive patterns appear even in languages that promise safety when engineers need deterministic cleanup, staged shutdowns, or externally imposed invariants. The library makes those guard rails explicit and mechanical in C so that the same discipline does not need to be reinvented per project. When we line up the best practices from each ecosystem, the question is no longer “who writes cleaner code” but “where does the lifetime knowledge reside?”

### Rust (Drop + ScopeGuard) stays on alert

Rust can rely on `Drop` and scope guards so that no explicit `close()` call sneaks through a fast path, yet the programmer still has to restate every invariant directly in the control flow.

```rust
use scopeguard::ScopeGuard;

fn guarded_session(repo: &Repo, cfg: &Config) -> Result<ArmedSession, Error> {
    let guard = repo.open().map_err(Error::open)?;
    let guard = scopeguard::guard(guard, |mut g| g.close());

    if guard.is_dead() {
        return Err(Error::DeadRepo);
    }

    let mut session = Session::new(cfg.clone()).map_err(Error::session)?;
    if session.remaining_budget() == 0 {
        return Err(Error::Depleted);
    }

    let key = cfg.key.as_deref().ok_or(Error::MissingKey)?;
    if key.len() < 32 {
        return Err(Error::WeakKey);
    }

    let guard = ScopeGuard::into_inner(guard);
    Ok(Session::armed(session, guard))
}
```

### C++ (smart pointers + custom deleters) never unclenches

Modern C++ leans on `std::unique_ptr`, custom deleters, and `tl::expected`/`std::expected` to guarantee exception safety, but the ergonomics still depend on wiring the same checks and releases at every return site.

```cpp
#include <memory>
#include <utility>

auto guarded_session(Repo& repo, const Config& cfg) -> tl::expected<ArmedSession, Error> {
    using guard_ptr = std::unique_ptr<RepoGuard, decltype(&RepoGuard::close)>;
    using session_ptr = std::unique_ptr<Session, decltype(&Session::abort)>;

    guard_ptr guard(repo.open().value(), &RepoGuard::close);
    if (guard->is_dead()) {
        return tl::unexpected(Error::DeadRepo{});
    }

    session_ptr session(Session::create(cfg).value(), &Session::abort);
    if (session->remaining_budget() == 0) {
        return tl::unexpected(Error::Depleted{});
    }

    const auto* key = cfg.key();
    if (!key || key->size() < 32) {
        return tl::unexpected(Error::WeakKey{});
    }

    guard->seal();
    session->prime(*key);

    auto raw_session = session.release();
    auto raw_guard = guard.release();
    return Session::arm(raw_session, raw_guard);
}
```

### LibTTAK (lifetime-as-data) just clocks scopes

LibTTAK encodes the lifetime directly into the allocation. Cleanup hooks and access validation stem from the data’s declared expiry rather than ad-hoc guard paths, so the same snippet is both the runtime policy and the business logic. The model is not tied to `int main`—production code usually builds helpers that manage lifetimes at subsystem boundaries and let higher layers decide when to advance or release them.

```c
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <ttak/mem/mem.h>

typedef struct {
    uint64_t expires_at;
    char token[64];
} session_blob_t;

static session_blob_t *session_claim(uint64_t now, uint64_t lifetime) {
    session_blob_t *blob = ttak_mem_alloc(sizeof(*blob), lifetime, now);
    if (!blob) {
        return NULL;
    }
    blob->expires_at = now + lifetime;
    snprintf(blob->token, sizeof(blob->token),
             "session expires@%" PRIu64, blob->expires_at);
    return blob;
}

static void session_tick(session_blob_t *blob, uint64_t tick) {
    session_blob_t *view = ttak_mem_access(blob, tick);
    if (!view) {
        printf("[tick %" PRIu64 "] lifetime closed\n", tick);
        return;
    }
    printf("[tick %" PRIu64 "] %s\n", tick, view->token);
}

static void session_release(session_blob_t **blob) {
    if (!blob || !*blob) {
        return;
    }
    ttak_mem_free(*blob);
    *blob = NULL;
}

void session_daemon(uint64_t now, const uint64_t *ticks, size_t n) {
    session_blob_t *blob = session_claim(now, 1200);
    if (!blob) {
        fputs("allocation failed\n", stderr);
        return;
    }
    for (size_t i = 0; i < n; ++i) {
        session_tick(blob, ticks[i]);
    }
    session_release(&blob);
}
```

Three best-practice snippets, same defensive posture: all checks are explicit, every slow path is guarded, and success must be restated. LibTTAK’s edge is that the lifetime is *data*, not control flow.

- **Lifetime knowledge** lives inside the allocation record, so `ttak_mem_access` enforces expiry with no duplicate branching while the runtime can still enumerate `tt_inspect_dirty_pointers`.
- **Operational coupling** becomes declarative: a single `ttak_mem_alloc` call covers creation, observation, and cleanup pressure (manual or `tt_autoclean_dirty_pointers`), whereas RAII code must restate that contract per call site.
- **Shared tooling** is uniform; whether the consumer is C, C++, or Rust-through-FFI, LibTTAK exposes the same hooks for tracing, leak hunting, and staged shutdowns.

That is why “Gentle. Predictable. Explicit.” is not a slogan but the literal data model.

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
