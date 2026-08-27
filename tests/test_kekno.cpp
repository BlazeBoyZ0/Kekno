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

static void testNativeFunctions() {
    bool ok = false;
    std::string out;

    // size()
    out = runCodeFresh("echo size([1, 2, 3]) ~", ok);
    TEST_ASSERT(ok && out.find("=> 3") != std::string::npos, "size array");
    out = runCodeFresh("echo size(\"hello\") ~", ok);
    TEST_ASSERT(ok && out.find("=> 5") != std::string::npos, "size string");
    out = runCodeFresh("echo size({\"a\": 1, \"b\": 2}) ~", ok);
    TEST_ASSERT(ok && out.find("=> 2") != std::string::npos, "size map");
    out = runCodeFresh("size() ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos, "size arity");
    out = runCodeFresh("size(123) ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos, "size invalid type");

    // keys() and values()
    out = runCodeFresh("let m = {\"a\": 10} ~ echo keys(m) ~", ok);
    TEST_ASSERT(ok && out.find("[\"a\"]") != std::string::npos, "keys valid");
    out = runCodeFresh("let m = {\"a\": 10} ~ echo values(m) ~", ok);
    TEST_ASSERT(ok && out.find("[10]") != std::string::npos, "values valid");
    out = runCodeFresh("keys(123) ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos, "keys invalid type");
    out = runCodeFresh("values(123) ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos, "values invalid type");

    // has()
    out = runCodeFresh("echo has({\"a\": 1}, \"a\") ~", ok);
    TEST_ASSERT(ok && out.find("=> true") != std::string::npos, "has map true");
    out = runCodeFresh("echo has({\"a\": 1}, \"b\") ~", ok);
    TEST_ASSERT(ok && out.find("=> false") != std::string::npos, "has map false");
    out = runCodeFresh("echo has([10, 20], 20) ~", ok);
    TEST_ASSERT(ok && out.find("=> true") != std::string::npos, "has array true");
    out = runCodeFresh("has(123, \"a\") ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos, "has invalid target");

    // purge()
    out = runCodeFresh("let m = {\"a\": 1, \"b\": 2} ~ purge(m, \"a\") ~ echo size(m) ~", ok);
    TEST_ASSERT(ok && out.find("=> 1") != std::string::npos, "purge map");
    out = runCodeFresh("let arr = [10, 20, 30] ~ purge(arr, 1) ~ echo arr ~", ok);
    TEST_ASSERT(ok && out.find("[10, 30]") != std::string::npos, "purge array");
    out = runCodeFresh("let arr = [10] ~ purge(arr, 5) ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos, "purge array OOB");
    out = runCodeFresh("let arr = [10] ~ purge(arr, 0.5) ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos, "purge array non-integer");

    // inject() and expel()
    out = runCodeFresh("let arr = [] ~ inject(arr, 42) ~ echo arr ~", ok);
    TEST_ASSERT(ok && out.find("[42]") != std::string::npos, "inject valid");
    out = runCodeFresh("let arr = [10, 20] ~ echo expel(arr) ~ echo arr ~", ok);
    TEST_ASSERT(ok && out.find("=> 20") != std::string::npos && out.find("[10]") != std::string::npos, "expel valid");
    out = runCodeFresh("expel([]) ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos, "expel empty");

    // scan()
    out = runCodeFresh("echo scan(123) ~ echo scan(\"hi\") ~ echo scan(true) ~ echo scan([]) ~ echo scan({}) ~ echo scan(nil) ~", ok);
    TEST_ASSERT(ok && out.find("number") != std::string::npos && out.find("string") != std::string::npos && out.find("array") != std::string::npos, "scan types");

    // cast_num()
    out = runCodeFresh("echo cast_num(\"123.45\") ~", ok);
    TEST_ASSERT(ok && out.find("=> 123.45") != std::string::npos, "cast_num valid string");
    out = runCodeFresh("echo cast_num(true) ~", ok);
    TEST_ASSERT(ok && out.find("=> 1") != std::string::npos, "cast_num bool");
    out = runCodeFresh("cast_num(\"invalid123\") ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos, "cast_num invalid string");
    out = runCodeFresh("cast_num(\"\") ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos, "cast_num empty string");
    out = runCodeFresh("cast_num([]) ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos, "cast_num array");

    // cast_str()
    out = runCodeFresh("echo cast_str(123) ~ echo cast_str(true) ~", ok);
    TEST_ASSERT(ok && out.find("123") != std::string::npos && out.find("true") != std::string::npos, "cast_str");

    // clock() and rand()
    out = runCodeFresh("echo clock() > 0 ~ echo rand() >= 0 ~", ok);
    TEST_ASSERT(ok && out.find("=> true") != std::string::npos, "clock and rand");

    // abs(), floor(), ceil(), sqrt(), clamp()
    out = runCodeFresh("echo abs(-5) ~ echo floor(3.7) ~ echo ceil(3.2) ~ echo sqrt(16) ~ echo clamp(15, 0, 10) ~", ok);
    TEST_ASSERT(ok && out.find("=> 5") != std::string::npos && out.find("=> 3") != std::string::npos && out.find("=> 4") != std::string::npos && out.find("=> 10") != std::string::npos, "math builtins");
    out = runCodeFresh("sqrt(-1) ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos, "sqrt negative");
    out = runCodeFresh("clamp(5, 10, 0) ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos, "clamp min > max");
}

static void testIndexing() {
    bool ok = false;
    std::string out;

    out = runCodeFresh("let a = [10, 20, 30] ~ echo a[1] ~ a[1] = 99 ~ echo a[1] ~", ok);
    TEST_ASSERT(ok && out.find("=> 20") != std::string::npos && out.find("=> 99") != std::string::npos, "array get/set index");

    out = runCodeFresh("let s = \"hello\" ~ echo s[1] ~", ok);
    TEST_ASSERT(ok && out.find("=> e") != std::string::npos, "string get index");

    out = runCodeFresh("let m = {\"k\": \"v\"} ~ echo m[\"k\"] ~ m[\"k\"] = \"v2\" ~ echo m[\"k\"] ~", ok);
    TEST_ASSERT(ok && out.find("=> v") != std::string::npos && out.find("=> v2") != std::string::npos, "map get/set index");

    out = runCodeFresh("let a = [1, 2] ~ a[5] ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos, "array OOB read");

    out = runCodeFresh("let a = [1, 2] ~ a[5] = 10 ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos, "array OOB write");

    out = runCodeFresh("let a = [1, 2] ~ a[1.5] ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos, "array float index");

    out = runCodeFresh("let s = \"abc\" ~ s[10] ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos, "string OOB index");

    out = runCodeFresh("let x = 100 ~ x[0] ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos, "indexing invalid type");
}

static void testSyntaxErrorsAndLoopControls() {
    bool ok = false;
    std::string out;

    // missing tildes
    out = runCodeFresh("let x = 10", ok);
    TEST_ASSERT(!ok && out.find("[Syntax Error]") != std::string::npos, "missing tilde");

    // mismatched delimiters
    out = runCodeFresh("let x = (1 + 2} ~", ok);
    TEST_ASSERT(!ok && out.find("[Syntax Error]") != std::string::npos, "mismatched paren/brace");

    out = runCodeFresh("let a = [1, 2) ~", ok);
    TEST_ASSERT(!ok && out.find("[Syntax Error]") != std::string::npos, "mismatched bracket/paren");

    // unterminated string
    out = runCodeFresh("let s = \"unclosed ~", ok);
    TEST_ASSERT(!ok && out.find("[Syntax Error]") != std::string::npos && out.find("Unterminated string") != std::string::npos, "unterminated string");

    // unclosed block comment
    out = runCodeFresh("/* unclosed comment", ok);
    TEST_ASSERT(!ok && out.find("[Syntax Error]") != std::string::npos && out.find("Unclosed block comment") != std::string::npos, "unclosed block comment");

    // halt and skip outside loop
    out = runCodeFresh("halt ~", ok);
    TEST_ASSERT(!ok && out.find("[Compiler Error]") != std::string::npos && out.find("Cannot use 'halt' outside of a loop") != std::string::npos, "halt outside loop");

    out = runCodeFresh("skip ~", ok);
    TEST_ASSERT(!ok && out.find("[Compiler Error]") != std::string::npos && out.find("Cannot use 'skip' outside of a loop") != std::string::npos, "skip outside loop");

    out = runCodeFresh("task foo() { halt ~ } ~", ok);
    TEST_ASSERT(!ok && out.find("[Compiler Error]") != std::string::npos && out.find("Cannot use 'halt' outside of a loop") != std::string::npos, "halt inside task");

    // valid halt/skip inside while loop
    out = runCodeFresh("let i = 0 ~ while (i < 3) { i = i + 1 ~ if (i == 2) { halt ~ } } echo i ~", ok);
    TEST_ASSERT(ok && out.find("=> 2") != std::string::npos, "valid halt in loop");
}

static void testGrabAndVMState() {
    bool ok = false;
    std::string out;

    // Missing file
    out = runCodeFresh("grab \"non_existent_file_12345.kek\" ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos && out.find("Could not open grab file") != std::string::npos, "grab missing file");

    // File with syntax error
    {
        std::ofstream badFile("temp_bad_syntax.kek");
        badFile << "let x = 10"; // missing tilde
        badFile.close();
    }
    out = runCodeFresh("grab \"temp_bad_syntax.kek\" ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos && out.find("Could not compile grab file") != std::string::npos, "grab syntax error file");
    std::remove("temp_bad_syntax.kek");

    // File with runtime error protecting global state
    {
        std::ofstream badRuntimeFile("temp_bad_runtime.kek");
        badRuntimeFile << "let grabbedVar = 999 ~ let bad = 1 / 0 ~";
        badRuntimeFile.close();
    }
    VM vm;
    out = runCode(vm, "let initialGlobal = 123 ~ grab \"temp_bad_runtime.kek\" ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos, "grab runtime error file");

    // Check that grabbedVar was rolled back and initialGlobal remains intact
    out = runCode(vm, "echo initialGlobal ~", ok);
    TEST_ASSERT(ok && out.find("=> 123") != std::string::npos, "global VM state preserved after grab failure");
    out = runCode(vm, "echo grabbedVar ~", ok);
    TEST_ASSERT(ok && out.find("[Runtime Error]") != std::string::npos && out.find("Undefined variable") != std::string::npos, "partial grab globals rolled back");
    std::remove("temp_bad_runtime.kek");

    // Valid grab file
    {
        std::ofstream validFile("temp_valid.kek");
        validFile << "task addTwo(a, b) { give a + b ~ } let exportedVal = 42 ~";
        validFile.close();
    }
    out = runCode(vm, "grab \"temp_valid.kek\" ~ echo addTwo(10, 20) ~ echo exportedVal ~", ok);
    TEST_ASSERT(ok && out.find("=> 30") != std::string::npos && out.find("=> 42") != std::string::npos, "valid grab file import");
    std::remove("temp_valid.kek");
}

static void testReplReset() {
    bool ok = false;
    VM vm;

    // Cause error
    runCode(vm, "1 / 0 ~", ok);
    vm.resetStack();

    // Ensure state works cleanly afterwards
    std::string out = runCode(vm, "let y = 50 ~ echo y ~", ok);
    TEST_ASSERT(ok && out.find("=> 50") != std::string::npos, "REPL stack reset after error");
}

int main() {
    std::cout << "Running Kekno Regression Test Suite..." << std::endl;

    testNativeFunctions();
    testIndexing();
    testSyntaxErrorsAndLoopControls();
    testGrabAndVMState();
    testReplReset();

    std::cout << "Tests Passed: " << g_testsPassed << std::endl;
    std::cout << "Tests Failed: " << g_testsFailed << std::endl;

    return (g_testsFailed == 0) ? 0 : 1;
}
