#!/bin/bash

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SHELL_BIN="$SCRIPT_DIR/../SimpleShell"
LOG_FILE=$(mktemp "${TMPDIR:-/tmp}/simpleshell-tests.XXXXXX")
trap 'rm -f "$LOG_FILE"' EXIT

GREEN='\033[0;32m'
RED='\033[0;31m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

echo "=== SimpleShell Test Run: $(date) ===" > "$LOG_FILE"

total_tests=0
failed_tests=0

run_shell() {
    local input_command="$1"
    (cd "$SCRIPT_DIR" && printf '%s\n' "$input_command" | "$SHELL_BIN") 2>&1 |
        tr -d '\r' |
        awk -v command="$input_command" 'seen == 0 && index($0, command) { seen = 1; next } { print }'
}

assert_cmd() {
    local test_name="$1"
    local input_command="$2"
    local expected_output="$3"

    ((total_tests++))

    actual_output=$(run_shell "$input_command")
    
    local passed=true

    if [[ ! "$actual_output" =~ "$expected_output" ]]; then
        passed=false
    fi

    if [ "$passed" = true ]; then
        echo -e "${GREEN}[ PASS ]${NC} $test_name"
    else
        ((failed_tests++))
        echo -e "${RED}[ FAIL ]${NC} $test_name"
        {
            echo "-----------------------------------"
            echo "Test: $test_name"
            echo "Input: $input_command"
            echo "Expected string: $expected_output"
            echo "Actual Output:"
            echo "$actual_output"
            echo "-----------------------------------"
        } >> "$LOG_FILE"
    fi
}

assert_not_cmd() {
    local test_name="$1"
    local input_command="$2"
    local unexpected_output="$3"

    ((total_tests++))
    actual_output=$(run_shell "$input_command")

    if [[ "$actual_output" =~ $unexpected_output ]]; then
        ((failed_tests++))
        echo -e "${RED}[ FAIL ]${NC} $test_name"
        {
            echo "-----------------------------------"
            echo "Test: $test_name"
            echo "Input: $input_command"
            echo "Unexpected string: $unexpected_output"
            echo "Actual Output:"
            echo "$actual_output"
            echo "-----------------------------------"
        } >> "$LOG_FILE"
    else
        echo -e "${GREEN}[ PASS ]${NC} $test_name"
    fi
}

echo -e "${CYAN}=== Starting SimpleShell Regression Suite ===${NC}"

# ==============================================================================
# TEST CASES
# ==============================================================================

# 1. Pipeline Status & AND/OR Operator Logic
assert_cmd "Pipeline Success -> AND Execution" \
           "echo 'target_val' | grep 'target' && echo 'Success'" \
           "Success"

assert_not_cmd "Pipeline Failure -> AND Short-Circuit" \
           "echo 'wrong_val' | grep 'target' && echo 'ShouldNotPrint'" \
           "ShouldNotPrint"

assert_cmd "Pipeline Failure -> OR Execution" \
           "echo 'wrong_val' | grep 'target' || echo 'Fallback'" \
           "Fallback"

# 2. Subshell Isolation for Built-ins
assert_not_cmd "Subshell Isolation (Exit in Pipe)" \
           "echo 'stay alive' | exit" \
           "stay alive"

# 3. Environment Variable Expansion
assert_cmd "Environment Variable Expansion" \
           "export TEST_VAR=Hello && echo \$TEST_VAR" \
           "Hello"

# 4. Basic Wildcard Processing (Looks for Makefile in the project root)
assert_cmd "Wildcard Pattern Matching" \
           "ls ../Makefile*" \
           "Makefile"

# 5. Advanced Lexer & Operator Edge Cases

assert_cmd "Lexer: Logical OR Operator (||)" \
           "false || echo 'Fallback Works'" \
           "Fallback Works"

assert_cmd "Lexer: Background Operator (&) and Semicolon (;)" \
           "echo 'First' ; echo 'Second'" \
           "Second"

assert_cmd "Lexer: Output Redirection (>) and Append (>>)" \
           "echo 'Line 1' > test_out.txt && echo 'Line 2' >> test_out.txt && cat test_out.txt && rm test_out.txt" \
           "Line 2"

