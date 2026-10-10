import Foundation
import Darwin
import IOKit

struct TempMap {
    let id: String
    let cpu: [String]
    let gpu: [String]
}

// Same SMC keys Stats averages for each Apple Silicon generation
// (exelban/stats Modules/Sensors/values.swift, fetched 2026-09-24).
let kMaps: [TempMap] = [
    TempMap(id: "m5", cpu: [
        "Tp00", "Tp04", "Tp08", "Tp0C", "Tp0G", "Tp0K",
        "Tp0O", "Tp0R", "Tp0U", "Tp0X", "Tp0a", "Tp0d",
        "Tp0g", "Tp0j", "Tp0m", "Tp0p", "Tp0u", "Tp0y",
    ], gpu: ["Tg0U", "Tg0X", "Tg0d", "Tg0g", "Tg0j", "Tg1Y", "Tg1c", "Tg1g"]),
    TempMap(id: "m4max", cpu: [
        "Te05", "Te0S", "Te09", "Te0H",
        "Tp01", "Tp05", "Tp09", "Tp0D", "Tp0V", "Tp0Y", "Tp0b", "Tp0e",
    ], gpu: ["Tg1U", "Tg1k", "Tg0K", "Tg0L", "Tg0d", "Tg0e", "Tg0j", "Tg0k"]),
    TempMap(id: "m4", cpu: [
        "Te05", "Te0S", "Te09", "Te0H",
        "Tp01", "Tp05", "Tp09", "Tp0D", "Tp0V", "Tp0Y", "Tp0b", "Tp0e",
    ], gpu: ["Tg0G", "Tg0H", "Tg0K", "Tg0L", "Tg0d", "Tg0e", "Tg0j", "Tg0k"]),
    TempMap(id: "m3", cpu: [
        "Te05", "Te0L", "Te0P", "Te0S",
        "Tf04", "Tf09", "Tf0A", "Tf0B", "Tf0D", "Tf0E",
        "Tf44", "Tf49", "Tf4A", "Tf4B", "Tf4D", "Tf4E",
    ], gpu: ["Tf14", "Tf18", "Tf19", "Tf1A", "Tf24", "Tf28", "Tf29", "Tf2A"]),
    TempMap(id: "m2", cpu: [
        "Tp1h", "Tp1t", "Tp1p", "Tp1l",
        "Tp01", "Tp05", "Tp09", "Tp0D", "Tp0X", "Tp0b", "Tp0f", "Tp0j",
    ], gpu: ["Tg0f", "Tg0j"]),
    TempMap(id: "m1", cpu: [
        "Tp09", "Tp0T",
        "Tp01", "Tp05", "Tp0D", "Tp0H", "Tp0L", "Tp0P", "Tp0X", "Tp0b",
    ], gpu: ["Tg05", "Tg0D", "Tg0L", "Tg0T"]),
]

struct Snapshot {
    var name = ""
    var host = ""
    var sensors = ""
    var cpu = 0.0
    var cpuOK = false
    var gpu: Double?
    var memUsed = 0.0
    var memTotal = 0.0
    var cpuTemp: Double?
    var gpuTemp: Double?
    var cpuTempN = 0
    var gpuTempN = 0
    var fan: Double?
    var fans: [Double] = []
    var power: Double?
    var ts = 0
}

let gLock = NSLock()
var gSnap = Snapshot()
var gOnce = false
var gPort: UInt16 = 9123

func sysctlString(_ name: String) -> String {
    var size = 0
    if sysctlbyname(name, nil, &size, nil, 0) != 0 || size <= 0 { return "" }
    var buf = [CChar](repeating: 0, count: size + 1)
    if sysctlbyname(name, &buf, &size, nil, 0) != 0 { return "" }
    return String(cString: buf)
}

func smcKey(_ key: String) -> Float? {
    var out: Float = 0
    let rc = key.withCString { smc_read_f($0, &out) }
    return rc == 0 ? out : nil
}

func validTemp(_ v: Float) -> Bool { v > 5 && v < 115 }

func meanTemps(_ keys: [String]) -> (Double, Int)? {
    var sum = 0.0
    var n = 0
    for k in keys {
        guard let v = smcKey(k), validTemp(v) else { continue }
        sum += Double(v)
        n += 1
    }
    if n == 0 { return nil }
    return (sum / Double(n), n)
}

