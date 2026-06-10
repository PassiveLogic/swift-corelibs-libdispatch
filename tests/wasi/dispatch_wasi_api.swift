import Dispatch
#if canImport(WASILibc)
import WASILibc
#endif

func check(_ ok: Bool, _ what: String) {
    print("\(ok ? "ok  " : "FAIL") - \(what)")
    if !ok { exit(1) }
}

print("api: start")

// 1. custom serial queue: FIFO ordering
let serial = DispatchQueue(label: "serial")
var order: [Int] = []
for i in 0..<3 { serial.async { order.append(i) } }
serial.async {
    check(order == [0, 1, 2], "serial queue FIFO order")
}

// 2. DispatchWorkItem + perform
var ran = false
let wi = DispatchWorkItem { ran = true }
serial.async(execute: wi)
serial.async { check(ran, "DispatchWorkItem executed") }

// 3. DispatchSemaphore fast path (signal before wait => no block)
let sem = DispatchSemaphore(value: 0)
sem.signal()
check(sem.wait(timeout: .now()) == .success, "semaphore signal/wait fast path")

// 4. concurrentPerform (runs inline serially on wasi)
let counter = UnsafeMutablePointer<Int>.allocate(capacity: 1)
counter.pointee = 0
DispatchQueue.concurrentPerform(iterations: 10) { _ in counter.pointee += 1 }
check(counter.pointee == 10, "concurrentPerform ran 10 iterations")
counter.deallocate()

// 5. DispatchData
let bytes: [UInt8] = [1, 2, 3, 4, 5]
let data = bytes.withUnsafeBytes { DispatchData(bytes: $0) }
check(data.count == 5, "DispatchData count")

// 6. Repeating DispatchSource timer: fire 3 times then cancel & exit
let timer = DispatchSource.makeTimerSource(queue: .global())
var fires = 0
timer.schedule(deadline: .now() + .milliseconds(20), repeating: .milliseconds(20))
timer.setEventHandler {
    fires += 1
    print("ok   - repeating timer fire \(fires)")
    if fires == 3 {
        timer.cancel()
        print("api: all checks passed")
        exit(0)
    }
}
timer.resume()

dispatchMain()
