import Dispatch
#if canImport(WASILibc)
import WASILibc
#endif

print("DispatchDemo: start")
let q = DispatchQueue(label: "work")
let group = DispatchGroup()
for i in 1...3 { q.async(group: group) { print("task \(i)") } }
group.notify(queue: .main) { print("all tasks done") }
DispatchQueue.main.asyncAfter(deadline: .now() + .milliseconds(50)) {
    print("timer fired -> exit")
    exit(0)
}
dispatchMain()
