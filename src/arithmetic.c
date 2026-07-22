#include "common.h"
#include <ctype.h>
#include "arithmetic.h"

// Forward declarations for recursive descent parser
static long long parse_expr(const char **p);
//static long long parse_term(const char **p); // COMMENTED FOR THE FUTURE
static long long parse_factor(const char **p);

static void skip_whitespace(const char **p) {
    while (**p && isspace((unsigned char)**p)) (*p)++;
}

char *expand_arithmetic(const char *word) {
      char result[1024] = "";
      const char *p = word;

      while (*p) {
	    if (strncmp(p, "$((", 3) == 0) {
		  p += 3;
		  const char *start = p;
		  int depth = 1;

		  while (*p && depth > 0) {
			if (*p == '(') depth++;
			else if (*p == ')') {
			      depth--;
			      break;
			}
			p++;
		  }

		  size_t expr_len = p - start;
		  char expr[512];
		  if (expr_len >= sizeof(expr)) expr_len = sizeof(expr) - 1;
		  strncpy(expr, start, expr_len);
		  expr[expr_len] = '\0';

		  // Evaluate expression
		  long long val = eval_arithmetic_expr(expr);

		  char val_buf[32];
		  snprintf(val_buf, sizeof(val_buf), "%lld", val);
		  strcat(result, val_buf);

		  if (*p == ')' && *(p + 1) == ')') p += 2; // skip closing ))
	    } else {
		  size_t len = strlen(result);
		  result[len] = *p;
		  result[len + 1] = '\0';
		  p++;
	    }
      }

      return strdup(result);
}

// Parses variables (e.g. VAR or $VAR) or integer literals
static long long parse_primary(const char **p) {
      skip_whitespace(p);

      if (**p == '(') {
	    (*p)++; // skip '('
	    long long val = parse_expr(p);
	    skip_whitespace(p);
	    if (**p == ')') (*p)++; // skip ')'
	    return val;
      }

      if (**p == '+' || **p == '-') {
	    int op = **p;
	    (*p)++;
	    long long val = parse_primary(p);
	    return (op == '-') ? -val : val;
      }

      // Handle Variable names or numbers
      if (isalpha((unsigned char)**p) || **p == '_' || **p == '$') {
	    if (**p == '$') (*p)++;
	    const char *start = *p;
	    while (isalnum((unsigned char)**p) || **p == '_') (*p)++;
	    size_t len = *p - start;
	    char varname[128];
	    if (len >= sizeof(varname)) len = sizeof(varname) - 1;
	    strncpy(varname, start, len);
	    varname[len] = '\0';

	    char *val_str = getenv(varname);
	    return val_str ? atoll(val_str) : 0;
      }

      // Number literal
      if (isdigit((unsigned char)**p)) {
	    long long val = 0;
	    while (isdigit((unsigned char)**p)) {
		  val = val * 10 + (**p - '0');
		  (*p)++;
	    }
	    return val;
      }	

      return 0;
}

// Factor: handles multiplication, division, and modulo (*, /, %)
static long long parse_factor(const char **p) {
      long long left = parse_primary(p);
      while (1) {
	    skip_whitespace(p);
	    if (**p == '*') {
		  (*p)++;
		  left *= parse_primary(p);
	    } else if (**p == '/') {
		  (*p)++;
		  long long denom = parse_primary(p);
		  left = (denom != 0) ? (left / denom) : 0; // Prevent divide by zero
	    } else if (**p == '%') {
		  (*p)++;
		  long long denom = parse_primary(p);
		  left = (denom != 0) ? (left % denom) : 0;
	    } else {
		  break;
	    }
      }
      return left;
}

// Expression: handles addition and subtraction (+, -)
static long long parse_expr(const char **p) {
      long long left = parse_factor(p);
      while (1) {
	    skip_whitespace(p);
	    if (**p == '+') {
		  (*p)++;
		  left += parse_factor(p);
	    } else if (**p == '-') {
		  (*p)++;
		  left -= parse_factor(p);
	    } else {
		  break;
	    }
      }
      return left;
}

long long eval_arithmetic_expr(const char *expr) {
      const char *p = expr;
      return parse_expr(&p);
}
