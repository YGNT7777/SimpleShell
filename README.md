1. raw input line
2. split by ';' → cmds[]
3. tokenize → Token**
4. expand + execute → char** argv internally


WANT TO DO
Input string
   ↓
Lexer (tokens)
   ↓
Parser
   ↓
AST (structured command tree)
   ↓
Executor (walk AST)