func pickMap(_ brand: String) -> TempMap {
    let b = brand
    let preferred: TempMap?
    if b.contains("M5") { preferred = kMaps[0] }
    else if b.contains("M4") && (b.contains("Pro") || b.contains("Max") || b.contains("Ultra")) { preferred = kMaps[1] }
    else if b.contains("M4") { preferred = kMaps[2] }
    else if b.contains("M3") { preferred = kMaps[3] }
    else if b.contains("M2") { preferred = kMaps[4] }
    else if b.contains("M1") { preferred = kMaps[5] }
    else { preferred = nil }
    if let p = preferred, meanTemps(p.cpu) != nil { return p }
    var best = kMaps[0]
    var bestN = 0
    for m in kMaps {
        let n = meanTemps(m.cpu)?.1 ?? 0
        if n > bestN { bestN = n; best = m }
    }
    return best
}

func cpuTicks() -> (Double, Double, Double, Double)? {
    var info = host_cpu_load_info()
    var count = mach_msg_type_number_t(MemoryLayout<host_cpu_load_info_data_t>.stride / MemoryLayout<integer_t>.stride)
    let rc = withUnsafeMutablePointer(to: &info) {
        $0.withMemoryRebound(to: integer_t.self, capacity: Int(count)) {
            host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO, $0, &count)
        }
    }
    if rc != KERN_SUCCESS { return nil }
    return (Double(info.cpu_ticks.0), Double(info.cpu_ticks.1), Double(info.cpu_ticks.2), Double(info.cpu_ticks.3))
}

func memoryUsedTotal() -> (Double, Double) {
    var stats = vm_statistics64()
    var count = mach_msg_type_number_t(MemoryLayout<vm_statistics64_data_t>.stride / MemoryLayout<integer_t>.stride)
    let rc = withUnsafeMutablePointer(to: &stats) {
        $0.withMemoryRebound(to: integer_t.self, capacity: Int(count)) {
            host_statistics64(mach_host_self(), HOST_VM_INFO64, $0, &count)
        }
    }
    var page = vm_page_size
    if page == 0 { page = vm_kernel_page_size }
    let p = Double(page)
    let gib = 1024.0 * 1024.0 * 1024.0
    var totalBytes: UInt64 = 0
    var sz = MemoryLayout<UInt64>.size
    sysctlbyname("hw.memsize", &totalBytes, &sz, nil, 0)
    let total = Double(totalBytes) / gib
    if rc != KERN_SUCCESS { return (0, total) }
    // Same definition as Stats RAM UsageReader:
    // active + inactive + speculative + wired + compressed - purgeable - external.
    let active = Double(stats.active_count) * p
    let inactive = Double(stats.inactive_count) * p
    let speculative = Double(stats.speculative_count) * p
    let wired = Double(stats.wire_count) * p
    let compressed = Double(stats.compressor_page_count) * p
    let purgeable = Double(stats.purgeable_count) * p
    let external = Double(stats.external_page_count) * p
    var used = (active + inactive + speculative + wired + compressed - purgeable - external) / gib
    if used < 0 { used = 0 }
    if total > 0 && used > total { used = total }
    return (used, total)
}

func gpuPercent() -> Double? {
    guard let match = IOServiceMatching("IOAccelerator") else { return nil }
    var it: io_iterator_t = 0
    if IOServiceGetMatchingServices(kIOMainPortDefault, match, &it) != KERN_SUCCESS { return nil }
    defer { IOObjectRelease(it) }
    var best: Double?
    var svc = IOIteratorNext(it)
    while svc != 0 {
        var props: Unmanaged<CFMutableDictionary>?
        if IORegistryEntryCreateCFProperties(svc, &props, kCFAllocatorDefault, 0) == KERN_SUCCESS,
           let dict = props?.takeRetainedValue() as? [String: Any],
           let perf = dict["PerformanceStatistics"] as? [String: Any],
           let num = perf["Device Utilization %"] as? NSNumber {
            let v = min(100.0, max(0.0, num.doubleValue))
            if best == nil || v > best! { best = v }
        }
        IOObjectRelease(svc)
        svc = IOIteratorNext(it)
    }
    return best
}