assert_cmd "Lexer: Input Redirection (<)" \
           "echo 'file contents' > test_in.txt && grep 'contents' < test_in.txt && rm test_in.txt" \
           "file contents"

assert_cmd "Lexer: Subshell Parentheses (Nested Groups)" \
           "(echo 'Inside Subshell')" \
           "Inside Subshell"

assert_cmd "Quoted wildcard remains literal" \
           "printf '%s\\n' '*'" \
           "*"

assert_cmd "Single quotes prevent variable expansion" \
           "echo '\$SIMPLE_SHELL_QUOTED'" \
           "\$SIMPLE_SHELL_QUOTED"

assert_cmd "Builtin output redirection" \
           "help > test_help.txt && grep 'SimpleShell' test_help.txt && rm test_help.txt" \
           "SimpleShell"

assert_cmd "Lexer: Unmatched Quote Error Handling" \
           "echo \"unmatched" \
           "Ssh: unmatched quote"

# 6. Command Substitution & Variable Engine Tests

assert_cmd "Command Substitution: Basic execution" \
           "echo \$(echo 'nested_output')" \
           "nested_output"

assert_cmd "Command Substitution: Balanced Parentheses Scanning" \
           "echo \$(echo '(inside)')" \
           "(inside)"

# 7. Parser Syntax Error Handling & Boundary Edge Cases

assert_cmd "Parser Error: Missing Input Redirection Target" \
           "cat <" \
           "Ssh: syntax error near unexpected token"

assert_cmd "Parser Error: Missing Output Redirection Target" \
           "echo 'hello' >" \
           "Ssh: syntax error near unexpected token"

assert_cmd "Parser Error: Missing Append Redirection Target" \
           "echo 'hello' >>" \
           "Ssh: syntax error near unexpected token"

assert_cmd "Parser Error: Subshell Missing Redirection Target" \
           "(echo 'test') >" \
           "Ssh: syntax error near unexpected token"

# 8. Sequence Control Structure Combinations

assert_cmd "Sequence Control: Semicolon Trailing Empty Statement" \
           "echo 'First'; " \
           "First"

assert_cmd "Sequence Control: Multiple Semicolons with Variable State" \
           "export VAR_VAL=Step1; echo \$VAR_VAL; export VAR_VAL=Step2; echo \$VAR_VAL" \
           "Step2"

assert_cmd "Operator Precedence: Mixed AND/OR Operations" \
           "true && false || echo 'OR executed'" \
           "OR executed"

# 9. Wildcard Glob Expansion

assert_cmd "Glob: Match Multiple C Files" \
           "ls ../src/*.c" \
           "executor.c" 

assert_cmd "Glob: Match Double Character Query" \
           "ls ../src/execut?r.c" \
           "executor.c"

assert_cmd "Glob: Match Double Character Query" \
           "ls ../src/execut??.c" \
           "executor.c"

assert_cmd "Glob: Fallback on No Match (GLOB_NOCHECK)" \
           "echo non_existent_file_*.txt" \
           "non_existent_file_*.txt"

# 10. Command Substitution $(...)

assert_cmd "Cmd Sub: Basic Stdout Capture" \
           "echo \$(echo 'subbed')" \
           "subbed"

assert_cmd "Cmd Sub: Strip Trailing Newlines" \
           "echo A\$(printf 'B\n\n\n')C" \
           "ABC"

assert_cmd "Cmd Sub: Nested Pipeline Capture" \
           "echo \$(echo 'hello world' | cut -d' ' -f1)" \
           "hello"


# 11. Environment Variable Parsing & Scope

assert_cmd "Env Var: Local Assignment and Retrieval" \
           "TEST_VAR=SimpleShell && echo \$TEST_VAR" \
           "SimpleShell"

assert_cmd "Env Var: Mixed Alphanumeric Keys with Text" \
           "USER_ID_9=42 && echo ID_\$USER_ID_9" \
           "ID_42"


# 12. Asynchronous Background Tasks

assert_cmd "Job Control: Spawning Asynchronous Process" \
           "sleep 1 & jobs" \
           "Running"

