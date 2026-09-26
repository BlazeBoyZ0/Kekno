#include <iostream>
#include <sstream>
#include <fstream>
#include <cassert>
#include <cmath>
#include <filesystem>
#include "compiler.h"
#include "vm.h"

namespace fs = std::filesystem;

static int g_testsPassed = 0;
static int g_testsFailed = 0;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (cond) { \
            g_testsPassed++; \
        } else { \
            g_testsFailed++; \
            std::cerr << "FAIL: " << msg << " (" << __FILE__ << ":" << __LINE__ << ")" << std::endl; \
        } \
    } while (0)

static std::string runCode(VM& vm, const std::string& code, bool& compileSuccess, const std::string& scriptPath = "") {
    std::stringstream buffer;
    std::streambuf* oldCout = std::cout.rdbuf(buffer.rdbuf());

    Chunk chunk;
    Compiler compiler(code, chunk);
    compileSuccess = compiler.compile();
    if (compileSuccess) {
        vm.run(chunk, scriptPath);
    }

    std::cout.rdbuf(oldCout);
    return buffer.str();
}

static std::string runCodeFresh(const std::string& code, bool& compileSuccess, const std::string& scriptPath = "") {
    VM vm;
    return runCode(vm, code, compileSuccess, scriptPath);
}

static void testNativeFunctionsAndMath() {
    bool ok = false;
    std::string out;

    // size()
    out = runCodeFresh("echo size([1, 2, 3]) ~", ok);
    TEST_ASSERT(ok && out.find("=> 3") != std::string::npos, "size array");
    out = runCodeFresh("echo size(\"hello\") ~", ok);
    TEST_ASSERT(ok && out.find("=> 5") != std::string::npos, "size string");
    out = runCodeFresh("echo size({\"a\": 1, \"b\": 2}) ~", ok);
    TEST_ASSERT(ok && out.find("=> 2") != std::string::npos, "size map");

    // keys() and values()
    out = runCodeFresh("let m = {\"a\": 10} ~ echo keys(m) ~", ok);
    TEST_ASSERT(ok && out.find("[\"a\"]") != std::string::npos, "keys valid");
    out = runCodeFresh("let m = {\"a\": 10} ~ echo values(m) ~", ok);
    TEST_ASSERT(ok && out.find("[10]") != std::string::npos, "values valid");

    // has()
    out = runCodeFresh("echo has({\"a\": 1}, \"a\") ~", ok);
    TEST_ASSERT(ok && out.find("=> true") != std::string::npos, "has map true");
    out = runCodeFresh("echo has([10, 20], 20) ~", ok);
    TEST_ASSERT(ok && out.find("=> true") != std::string::npos, "has array true");

    // scan()
    out = runCodeFresh("echo scan(123) ~ echo scan(3.14) ~ echo scan('a') ~ echo scan(\"hi\") ~ echo scan(true) ~ echo scan([]) ~ echo scan({}) ~ echo scan(nil) ~", ok);
    TEST_ASSERT(ok && out.find("int") != std::string::npos && out.find("float") != std::string::npos && out.find("char") != std::string::npos && out.find("string") != std::string::npos, "scan types");

    // abs(), floor(), ceil(), sqrt(), clamp()
    out = runCodeFresh("echo abs(-5) ~ echo floor(3.7) ~ echo ceil(3.2) ~ echo sqrt(16) ~ echo clamp(15, 0, 10) ~", ok);
    TEST_ASSERT(ok && out.find("=> 5") != std::string::npos && out.find("=> 3.0") != std::string::npos && out.find("=> 4.0") != std::string::npos && out.find("=> 10") != std::string::npos, "math builtins");
}