func readFans() -> [Double] {
    guard let nRaw = smcKey("FNum") else { return [] }
    let n = min(6, max(0, Int(nRaw.rounded())))
    var out: [Double] = []
    for i in 0..<n {
        let key = "F\(i)Ac"
        guard let v = smcKey(key), v >= 0, v < 20000 else { continue }
        out.append(Double(v))
    }
    return out
}

func jsonNumber(_ v: Double) -> String {
    if !v.isFinite { return "0.0" }
    let neg = v < 0
    let scaled = Int((abs(v) * 10.0).rounded())
    return "\(neg ? "-" : "")\(scaled / 10).\(abs(scaled % 10))"
}

func jsonEscape(_ s: String) -> String {
    var o = ""
    for ch in s.unicodeScalars {
        switch ch {
        case "\\": o += "\\\\"
        case "\"": o += "\\\""
        default:
            if ch.value < 0x20 { o += "?" }
            else { o.unicodeScalars.append(ch) }
        }
    }
    return o
}

func jsonOf(_ s: Snapshot) -> String {
    var p: [String] = []
    p.append("\"name\":\"\(jsonEscape(s.name))\"")
    p.append("\"host\":\"\(jsonEscape(s.host))\"")
    if s.cpuOK { p.append("\"cpu\":\(jsonNumber(s.cpu))") }
    if let g = s.gpu { p.append("\"gpu\":\(jsonNumber(g))") }
    p.append("\"memory_used_gb\":\(jsonNumber(s.memUsed))")
    p.append("\"memory_total_gb\":\(jsonNumber(s.memTotal))")
    if let t = s.cpuTemp { p.append("\"cpu_temp_c\":\(jsonNumber(t))") }
    if let t = s.gpuTemp { p.append("\"gpu_temp_c\":\(jsonNumber(t))") }
    if let f = s.fan { p.append("\"fan_rpm\":\(Int(f.rounded()))") }
    if let w = s.power { p.append("\"power_w\":\(jsonNumber(w))") }
    p.append("\"timestamp\":\(s.ts)")
    return "{" + p.joined(separator: ",") + "}"
}

var gPrevCPU: (Double, Double, Double, Double)?
var gMap: TempMap = kMaps[0]
var gMapReady = false

func sample() {
    let brand = sysctlString("machdep.cpu.brand_string")
    if !gMapReady {
        gMap = pickMap(brand)
        gMapReady = true
        fputs("macmetrics sensors=\(gMap.id) chip=\(brand)\n", stderr)
    }
    var snap = Snapshot()
    snap.name = Host.current().localizedName ?? ProcessInfo.processInfo.hostName
    snap.host = ProcessInfo.processInfo.hostName
    snap.sensors = gMap.id
    snap.ts = Int(time(nil))

    if let now = cpuTicks() {
        if let prev = gPrevCPU {
            let du = max(0, now.0 - prev.0)
            let ds = max(0, now.1 - prev.1)
            let di = max(0, now.2 - prev.2)
            let dn = max(0, now.3 - prev.3)
            let total = du + ds + di + dn
            if total > 0 {
                snap.cpu = (du + ds + dn) / total * 100.0
                snap.cpuOK = true
            }
        }
        gPrevCPU = now
    }
    snap.gpu = gpuPercent()
    let mem = memoryUsedTotal()
    snap.memUsed = mem.0
    snap.memTotal = mem.1
    if smc_open() == 0 {
        if let (avg, n) = meanTemps(gMap.cpu) { snap.cpuTemp = avg; snap.cpuTempN = n }
        if let (avg, n) = meanTemps(gMap.gpu) { snap.gpuTemp = avg; snap.gpuTempN = n }
        let fans = readFans()
        snap.fans = fans
        if let m = fans.max() { snap.fan = m }
        if let p = smcKey("PSTR"), p >= 0.05, p <= 800 { snap.power = Double(p) }
    }
    gLock.lock()
    gSnap = snap
    gLock.unlock()
}

