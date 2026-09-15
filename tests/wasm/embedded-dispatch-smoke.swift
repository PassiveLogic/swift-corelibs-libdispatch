// Embedded Swift consumer smoke test for the cooperative WASI backend.
// Compiled with -enable-experimental-feature Embedded against the
// Dispatch overlay built in embedded mode. Mirrors the coverage of
// threads-dispatch-smoke.c: async submission, cross-context semaphore
// use, serial sync, and a dispatch_after timer.
//
// Expected output: PASS: async=8/8 sync=1 group=1 timer=1 source=1 data=4

import Dispatch

let asyncTotal = 8
var asyncRan = 0
var syncRan = 0
var groupRan = 0
var timerFired = 0

let q = DispatchQueue(label: "wasi.embedded.smoke")

// Cooperative backend: a top-level async runs eagerly, before return.
for _ in 0..<asyncTotal {
    q.async { asyncRan += 1 }
}
guard asyncRan == asyncTotal else { fatalError("async blocks did not run eagerly") }

// Serial sync funnels through the same queue.
q.sync { syncRan = 1 }

// Group wait: enter/leave across an async block, then wait.
let group = DispatchGroup()
q.async(group: group) { groupRan = 1 }
guard group.wait(timeout: .now() + 2) == .success else { fatalError("group wait timed out") }

// Timer: dispatch_after fires while a blocking wait pumps.
let sem = DispatchSemaphore(value: 0)
q.asyncAfter(deadline: .now() + .milliseconds(100)) {
    timerFired = 1
    sem.signal()
}
guard sem.wait(timeout: .now() + 2) == .success else { fatalError("timer did not fire") }

// Timer source: exercises the class-bound existential returned by the
// factory, the path that needed the AnyObject constraint.
var sourceFired = 0
let src = DispatchSource.makeTimerSource(queue: q)
src.schedule(deadline: .now() + .milliseconds(50))
let srcSem = DispatchSemaphore(value: 0)
src.setEventHandler {
    sourceFired = 1
    srcSem.signal()
}
src.resume()
guard srcSem.wait(timeout: .now() + 2) == .success else { fatalError("timer source did not fire") }
src.cancel()

// DispatchData: exercises Data.swift and the shim funnel at runtime.
var bytes: [UInt8] = [1, 2]
var data = DispatchData(bytes: UnsafeRawBufferPointer(start: &bytes, count: 2))
var more: [UInt8] = [3, 4]
data.append(UnsafeRawBufferPointer(start: &more, count: 2))
var sum = 0
for b in data { sum += Int(b) }
guard sum == 10 else { fatalError("dispatch data walk failed") }

print("PASS: async=\(asyncRan)/\(asyncTotal) sync=\(syncRan) group=\(groupRan) timer=\(timerFired) source=\(sourceFired) data=\(data.count)")