static void testNumericAndArithmetic() {
    bool ok = false;
    std::string out;

    // 5 + 6 -> int
    out = runCodeFresh("let x = 5 + 6 ~ echo scan(x) ~ echo x ~", ok);
    TEST_ASSERT(ok && out.find("=> int") != std::string::npos && out.find("=> 11") != std::string::npos, "5 + 6 is int");

    // 10 / 5 -> int
    out = runCodeFresh("let x = 10 / 5 ~ echo scan(x) ~ echo x ~", ok);
    TEST_ASSERT(ok && out.find("=> int") != std::string::npos && out.find("=> 2") != std::string::npos, "10 / 5 is int");

    // 10 / 3 -> float
    out = runCodeFresh("let x = 10 / 3 ~ echo scan(x) ~", ok);
    TEST_ASSERT(ok && out.find("=> float") != std::string::npos, "10 / 3 is float");

    // Any operation involving float -> float
    out = runCodeFresh("let x = 5 + 2.5 ~ echo scan(x) ~", ok);
    TEST_ASSERT(ok && out.find("=> float") != std::string::npos, "int + float is float");

    // % and ^
    out = runCodeFresh("echo 10 % 3 ~ echo 10.5 % 3.0 ~ echo 2 ^ 3 ~ echo 2 ^ -1 ~", ok);
    TEST_ASSERT(ok && out.find("=> 1") != std::string::npos && out.find("=> 1.5") != std::string::npos && out.find("=> 8") != std::string::npos && out.find("=> 0.5") != std::string::npos, "% and ^ rules");

    // Numeric equality: 5 == 5.000 is true
    out = runCodeFresh("echo (5 == 5.000) ~", ok);
    TEST_ASSERT(ok && out.find("=> true") != std::string::npos, "5 == 5.000 is true");

    // 64-bit integer overflow error
    out = runCodeFresh("let a = 9223372036854775807 ~ let b = a + 1 ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos && out.find("overflow") != std::string::npos, "integer overflow error");
}

static void testArrayMethods() {
    bool ok = false;
    std::string out;

    // push() with multiple values & spread syntax
    out = runCodeFresh("let nums = [1] ~ nums.push(2, 3) ~ let other = [4, 5] ~ nums.push(other...) ~ echo nums ~", ok);
    TEST_ASSERT(ok && out.find("[1, 2, 3, 4, 5]") != std::string::npos, "array push multiple and spread");

    // pop() default and indexed with negative indices
    out = runCodeFresh("let nums = [10, 20, 30, 40] ~ echo nums.pop() ~ echo nums.pop(ind=0) ~ echo nums.pop(ind=-1) ~ echo nums ~", ok);
    TEST_ASSERT(ok && out.find("=> 40") != std::string::npos && out.find("=> 10") != std::string::npos && out.find("=> 30") != std::string::npos && out.find("[20]") != std::string::npos, "array pop default & indexed");

    // insert()
    out = runCodeFresh("let nums = [1, 3] ~ nums.insert(ind=1, val=2) ~ nums.insert(ind=-1, val=4) ~ echo nums ~", ok);
    TEST_ASSERT(ok && out.find("[1, 2, 3, 4]") != std::string::npos, "array insert ind and negative -1 append");

    // remove() mutually exclusive ind and val
    out = runCodeFresh("let nums = [10, 20, 30] ~ echo nums.remove(ind=1) ~ echo nums.remove(val=30) ~ echo nums ~", ok);
    TEST_ASSERT(ok && out.find("=> 20") != std::string::npos && out.find("=> 30") != std::string::npos && out.find("[10]") != std::string::npos, "array remove ind & val modes");

    out = runCodeFresh("let nums = [10] ~ nums.remove(ind=0, val=10) ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos, "array remove both ind and val error");

    // contains() and index_of() value-based numeric equality
    out = runCodeFresh("let nums = [1, 5.0, \"a\"] ~ echo nums.contains(5) ~ echo nums.index_of(5) ~ echo nums.index_of(99) ~", ok);
    TEST_ASSERT(ok && out.find("=> true") != std::string::npos && out.find("=> 1") != std::string::npos && out.find("=> nil") != std::string::npos, "contains and index_of numeric equality");

    // reverse() & clear() & .length
    out = runCodeFresh("let nums = [1, 2, 3] ~ echo nums.length ~ nums.reverse() ~ echo nums ~ echo nums.clear() ~ echo nums.length ~", ok);
    TEST_ASSERT(ok && out.find("=> 3") != std::string::npos && out.find("[3, 2, 1]") != std::string::npos && out.find("[]") != std::string::npos && out.find("=> 0") != std::string::npos, "reverse clear length");

    // sort() comparable values and incomparable error
    out = runCodeFresh("let nums = [3, 1.5, 2] ~ nums.sort() ~ echo nums ~", ok);
    TEST_ASSERT(ok && out.find("[1.5, 2, 3]") != std::string::npos, "sort mixed numeric");

    out = runCodeFresh("let arr = [1, \"a\"] ~ arr.sort() ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos && out.find("non-comparable") != std::string::npos, "sort non-comparable error");

    // slicing syntax & method
    out = runCodeFresh("let nums = [0, 1, 2, 3, 4] ~ echo nums[1:4] ~ echo nums[::-1] ~ echo nums.slice(start=1, end=3) ~", ok);
    TEST_ASSERT(ok && out.find("[1, 2, 3]") != std::string::npos && out.find("[4, 3, 2, 1, 0]") != std::string::npos && out.find("[1, 2]") != std::string::npos, "array slicing syntax and method");

    out = runCodeFresh("let nums = [1, 2] ~ echo nums[::0] ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos && out.find("step cannot be zero") != std::string::npos, "slice step 0 error");

    // join()
    out = runCodeFresh("let arr = [1, \"a\", true] ~ echo arr.join(\"-\") ~ echo [].join(\",\") ~", ok);
    TEST_ASSERT(ok && out.find("=> 1-a-true") != std::string::npos, "join method");
}

static void testMapMethods() {
    bool ok = false;
    std::string out;

    // put(), get(), contains(), remove(), keys(), values(), clear(), .length
    out = runCodeFresh("let data = {} ~ data.put(key=\"name\", val=\"BBZ\") ~ data[\"age\"] = 25 ~ echo data.length ~ echo data.get(\"name\") ~ echo data.contains(\"age\") ~", ok);
    TEST_ASSERT(ok && out.find("=> 2") != std::string::npos && out.find("=> BBZ") != std::string::npos && out.find("=> true") != std::string::npos, "map put get contains length");

    // Updating existing key moves key to end of insertion order
    out = runCodeFresh("let m = {} ~ m.put(\"a\", 1) ~ m.put(\"b\", 2) ~ m.put(\"a\", 3) ~ echo m.keys() ~ echo m.values() ~", ok);
    TEST_ASSERT(ok && out.find("[\"b\", \"a\"]") != std::string::npos && out.find("[2, 3]") != std::string::npos, "map insertion order re-insertion");

    // remove() returns removed value or nil
    out = runCodeFresh("let m = {\"a\": 10} ~ echo m.remove(\"a\") ~ echo m.remove(\"missing\") ~", ok);
    TEST_ASSERT(ok && out.find("=> 10") != std::string::npos && out.find("=> nil") != std::string::npos, "map remove return value");

    // Scalar key types (int, float, string, char, bool) & numeric key equality (1 == 1.0)
    out = runCodeFresh("let m = {} ~ m[1] = \"one\" ~ m[1.0] = \"float_one\" ~ m['c'] = \"char_c\" ~ m[true] = \"bool_true\" ~ echo m.length ~ echo m[1] ~", ok);
    TEST_ASSERT(ok && out.find("=> 3") != std::string::npos && out.find("=> float_one") != std::string::npos, "scalar key types & numeric equality 1 == 1.0");

    // Rejection of invalid key types (array, map, func)
    out = runCodeFresh("let m = {} ~ m[[1, 2]] = 10 ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos && out.find("supported scalar type") != std::string::npos, "reject array key in map");

    // Typed maps
    out = runCodeFresh("let map<string, int> tm = {\"a\": 1} ~ tm.put(key=\"b\", val=\"bad\") ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos, "typed map value violation");
}

static void testHigherOrderCollectionOperations() {
    bool ok = false;
    std::string out;

    // Array map(), filter(), reduce()
    out = runCodeFresh("let nums = [1, 2, 3, 4] ~ echo nums.map(task(x) { give x * 2 ~ }) ~", ok);
    TEST_ASSERT(ok && out.find("[2, 4, 6, 8]") != std::string::npos, "array map");

    out = runCodeFresh("let nums = [1, 2, 3, 4] ~ echo nums.filter(task(x) { give x % 2 == 0 ~ }) ~", ok);
    TEST_ASSERT(ok && out.find("[2, 4]") != std::string::npos, "array filter");

    out = runCodeFresh("let nums = [1, 2, 3, 4] ~ echo nums.reduce(task(acc, x) { give acc + x ~ }, 10) ~ echo nums.reduce(task(acc, x) { give acc + x ~ }) ~", ok);
    TEST_ASSERT(ok && out.find("=> 20") != std::string::npos && out.find("=> 10") != std::string::npos, "array reduce with and without initial");

    // Empty reduce fallback
    out = runCodeFresh("echo [].reduce(task(a, b) { give a + b ~ }) ~ echo [].reduce(task(a, b) { give a + b ~ }, 100) ~", ok);
    TEST_ASSERT(ok && out.find("=> nil") != std::string::npos && out.find("=> 100") != std::string::npos, "empty array reduce fallback");

    // Map map(), filter(), reduce()
    out = runCodeFresh("let m = {\"a\": 1, \"b\": 2} ~ echo m.map(task(val, key) { give val * 10 ~ }) ~", ok);
    TEST_ASSERT(ok && out.find("{\"a\": 10, \"b\": 20}") != std::string::npos, "map map()");

    out = runCodeFresh("let m = {\"a\": 1, \"b\": 2} ~ echo m.filter(task(val, key) { give val > 1 ~ }) ~", ok);
    TEST_ASSERT(ok && out.find("{\"b\": 2}") != std::string::npos, "map filter()");

    // Callback parameter count validation (error if callback declares too many parameters)
    out = runCodeFresh("let nums = [1] ~ nums.map(task(a, b, c, d) { give a ~ }) ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos && out.find("more parameters") != std::string::npos, "callback declared too many parameters error");

    // Structural mutation guard during iteration error
    out = runCodeFresh("let nums = [1, 2] ~ nums.map(task(x) { nums.push(99) ~ give x ~ }) ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos && out.find("Cannot structurally mutate") != std::string::npos, "structural mutation guard error");
}

static void testNamedArguments() {
    bool ok = false;
    std::string out;

    // Positional + named call
    out = runCodeFresh("task greet(greeting, name) { give greeting + \" \" + name ~ } echo greet(name=\"BBZ\", greeting=\"Hello\") ~", ok);
    TEST_ASSERT(ok && out.find("Hello BBZ") != std::string::npos, "task named arguments");

    // Unexpected argument name error
    out = runCodeFresh("task foo(a) { give a ~ } foo(bad=1) ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos && out.find("has no parameter named 'bad'") != std::string::npos, "unexpected argument name error");

    // Duplicate argument error
    out = runCodeFresh("task foo(a) { give a ~ } foo(10, a=20) ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos && out.find("duplicate argument 'a'") != std::string::npos, "duplicate argument error");

    // Missing argument error
    out = runCodeFresh("task foo(a, b) { give a + b ~ } foo(a=1) ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos && out.find("missing required argument 'b'") != std::string::npos, "missing argument error");
}

static void testStringMethodsAndUnicode() {
    bool ok = false;
    std::string out;

    // Unicode indexing, slicing, .length, split("")
    out = runCodeFresh("let s = \"👋 world\" ~ echo scan(s[0]) ~ echo s.length ~ echo s[0:2] ~ echo s.split(\"\") ~", ok);
    TEST_ASSERT(ok && out.find("=> char") != std::string::npos && out.find("=> 7") != std::string::npos && out.find("👋 ") != std::string::npos, "unicode indexing slicing length split");

    // String methods: upper, lower, trim, contains, starts_with, ends_with, split, replace
    out = runCodeFresh("let s = \"  Hello World  \" ~ echo s.trim().upper() ~ echo s.trim().lower() ~", ok);
    TEST_ASSERT(ok && out.find("HELLO WORLD") != std::string::npos && out.find("hello world") != std::string::npos, "string upper lower trim methods");

    out = runCodeFresh("let text = \"a,b,,c\" ~ echo text.split(\",\") ~ echo \"a b c\".split() ~", ok);
    TEST_ASSERT(ok && out.find("[\"a\", \"b\", \"\", \"c\"]") != std::string::npos && out.find("[\"a\", \"b\", \"c\"]") != std::string::npos, "string split modes");

    // Replace modes
    // Value mode replaces ONLY first occurrence
    out = runCodeFresh("echo \"a b a b\".replace(val=\"a\", replacement=\"X\") ~", ok);
    TEST_ASSERT(ok && out.find("X b a b") != std::string::npos, "replace value mode first match only");

    // Empty val error
    out = runCodeFresh("\"hello\".replace(val=\"\", replacement=\"X\") ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos && out.find("val cannot be empty") != std::string::npos, "replace empty val error");

    // Index mode
    out = runCodeFresh("echo \"hello world\".replace(ind=5, replacement=\" Kekno\") ~", ok);
    TEST_ASSERT(ok && out.find("hello Kekno") != std::string::npos, "replace index mode");

    // Slice range mode
    out = runCodeFresh("echo \"hello world\".replace(ind=1:5, replacement=\"i\") ~", ok);
    TEST_ASSERT(ok && out.find("hi world") != std::string::npos, "replace slice range mode");
}

static void testRemovedLegacyAPIsAndCasts() {
    bool ok = false;
    std::string out;

    // Check that legacy APIs are removed and throw undefined error
    out = runCodeFresh("purge({}, \"a\") ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos && out.find("Undefined variable 'purge'") != std::string::npos, "removed purge API");

    out = runCodeFresh("inject([], 1) ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos && out.find("Undefined variable 'inject'") != std::string::npos, "removed inject API");

    out = runCodeFresh("expel([1]) ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos && out.find("Undefined variable 'expel'") != std::string::npos, "removed expel API");

    out = runCodeFresh("cast_num(\"123\") ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos && out.find("Undefined variable 'cast_num'") != std::string::npos, "removed cast_num API");

    out = runCodeFresh("cast_str(123) ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos && out.find("Undefined variable 'cast_str'") != std::string::npos, "removed cast_str API");

    out = runCodeFresh("upper(\"abc\") ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos && out.find("Undefined variable 'upper'") != std::string::npos, "removed global upper API");

    // Explicit casts
    out = runCodeFresh("echo cast_int(3.7) ~ echo cast_float(5) ~ echo cast_string(true) ~ echo cast_char(\"Z\") ~ echo cast_array(\"ab\") ~ echo cast_map({}) ~", ok);
    TEST_ASSERT(ok && out.find("=> 4") != std::string::npos && out.find("=> 5.0") != std::string::npos && out.find("=> true") != std::string::npos && out.find("=> Z") != std::string::npos && out.find("['a', 'b']") != std::string::npos, "explicit cast functions");
}

static void testControlFlowForElseIfAndConst() {
    bool ok = false;
    std::string out;

    // Native for loop
    out = runCodeFresh("let sum = 0 ~ for (let i = 1 ~ i <= 5 ~ i += 1) { sum += i ~ } echo sum ~", ok);
    TEST_ASSERT(ok && out.find("=> 15") != std::string::npos, "for loop sum");

    // else if chain
    out = runCodeFresh("let x = 2 ~ if (x == 1) { echo \"one\" ~ } else if (x == 2) { echo \"two\" ~ } else { echo \"other\" ~ }", ok);
    TEST_ASSERT(ok && out.find("=> two") != std::string::npos, "else if chain");

    // const variable immutability
    out = runCodeFresh("const int maxVal = 100 ~ maxVal = 200 ~", ok);
    TEST_ASSERT(!ok && out.find("[Compiler Error]") != std::string::npos && out.find("Cannot reassign constant") != std::string::npos, "const immutability error");
}

static void testCompoundAssignments() {
    bool ok = false;
    std::string out;

    out = runCodeFresh("let x = 10 ~ x += 5 ~ x -= 2 ~ x *= 3 ~ x /= 2 ~ x %= 5 ~ echo x ~", ok);
    TEST_ASSERT(ok && out.find("=> 4") != std::string::npos, "compound assignment operators");
}

static void testTypedDeclarationsAndParameters() {
    bool ok = false;
    std::string out;

    // Explicit typed collections syntax support
    out = runCodeFresh("let array<int> nums = [1, 2, 3] ~ echo nums ~", ok);
    TEST_ASSERT(ok && out.find("[1, 2, 3]") != std::string::npos, "array<int> syntax");

    out = runCodeFresh("let map<string, int> m = {\"a\": 1} ~ echo m ~", ok);
    TEST_ASSERT(ok && out.find("{\"a\": 1}") != std::string::npos, "map<string, int> syntax");

    // Typed task parameters validation & implicit int->float coercion
    out = runCodeFresh("task addFloats(float a, float b) { give a + b ~ } echo addFloats(10, 20) ~", ok);
    TEST_ASSERT(ok && out.find("=> 30.0") != std::string::npos, "task float params with int coercion");

    // Typed parameter type mismatch error
    out = runCodeFresh("task addInts(int a, int b) { give a + b ~ } addInts(\"hello\", 5) ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos && out.find("expects type int") != std::string::npos, "task int param type mismatch");
}

static void testTasksAndRecursion() {
    bool ok = false;
    std::string out;

    // Recursion
    out = runCodeFresh("task fib(n) { if (n <= 1) { give n ~ } give fib(n - 1) + fib(n - 2) ~ } echo fib(7) ~", ok);
    TEST_ASSERT(ok && out.find("=> 13") != std::string::npos, "task recursion");

    // Implicit nil return
    out = runCodeFresh("task noReturn() { let x = 10 ~ } echo noReturn() ~", ok);
    TEST_ASSERT(ok && out.find("=> nil") != std::string::npos, "task implicit nil return");
}

static void testHaltSkipScopeCleanup() {
    bool ok = false;
    std::string out;

    // Halt inside nested block popping locals
    out = runCodeFresh("let sum = 0 ~ for (let i = 0 ~ i < 5 ~ i += 1) { { let localA = 10 ~ if (i == 2) { halt ~ } } sum += i ~ } echo sum ~", ok);
    TEST_ASSERT(ok && out.find("=> 1") != std::string::npos, "halt scope cleanup");

    // Skip inside nested block popping locals
    out = runCodeFresh("let sum = 0 ~ for (let i = 0 ~ i < 5 ~ i += 1) { { let localA = 10 ~ if (i == 2) { skip ~ } } sum += i ~ } echo sum ~", ok);
    TEST_ASSERT(ok && out.find("=> 8") != std::string::npos, "skip scope cleanup");
}

static void testMalformedNumericLiterals() {
    bool ok = false;
    std::string out;

    out = runCodeFresh("let x = . ~", ok);
    TEST_ASSERT(!ok && out.find("Syntax Error") != std::string::npos, "malformed numeric dot");

    out = runCodeFresh("let x = 1.2.3 ~", ok);
    TEST_ASSERT(!ok && out.find("Syntax Error") != std::string::npos, "malformed numeric multi dot");

    out = runCodeFresh("let x = 1..2 ~", ok);
    TEST_ASSERT(!ok && out.find("Syntax Error") != std::string::npos, "malformed numeric double dot");
}

static void testArrayIndexing() {
    bool ok = false;
    std::string out;

    out = runCodeFresh("let arr = [10, 20] ~ echo arr[1.5] ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos, "array float index runtime error");

    out = runCodeFresh("let arr = [10, 20] ~ echo arr[5] ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos && out.find("out of bounds") != std::string::npos, "array index OOB");
}

static void testDiagnostics() {
    bool ok = false;
    std::string out;

    out = runCodeFresh("let x = (1 + 2} ~", ok);
    TEST_ASSERT(!ok && out.find("line 1") != std::string::npos && out.find("^") != std::string::npos, "diagnostics formatting");
}

static void test16BitOperandLimits() {
    Chunk chunk;
    Value v(static_cast<int64_t>(10));
    try {
        for (int i = 0; i < 65536; i++) {
            chunk.addConstant(v);
        }
        bool threw = false;
        try {
            chunk.addConstant(v);
        } catch (const std::runtime_error& err) {
            threw = true;
            std::string msg = err.what();
            TEST_ASSERT(msg.find("Constant pool limit exceeded") != std::string::npos, "16-bit constant limit exception message");
        }
        TEST_ASSERT(threw, "16-bit constant pool limit rejection");
    } catch (...) {
        TEST_ASSERT(false, "unexpected exception in 16-bit constant limit test");
    }
}

static void testGrabAndVMState() {
    bool ok = false;
    std::string out;

    out = runCodeFresh("grab non_existent_mod_12345 ~", ok);
    TEST_ASSERT(ok && out.find("[Module Error]") != std::string::npos && out.find("Could not find module 'non_existent_mod_12345'") != std::string::npos, "grab missing module diagnostic");

    {
        std::ofstream validFile("temp_valid.kek");
        validFile << "pub task addTwo(a, b) { give a + b ~ } pub let exportedVal = 42 ~";
        validFile.close();
    }
    VM vm;
    out = runCode(vm, "grab temp_valid ~ echo temp_valid.addTwo(10, 20) ~ echo temp_valid.exportedVal ~", ok);
    TEST_ASSERT(ok && out.find("=> 30") != std::string::npos && out.find("=> 42") != std::string::npos, "valid grab module import");
    std::remove("temp_valid.kek");
}

static void testV046Patches() {
    bool ok = false;
    std::string out;

    out = runCodeFresh("task testArr(array<int> nums) { echo nums ~ } testArr([1, 2, 3]) ~", ok);
    TEST_ASSERT(ok && out.find("[1, 2, 3]") != std::string::npos, "typed task param valid array<int>");

    out = runCodeFresh("task testArr(array<int> nums) { echo nums ~ } testArr([1, \"hello\"]) ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos, "typed task param invalid array<int>");

    out = runCodeFresh("task testMap(map<string, int> data) { echo data ~ } testMap({\"a\": 10}) ~", ok);
    TEST_ASSERT(ok && out.find("{\"a\": 10}") != std::string::npos, "typed task param valid map<string, int>");

    out = runCodeFresh("task testMap(map<string, int> data) { echo data ~ } testMap({\"a\": \"hello\"}) ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos, "typed task param invalid map<string, int>");

    out = runCodeFresh("task testNested(array<array<int>> matrix) { echo matrix ~ } testNested([[1, 2], [3, 4]]) ~", ok);
    TEST_ASSERT(ok && out.find("[[1, 2], [3, 4]]") != std::string::npos, "typed task param valid nested collection");

    out = runCodeFresh("task testNested(array<array<int>> matrix) { echo matrix ~ } testNested([[1, 2], [\"hello\"]]) ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos, "typed task param invalid nested collection");

    out = runCodeFresh("task testTaskLocal() { let int x = 10 ~ x = \"hello\" ~ } testTaskLocal() ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos, "task local variable reassignment mismatch error");

    out = runCodeFresh("let array<array<int>> grid = [[1, 2], [3, 4]] ~ grid[0] = [\"hello\"] ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos, "nested collection index assignment mismatch error");

    out = runCodeFresh("let int x = 10 ~ x = \"hello\" ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos, "typed int reassignment mismatch error");

    out = runCodeFresh("let int x = 3.5 ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos, "typed int init float mismatch error");

    out = runCodeFresh("let array<int> nums = [1, 2, 3] ~ echo nums ~", ok);
    TEST_ASSERT(ok && out.find("[1, 2, 3]") != std::string::npos, "valid typed array<int>");

    out = runCodeFresh("let array<int> nums = [1, \"hello\"] ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos, "typed array<int> content mismatch");

    out = runCodeFresh("let array<int> nums = [1, 2, 3] ~ nums[0] = \"hello\" ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos, "typed array<int> index assignment mismatch");

    out = runCodeFresh("let int x = 10 ~ x += 2.5 ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos, "typed int compound addition mismatch");

    out = runCodeFresh("let int g = 10 ~ g = \"hello\" ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos, "global int reassignment mismatch");

    out = runCodeFresh("let x = 5 ~ x = \"hello\" ~ x = true ~ echo x ~", ok);
    TEST_ASSERT(ok && out.find("=> true") != std::string::npos, "dynamic variable retains dynamic reassignments");

    out = runCodeFresh("let float f = 10 ~ echo f ~", ok);
    TEST_ASSERT(ok && out.find("=> 10.0") != std::string::npos, "float variable accepts int via implicit coercion");

    out = runCodeFresh("echo 'A' ~", ok);
    TEST_ASSERT(ok && out.find("=> A") != std::string::npos, "normal char literal echo");

    out = runCodeFresh("echo '\\z' ~", ok);
    TEST_ASSERT(!ok && out.find("Syntax Error") != std::string::npos, "malformed char literal gives standard syntax error");

    std::string codeWithConsts = "";
    for (int i = 0; i < 65537; i++) {
        codeWithConsts += "let x" + std::to_string(i) + " = " + std::to_string(i) + " ~\n";
    }
    out = runCodeFresh(codeWithConsts, ok);
    TEST_ASSERT(!ok && out.find("Compiler Error") != std::string::npos && out.find("Constant pool limit exceeded") != std::string::npos, "compiler constant pool overflow caught gracefully");

    out = runCodeFresh("echo 2 ^ 63 ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos && out.find("overflow") != std::string::npos, "2^63 power overflow error");

    out = runCodeFresh("echo 10 ^ 20 ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos && out.find("overflow") != std::string::npos, "10^20 power overflow error");

    out = runCodeFresh("echo 2 ^ 3 ~", ok);
    TEST_ASSERT(ok && out.find("=> 8") != std::string::npos, "2^3 integer power valid");

    out = runCodeFresh("echo 0 ^ -1 ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos, "0^-1 runtime error");

    out = runCodeFresh("echo cast_int(1000000000000000000000000000000000000000000.0) ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos && out.find("out of 64-bit integer range") != std::string::npos, "cast_int overflow error");

    out = runCodeFresh("echo cast_int(3.4) ~ echo cast_int(3.5) ~", ok);
    TEST_ASSERT(ok && out.find("=> 3") != std::string::npos && out.find("=> 4") != std::string::npos, "cast_int rounding preserved");

    out = runCodeFresh("let minVal = -9223372036854775807 - 1 ~ echo minVal / -1 ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos, "INT64_MIN / -1 runtime error");

    out = runCodeFresh("let minVal = -9223372036854775807 - 1 ~ echo minVal % -1 ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos, "INT64_MIN % -1 runtime error");
}

static void testV050Features() {
    bool ok = false;
    std::string out;

    std::string funcCode =
        "task add(int a, int b) {\n"
        "    give a + b~\n"
        "}\n"
        "task run(func f, int x, int y) {\n"
        "    give f(x, y)~\n"
        "}\n"
        "let f = add~\n"
        "echo run(f, 10, 20)~\n";
    out = runCodeFresh(funcCode, ok);
    TEST_ASSERT(ok && out.find("=> 30") != std::string::npos, "func parameter and call");

    out = runCodeFresh("task run(func f) { give f()~ } run(123)~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos && out.find("expects type func") != std::string::npos, "func param type mismatch error");

    out = runCodeFresh("let x = 100~ x()~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos && out.find("Can only call task values") != std::string::npos, "call non-task error");

    std::string adderCode =
        "task makeAdder(int x) {\n"
        "    task add(int y) {\n"
        "        give x + y~\n"
        "    }\n"
        "    give add~\n"
        "}\n"
        "let add5 = makeAdder(5)~\n"
        "echo add5(10)~\n";
    out = runCodeFresh(adderCode, ok);
    TEST_ASSERT(ok && out.find("=> 15") != std::string::npos, "closure outliving parent function frame");

    std::string indepCode =
        "task makeAdder(int x) {\n"
        "    task add(int y) {\n"
        "        give x + y~\n"
        "    }\n"
        "    give add~\n"
        "}\n"
        "let a = makeAdder(5)~\n"
        "let b = makeAdder(20)~\n"
        "echo a(1)~\n"
        "echo b(1)~\n";
    out = runCodeFresh(indepCode, ok);
    TEST_ASSERT(ok && out.find("=> 6") != std::string::npos && out.find("=> 21") != std::string::npos, "multiple independent closures");

    std::string counterCode =
        "task makeCounter() {\n"
        "    let count = 0~\n"
        "    task inc() {\n"
        "        count++~\n"
        "        give count~\n"
        "    }\n"
        "    give inc~\n"
        "}\n"
        "let c = makeCounter()~\n"
        "echo c()~\n"
        "echo c()~\n"
        "echo c()~\n";
    out = runCodeFresh(counterCode, ok);
    TEST_ASSERT(ok && out.find("=> 1") != std::string::npos && out.find("=> 2") != std::string::npos && out.find("=> 3") != std::string::npos, "captured variable mutation");

    std::string sharedUpvalueCode =
        "task makePair() {\n"
        "    let x = 10~\n"
        "    task get() { give x~ }\n"
        "    task set(v) { x = v~ }\n"
        "    give [get, set]~\n"
        "}\n"
        "let pair = makePair()~\n"
        "let getter = pair[0]~\n"
        "let setter = pair[1]~\n"
        "echo getter()~\n"
        "setter(42)~\n"
        "echo getter()~\n";
    out = runCodeFresh(sharedUpvalueCode, ok);
    TEST_ASSERT(ok && out.find("=> 10") != std::string::npos && out.find("=> 42") != std::string::npos, "shared upvalue mutation across closures");

    std::string nestedClosureCode =
        "task level1(a) {\n"
        "    task level2(b) {\n"
        "        task level3(c) {\n"
        "            give a + b + c~\n"
        "        }\n"
        "        give level3~\n"
        "    }\n"
        "    give level2~\n"
        "}\n"
        "echo level1(10)(20)(30)~\n";
    out = runCodeFresh(nestedClosureCode, ok);
    TEST_ASSERT(ok && out.find("=> 60") != std::string::npos, "3 level nested closures");

    std::string nestedRecCode =
        "task outer() {\n"
        "    task inner(int n) {\n"
        "        if (n <= 0) {\n"
        "            give 0~\n"
        "        }\n"
        "        give inner(n - 1)~\n"
        "    }\n"
        "    give inner(3)~\n"
        "}\n"
        "echo outer()~\n";
    out = runCodeFresh(nestedRecCode, ok);
    TEST_ASSERT(ok && out.find("=> 0") != std::string::npos, "nested function recursion");

    out = runCodeFresh("let x = 5~ echo x++~ echo x~", ok);
    TEST_ASSERT(ok && out.find("=> 5") != std::string::npos && out.find("=> 6") != std::string::npos, "postfix ++ returns old value and increments");

    out = runCodeFresh("let x = 5~ echo ++x~ echo x~", ok);
    TEST_ASSERT(ok && out.find("=> 6") != std::string::npos, "prefix ++ returns new value and increments");

    out = runCodeFresh("let x = 5~ echo x--~ echo x~", ok);
    TEST_ASSERT(ok && out.find("=> 5") != std::string::npos && out.find("=> 4") != std::string::npos, "postfix -- returns old value and decrements");

    out = runCodeFresh("let x = 5~ echo --x~ echo x~", ok);
    TEST_ASSERT(ok && out.find("=> 4") != std::string::npos, "prefix -- returns new value and decrements");

    out = runCodeFresh("let float f = 2.5~ f++~ echo f~", ok);
    TEST_ASSERT(ok && out.find("=> 3.5") != std::string::npos, "float ++");

    out = runCodeFresh("let arr = [10, 20]~ echo arr[0]++~ echo arr[0]~", ok);
    TEST_ASSERT(ok && out.find("=> 10") != std::string::npos && out.find("=> 11") != std::string::npos, "array element postfix ++");

    out = runCodeFresh("let arr = [10, 20]~ echo ++arr[1]~ echo arr[1]~", ok);
    TEST_ASSERT(ok && out.find("=> 21") != std::string::npos, "array element prefix ++");

    out = runCodeFresh("let m = {\"a\": 5}~ m[\"a\"]++~ echo m[\"a\"]~", ok);
    TEST_ASSERT(ok && out.find("=> 6") != std::string::npos, "map element postfix ++");

    out = runCodeFresh("const int c = 10~ c++~", ok);
    TEST_ASSERT(!ok && out.find("Cannot reassign constant") != std::string::npos, "const increment compiler error");

    out = runCodeFresh("let s = \"hello\"~ s++~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos && out.find("'++' operand must be a number") != std::string::npos, "non-numeric ++ runtime error");

    out = runCodeFresh("let int maxVal = 9223372036854775807~ maxVal++~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos && out.find("overflow") != std::string::npos, "integer overflow ++ runtime error");
}

static void testV052Modules() {
    bool ok = false;
    std::string out;

    fs::create_directories("test_mods/lib");

    {
        std::ofstream f("test_mods/math.kek");
        f << "echo \"math init\"~\n"
          << "pub let pi = 3.14~\n"
          << "pub const int MAX = 100~\n"
          << "priv let secret = 42~\n"
          << "pub task sqrt(x) { give x~\n }\n"
          << "priv task helper() { give secret~\n }\n"
          << "let counter = 0~\n"
          << "pub task incState() { counter++~ give counter~\n }\n"
          << "pub task getState() { give counter~\n }\n";
        f.close();
    }

    {
        std::ofstream f("test_mods/lib/num.kek");
        f << "grab helper as h~\n"
          << "pub task five() { give 5~\n }\n"
          << "pub task getValue() { give h.val~\n }\n";
        f.close();
    }

    {
        std::ofstream f("test_mods/lib/helper.kek");
        f << "pub let val = 42~\n";
        f.close();
    }

    {
        std::ofstream f1("test_mods/circ_a.kek");
        f1 << "grab circ_b~\n";
        f1.close();
        std::ofstream f2("test_mods/circ_b.kek");
        f2 << "grab circ_a~\n";
        f2.close();
    }

    {
        std::ofstream f("test_mods/mod_data.kek");
        f << "pub let arr = [1, 2, 3]~\n"
          << "pub let m = {\"a\": 10}~\n"
          << "pub const int LIMIT = 50~\n";
        f.close();
    }

    std::string mainCode1 =
        "grab math~\n"
        "grab math as m~\n"
        "echo math.pi~\n"
        "echo m.MAX~\n"
        "echo math.sqrt(9)~\n"
        "math.incState()~\n"
        "echo m.getState()~\n";

    out = runCodeFresh(mainCode1, ok, "test_mods/main.kek");
    TEST_ASSERT(ok && out.find("math init") != std::string::npos, "module top-level init executed");
    size_t pos1 = out.find("math init");
    size_t pos2 = (pos1 != std::string::npos) ? out.find("math init", pos1 + 1) : std::string::npos;
    TEST_ASSERT(pos1 != std::string::npos && pos2 == std::string::npos, "module initialized exactly once");
    TEST_ASSERT(out.find("=> 3.14") != std::string::npos, "pub let member access");
    TEST_ASSERT(out.find("=> 100") != std::string::npos, "pub const member access via alias");
    TEST_ASSERT(out.find("=> 9") != std::string::npos, "pub task call member access");
    TEST_ASSERT(out.find("=> 1") != std::string::npos, "aliased module shares state");

    out = runCodeFresh("grab math~\n echo math.secret~\n", ok, "test_mods/main.kek");
    TEST_ASSERT(ok && out.find("[Module Error]") != std::string::npos && out.find("secret' is private in module 'math'") != std::string::npos, "private variable access rejected");

    out = runCodeFresh("grab math~\n math.helper()~\n", ok, "test_mods/main.kek");
    TEST_ASSERT(ok && out.find("[Module Error]") != std::string::npos && out.find("helper' is private in module 'math'") != std::string::npos, "private task access rejected");

    out = runCodeFresh("grab math~\n echo math.unknown~\n", ok, "test_mods/main.kek");
    TEST_ASSERT(ok && out.find("[Member Error]") != std::string::npos && out.find("Member 'unknown' does not exist") != std::string::npos, "nonexistent member error");

    out = runCodeFresh("grab lib.num as n~\n echo n.five()~\n echo n.getValue()~\n", ok, "test_mods/main.kek");
    TEST_ASSERT(ok && out.find("=> 5") != std::string::npos && out.find("=> 42") != std::string::npos, "relative module dependency resolution");

    out = runCodeFresh("grab lib.num~\n echo lib.num.five()~\n", ok, "test_mods/main.kek");
    TEST_ASSERT(ok && out.find("=> 5") != std::string::npos, "nested path import without alias allows lib.num.five()");

    out = runCodeFresh("grab circ_a~\n", ok, "test_mods/main.kek");
    TEST_ASSERT(ok && out.find("[Module Error]") != std::string::npos && out.find("Circular module dependency detected") != std::string::npos && out.find("circ_a -> circ_b -> circ_a") != std::string::npos, "circular dependency detection");

    out = runCodeFresh("grab math as m~\n m = 123~\n", ok, "test_mods/main.kek");
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos && out.find("Cannot reassign module alias 'm'") != std::string::npos, "module alias immutability");

    out = runCodeFresh("task foo() { pub let x = 10~ }\n", ok);
    TEST_ASSERT(!ok && out.find("[Compiler Error]") != std::string::npos, "pub on local variable rejected at compile time");

    out = runCodeFresh("task foo(pub int x) {}\n", ok);
    TEST_ASSERT(!ok && out.find("[Compiler Error]") != std::string::npos, "pub on parameter rejected at compile time");

    out = runCodeFresh("task foo() { grab math~ }\n", ok);
    TEST_ASSERT(!ok && out.find("[Compiler Error]") != std::string::npos && out.find("'grab' is allowed only at top-level module scope") != std::string::npos, "grab inside task rejected");

    out = runCodeFresh("if (true) { grab math~ }\n", ok);
    TEST_ASSERT(!ok && out.find("[Compiler Error]") != std::string::npos, "grab inside block/conditional rejected");

    std::string dataCode =
        "grab mod_data as d~\n"
        "d.arr[0] = 99~\n"
        "echo d.arr[0]~\n"
        "d.m[\"a\"] = 100~\n"
        "echo d.m[\"a\"]~\n"
        "d.LIMIT = 200~\n";
    out = runCodeFresh(dataCode, ok, "test_mods/main.kek");
    TEST_ASSERT(ok && out.find("=> 99") != std::string::npos && out.find("=> 100") != std::string::npos && out.find("Cannot reassign constant variable 'LIMIT'") != std::string::npos, "public mutable collections and pub const enforcement");

    fs::remove_all("test_mods");
}

void testV056RegressionSuite() {
    bool ok = false;
    std::string out;

    // 1. Typed map key and value rejections
    out = runCodeFresh("let map<string, int> data = {} ~ data[123] = 10 ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos && out.find("Type mismatch for map key assignment") != std::string::npos, "typed map key rejection on direct indexing");

    out = runCodeFresh("let map<string, int> data = {} ~ data[\"x\"] = \"wrong\" ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos && out.find("Type mismatch for map assignment") != std::string::npos, "typed map value rejection on direct indexing");

    out = runCodeFresh("let map<string, int> data = {} ~ data.put(key=123, val=10) ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos && out.find("Map put key type mismatch") != std::string::npos, "typed map key rejection on put()");

    out = runCodeFresh("let map<string, int> data = {} ~ data.put(key=\"x\", val=\"wrong\") ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos && out.find("Map put value type mismatch") != std::string::npos, "typed map value rejection on put()");

    out = runCodeFresh("let map<string, int> data = {123: 10} ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos, "typed map initialization key mismatch rejection");

    // 2. Typed array map() and typed map map() result validation
    out = runCodeFresh("let array<int> nums = [1, 2, 3] ~ task bad(x) { give \"wrong\" ~ } nums.map(bad) ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos && out.find("incompatible with array element type") != std::string::npos, "typed array map() callback return type validation");

    out = runCodeFresh("let map<string, int> m = {\"a\": 1} ~ task bad(v, k) { give \"wrong\" ~ } m.map(bad) ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos && out.find("incompatible with map value type") != std::string::npos, "typed map map() callback return type validation");

    out = runCodeFresh("let nums = [1, 2] ~ task bad(x) { give \"str\" ~ } echo nums.map(bad) ~", ok);
    TEST_ASSERT(ok && out.find("[\"str\", \"str\"]") != std::string::npos, "dynamic array map() allows type transformation");

    out = runCodeFresh("let m = {\"a\": 1} ~ task bad(v, k) { give \"str\" ~ } echo m.map(bad) ~", ok);
    TEST_ASSERT(ok && out.find("{\"a\": \"str\"}") != std::string::npos, "dynamic map map() allows value type transformation");

    // 3. Named arguments validation on collection and string methods
    out = runCodeFresh("[1, 2].contains(foo=2) ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos && out.find("Unexpected argument name 'foo'") != std::string::npos, "contains() rejects unknown named argument");

    out = runCodeFresh("[1, 2].contains(2, val=2) ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos && out.find("Duplicate argument 'val'") != std::string::npos, "contains() rejects duplicate argument");

    out = runCodeFresh("[1, 2].contains() ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos && out.find("contains() expects 1 argument") != std::string::npos, "contains() rejects missing argument");

    out = runCodeFresh("[1, 2].contains(1, 2) ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos && out.find("Too many arguments provided") != std::string::npos, "contains() rejects too many arguments");

    out = runCodeFresh("\"hello\".starts_with(foo=\"h\") ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos && out.find("Unexpected argument name 'foo'") != std::string::npos, "starts_with() rejects unknown argument name");

    out = runCodeFresh("let arr = [] ~ arr.push(val=10) ~ echo arr ~", ok);
    TEST_ASSERT(ok && out.find("[10]") != std::string::npos, "push() named argument 'val'");

    out = runCodeFresh("let arr = [1, 2, 3] ~ echo arr.pop(ind=0) ~", ok);
    TEST_ASSERT(ok && out.find("=> 1") != std::string::npos, "pop() named argument 'ind'");

    out = runCodeFresh("let arr = [1, 3] ~ arr.insert(ind=1, val=2) ~ echo arr ~", ok);
    TEST_ASSERT(ok && out.find("[1, 2, 3]") != std::string::npos, "insert() named arguments");

    out = runCodeFresh("let arr = [10, 20] ~ arr.remove(val=10) ~ echo arr ~", ok);
    TEST_ASSERT(ok && out.find("[20]") != std::string::npos, "remove() named argument 'val'");

    out = runCodeFresh("let m = {} ~ m.put(key=\"k\", val=\"v\") ~ echo m.get(key=\"k\") ~ echo m.contains(key=\"k\") ~", ok);
    TEST_ASSERT(ok && out.find("=> v") != std::string::npos && out.find("=> true") != std::string::npos, "map put(), get(), contains() named arguments");

    // 4. Negative indexing, slicing boundaries, empty collections, nil values
    out = runCodeFresh("let arr = [10, 20, 30] ~ echo arr[-1] ~ echo arr[-3] ~", ok);
    TEST_ASSERT(ok && out.find("=> 30") != std::string::npos && out.find("=> 10") != std::string::npos, "array negative indexing");

    out = runCodeFresh("let s = \"hello\" ~ echo s[::-1] ~", ok);
    TEST_ASSERT(ok && out.find("olleh") != std::string::npos, "string step -1 reverse slice");

    out = runCodeFresh("[].pop() ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos && out.find("Cannot pop from empty array") != std::string::npos, "empty array pop error");

    out = runCodeFresh("let m = {} ~ echo m.get(\"absent\") ~", ok);
    TEST_ASSERT(ok && out.find("=> nil") != std::string::npos, "map get absent key gives nil");

    // 5. Numeric key equality and map insertion order after key update
    out = runCodeFresh("let m = {} ~ m[1] = \"int\" ~ m[1.0] = \"float\" ~ echo m.length ~ echo m[1] ~", ok);
    TEST_ASSERT(ok && out.find("=> 1") != std::string::npos && out.find("=> float") != std::string::npos, "numeric key equality between int 1 and float 1.0");

    out = runCodeFresh("let m = {\"a\": 1, \"b\": 2, \"c\": 3} ~ m[\"a\"] = 10 ~ echo m.keys() ~", ok);
    TEST_ASSERT(ok && out.find("[\"b\", \"c\", \"a\"]") != std::string::npos, "map insertion order updated when reassigning existing key");

    // 6. Unicode string indexing, slicing, split(""), upper, lower
    out = runCodeFresh("let s = \"Hello 👋 World 🌍\" ~ echo s.length ~ echo s[0] ~ echo s.upper() ~ echo s.lower() ~", ok);
    TEST_ASSERT(ok && out.find("=> 15") != std::string::npos && out.find("H") != std::string::npos && out.find("HELLO 👋 WORLD 🌍") != std::string::npos && out.find("hello 👋 world 🌍") != std::string::npos, "unicode string operations without corruption");
}

static void testV058NewFeaturesAndIntegrations() {
    bool ok = false;
    std::string out;

    // 1. typeof() on all supported value types & nil
    out = runCodeFresh("echo typeof(10) ~ echo typeof(3.14) ~ echo typeof('c') ~ echo typeof(\"str\") ~ echo typeof(true) ~ echo typeof([]) ~ echo typeof({}) ~ echo typeof(task(){}) ~ echo typeof(nil) ~", ok);
    TEST_ASSERT(ok && out.find("int") != std::string::npos && out.find("float") != std::string::npos && out.find("char") != std::string::npos &&
                out.find("string") != std::string::npos && out.find("bool") != std::string::npos && out.find("array") != std::string::npos &&
                out.find("map") != std::string::npos && out.find("func") != std::string::npos && out.find("nil") != std::string::npos, "typeof on all types");

    // typeof on let variables, function results, callbacks
    out = runCodeFresh("let x = nil ~ echo typeof(x) ~ x = 42 ~ echo typeof(x) ~ task getArr() { give [1] ~ } echo typeof(getArr()) ~", ok);
    TEST_ASSERT(ok && out.find("=> nil") != std::string::npos && out.find("=> int") != std::string::npos && out.find("=> array") != std::string::npos, "typeof dynamic let and returns");

    // 2. nil comparisons and flow
    out = runCodeFresh("echo (nil == nil) ~ echo (10 == nil) ~ echo (nil != \"a\") ~", ok);
    TEST_ASSERT(ok && out.find("=> true") != std::string::npos && out.find("=> false") != std::string::npos && out.find("=> true") != std::string::npos, "nil comparisons");

    out = runCodeFresh("let arr = [1, nil, 3] ~ echo arr.contains(nil) ~ echo arr.index_of(nil) ~", ok);
    TEST_ASSERT(ok && out.find("=> true") != std::string::npos && out.find("=> 1") != std::string::npos, "nil inside arrays contains and index_of");

    out = runCodeFresh("let m = {\"a\": nil} ~ echo m.contains(\"a\") ~ echo m.get(\"a\") ~ echo m.get(\"b\") ~", ok);
    TEST_ASSERT(ok && out.find("=> true") != std::string::npos && out.find("=> nil") != std::string::npos, "nil values in map");

    out = runCodeFresh("let arr = [1, nil, 2] ~ echo arr.map(task(x) { give x ~ }) ~", ok);
    TEST_ASSERT(ok && out.find("[1, nil, 2]") != std::string::npos, "nil through array map()");

    // 3. Deeply nested expressions
    std::string deepExpr = "1";
    for (int i = 0; i < 350; i++) deepExpr = "(" + deepExpr + " + 1)";
    deepExpr = "let x = " + deepExpr + " ~";
    out = runCodeFresh(deepExpr, ok);
    TEST_ASSERT(!ok && out.find("Expression nesting limit exceeded") != std::string::npos, "compiler deeply nested expression limit");

    // 4. Recursive collections cycle-safe stringification
    out = runCodeFresh("let a = [] ~ a.push(a) ~ echo a ~", ok);
    TEST_ASSERT(ok && out.find("[[...]]") != std::string::npos, "cycle-safe recursive array echo");

    out = runCodeFresh("let m = {} ~ m[\"self\"] = m ~ echo m ~", ok);
    TEST_ASSERT(ok && out.find("{\"self\": {...}}") != std::string::npos, "cycle-safe recursive map echo");

    // 5. Array remove(val=...) return value
    out = runCodeFresh("let a = [10, 20, 30] ~ echo a.remove(val=20) ~ echo a.remove(val=99) ~", ok);
    TEST_ASSERT(ok && out.find("=> 20") != std::string::npos && out.find("=> nil") != std::string::npos, "array remove val returns value or nil");

    // 6. Dynamic -> typed collection assignment
    out = runCodeFresh("let a = [1, 2] ~ let array<float> b = a ~ b.push(3.5) ~ echo a ~ echo b ~", ok);
    TEST_ASSERT(ok && out.find("[1, 2]") != std::string::npos && out.find("[1.0, 2.0, 3.5]") != std::string::npos, "dynamic to typed array assignment does not mutate original type or contents");

    out = runCodeFresh("let m1 = {\"a\": 1} ~ let map<string, float> m2 = m1 ~ m2[\"b\"] = 2.5 ~ echo m1 ~ echo m2 ~", ok);
    TEST_ASSERT(ok && out.find("{\"a\": 1}") != std::string::npos && out.find("{\"a\": 1.0, \"b\": 2.5}") != std::string::npos, "dynamic to typed map assignment does not mutate original type or contents");

    // 7. Failed module initialization and rollback
    fs::create_directories("test_v58_mods");
    {
        std::ofstream f("test_v58_mods/bad_init.kek");
        f << "let x = 10 / 0 ~\n";
        f.close();
    }
    VM vm;
    out = runCode(vm, "grab bad_init ~\n", ok, "test_v58_mods/main.kek");
    TEST_ASSERT(ok && out.find("Division by zero") != std::string::npos, "failed module init reports runtime error");

    out = runCode(vm, "grab bad_init ~\n", ok, "test_v58_mods/main.kek");
    TEST_ASSERT(ok && out.find("Division by zero") != std::string::npos, "rolled back module re-attempts init rather than caching error state as success");

    fs::remove_all("test_v58_mods");

    // 8. Dotted module paths
    fs::create_directories("test_dotted/lib");
    {
        std::ofstream f("test_dotted/lib/math.kek");
        f << "pub task add(a, b) { give a + b ~ }\n";
        f.close();
    }
    out = runCodeFresh("grab lib.math ~ echo lib.math.add(10, 20) ~", ok, "test_dotted/main.kek");
    TEST_ASSERT(ok && out.find("=> 30") != std::string::npos, "dotted module path import and invocation");
    fs::remove_all("test_dotted");

    // 9. Call context diagnostics
    out = runCodeFresh("task outer() { inner() ~ } task inner() { let x = 1 / 0 ~ } outer() ~", ok);
    TEST_ASSERT(ok && out.find("at inner()") != std::string::npos && out.find("at outer()") != std::string::npos, "nested task call traceback diagnostics");
}

static void testV059RegressionSuite() {
    bool ok = false;
    std::string out;

    // 1. Captured locals and halt / skip
    std::string closureHaltCode =
        "let getFn = nil~\n"
        "for (let i = 0~ i < 5~ i += 1) {\n"
        "    let captured = i * 10~\n"
        "    task fn() { give captured~\n }\n"
        "    if (i == 2) {\n"
        "        getFn = fn~\n"
        "        halt~\n"
        "    }\n"
        "}\n"
        "echo getFn()~\n";
    out = runCodeFresh(closureHaltCode, ok);
    TEST_ASSERT(ok && out.find("=> 20") != std::string::npos, "captured local closed correctly on halt");

    std::string closureSkipCode =
        "let fnList = []~\n"
        "for (let i = 0~ i < 5~ i += 1) {\n"
        "    let captured = i * 10~\n"
        "    task fn() { give captured~\n }\n"
        "    fnList.push(fn)~\n"
        "    if (i == 2) {\n"
        "        skip~\n"
        "    }\n"
        "}\n"
        "echo fnList[2]()~\n";
    out = runCodeFresh(closureSkipCode, ok);
    TEST_ASSERT(ok && out.find("=> 20") != std::string::npos, "captured local closed correctly on skip");

    std::string nestedCapturedCode =
        "task makeClosures() {\n"
        "    let funcs = []~\n"
        "    for (let i = 0~ i < 3~ i += 1) {\n"
        "        let x = i + 1~\n"
        "        for (let j = 0~ j < 2~ j += 1) {\n"
        "            let y = (j + 1) * 100~\n"
        "            task closure() { give x + y~\n }\n"
        "            funcs.push(closure)~\n"
        "            if (j == 0) { skip~\n }\n"
        "        }\n"
        "        if (i == 1) { halt~\n }\n"
        "    }\n"
        "    give funcs~\n"
        "}\n"
        "let fList = makeClosures()~\n"
        "echo fList[0]()~\n";
    out = runCodeFresh(nestedCapturedCode, ok);
    TEST_ASSERT(ok && out.find("=> 101") != std::string::npos, "nested captured locals with halt and skip");

    // 2. Recursive typed collection conversion
    std::string recArrayCode =
        "let a = [[1, 2]]~\n"
        "let array<array<int>> ai = a~\n"
        "let array<array<float>> af = ai~\n"
        "af[0][0] = 99.0~\n"
        "echo a[0][0]~\n"
        "echo ai[0][0]~\n"
        "echo af[0][0]~\n";
    out = runCodeFresh(recArrayCode, ok);
    TEST_ASSERT(ok && out.find("=> 1\n=> 1\n=> 99.0") != std::string::npos, "recursive typed array conversion independent semantics");

    std::string recMapCode =
        "let m = {\"k\": {\"inner\": 1}}~\n"
        "let map<string, map<string, float>> mf = m~\n"
        "mf[\"k\"][\"inner\"] = 42.5~\n"
        "echo m[\"k\"][\"inner\"]~\n"
        "echo mf[\"k\"][\"inner\"]~\n";
    out = runCodeFresh(recMapCode, ok);
    TEST_ASSERT(ok && out.find("=> 1\n=> 42.5") != std::string::npos, "recursive typed map conversion independent semantics");

    std::string arrayWithMapsCode =
        "let am = [{\"x\": 1}]~\n"
        "let array<map<string, float>> am_f = am~\n"
        "am_f[0][\"x\"] = 9.5~\n"
        "echo am[0][\"x\"]~\n"
        "echo am_f[0][\"x\"]~\n";
    out = runCodeFresh(arrayWithMapsCode, ok);
    TEST_ASSERT(ok && out.find("=> 1\n=> 9.5") != std::string::npos, "array containing maps recursive conversion");

    std::string mapWithArraysCode =
        "let ma = {\"k\": [10, 20]}~\n"
        "let map<string, array<float>> ma_f = ma~\n"
        "ma_f[\"k\"][0] = 99.9~\n"
        "echo ma[\"k\"][0]~\n"
        "echo ma_f[\"k\"][0]~\n";
    out = runCodeFresh(mapWithArraysCode, ok);
    TEST_ASSERT(ok && out.find("=> 10\n=> 99.9") != std::string::npos, "map containing arrays recursive conversion");

    // 3. Module namespace merging and order independence
    fs::create_directories("test_v59_mods/a/b");
    {
        std::ofstream f("test_v59_mods/a.kek");
        f << "pub let name = \"mod_a\"~\n";
        f.close();
    }
    {
        std::ofstream f("test_v59_mods/a/b.kek");
        f << "pub let name = \"mod_ab\"~\n";
        f.close();
    }
    {
        std::ofstream f("test_v59_mods/a/b/d.kek");
        f << "pub let name = \"mod_abd\"~\n";
        f.close();
    }
    {
        std::ofstream f("test_v59_mods/a/c.kek");
        f << "pub let name = \"mod_ac\"~\n";
        f.close();
    }

    out = runCodeFresh("grab a.b~\n grab a~\n echo a.name~\n echo a.b.name~\n", ok, "test_v59_mods/main.kek");
    TEST_ASSERT(ok && out.find("=> mod_a") != std::string::npos && out.find("=> mod_ab") != std::string::npos, "module import order grab a.b then grab a");

    out = runCodeFresh("grab a~\n grab a.b~\n echo a.name~\n echo a.b.name~\n", ok, "test_v59_mods/main.kek");
    TEST_ASSERT(ok && out.find("=> mod_a") != std::string::npos && out.find("=> mod_ab") != std::string::npos, "module import order grab a then grab a.b");

    out = runCodeFresh("grab a.b.d~\n grab a.c~\n grab a.b~\n grab a~\n echo a.name~\n echo a.b.name~\n echo a.c.name~\n echo a.b.d.name~\n", ok, "test_v59_mods/main.kek");
    TEST_ASSERT(ok && out.find("=> mod_a") != std::string::npos && out.find("=> mod_ab") != std::string::npos && out.find("=> mod_ac") != std::string::npos && out.find("=> mod_abd") != std::string::npos, "multiple nested dotted module paths coexist");

    fs::remove_all("test_v59_mods");

    // 4. Slice argument validation
    out = runCodeFresh("let arr = [1, 2, 3]~ echo arr[1.5:2]~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos && out.find("Slice start index must be an integer") != std::string::npos, "fractional slice start rejection");

    out = runCodeFresh("let arr = [1, 2, 3]~ echo arr[1:2.5]~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos && out.find("Slice end index must be an integer") != std::string::npos, "fractional slice end rejection");

    out = runCodeFresh("let arr = [1, 2, 3]~ echo arr.slice(start=1.5)~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos && out.find("Slice start index must be an integer") != std::string::npos, "array.slice method fractional start rejection");

    out = runCodeFresh("let str = \"hello\"~ echo str[1.5:3]~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos, "string fractional slice start rejection");

    // 5. Full int64 range and abs(INT64_MIN)
    out = runCodeFresh("let minVal = -9223372036854775808~ echo minVal~", ok);
    TEST_ASSERT(ok && out.find("=> -9223372036854775808") != std::string::npos, "full int64 min value -9223372036854775808");

    out = runCodeFresh("let maxVal = 9223372036854775807~ echo maxVal~", ok);
    TEST_ASSERT(ok && out.find("=> 9223372036854775807") != std::string::npos, "full int64 max value 9223372036854775807");

    out = runCodeFresh("echo abs(-9223372036854775808)~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos && out.find("integer overflow in abs()") != std::string::npos, "abs(INT64_MIN) integer overflow runtime error");

    out = runCodeFresh("echo -(-9223372036854775808)~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos && out.find("overflow") != std::string::npos, "unary negation of INT64_MIN overflow error");

    out = runCodeFresh("let bad = 9223372036854775808~", ok);
    TEST_ASSERT(!ok && out.find("Compiler Error") != std::string::npos && out.find("out of 64-bit range") != std::string::npos, "positive 9223372036854775808 rejected");

    // 6. Typed map key coercion
    out = runCodeFresh("let m = {1: \"x\"}~\n let map<float, string> n = m~\n echo n.keys()~\n echo typeof(n.keys()[0])~\n", ok);
    TEST_ASSERT(ok && out.find("[1.0]") != std::string::npos && out.find("=> float") != std::string::npos, "int -> float map key coercion");

    out = runCodeFresh("let m = {1.0: \"x\"}~\n let map<int, string> n = m~\n", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos, "float -> int map key conversion rejected for float keys");

    out = runCodeFresh("let m = {1.5: \"x\"}~\n let map<int, string> n = m~\n", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos, "float -> int map key conversion rejected when non-whole number");

    out = runCodeFresh("let map<float, string> m = {}~\n m[1] = \"val\"~\n echo typeof(m.keys()[0])~\n", ok);
    TEST_ASSERT(ok && out.find("=> float") != std::string::npos, "direct indexing assignment float map key coercion");

    // 7. CLI Exit Codes check
    {
        std::ofstream f("/tmp/test_cli_ok.kek");
        f << "echo 100~\n";
        f.close();

        std::ofstream f2("/tmp/test_cli_err.kek");
        f2 << "let x = 1 / 0~\n";
        f2.close();

        int status1 = std::system("([ -f ./kekno ] && ./kekno /tmp/test_cli_ok.kek || ./build/kekno /tmp/test_cli_ok.kek) >/dev/null 2>&1");
        TEST_ASSERT(WEXITSTATUS(status1) == 0, "CLI exit code 0 on success");

        int status2 = std::system("([ -f ./kekno ] && ./kekno /tmp/test_cli_err.kek || ./build/kekno /tmp/test_cli_err.kek) >/dev/null 2>&1");
        TEST_ASSERT(WEXITSTATUS(status2) != 0, "CLI exit code non-zero on runtime error");

        int status3 = std::system("([ -f ./kekno ] && ./kekno /tmp/non_existent_file.kek || ./build/kekno /tmp/non_existent_file.kek) >/dev/null 2>&1");
        TEST_ASSERT(WEXITSTATUS(status3) != 0, "CLI exit code non-zero on missing file");

        std::remove("/tmp/test_cli_ok.kek");
        std::remove("/tmp/test_cli_err.kek");
    }
}

static void testV060StructsAndOperators() {
    bool ok = false;
    std::string out;

    // 1. Basic struct declarations & zero-field structs
    out = runCodeFresh("build Zero {}~\n let z = Zero()~\n echo typeof(z)~\n", ok);
    TEST_ASSERT(ok && out.find("=> Zero") != std::string::npos, "zero field struct construction and typeof");

    // 2. Positional & named construction, missing fields default to nil
    std::string personCode =
        "build Person {\n"
        "    const name : string~\n"
        "    let age : int~\n"
        "}~\n"
        "let p1 = Person(\"BBZ\", 15)~\n"
        "echo p1.name~\n"
        "echo p1.age~\n"
        "let p2 = Person(age=20, name=\"Alice\")~\n"
        "echo p2.name~\n"
        "echo p2.age~\n"
        "let p3 = Person(\"Charlie\")~\n"
        "echo p3.name~\n"
        "echo p3.age~\n";
    out = runCodeFresh(personCode, ok);
    TEST_ASSERT(ok && out.find("=> BBZ") != std::string::npos && out.find("=> 15") != std::string::npos &&
                out.find("=> Alice") != std::string::npos && out.find("=> 20") != std::string::npos &&
                out.find("=> Charlie") != std::string::npos && out.find("=> nil") != std::string::npos,
                "positional, named, and default constructor arguments");

    // 3. Constructor error checking
    out = runCodeFresh("build Point { let x : int~ }~ Point(1, 2)~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos, "constructor too many positional args error");

    out = runCodeFresh("build Point { let x : int~ }~ Point(y=10)~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos && out.find("Unknown named argument") != std::string::npos, "constructor unknown named arg error");

    out = runCodeFresh("build Point { let x : int~ }~ Point(x=1, x=2)~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos, "constructor duplicate argument error");

    out = runCodeFresh("build Point { let x : int~ }~ Point(\"wrong\")~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos && out.find("Type mismatch") != std::string::npos, "constructor invalid field type error");

    // 4. Duplicate field name compile-time error
    out = runCodeFresh("build Bad { let x : int~\n let x : string~ }~", ok);
    TEST_ASSERT(!ok && out.find("Compiler Error") != std::string::npos && out.find("Duplicate field") != std::string::npos, "duplicate field compile error");

    // 5. Const fields and const struct variable checks
    out = runCodeFresh("build Person { const name : string~\n let age : int~ }~ let p = Person(\"A\", 10)~ p.name = \"B\"~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos && out.find("constant field") != std::string::npos, "reassigning const field error");

    out = runCodeFresh("build Person { let name : string~\n let age : int~ }~ const p = Person(\"A\", 10)~ p.age = 20~", ok);
    TEST_ASSERT(!ok && out.find("Compiler Error") != std::string::npos && out.find("const variable") != std::string::npos, "mutating field of const struct variable compile error");

    // 6. Independent value copy semantics (assignment & parameters)
    std::string copyCode =
        "build Person {\n"
        "    let name : string~\n"
        "    let age : int~\n"
        "}~\n"
        "let p1 = Person(\"A\", 10)~\n"
        "let p2 = p1~\n"
        "p2.age = 99~\n"
        "echo p1.age~\n"
        "echo p2.age~\n"
        "task updateAge(Person p) {\n"
        "    p.age = 500~\n"
        "}\n"
        "updateAge(p1)~\n"
        "echo p1.age~\n";
    out = runCodeFresh(copyCode, ok);
    TEST_ASSERT(ok && out.find("=> 10\n=> 99\n=> 10") != std::string::npos, "struct value copy semantics on assignment and task param");

    // 7. Structs in arrays and maps, with nested property updates
    std::string collectionCode =
        "build Person {\n"
        "    let name : string~\n"
        "    let age : int~\n"
        "}~\n"
        "let people : array<Person> = [\n"
        "    Person(\"A\", 15),\n"
        "    Person(\"B\", 16)\n"
        "]~\n"
        "people[0].age = 17~\n"
        "echo people[0].age~\n"
        "let mapByName : map<string, Person> = {}~\n"
        "mapByName.put(\"BBZ\", Person(\"BBZ\", 25))~\n"
        "echo mapByName.get(\"BBZ\").age~\n";
    out = runCodeFresh(collectionCode, ok);
    TEST_ASSERT(ok && out.find("=> 17") != std::string::npos && out.find("=> 25") != std::string::npos, "structs inside typed arrays and maps with member updates");

    // 8. Methods, self keyword, and method calls
    std::string methodCode =
        "build Counter {\n"
        "    let count : int~\n"
        "    task increment() {\n"
        "        self.count = self.count + 1~\n"
        "    }\n"
        "    task reset() {\n"
        "        count = 0~\n"
        "    }\n"
        "}~\n"
        "let c = Counter(10)~\n"
        "c.increment()~\n"
        "echo c.count~\n"
        "c.reset()~\n"
        "echo c.count~\n";
    out = runCodeFresh(methodCode, ok);
    TEST_ASSERT(ok && out.find("=> 11\n=> 0") != std::string::npos, "struct methods, self, and direct field access");

    // 9. Assigning to self compile error
    out = runCodeFresh("build C { let x : int~ task bad() { self = C(5)~ } }~", ok);
    TEST_ASSERT(!ok && out.find("Compiler Error") != std::string::npos, "reassigning self error");

    // 10. Same-type private access in methods
    std::string sameTypePrivateCode =
        "build Person {\n"
        "    priv let secret : int~\n"
        "    pub task setSecret(s : int) { secret = s~ }\n"
        "    pub task compareSecret(Person other) {\n"
        "        give self.secret > other.secret~\n"
        "    }\n"
        "}~\n"
        "let p1 = Person(0)~\n"
        "let p2 = Person(0)~\n"
        "p1.setSecret(100)~\n"
        "p2.setSecret(50)~\n"
        "echo p1.compareSecret(p2)~\n";
    out = runCodeFresh(sameTypePrivateCode, ok);
    TEST_ASSERT(ok && out.find("=> true") != std::string::npos, "same-type private field access in method");

    // 11. Cross-module visibility
    std::string modFile = "/tmp/mod_v060.kek";
    std::ofstream fMod(modFile);
    fMod << "pub build Item {\n"
         << "    pub let id : int~\n"
         << "    priv let secret : string~\n"
         << "    pub task getSecret() { give secret~ }\n"
         << "}~\n";
    fMod.close();

    std::string useModCode =
        "grab /tmp/mod_v060 as mod~\n"
        "let item = mod.Item(42, \"hidden\")~\n"
        "echo item.id~\n"
        "echo item.getSecret()~\n";
    out = runCodeFresh(useModCode, ok);
    TEST_ASSERT(ok && out.find("=> 42\n=> hidden") != std::string::npos, "cross-module pub build construction and member access");

    std::string privateFieldCode =
        "grab /tmp/mod_v060 as mod~\n"
        "let item = mod.Item(42, \"hidden\")~\n"
        "echo item.secret~\n";
    out = runCodeFresh(privateFieldCode, ok);
    TEST_ASSERT(ok && out.find("[Module Error]") != std::string::npos && out.find("private field") != std::string::npos, "cross-module private field error");
    std::remove(modFile.c_str());

    // 12. Operator overloading (binary +, -, ==, !=, unary -) and compound assignment
    std::string vecCode =
        "build Vec2 {\n"
        "    let x : float~\n"
        "    let y : float~\n"
        "    operator +(other : Vec2) {\n"
        "        give Vec2(self.x + other.x, self.y + other.y)~\n"
        "    }\n"
        "    operator -(other : Vec2) {\n"
        "        give Vec2(self.x - other.x, self.y - other.y)~\n"
        "    }\n"
        "    operator -() {\n"
        "        give Vec2(-self.x, -self.y)~\n"
        "    }\n"
        "    operator ==(other : Vec2) {\n"
        "        give self.x == other.x and self.y == other.y~\n"
        "    }\n"
        "}~\n"
        "let v1 = Vec2(2.0, 3.0)~\n"
        "let v2 = Vec2(4.0, 5.0)~\n"
        "let v3 = v1 + v2~\n"
        "echo v3.x~\n"
        "echo v3.y~\n"
        "let v4 = -v1~\n"
        "echo v4.x~\n"
        "echo v4.y~\n"
        "echo v1 == Vec2(2.0, 3.0)~\n"
        "echo v1 != v2~\n";
    out = runCodeFresh(vecCode, ok);
    TEST_ASSERT(ok && out.find("=> 6") != std::string::npos && out.find("=> 8") != std::string::npos &&
                out.find("=> -2") != std::string::npos && out.find("=> -3") != std::string::npos &&
                out.find("=> true\n=> true") != std::string::npos, "operator overloading +, binary -, unary -, ==, and !=");

    // 13. Recursive struct types and cycle safety (equality, string conversion)
    std::string nodeCode =
        "build Node {\n"
        "    let val : int~\n"
        "    let next : Node~\n"
        "}~\n"
        "let n1 = Node(1, nil)~\n"
        "let n2 = Node(2, n1)~\n"
        "echo n2.val~\n"
        "echo n2.next.val~\n"
        "echo n2~\n";
    out = runCodeFresh(nodeCode, ok);
    TEST_ASSERT(ok && out.find("=> 2\n=> 1") != std::string::npos && out.find("Node{val: 2, next: Node{val: 1, next: nil}}") != std::string::npos, "recursive struct and nested toString");
}

static void test_v061_patch_features() {
    bool ok = false;
    std::string out;

    // 1. float -> int assignment rejection
    out = runCodeFresh("let int x = 2.0~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos, "float -> int variable assignment rejected");

    // 2. float -> int task argument rejection
    out = runCodeFresh("task foo(x : int) {} foo(2.0)~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos, "float -> int task argument rejected");

    // 3. float -> int struct construction rejection
    out = runCodeFresh("build S { let x : int~ }~ S(2.0)~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos, "float -> int struct construction rejected");

    // 4. float -> int field assignment rejection
    out = runCodeFresh("build S { let x : int~ }~ let s = S(1)~ s.x = 2.0~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos, "float -> int field assignment rejected");

    // 5. int -> float still allowed
    out = runCodeFresh("let float x = 2~ task bar(y : float) { give y~ } echo bar(3)~", ok);
    TEST_ASSERT(ok && out.find("=> 3.0") != std::string::npos, "int -> float coercion allowed");

    // 6. distinct same-named structs across modules
    {
        std::ofstream fA("/tmp/modA.kek");
        fA << "pub build Person { pub let name : string~ }~\n"
           << "pub task makePerson() { give Person(\"A\")~ }\n";
        fA.close();

        std::ofstream fB("/tmp/modB.kek");
        fB << "grab /tmp/modA as modA~\n"
           << "pub build Person { pub let age : int~ }~\n"
           << "pub task checkA(p : modA.Person) { give p.name~ }\n"
           << "pub task checkB(p : Person) { give p.age~ }\n";
        fB.close();

        std::string testCode =
            "grab /tmp/modA as modA~\n"
            "grab /tmp/modB as modB~\n"
            "let pa = modA.makePerson()~\n"
            "echo modB.checkA(pa)~\n"
            "let pb = modB.Person(20)~\n"
            "echo modB.checkB(pb)~\n";
        out = runCodeFresh(testCode, ok);
        TEST_ASSERT(ok && out.find("=> A") != std::string::npos && out.find("=> 20") != std::string::npos, "distinct same-named structs across modules positive");

        std::string badCode =
            "grab /tmp/modA as modA~\n"
            "grab /tmp/modB as modB~\n"
            "let pa = modA.makePerson()~\n"
            "modB.checkB(pa)~\n";
        out = runCodeFresh(badCode, ok);
        TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos, "distinct same-named structs mismatch rejected");

        std::remove("/tmp/modA.kek");
        std::remove("/tmp/modB.kek");
    }

    // 7. unknown struct type errors
    out = runCodeFresh("build Person { let x : Missing~ }~", ok);
    TEST_ASSERT(!ok && out.find("Compiler Error") != std::string::npos && out.find("Unknown type 'Missing'") != std::string::npos, "unknown struct type in field rejected");

    // 8. nested typed collection type errors
    out = runCodeFresh("let array<Missing> arr = []~", ok);
    TEST_ASSERT(!ok && out.find("Compiler Error") != std::string::npos && out.find("Unknown type 'Missing'") != std::string::npos, "unknown struct type in array rejected");

    out = runCodeFresh("let map<string, Missing> m = {}~", ok);
    TEST_ASSERT(!ok && out.find("Compiler Error") != std::string::npos && out.find("Unknown type 'Missing'") != std::string::npos, "unknown struct type in map rejected");

    // 9. deep const struct mutation rejection
    out = runCodeFresh("build Person { let age : int~ }~ const p = Person(10)~ p.age = 20~", ok);
    TEST_ASSERT(!ok && out.find("Compiler Error") != std::string::npos, "direct field mutation of const struct rejected");

    // 10. const nested struct mutation rejection
    out = runCodeFresh("build Inner { let x : int~ }~ build Outer { let inner : Inner~ }~ const o = Outer(Inner(1))~ o.inner.x = 2~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos, "const nested struct field mutation rejected");

    // 11. const nested array/map mutation rejection
    out = runCodeFresh("build Person { let items : array~ }~ const p = Person([1])~ p.items.push(2)~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos, "const nested array method mutation rejected");

    out = runCodeFresh("build Person { let m : map~ }~ const p = Person({})~ p.m.put(\"k\", 1)~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos, "const nested map method mutation rejected");

    // 12. const receiver mutation through methods
    out = runCodeFresh("build Person { let age : int~ task mutate() { self.age = 99~ } }~ const p = Person(20)~ p.mutate()~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos, "const receiver mutation through method rejected");

    // 13. read-only methods on const receivers still working
    out = runCodeFresh("build Person { let age : int~ task getAge() { give self.age~ } }~ const p = Person(20)~ echo p.getAge()~", ok);
    TEST_ASSERT(ok && out.find("=> 20") != std::string::npos, "read-only methods on const receivers preserved");

    // 14. independent struct copies in arrays
    out = runCodeFresh("build Pt { let x : int~ }~ let p = Pt(1)~ let arr = []~ arr.push(p)~ arr[0].x = 99~ echo p.x~ echo arr[0].x~", ok);
    TEST_ASSERT(ok && out.find("=> 1\n=> 99") != std::string::npos, "independent struct copies in arrays");

    // 15. independent struct copies in maps
    out = runCodeFresh("build Pt { let x : int~ }~ let p = Pt(1)~ let m = {}~ m[\"k\"] = p~ m[\"k\"].x = 88~ echo p.x~ echo m[\"k\"].x~", ok);
    TEST_ASSERT(ok && out.find("=> 1\n=> 88") != std::string::npos, "independent struct copies in maps");

    // 16. independent nested struct copies
    out = runCodeFresh("build Inner { let x : int~ }~ build Outer { let inner : Inner~ }~ let i = Inner(1)~ let o = Outer(i)~ o.inner.x = 55~ echo i.x~ echo o.inner.x~", ok);
    TEST_ASSERT(ok && out.find("=> 1\n=> 55") != std::string::npos, "independent nested struct copies");

    // 17. private access from same-module non-method code rejected
    out = runCodeFresh("build Person { priv let age : int~ }~ task checkAge(p : Person) { echo p.age~ } checkAge(Person(20))~", ok);
    TEST_ASSERT(ok && out.find("[Module Error]") != std::string::npos && out.find("private field") != std::string::npos, "private access from non-method task rejected");

    // 18. valid same-type private access preserved
    out = runCodeFresh("build Person { priv let age : int~ pub task setAge(a : int) { age = a~ } pub task isOlder(other : Person) { give self.age > other.age~ } }~ let p1 = Person(0)~ p1.setAge(30)~ let p2 = Person(0)~ p2.setAge(20)~ echo p1.isOlder(p2)~", ok);
    TEST_ASSERT(ok && out.find("=> true") != std::string::npos, "valid same-type private access preserved");

    // 19. duplicate struct declarations rejected
    out = runCodeFresh("build Person { let x : int~ }~ build Person { let y : int~ }~", ok);
    TEST_ASSERT(!ok && out.find("Compiler Error") != std::string::npos && out.find("Duplicate struct declaration") != std::string::npos, "duplicate struct declaration rejected");

    // 20. duplicate method declarations rejected
    out = runCodeFresh("build Foo { task bar() {} task bar() {} }~", ok);
    TEST_ASSERT(!ok && out.find("Compiler Error") != std::string::npos && out.find("Duplicate method declaration") != std::string::npos, "duplicate method declaration rejected");

    // 21. operator overload type mismatch
    out = runCodeFresh("build V { let x : int~ operator +(other : int) { give V(self.x + other)~ } }~ V(1) + \"x\"~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos && out.find("No matching operator overload '+'") != std::string::npos, "operator overload argument mismatch rejected without fallthrough");

    // 22. operator overload with float/int compatibility
    out = runCodeFresh("build V { let x : float~ operator +(other : float) { give V(self.x + other)~ } }~ let v = V(1.0) + 2~ echo v.x~", ok);
    TEST_ASSERT(ok && out.find("=> 3.0") != std::string::npos, "operator overload allows int -> float coercion");

    // 23. explicit task return-type syntax rejected
    out = runCodeFresh("task foo() : int { give 10~ }", ok);
    TEST_ASSERT(!ok && out.find("Compiler Error") != std::string::npos && out.find("Task return type declarations are not supported") != std::string::npos, "explicit task return-type syntax rejected");

    out = runCodeFresh("build S { task bar() : string { give \"hi\"~ } }~", ok);
    TEST_ASSERT(!ok && out.find("Compiler Error") != std::string::npos && out.find("Task return type declarations are not supported") != std::string::npos, "explicit method return-type syntax rejected");
}

int main() {
    std::cout << "Running Kekno v0.6.1 Complete Test Suite..." << std::endl;

    testNativeFunctionsAndMath();
    testNumericAndArithmetic();
    testArrayMethods();
    testMapMethods();
    testHigherOrderCollectionOperations();
    testNamedArguments();
    testStringMethodsAndUnicode();
    testRemovedLegacyAPIsAndCasts();
    testControlFlowForElseIfAndConst();
    testCompoundAssignments();
    testTypedDeclarationsAndParameters();
    testTasksAndRecursion();
    testHaltSkipScopeCleanup();
    testMalformedNumericLiterals();
    testArrayIndexing();
    testDiagnostics();
    test16BitOperandLimits();
    testGrabAndVMState();
    testV046Patches();
    testV050Features();
    testV052Modules();
    testV056RegressionSuite();
    testV058NewFeaturesAndIntegrations();
    testV059RegressionSuite();
    testV060StructsAndOperators();
    test_v061_patch_features();

    std::cout << "Tests Passed: " << g_testsPassed << std::endl;
    std::cout << "Tests Failed: " << g_testsFailed << std::endl;

    return (g_testsFailed == 0) ? 0 : 1;
}
