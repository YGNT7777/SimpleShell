# SimpleShell

![Preview](preview.png)

**SimpleShell** is a Unix shell written in C that implements many of the core features found in modern Unix shells. The project focuses on process management, parsing, shell expansions, and Unix system programming while maintaining a clean and modular architecture.

## Architecture

SimpleShell follows a modular architecture inspired by traditional language processors:

- **Lexer**: Converts raw command input into tokens while handling operators, quotes, and shell syntax.
- **Parser**: Builds an Abstract Syntax Tree (AST) representing command structure, pipelines, logical operators, and redirections.
- **Expansion Engine**: Performs variable expansion, wildcard expansion, command substitution, and arithmetic expansion.
- **Executor**: Traverses the AST and manages process creation, pipelines, redirections, and job control.

## Lexer and command grammar

The lexer reads an input line from left to right and emits **words** and **operators**. Whitespace separates words unless it appears inside quotes. Operators do not need surrounding whitespace: `echo one|grep o` and `echo one | grep o` produce the same pipeline.

```text
line        := sequence
sequence    := logical ( (';' | '&') logical )*
logical     := pipeline ( ('&&' | '||') pipeline )*
pipeline    := primary ( '|' primary )*
primary     := command | '(' sequence ')' redirection*
command     := ( word | redirection )+
redirection := '<' word | '>' word | '>>' word
word        := unquoted-text | single-quoted-text | double-quoted-text
```

Operators are recognized as the following tokens:

| Token | Meaning |
| --- | --- |
| `|` | Pipe stdout from the command on the left to stdin on the right |
| `;` | Run the next command after the preceding command finishes |
| `&` | Run the preceding command in the background |
| `&&` | Run the right side only when the left side succeeds |
| `||` | Run the right side only when the left side fails |
| `<`, `>`, `>>` | Input, overwrite-output, and append-output redirection |
| `(`, `)` | Execute a grouped sequence in a subshell |

Quotes are removed before execution. Single-quoted words are literal: they do not perform variable, command, arithmetic, or wildcard expansion. Double-quoted words allow expansions but prevent wildcard expansion. Unquoted words may use `$VAR`, `$(command)`, `$((expression))`, `*`, `?`, and `[]` patterns. The lexer also keeps parentheses inside `$(...)` and `$((...))` together so nested substitutions are parsed as one word.

## Features
- Arithmetic Expressions
- Process Management
- Pipelines (|)
- Background execution (&)
- Job management
- Sequential execution (';')
- Logical operators (&& and ||)
- Input / Output Redirection
- Input redirection (<)
- Output redirection (>)
- Append redirection (>>)
- Shell Expansions
- Environment variable expansion ($VAR)
- Wildcard (glob) expansion (*, ?)
- Command substitution ($(...))
- Arithmetic expansion ($((...)))

## Additional Features
- Tab completion
- Automated regression test suite
- Modular codebase with separate lexer, parser, and executor components

## Building
make

## Running
./SimpleShell

## Testing
From the repository root, run:

```sh
./tests/run_tests.sh
```

The suite locates the executable relative to the script, so it can be run from any working directory. Failure logs are stored in a temporary file and its path is printed on failure.

## Contributions
Contributions are welcome!

If you'd like to contribute, feel free to open an issue or submit a pull request.

Please ensure that:

- New features include appropriate unit tests.
- All existing and new tests pass before submitting a pull request.
- The code follows the existing project style.
