import Dispatch

let queue = DispatchQueue(label: "wasi.swift.consumer")
var value = 0
queue.sync { value = 1 }
guard value == 1 else { fatalError("consumer did not run") }
print("Swift consumer OK")
