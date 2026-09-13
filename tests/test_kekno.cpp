#include <iostream>
#include <sstream>
#include <fstream>
#include <cassert>
#include <cmath>
#include "compiler.h"
#include "vm.h"

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

static std::string runCode(VM& vm, const std::string& code, bool& compileSuccess) {
    std::stringstream buffer;
    std::streambuf* oldCout = std::cout.rdbuf(buffer.rdbuf());

    Chunk chunk;
    Compiler compiler(code, chunk);
    compileSuccess = compiler.compile();
    if (compileSuccess) {
        vm.run(chunk);
    }

    std::cout.rdbuf(oldCout);
    return buffer.str();
}

static std::string runCodeFresh(const std::string& code, bool& compileSuccess) {
    VM vm;
    return runCode(vm, code, compileSuccess);
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

    // purge()
    out = runCodeFresh("let m = {\"a\": 1, \"b\": 2} ~ purge(m, \"a\") ~ echo size(m) ~", ok);
    TEST_ASSERT(ok && out.find("=> 1") != std::string::npos, "purge map");
    out = runCodeFresh("let arr = [10, 20, 30] ~ purge(arr, 1) ~ echo arr ~", ok);
    TEST_ASSERT(ok && out.find("[10, 30]") != std::string::npos, "purge array");

    // inject() and expel()
    out = runCodeFresh("let arr = [] ~ inject(arr, 42) ~ echo arr ~", ok);
    TEST_ASSERT(ok && out.find("[42]") != std::string::npos, "inject valid");
    out = runCodeFresh("let arr = [10, 20] ~ echo expel(arr) ~ echo arr ~", ok);
    TEST_ASSERT(ok && out.find("=> 20") != std::string::npos && out.find("[10]") != std::string::npos, "expel valid");

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

static void testCharAndStringUtilities() {
    bool ok = false;
    std::string out;

    // Char literal & UTF-8
    out = runCodeFresh("let c = 'A' ~ echo scan(c) ~ echo c ~", ok);
    TEST_ASSERT(ok && out.find("=> char") != std::string::npos && out.find("=> A") != std::string::npos, "char literal");

    // String/char concatenation in both directions
    out = runCodeFresh("let s = \"hello \" + 'W' ~ echo s ~ echo 'W' + \" world\" ~", ok);
    TEST_ASSERT(ok && out.find("hello W") != std::string::npos && out.find("W world") != std::string::npos, "string char concat");

    // String builtins: upper, lower, trim, contains, starts_with, ends_with, split, join, replace
    out = runCodeFresh("echo upper(\"kekno\") ~ echo lower(\"KEKNO\") ~ echo trim(\"  hi  \") ~", ok);
    TEST_ASSERT(ok && out.find("=> KEKNO") != std::string::npos && out.find("=> kekno") != std::string::npos && out.find("=> hi") != std::string::npos, "upper lower trim");

    out = runCodeFresh("echo contains(\"abcdef\", \"cd\") ~ echo starts_with(\"abcdef\", \"ab\") ~ echo ends_with(\"abcdef\", \"ef\") ~", ok);
    TEST_ASSERT(ok && out.find("=> true") != std::string::npos, "contains starts_with ends_with");

    out = runCodeFresh("let parts = split(\"a,b,c\", \",\") ~ echo join(parts, \"-\") ~ echo replace(\"hello world\", \"world\", \"Kekno\") ~", ok);
    TEST_ASSERT(ok && out.find("a-b-c") != std::string::npos && out.find("hello Kekno") != std::string::npos, "split join replace");
}

static void testCasts() {
    bool ok = false;
    std::string out;

    // cast_int() nearest rounding with .5 upward
    out = runCodeFresh("echo cast_int(3.4) ~ echo cast_int(3.5) ~ echo cast_int(\"42\") ~", ok);
    TEST_ASSERT(ok && out.find("=> 3") != std::string::npos && out.find("=> 4") != std::string::npos && out.find("=> 42") != std::string::npos, "cast_int rounding");

    // cast_float(), cast_string(), cast_char(), cast_array(), cast_map()
    out = runCodeFresh("echo cast_float(10) ~ echo cast_string(100) ~ echo cast_char(\"X\") ~ echo cast_array(\"hi\") ~", ok);
    TEST_ASSERT(ok && out.find("=> 10.0") != std::string::npos && out.find("=> 100") != std::string::npos && out.find("=> X") != std::string::npos && out.find("['h', 'i']") != std::string::npos, "casts");

    // cast_char() rejected on multi-char string
    out = runCodeFresh("cast_char(\"hello\") ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos, "cast_char multi-char reject");
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
    // Add 65536 constants to chunk to test constant pool 16-bit limit
    try {
        for (int i = 0; i < 65536; i++) {
            chunk.addConstant(v);
        }
        bool threw = false;
        try {
            chunk.addConstant(v); // 65537th constant
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

    // Missing file
    out = runCodeFresh("grab \"non_existent_file_12345.kek\" ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos && out.find("Could not open grab file") != std::string::npos, "grab missing file");

    // Valid grab file
    {
        std::ofstream validFile("temp_valid.kek");
        validFile << "task addTwo(a, b) { give a + b ~ } let exportedVal = 42 ~";
        validFile.close();
    }
    VM vm;
    out = runCode(vm, "grab \"temp_valid.kek\" ~ echo addTwo(10, 20) ~ echo exportedVal ~", ok);
    TEST_ASSERT(ok && out.find("=> 30") != std::string::npos && out.find("=> 42") != std::string::npos, "valid grab file import");
    std::remove("temp_valid.kek");
}

int main() {
    std::cout << "Running Kekno v0.4.5 Regression Test Suite..." << std::endl;

    testNativeFunctionsAndMath();
    testNumericAndArithmetic();
    testCharAndStringUtilities();
    testCasts();
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

    std::cout << "Tests Passed: " << g_testsPassed << std::endl;
    std::cout << "Tests Failed: " << g_testsFailed << std::endl;

    return (g_testsFailed == 0) ? 0 : 1;
}