# 13. Programmable Tab Completion Contexts

assert_cmd "Builtin Execution: Help Output Verification" \
           "help" \
           "SimpleShell"

assert_cmd "Builtin Execution: Jobs State Verification" \
           "jobs" \
           "No current background jobs."

# 14. Arithmetic Expansion $((...))

assert_cmd "Arithmetic: Basic Addition" \
           "echo \$((1+2))" \
           "3"

assert_cmd "Arithmetic: Basic Subtraction" \
           "echo \$((10-3))" \
           "7"

assert_cmd "Arithmetic: Basic Multiplication" \
           "echo \$((5*6))" \
           "30"

assert_cmd "Arithmetic: Basic Division" \
           "echo \$((20/4))" \
           "5"

assert_cmd "Arithmetic: Basic Modulo" \
           "echo \$((20%6))" \
           "2"

assert_cmd "Arithmetic: Operator Precedence" \
           "echo \$((2+3*4))" \
           "14"

assert_cmd "Arithmetic: Parentheses Override Precedence" \
           "echo \$(((2+3)*4))" \
           "20"

assert_cmd "Arithmetic: Nested Parentheses" \
           "echo \$((2*(3+(4*5))))" \
           "46"

assert_cmd "Arithmetic: Unary Minus" \
           "echo \$((-5))" \
           "-5"

assert_cmd "Arithmetic: Unary Plus" \
           "echo \$((+5))" \
           "5"

assert_cmd "Arithmetic: Negative Expression" \
           "echo \$((-(3+2)))" \
           "-5"

assert_cmd "Arithmetic: Negative Multiplication" \
           "echo \$((-3*-4))" \
           "12"

assert_cmd "Arithmetic: Embedded In Word" \
           "echo A\$((2+3))B" \
           "A5B"

assert_cmd "Arithmetic: Multiple Expansions" \
           "echo \$((1+2)) \$((3+4))" \
           "3 7"

assert_cmd "Arithmetic: Multiple Embedded Expansions" \
           "echo X\$((1+2))Y\$((3+4))Z" \
           "X3Y7Z"

assert_cmd "Arithmetic: Whitespace Handling" \
           "echo \$(( 1 + 2 ))" \
           "3"

assert_cmd "Arithmetic: Whitespace With Parentheses" \
           "echo \$(( 2 * ( 3 + 4 ) ))" \
           "14"

assert_cmd "Arithmetic: Local Variable" \
           "NUM=5 && echo \$((NUM+3))" \
           "8"

assert_cmd "Arithmetic: Multiple Variables" \
           "A=10 && B=4 && echo \$((A*B))" \
           "40"

assert_cmd "Arithmetic: Variable Precedence" \
           "A=10 && B=4 && echo \$((A+B*2))" \
           "18"

assert_cmd "Arithmetic: Undefined Variable Defaults To Zero" \
           "echo \$((UNDEFINED+5))" \
           "5"

assert_cmd "Arithmetic: Long Expression" \
           "echo \$((1+2+3+4+5+6+7+8+9+10))" \
           "55"

assert_cmd "Arithmetic: Mixed Operators" \
           "echo \$(((1+2)*(3+4)-(5*6)+100/5))" \
           "11"

assert_cmd "Arithmetic: Division By Zero" \
           "echo \$((10/0))" \
           "0"

assert_cmd "Arithmetic: Local Variable" \
           "X=7 && echo \$((X+5))" \
           "12"

assert_cmd "Arithmetic: Local Variable In Word" \
           "VALUE=9 && echo result_\$((VALUE*2))" \
           "result_18"

# ==============================================================================
# SUMMARY
# ==============================================================================
echo ""
echo -e "${CYAN}=== Test Summary ===${NC}"
echo -e "Total Executed: $total_tests"
if [ "$failed_tests" -eq 0 ]; then
    echo -e "${GREEN}All tests passed successfully!${NC}"
else
    echo -e "${RED}Failed Tests: $failed_tests${NC}"
    echo -e "Failure details: ${CYAN}$LOG_FILE${NC}"
    trap - EXIT
fi
