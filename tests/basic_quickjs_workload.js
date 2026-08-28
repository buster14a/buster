/* Deterministic QuickJS workload for the Buster compatibility harness.
   Every line it prints has to be identical between a Buster-built and a
   Clang-built qjs: the point is to compare the produced engines, so nothing
   here may depend on the clock, the allocator's addresses, the environment or
   iteration over unordered data.  The areas are the ones the compatibility
   target stresses: floating point edge cases, 64-bit and bigint arithmetic,
   tagged values, closures and generators, garbage collection, regular
   expressions, bytecode and the standard library. */

function line(name, value) {
    print(name + ": " + value);
}

/* Floating point: rounding, subnormals, the sign of zero, and the shortest
   round-tripping decimal form dtoa.c is responsible for. */
line("float.repr", [0.1, 1 / 3, 1e21, 1e-7, 5e-324, 1.7976931348623157e308].join(" "));
line("float.round", [(0.1 + 0.2).toFixed(17), (2 ** 53).toString(), (2 ** 53 + 2).toString()].join(" "));
line("float.zero", [Object.is(-0, 0 * -1), 1 / -0, Math.sign(-0)].join(" "));
line("float.nan", [NaN === NaN, Number.isNaN(0 / 0), Math.max(), Math.min()].join(" "));
line("float.precision", [Math.sqrt(2), Math.cbrt(27), Math.hypot(3, 4), Math.fround(1.1)].join(" "));
line("float.parse", [parseFloat("3.14e2"), Number("0x10"), (255).toString(16), (0.5).toString(2)].join(" "));

/* Integers and bigints: 32-bit wrapping, the shift operators, and arbitrary
   precision arithmetic. */
line("int.shift", [-4 >> 1, -4 >>> 1, 1 << 31, (1 << 31) >>> 0, -1 >>> 28].join(" "));
line("int.bits", [0xffffffff | 0, ~0, 5 % -3, -5 % 3, (2 ** 31) | 0].join(" "));
let factorial = 1n;
for (let index = 1n; index <= 30n; index += 1n) factorial *= index;
line("bigint.factorial", factorial.toString());
line("bigint.ops", [(1n << 100n).toString(), (-7n / 2n).toString(), (-7n % 2n).toString(), BigInt.asIntN(8, 255n).toString()].join(" "));
line("bigint.compare", [1n < 2, 2n == 2, 2n === 2, BigInt("0x1f").toString()].join(" "));

/* Tagged values and property shapes. */
const shapes = [];
for (let index = 0; index < 64; index += 1) {
    const object = {};
    object["key" + index] = index;
    object.shared = index * 2;
    shapes.push(object);
}
line("object.shapes", shapes[63].key63 + " " + shapes[63].shared + " " + Object.keys(shapes[0]).join(","));
line("object.descriptor", JSON.stringify(Object.getOwnPropertyDescriptor({ value: 1 }, "value")));

/* Closures, generators and exceptions. */
function counter() {
    let count = 0;
    return () => (count += 1);
}
const next = counter();
next(); next();
line("closure.count", next());
function* fibonacci() {
    let [previous, current] = [0, 1];
    while (true) {
        yield previous;
        [previous, current] = [current, previous + current];
    }
}
const fibonacciValues = [];
for (const value of fibonacci()) {
    if (fibonacciValues.length === 20) break;
    fibonacciValues.push(value);
}
line("generator.fib", fibonacciValues.join(","));
function thrower(depth) {
    if (depth === 0) throw new RangeError("bottom");
    return thrower(depth - 1);
}
try {
    thrower(64);
} catch (error) {
    line("exception", error.name + " " + error.message);
}

/* Garbage collection: build and drop a cyclic graph, then ask the runtime to
   collect it.  The observable result is that the survivors are intact. */