func httpResponse(_ code: Int, _ type: String, _ body: String) -> String {
    let reason = code == 200 ? "OK" : "Not Found"
    return "HTTP/1.1 \(code) \(reason)\r\nContent-Type: \(type)\r\nContent-Length: \(body.utf8.count)\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n\(body)"
}

func serve() {
    let fd = socket(AF_INET, SOCK_STREAM, 0)
    if fd < 0 { fputs("macmetrics socket failed\n", stderr); exit(1) }
    var opt: Int32 = 1
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, socklen_t(MemoryLayout.size(ofValue: opt)))
    var addr = sockaddr_in()
    addr.sin_family = sa_family_t(AF_INET)
    addr.sin_port = gPort.bigEndian
    addr.sin_addr.s_addr = in_addr_t(0)
    let bound = withUnsafePointer(to: &addr) {
        $0.withMemoryRebound(to: sockaddr.self, capacity: 1) {
            bind(fd, $0, socklen_t(MemoryLayout<sockaddr_in>.size))
        }
    }
    if bound != 0 || listen(fd, 8) != 0 {
        fputs("macmetrics bind :\(gPort) failed\n", stderr)
        exit(1)
    }
    fputs("macmetrics listening on 0.0.0.0:\(gPort)\n", stderr)
    while true {
        var clientAddr = sockaddr_in()
        var len = socklen_t(MemoryLayout<sockaddr_in>.size)
        let cfd = withUnsafeMutablePointer(to: &clientAddr) {
            $0.withMemoryRebound(to: sockaddr.self, capacity: 1) {
                accept(fd, $0, &len)
            }
        }
        if cfd < 0 { continue }
        var tv = timeval(tv_sec: 2, tv_usec: 0)
        setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv, socklen_t(MemoryLayout<timeval>.size))
        var buf = [UInt8](repeating: 0, count: 1024)
        let n = recv(cfd, &buf, buf.count, 0)
        var path = "/"
        if n > 0, let req = String(bytes: buf.prefix(n), encoding: .utf8) {
            let line = req.split(separator: "\n", maxSplits: 1).first ?? ""
            let parts = line.split(separator: " ")
            if parts.count >= 2 { path = String(parts[1]) }
            if let q = path.firstIndex(of: "?") { path = String(path[..<q]) }
        }
        gLock.lock()
        let snap = gSnap
        gLock.unlock()
        let body: String
        let code: Int
        let type: String
        if path == "/metrics" || path == "/" {
            body = jsonOf(snap)
            code = 200
            type = "application/json"
        } else if path == "/health" {
            body = "ok\n"
            code = 200
            type = "text/plain"
        } else {
            body = "{\"error\":\"not found\"}"
            code = 404
            type = "application/json"
        }
        let resp = httpResponse(code, type, body)
        _ = resp.withCString { send(cfd, $0, strlen($0), 0) }
        close(cfd)
    }
}

let args = CommandLine.arguments
if let i = args.firstIndex(of: "--port"), i + 1 < args.count, let p = UInt16(args[i + 1]) {
    gPort = p
}
gOnce = args.contains("--once")

if smc_open() != 0 {
    fputs("macmetrics AppleSMC open failed; temps, fan, and power will be omitted\n", stderr)
}

if gOnce {
    sample()
    Thread.sleep(forTimeInterval: 1.0)
    sample()
    gLock.lock()
    let s = gSnap
    gLock.unlock()
    let fans = s.fans.map { String(Int($0.rounded())) }.joined(separator: ",")
    fputs("cpu \(jsonNumber(s.cpu)) gpu \(s.gpu.map(jsonNumber) ?? "nil") mem \(jsonNumber(s.memUsed))/\(jsonNumber(s.memTotal)) cpu_t \(s.cpuTemp.map(jsonNumber) ?? "nil") n=\(s.cpuTempN) gpu_t \(s.gpuTemp.map(jsonNumber) ?? "nil") n=\(s.gpuTempN) fan \(s.fan.map { String(Int($0.rounded())) } ?? "nil") [\(fans)] power \(s.power.map(jsonNumber) ?? "nil")\n", stderr)
    print(jsonOf(s))
    exit(0)
}

sample()
Thread.detachNewThread { serve() }
while true {
    Thread.sleep(forTimeInterval: 1.0)
    sample()
}
