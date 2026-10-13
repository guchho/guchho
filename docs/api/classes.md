---
title: API classes
description: The classes the guchho package exports.
---

# API classes

## BuildFailure

A build that failed. Carries the diagnostics it failed with.

```ts
class BuildFailure extends Error {
  constructor(errors: Message[], warnings?: Message[])
  readonly errors: Message[]
  readonly warnings: Message[]
}
```

## ServiceError

The service itself could not be reached or could not answer. A different thing
from a `BuildFailure`, with a different fix: this one is about the
installation, that one is about the code being built.

```ts
class ServiceError extends Error {
  readonly code?: number
  readonly errors?: Message[]
  readonly warnings?: Message[]
}
```

## BuildContext

A build set up once and built as many times as asked. Dispose it when finished:
it holds a watcher, and sometimes a listening socket.

```ts
interface BuildContext {
  rebuild(): Promise<BuildResult>
  watch(options?: {
    delay?: number
    onRebuild?: (result: BuildResult) => void
  }): Promise<void>
  cancel(): Promise<void>
  serve(options?: {
    servedir?: string
    port?: number
    host?: string
    fallback?: string
  }): Promise<{ host: string; port: number; hosts: string[] }>
  dispose(): Promise<void>
}
```

`watch()` here is not `guchho watch`: the service answers requests and sends
nothing unasked, so a rebuild has to be asked for.

## Watcher

A running watcher, from `watch()`.

```ts
interface Watcher {
  readonly result: BuildResult | null
  readonly interval: number
  readonly context: BuildContext
  readonly closed: boolean
  on(event: 'buildStart', listener: () => void): this
  on(event: 'buildEnd', listener: (result: BuildResult) => void): this
  on(event: 'error', listener: (error: Error) => void): this
  off(event: string, listener: (...args: never[]) => void): this
  rebuild(): Promise<BuildResult>
  close(): Promise<void>
  [Symbol.asyncDispose](): Promise<void>
}
```

Every `buildEnd` is a rebuild this handle performed. Asking for a rebuild runs a
build pass whether or not anything changed, which is why `interval` is opt-in
and has a real price - set it to `0` and drive it yourself.

## DevServer

A running dev server, from `serve()`.

```ts
interface DevServer {
  readonly host: string
  readonly port: number
  readonly hosts: string[]
  readonly url: string
  readonly context: BuildContext
  readonly closed: boolean
  rebuild(): Promise<BuildResult>
  close(): Promise<void>
  [Symbol.asyncDispose](): Promise<void>
}
```

`url` is built from a connectable address even when the engine bound a
wildcard, because `0.0.0.0` is not a destination a browser can route to.

## See also

- [JavaScript API](README.md)
- [Errors & warnings](../reference/errors-and-warnings.md)
