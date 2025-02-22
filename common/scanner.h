#include <tree_sitter/parser.h>
#include <wctype.h>

enum TokenType {
  AUTOMATIC_SEMICOLON,
  TEMPLATE_FRAGMENT,
  TERNARY_QMARK,
  COMMENT_BLOCK_CONTENT,
  BINARY_OPERATORS,
  FUNCTION_SIGNATURE_AUTOMATIC_SEMICOLON, 
};

static void advance(TSLexer *lexer) { lexer->advance(lexer, false); }
static void skip(TSLexer *lexer) { lexer->advance(lexer, true); }

static bool scan_template_fragment(TSLexer *lexer) {
  lexer->result_symbol = TEMPLATE_FRAGMENT;
  for (bool has_content = false;; has_content = true) {
    lexer->mark_end(lexer);
    int lookahead = lexer->lookahead;
    if(iswspace(lookahead)){
      return has_content;
    }
    switch (lexer->lookahead) {
    case '`':
      return has_content;
    case '\0':
      return false;
    case '$':
      advance(lexer);
      if (lexer->lookahead == '{') return has_content;
      break;
    case '\\':
      return has_content;
    default:
      advance(lexer);
    }
  }
}

static bool scan_whitespace_and_comments(TSLexer *lexer) {
  for (;;) {
    while (iswspace(lexer->lookahead)) {
      skip(lexer);
    }

    if (lexer->lookahead == '/') {
      skip(lexer);

      if (lexer->lookahead == '/') {
        skip(lexer);
        while (lexer->lookahead != 0 && lexer->lookahead != '\n') {
          skip(lexer);
        }
      } else if (lexer->lookahead == '*') {
        skip(lexer);
        while (lexer->lookahead != 0) {
          if (lexer->lookahead == '*') {
            skip(lexer);
            if (lexer->lookahead == '/') {
              skip(lexer);
              break;
            }
          } else {
            skip(lexer);
          }
        }
      } else {
        return false;
      }
    } else {
      return true;
    }
  }
}

static bool scan_automatic_semicolon(TSLexer *lexer, const bool *valid_symbols){
  lexer->result_symbol = AUTOMATIC_SEMICOLON;
  lexer->mark_end(lexer);

  for (;;) {
    if (lexer->lookahead == 0) return true;
    if (lexer->lookahead == '}') {
      // Automatic semicolon insertion breaks detection of object patterns
      // in a typed context:
      //   type F = ({a}: {a: number}) => number;
      // Therefore, disable automatic semicolons when followed by typing
      do {
        skip(lexer);
      } while (iswspace(lexer->lookahead));
      if (lexer->lookahead == ':') return false;
      return true;
    }
    if (!iswspace(lexer->lookahead)) return false;
    if (lexer->lookahead == '\n') break;
    skip(lexer);
  }

  skip(lexer);

  if (!scan_whitespace_and_comments(lexer)) return false;

  switch (lexer->lookahead) {
  case ',':
  case '.':
  case ';':
  case '*':
  case '%':
  case '>':
  case '<':
  case '=':
  case '?':
  case '^':
  case '|':
  case '&':
  case '/':
  case ':':
    return false;

  case '{':
    if (valid_symbols[FUNCTION_SIGNATURE_AUTOMATIC_SEMICOLON]) return false;
    break;

    // Don't insert a semicolon before a '[' or '(', unless we're parsing
    // a type. Detect whether we're parsing a type or an expression using
    // the validity of a binary operator token.
  case '(':
  case '[':
    if (valid_symbols[BINARY_OPERATORS]) return false;
    break;

    // Insert a semicolon before `--` and `++`, but not before binary `+` or `-`.
  case '+':
    skip(lexer);
    return lexer->lookahead == '+';
  case '-':
    skip(lexer);
    return lexer->lookahead == '-';

    // Don't insert a semicolon before `!=`, but do insert one before a unary `!`.
  case '!':
    skip(lexer);
    return lexer->lookahead != '=';

    // Don't insert a semicolon before `in` or `instanceof`, but do insert one
    // before an identifier.
  case 'i':
    skip(lexer);

    if (lexer->lookahead != 'n') return true;
    skip(lexer);

    if (!iswalpha(lexer->lookahead)) return false;

    for (unsigned i = 0; i < 8; i++) {
      if (lexer->lookahead != "stanceof"[i]) return true;
      skip(lexer);
    }

    if (!iswalpha(lexer->lookahead)) return false;
    break;
  }

  return true;
}

static bool scan_ternary_qmark(TSLexer *lexer) {
  for(;;) {
    if (!iswspace(lexer->lookahead)) break;
    skip(lexer);
  }

  if (lexer->lookahead == '?') {
    advance(lexer);

    if (lexer->lookahead == '?') return false;
    /* Optional chaining. */
    if (lexer->lookahead == '.') return false;

    /* TypeScript optional arguments contain the ?: sequence, possibly
       with whitespace. */
    for(;;) {
      if (!iswspace(lexer->lookahead)) break;
      skip(lexer);
    }
    if (lexer->lookahead == ':') return false;
    if (lexer->lookahead == ')') return false;
    if (lexer->lookahead == ',') return false;

    lexer->mark_end(lexer);
    lexer->result_symbol = TERNARY_QMARK;

    if (lexer->lookahead == '.') {
      advance(lexer);
      if (iswdigit(lexer->lookahead)) return true;
      return false;
    }
    return true;
  }
  return false;
}

static bool scan_comment_block_content(TSLexer *lexer) {
  lexer->result_symbol = COMMENT_BLOCK_CONTENT;
  for (bool has_content = false;; has_content = true) {
    lexer->mark_end(lexer);
    int lookahead = lexer->lookahead;
    switch (lookahead) {
      case '\0':
        return false;
      case '*':
        advance(lexer);
        if (lexer->lookahead == '/') return has_content;
        break;
      default:
        advance(lexer);
    }
  }
}

static inline bool external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid_symbols) {
  if (valid_symbols[TEMPLATE_FRAGMENT]) {
    if (valid_symbols[AUTOMATIC_SEMICOLON]) return false;
    return scan_template_fragment(lexer);
  } else if (
    valid_symbols[AUTOMATIC_SEMICOLON] ||
    valid_symbols[FUNCTION_SIGNATURE_AUTOMATIC_SEMICOLON]
             ) {
    bool ret = scan_automatic_semicolon(lexer, valid_symbols);
    if (!ret && valid_symbols[TERNARY_QMARK] && lexer->lookahead == '?')
      return scan_ternary_qmark(lexer);
    return ret;
  }
  if (valid_symbols[TERNARY_QMARK]) {
    return scan_ternary_qmark(lexer);
  }
  if (valid_symbols[COMMENT_BLOCK_CONTENT]) {
    return scan_comment_block_content(lexer);
  }

  return false;

}