function buildCycles(count) {
    let head = null;
    for (let index = 0; index < count; index += 1) {
        const node = { index: index, next: head, self: null };
        node.self = node;
        head = node;
    }
    return head;
}
let survivors = buildCycles(2000);
buildCycles(20000);
if (typeof std !== "undefined" && std.gc) std.gc();
let survivorSum = 0;
for (let node = survivors; node; node = node.next) survivorSum += node.index;
line("gc.survivors", survivorSum + " " + (survivors.self === survivors));

/* Regular expressions: the compiler, the backtracker and the unicode tables. */
const pattern = /([A-Za-z]+)\s+(\d+)(?:\s+(?<tail>\w+))?/u;
line("regexp.exec", JSON.stringify(pattern.exec("alpha 42 omega").slice(0, 4)));
line("regexp.groups", pattern.exec("beta 7").groups.tail);
line("regexp.replace", "a1b22c333".replace(/(\d+)/g, (m) => "[" + m.length + "]"));
line("regexp.unicode", [/\p{Letter}/u.test("é"), /\p{Nd}/u.test("٣"), "ABC".match(/b/iu).index].join(" "));
line("regexp.split", "1, 2 ,3".split(/\s*,\s*/).join("|"));

/* Strings, unicode and normalization. */
line("string.unicode", ["é".normalize("NFD").length, "é".normalize("NFC"), "😀".length, [..."😀a"].length].join(" "));
line("string.case", ["ß".toUpperCase(), "İ".toLowerCase().length, "abc".localeCompare("abd")].join(" "));
line("string.pad", ["7".padStart(3, "0"), "x".repeat(4), "  trim  ".trim()].join(" "));

/* JSON and structured serialization. */
const document = { name: "quickjs", version: [2026, 6, 4], values: [1, -0.5, true, null], nested: { deep: { deeper: "yes" } } };
line("json.roundtrip", JSON.stringify(JSON.parse(JSON.stringify(document))));
line("json.replacer", JSON.stringify(document, ["name", "version"]));
line("json.space", JSON.stringify({ a: 1 }, null, 2).replace(/\n/g, "\\n"));

/* Typed arrays and DataView: the byte-level view of the same numbers. */
const buffer = new ArrayBuffer(16);
const view = new DataView(buffer);
view.setFloat64(0, 1.5, true);
view.setInt32(8, -123456, false);
view.setBigUint64(0, 0x0102030405060708n, false);
const bytes = new Uint8Array(buffer);
line("dataview.bytes", Array.from(bytes.slice(0, 12)).join(","));
line("dataview.read", [view.getBigUint64(0, false).toString(), view.getInt32(8, false), view.getUint16(8, true)].join(" "));
const typed = new Float64Array([3, 1, 2, -0, 0, NaN]);
typed.sort();
line("typedarray.sort", Array.from(typed).map((v) => Object.is(v, -0) ? "-0" : String(v)).join(","));

/* Collections and iteration order. */
const map = new Map([["b", 2], ["a", 1]]);
map.set("c", 3);
map.delete("b");
line("map", JSON.stringify([...map.entries()]));
const set = new Set([3, 1, 3, 2]);
line("set", [...set].join(",") + " " + set.size);

/* Sorting: a comparator-driven sort of a deliberately awkward array. */
const values = [10, 9, 1, -3, 0, 2.5, -0, 100, 7];
line("sort.numeric", values.slice().sort((a, b) => a - b).join(","));
line("sort.default", values.slice().sort().join(","));

/* Bytecode: eval and Function build and run new code at runtime. */
line("eval", eval("(function (a, b) { return a * b + 1; })")(6, 7));
line("function", new Function("a", "return a.map((x) => x * 2).join('-');")([1, 2, 3]));

/* Dates: fixed epoch values only, never the clock. */
const date = new Date(Date.UTC(2026, 5, 4, 12, 34, 56, 789));
line("date", [date.toISOString(), date.getTime(), Date.UTC(1970, 0, 1)].join(" "));

print("workload.done");
