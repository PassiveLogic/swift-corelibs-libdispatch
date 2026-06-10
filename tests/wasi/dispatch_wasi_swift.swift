// WASI Swift-overlay test: a downstream consumer using `import Dispatch`.
// Exercises DispatchQueue.async, DispatchGroup(+notify), and asyncAfter, driven
// by dispatchMain(). See swift-overlay.md for the build recipe.
import Dispatch
#if canImport(WASILibc)
import WASILibc
#endif

print("dtest: start")

let group = DispatchGroup()
DispatchQueue.global().async(group: group) {
    print("[dispatch] grouped async ran")
}
group.notify(queue: DispatchQueue.global()) {
    print("[dispatch] group.notify ran")
}
DispatchQueue.global().asyncAfter(deadline: .now() + .milliseconds(100)) {
    print("[dispatch] asyncAfter(100ms) fired -> exit")
    exit(0)
}

dispatchMain()
