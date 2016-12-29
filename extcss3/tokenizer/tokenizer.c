#include "../intern.h"
#include "../utils.h"
#include "preprocessor.h"
#include "context.h"
#include "tokenizer.h"

#include <string.h>

/* ==================================================================================================== */

/* HELPER */
static int _extcss3_cleanup_tokenizer(int ret, extcss3_intern *intern, bool token, bool ctxt);
static int _extcss3_next_char(extcss3_intern *intern);
static int _extcss3_token_add(extcss3_intern *intern, extcss3_token *token);

/* TOKEN FILLER */
static int _extcss3_fill_fixed_token(extcss3_intern *intern, extcss3_token *token, short int type, unsigned short int chars);
static int _extcss3_fill_ws_token(extcss3_intern *intern, extcss3_token *token);
static int _extcss3_fill_hash_token(extcss3_intern *intern, extcss3_token *token);
static int _extcss3_fill_at_token(extcss3_intern *intern, extcss3_token *token);
static int _extcss3_fill_comment_token(extcss3_intern *intern, extcss3_token *token);
static int _extcss3_fill_unicode_range_token(extcss3_intern *intern, extcss3_token *token);
static int _extcss3_fill_ident_like_token(extcss3_intern *intern, extcss3_token *token);
static int _extcss3_fill_url_token(extcss3_intern *intern, extcss3_token *token);
static int _extcss3_fill_string_token(extcss3_intern *intern, extcss3_token *token);
static int _extcss3_fill_number_token(extcss3_intern *intern, extcss3_token *token);

/* CONSUMER */
static int _extcss3_consume_escaped(extcss3_intern *intern);
static int _extcss3_consume_bad_url_remnants(extcss3_intern *intern);
static int _extcss3_consume_name(extcss3_intern *intern);

/* CHECKER */
static bool _extcss3_check_start_valid_escape(char *str);
static bool _extcss3_check_start_name(char *str);
static bool _extcss3_check_start_ident(char *str);
static bool _extcss3_check_start_number(char *str);
static bool _extcss3_check_is_name(char *str);

/* ==================================================================================================== */

int extcss3_tokenize(extcss3_intern *intern)
{
	extcss3_token *token;
	int i, ret;

	/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

	if ((intern == NULL) || (intern->copy.str == NULL)) {
		return EXTCSS3_ERR_NULL_PTR;
	} else if ((intern->base_token = token = extcss3_create_token()) == NULL) {
		return EXTCSS3_ERR_MEMORY;
	} else if ((intern->base_ctxt = intern->last_ctxt = extcss3_create_ctxt()) == NULL) {
		return _extcss3_cleanup_tokenizer(EXTCSS3_ERR_MEMORY, intern, true, false);
	}

	/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

	/**
	 * To be able to identify some token types we must be able to
	 * look forward in the "prepared" string. Therefor we "preload"
	 * "i" characters. The "reader" and "writer" pointers of the
	 * state machine run parallel but offset by "i" characters.
	 */
	for (i = 0; i < 5; i++) {
		if ((ret = extcss3_preprocess(intern)) != 0) {
			return _extcss3_cleanup_tokenizer(ret, intern, true, true);
		}
	}

	/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

	/* https://www.w3.org/TR/css-syntax-3/#consume-a-token */
	while ((intern->last_token == NULL) || (intern->last_token->type != EXTCSS3_TYPE_EOF)) {
		if (EXTCSS3_IS_WS(*intern->state.reader)) {
			if ((ret = _extcss3_fill_ws_token(intern, token)) != 0) {
				return _extcss3_cleanup_tokenizer(ret, intern, true, true);
			}
		} else if ((*intern->state.reader == '"') || (*intern->state.reader == '\'')) {
			if ((ret = _extcss3_fill_string_token(intern, token)) != 0) {
				return _extcss3_cleanup_tokenizer(ret, intern, true, true);
			}
		} else if (*intern->state.reader == '#') {
			if (
				(EXTCSS3_SUCCESS == _extcss3_check_is_name(intern->state.reader + 1)) ||
				(EXTCSS3_SUCCESS == _extcss3_check_start_valid_escape(intern->state.reader + 1))
			) {
				if ((ret = _extcss3_fill_hash_token(intern, token)) != 0) {
					return _extcss3_cleanup_tokenizer(ret, intern, true, true);
				}
			} else {
				if ((ret = _extcss3_fill_fixed_token(intern, token, EXTCSS3_TYPE_DELIM, 1)) != 0) {
					return _extcss3_cleanup_tokenizer(ret, intern, true, true);
				}
			}
		} else if (*intern->state.reader == '$') {
			if (intern->state.reader[1] == '=') {
				if ((ret = _extcss3_fill_fixed_token(intern, token, EXTCSS3_TYPE_SUFFIX_MATCH, 2)) != 0) {
					return _extcss3_cleanup_tokenizer(ret, intern, true, true);
				}
			} else {
				if ((ret = _extcss3_fill_fixed_token(intern, token, EXTCSS3_TYPE_DELIM, 1)) != 0) {
					return _extcss3_cleanup_tokenizer(ret, intern, true, true);
				}
			}
		} else if (*intern->state.reader == '(') {
			if ((ret = _extcss3_fill_fixed_token(intern, token, EXTCSS3_TYPE_BR_RO, 1)) != 0) {
				return _extcss3_cleanup_tokenizer(ret, intern, true, true);
			}
		} else if (*intern->state.reader == ')') {
			if ((ret = _extcss3_fill_fixed_token(intern, token, EXTCSS3_TYPE_BR_RC, 1)) != 0) {
				return _extcss3_cleanup_tokenizer(ret, intern, true, true);
			}
		} else if (*intern->state.reader == '[') {
			if ((ret = _extcss3_fill_fixed_token(intern, token, EXTCSS3_TYPE_BR_SO, 1)) != 0) {
				return _extcss3_cleanup_tokenizer(ret, intern, true, true);
			}
		} else if (*intern->state.reader == ']') {
			if ((ret = _extcss3_fill_fixed_token(intern, token, EXTCSS3_TYPE_BR_SC, 1)) != 0) {
				return _extcss3_cleanup_tokenizer(ret, intern, true, true);
			}
		} else if (*intern->state.reader == '{') {
			if ((ret = _extcss3_fill_fixed_token(intern, token, EXTCSS3_TYPE_BR_CO, 1)) != 0) {
				return _extcss3_cleanup_tokenizer(ret, intern, true, true);
			}
		} else if (*intern->state.reader == '}') {
			if ((ret = _extcss3_fill_fixed_token(intern, token, EXTCSS3_TYPE_BR_CC, 1)) != 0) {
				return _extcss3_cleanup_tokenizer(ret, intern, true, true);
			}
		} else if (*intern->state.reader == ',') {
			if ((ret = _extcss3_fill_fixed_token(intern, token, EXTCSS3_TYPE_COMMA, 1)) != 0) {
				return _extcss3_cleanup_tokenizer(ret, intern, true, true);
			}
		} else if (*intern->state.reader == ':') {
			if ((ret = _extcss3_fill_fixed_token(intern, token, EXTCSS3_TYPE_COLON, 1)) != 0) {
				return _extcss3_cleanup_tokenizer(ret, intern, true, true);
			}
		} else if (*intern->state.reader == ';') {
			if ((ret = _extcss3_fill_fixed_token(intern, token, EXTCSS3_TYPE_SEMICOLON, 1)) != 0) {
				return _extcss3_cleanup_tokenizer(ret, intern, true, true);
			}
		} else if (*intern->state.reader == '*') {
			if (intern->state.reader[1] == '=') {
				if ((ret = _extcss3_fill_fixed_token(intern, token, EXTCSS3_TYPE_SUBSTR_MATCH, 2)) != 0) {
					return _extcss3_cleanup_tokenizer(ret, intern, true, true);
				}
			} else {
				if ((ret = _extcss3_fill_fixed_token(intern, token, EXTCSS3_TYPE_DELIM, 1)) != 0) {
					return _extcss3_cleanup_tokenizer(ret, intern, true, true);
				}
			}
		} else if ((*intern->state.reader == '+') || (*intern->state.reader == '.')) {
			if (EXTCSS3_SUCCESS == _extcss3_check_start_number(intern->state.reader)) {
				if ((ret = _extcss3_fill_number_token(intern, token)) != 0) {
					return _extcss3_cleanup_tokenizer(ret, intern, true, true);
				}
			} else {
				if ((ret = _extcss3_fill_fixed_token(intern, token, EXTCSS3_TYPE_DELIM, 1)) != 0) {
					return _extcss3_cleanup_tokenizer(ret, intern, true, true);
				}
			}
		} else if (*intern->state.reader == '-') {
			if (EXTCSS3_SUCCESS == _extcss3_check_start_number(intern->state.reader)) {
				if ((ret = _extcss3_fill_number_token(intern, token)) != 0) {
					return _extcss3_cleanup_tokenizer(ret, intern, true, true);
				}
			} else if (EXTCSS3_SUCCESS == _extcss3_check_start_ident(intern->state.reader)) {
				if ((ret = _extcss3_fill_ident_like_token(intern, token)) != 0) {
					return _extcss3_cleanup_tokenizer(ret, intern, true, true);
				}
			} else if ((intern->state.reader[1] == '-') && (intern->state.reader[2] == '>')) {
				if ((ret = _extcss3_fill_fixed_token(intern, token, EXTCSS3_TYPE_CDC, 3)) != 0) {
					return _extcss3_cleanup_tokenizer(ret, intern, true, true);
				}
			} else {
				if ((ret = _extcss3_fill_fixed_token(intern, token, EXTCSS3_TYPE_DELIM, 1)) != 0) {
					return _extcss3_cleanup_tokenizer(ret, intern, true, true);
				}
			}
		} else if (*intern->state.reader == '/') {
			if (intern->state.reader[1] == '*') {
				if ((ret = _extcss3_fill_comment_token(intern, token)) != 0) {
					return _extcss3_cleanup_tokenizer(ret, intern, true, true);
				}
			} else {
				if ((ret = _extcss3_fill_fixed_token(intern, token, EXTCSS3_TYPE_DELIM, 1)) != 0) {
					return _extcss3_cleanup_tokenizer(ret, intern, true, true);
				}
			}
		} else if (*intern->state.reader == '<') {
			if (intern->state.reader[1] == '!' && intern->state.reader[2] == '-' && intern->state.reader[3] == '-') {
				if ((ret = _extcss3_fill_fixed_token(intern, token, EXTCSS3_TYPE_CDO, 4)) != 0) {
					return _extcss3_cleanup_tokenizer(ret, intern, true, true);
				}
			} else {
				if ((ret = _extcss3_fill_fixed_token(intern, token, EXTCSS3_TYPE_DELIM, 1)) != 0) {
					return _extcss3_cleanup_tokenizer(ret, intern, true, true);
				}
			}
		} else if (*intern->state.reader == '@') {
			if (EXTCSS3_SUCCESS == _extcss3_check_start_ident(intern->state.reader + 1)) {
				if ((ret = _extcss3_fill_at_token(intern, token)) != 0) {
					return _extcss3_cleanup_tokenizer(ret, intern, true, true);
				}
			} else {
				if ((ret = _extcss3_fill_fixed_token(intern, token, EXTCSS3_TYPE_DELIM, 1)) != 0) {
					return _extcss3_cleanup_tokenizer(ret, intern, true, true);
				}
			}
		} else if (*intern->state.reader == '\\') {
			if (EXTCSS3_SUCCESS == _extcss3_check_start_valid_escape(intern->state.reader + 1)) {
				if ((ret = _extcss3_fill_ident_like_token(intern, token)) != 0) {
					return _extcss3_cleanup_tokenizer(ret, intern, true, true);
				}
			} else {
				if ((ret = _extcss3_fill_fixed_token(intern, token, EXTCSS3_TYPE_DELIM, 1)) != 0) {
					return _extcss3_cleanup_tokenizer(ret, intern, true, true);
				}
			}
		} else if (*intern->state.reader == '^') {
			if (intern->state.reader[1] == '=') {
				if ((ret = _extcss3_fill_fixed_token(intern, token, EXTCSS3_TYPE_PREFIX_MATCH, 2)) != 0) {
					return _extcss3_cleanup_tokenizer(ret, intern, true, true);
				}
			} else {
				if ((ret = _extcss3_fill_fixed_token(intern, token, EXTCSS3_TYPE_DELIM, 1)) != 0) {
					return _extcss3_cleanup_tokenizer(ret, intern, true, true);
				}
			}
		} else if (EXTCSS3_IS_DIGIT(*intern->state.reader)) {
			if ((ret = _extcss3_fill_number_token(intern, token)) != 0) {
				return _extcss3_cleanup_tokenizer(ret, intern, true, true);
			}
		} else if (EXTCSS3_CHARS_EQ(*intern->state.reader, 'u')) {
			if ((intern->state.reader[1] == '+') && ((intern->state.reader[2] == '?') || EXTCSS3_IS_HEX(intern->state.reader[2]))) {
				if ((ret = _extcss3_fill_unicode_range_token(intern, token)) != 0) {
					return _extcss3_cleanup_tokenizer(ret, intern, true, true);
				}
			} else {
				if ((ret = _extcss3_fill_ident_like_token(intern, token)) != 0) {
					return _extcss3_cleanup_tokenizer(ret, intern, true, true);
				}
			}
		} else if (EXTCSS3_SUCCESS == _extcss3_check_start_name(intern->state.reader)) {
			if ((ret = _extcss3_fill_ident_like_token(intern, token)) != 0) {
				return _extcss3_cleanup_tokenizer(ret, intern, true, true);
			}
		} else if (*intern->state.reader == '|') {
			if (intern->state.reader[1] == '=') {
				if ((ret = _extcss3_fill_fixed_token(intern, token, EXTCSS3_TYPE_DASH_MATCH, 2)) != 0) {
					return _extcss3_cleanup_tokenizer(ret, intern, true, true);
				}
			} else if (intern->state.reader[1] == '|') {
				if ((ret = _extcss3_fill_fixed_token(intern, token, EXTCSS3_TYPE_COLUMN, 2)) != 0) {
					return _extcss3_cleanup_tokenizer(ret, intern, true, true);
				}
			} else {
				if ((ret = _extcss3_fill_fixed_token(intern, token, EXTCSS3_TYPE_DELIM, 1)) != 0) {
					return _extcss3_cleanup_tokenizer(ret, intern, true, true);
				}
			}
		} else if (*intern->state.reader == '~') {
			if (intern->state.reader[1] == '=') {
				if ((ret = _extcss3_fill_fixed_token(intern, token, EXTCSS3_TYPE_INCLUDE_MATCH, 2)) != 0) {
					return _extcss3_cleanup_tokenizer(ret, intern, true, true);
				}
			} else {
				if ((ret = _extcss3_fill_fixed_token(intern, token, EXTCSS3_TYPE_DELIM, 1)) != 0) {
					return _extcss3_cleanup_tokenizer(ret, intern, true, true);
				}
			}
		} else if (*intern->state.reader == '\0') {
			if ((ret = _extcss3_fill_fixed_token(intern, token, EXTCSS3_TYPE_EOF, 1)) != 0) {
				return _extcss3_cleanup_tokenizer(ret, intern, true, true);
			}
		} else {
			if ((ret = _extcss3_fill_fixed_token(intern, token, EXTCSS3_TYPE_DELIM, 1)) != 0) {
				return _extcss3_cleanup_tokenizer(ret, intern, true, true);
			}
		}

		/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

		if ((ret = _extcss3_token_add(intern, token)) != 0) {
			return _extcss3_cleanup_tokenizer(ret, intern, true, true);
		}

		/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

		if (token->type == EXTCSS3_TYPE_EOF) {
			break;
		} else if ((token = extcss3_create_token()) == NULL) {
			return _extcss3_cleanup_tokenizer(EXTCSS3_ERR_MEMORY, intern, true, true);
		}
	}

	/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

	return _extcss3_cleanup_tokenizer(0, intern, false, true);
}

/* ==================================================================================================== */
/* HELPER */

static inline int _extcss3_cleanup_tokenizer(int ret, extcss3_intern *intern, bool token, bool ctxt)
{
	if (token) {
		extcss3_release_token(intern->base_token, true);
		intern->base_token = NULL;
	}

	if (ctxt) {
		extcss3_release_ctxt(intern->base_ctxt, true);
		intern->base_ctxt = NULL;
	}

	return ret;
}

static inline int _extcss3_next_char(extcss3_intern *intern)
{
	int ret;

	if ((ret = extcss3_preprocess(intern)) != 0) {
		return ret;
	} else if (*intern->state.reader != '\0') {
		intern->state.reader += extcss3_char_len(*intern->state.reader);
	}

	return 0;
}

static inline int _extcss3_token_add(extcss3_intern *intern, extcss3_token *token)
{
	extcss3_token *prev;
	int ret;

	if ((token->prev = intern->last_token) != NULL) {
		intern->last_token->next = token;
	}

	intern->last_token = token;

	// Mark the first @import <string> token as an <url> token
	if (token->type == EXTCSS3_TYPE_STRING) {
		prev = token->prev;

		while (prev) {
			if ((prev->type == EXTCSS3_TYPE_WS) || (prev->type == EXTCSS3_TYPE_COMMENT)) {
				prev = prev->prev;
				continue;
			} else if (prev->type != EXTCSS3_TYPE_AT_KEYWORD) {
				break;
			}

			if (prev->data.len == 7 /* strlen("@import") */) {
				if (memcmp(prev->data.str, "@import", 7) == 0) {
					token->type		= EXTCSS3_TYPE_URL;
					token->flag		= EXTCSS3_FLAG_AT_URL_STRING;
					token->info.str	= token->data.str;
					token->info.len	= 1;
				}
			}

			break;
		}
	}

	/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

	if (EXTCSS3_TYPE_IS_MODIFIABLE(token->type) && (intern->modifier.callback != NULL)) {
		intern->modifier.callback(intern);
	}

	/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

	if ((ret = extcss3_ctxt_update(intern)) != 0) {
		return ret;
	}

	/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

	return 0;
}

/* ==================================================================================================== */
/* TOKEN FILLER */

/**
 * Add one of the tokens with a fixed bytes length:
 * 
 * (1) EXTCSS3_TYPE_DELIM
 * (2) EXTCSS3_TYPE_INCLUDE_MATCH
 * (2) EXTCSS3_TYPE_DASH_MATCH
 * (2) EXTCSS3_TYPE_PREFIX_MATCH
 * (2) EXTCSS3_TYPE_SUFFIX_MATCH
 * (2) EXTCSS3_TYPE_SUBSTR_MATCH
 * (2) EXTCSS3_TYPE_COLUMN
 * (4) EXTCSS3_TYPE_CDO
 * (3) EXTCSS3_TYPE_CDC
 * (1) EXTCSS3_TYPE_COLON
 * (1) EXTCSS3_TYPE_SEMICOLON
 * (1) EXTCSS3_TYPE_COMMA
 * (1) EXTCSS3_TYPE_BR_RO
 * (1) EXTCSS3_TYPE_BR_RC
 * (1) EXTCSS3_TYPE_BR_SO
 * (1) EXTCSS3_TYPE_BR_SC
 * (1) EXTCSS3_TYPE_BR_CO
 * (1) EXTCSS3_TYPE_BR_CC
 * (1) EXTCSS3_TYPE_EOF
 */
static inline int _extcss3_fill_fixed_token(extcss3_intern *intern, extcss3_token *token, short int type, unsigned short int chars)
{
	int ret;

	token->type = type;
	token->data.str = intern->state.reader;

	if (token->type != EXTCSS3_TYPE_EOF) {
		while (chars-- > 0) {
			if ((ret = _extcss3_next_char(intern)) != 0) {
				return ret;
			}
		}
	}

	token->data.len = intern->state.reader - token->data.str;

	return 0;
}

/**
 * https://www.w3.org/TR/css-syntax-3/#consume-a-token (whitespace)
 */
static inline int _extcss3_fill_ws_token(extcss3_intern *intern, extcss3_token *token)
{
	int ret;

	token->type = EXTCSS3_TYPE_WS;
	token->data.str = intern->state.reader;

	while (EXTCSS3_IS_WS(*intern->state.reader)) {
		if ((ret = _extcss3_next_char(intern)) != 0) {
			return ret;
		}
	}

	token->data.len = intern->state.reader - token->data.str;

	return 0;
}

/**
 * https://www.w3.org/TR/css-syntax-3/#consume-a-token (U+0023 NUMBER SIGN (#))
 */
static inline int _extcss3_fill_hash_token(extcss3_intern *intern, extcss3_token *token)
{
	int ret;

	token->type = EXTCSS3_TYPE_HASH;
	token->flag = EXTCSS3_FLAG_UNRESTRICTED;
	token->data.str = intern->state.reader;

	if (EXTCSS3_SUCCESS == _extcss3_check_start_ident(intern->state.reader + 1)) {
		token->flag = EXTCSS3_FLAG_ID;
	}

	if ((ret = _extcss3_consume_name(intern)) != 0) {
		return ret;
	}

	token->data.len = intern->state.reader - token->data.str;

	return 0;
}

/**
 * https://www.w3.org/TR/css-syntax-3/#consume-a-token (U+0040 COMMERCIAL AT (@))
 */
static inline int _extcss3_fill_at_token(extcss3_intern *intern, extcss3_token *token)
{
	int ret;

	token->type = EXTCSS3_TYPE_AT_KEYWORD;
	token->data.str = intern->state.reader;

	if ((ret = _extcss3_consume_name(intern)) != 0) {
		return ret;
	}

	token->data.len = intern->state.reader - token->data.str;

	return 0;
}

/**
 * https://www.w3.org/TR/css-syntax-3/#consume-a-token (U+002F SOLIDUS (/))
 */
static inline int _extcss3_fill_comment_token(extcss3_intern *intern, extcss3_token *token)
{
	int ret;

	token->type = EXTCSS3_TYPE_COMMENT;
	token->data.str = intern->state.reader;

	// Consume '/' and '*'
	if ((ret = _extcss3_next_char(intern)) != 0) {
		return ret;
	}
	if ((ret = _extcss3_next_char(intern)) != 0) {
		return ret;
	}

	while ((*intern->state.reader != '\0') && ((*intern->state.reader != '*') || (intern->state.reader[1] != '/'))) {
		if ((ret = _extcss3_next_char(intern)) != 0) {
			return ret;
		}
	}

	// Consume '*' and '/'
	if (*intern->state.reader != '\0') {
		if ((ret = _extcss3_next_char(intern)) != 0) {
			return ret;
		}

		if (*intern->state.reader != '\0') {
			if ((ret = _extcss3_next_char(intern)) != 0) {
				return ret;
			}
		}
	}

	token->data.len = intern->state.reader - token->data.str;

	return 0;
}

/**
 * https://www.w3.org/TR/css-syntax-3/#consume-a-unicode-range-token
 */
static inline int _extcss3_fill_unicode_range_token(extcss3_intern *intern, extcss3_token *token)
{
	int ret;
	unsigned short int i, q;

	token->type = EXTCSS3_TYPE_UNICODE_RANGE;
	token->data.str = intern->state.reader;

	// Consume 'U' and '+'
	if ((ret = _extcss3_next_char(intern)) != 0) {
		return ret;
	}
	if ((ret = _extcss3_next_char(intern)) != 0) {
		return ret;
	}

	for (i = 0, q = 0; i < 6; i++) {
		if (!EXTCSS3_IS_HEX(*intern->state.reader)) {
			if (*intern->state.reader == '?') {
				q++;
			} else {
				break;
			}
		}

		if ((ret = _extcss3_next_char(intern)) != 0) {
			return ret;
		}
	}

	// !!! The intepretaion of the start and the end of the range is intentionally skipped !!!

	if (q || (*intern->state.reader != '-') || !EXTCSS3_IS_HEX(intern->state.reader[1])) {
		token->data.len = intern->state.reader - token->data.str;
	} else {
		if ((ret = _extcss3_next_char(intern)) != 0) {
			return ret;
		}

		for (i = 0; i < 6; i++) {
			if (!EXTCSS3_IS_HEX(*intern->state.reader)) {
				break;
			}

			if ((ret = _extcss3_next_char(intern)) != 0) {
				return ret;
			}
		}

		token->data.len = intern->state.reader - token->data.str;
	}

	return 0;
}

/**
 * https://www.w3.org/TR/css-syntax-3/#consume-an-ident-like-token
 */
static inline int _extcss3_fill_ident_like_token(extcss3_intern *intern, extcss3_token *token)
{
	int ret;

	token->data.str = intern->state.reader;

	if ((ret = _extcss3_consume_name(intern)) != 0) {
		return ret;
	}

	if (*intern->state.reader == '(') {
		if (
			((intern->state.reader - token->data.str) == 3) &&
			EXTCSS3_CHARS_EQ(token->data.str[0], 'u') &&
			EXTCSS3_CHARS_EQ(token->data.str[1], 'r') &&
			EXTCSS3_CHARS_EQ(token->data.str[2], 'l')
		) {
			return _extcss3_fill_url_token(intern, token);
		} else {
			token->type = EXTCSS3_TYPE_FUNCTION;
			token->data.len = intern->state.reader - token->data.str;

			// Consume the '(' after the function name
			if ((ret = _extcss3_next_char(intern)) != 0) {
				return ret;
			}
		}
	} else {
		token->type = EXTCSS3_TYPE_IDENT;
		token->data.len = intern->state.reader - token->data.str;
	}

	return 0;
}

/**
 * https://www.w3.org/TR/css-syntax-3/#consume-a-url-token
 */
static inline int _extcss3_fill_url_token(extcss3_intern *intern, extcss3_token *token)
{
	extcss3_token tmp;
	int ret;

	// Consume the '(' after "url"
	if ((ret = _extcss3_next_char(intern)) != 0) {
		return ret;
	}

	// Consume all leading whitespaces
	while (EXTCSS3_IS_WS(*intern->state.reader)) {
		if ((ret = _extcss3_next_char(intern)) != 0) {
			return ret;
		}
	}

	token->type = EXTCSS3_TYPE_URL;
	token->data.str = intern->state.reader;

	if (*intern->state.reader == '\0') {
		token->data.len = 0;
	} else if ((*intern->state.reader == '"') || (*intern->state.reader == '\'')) {
		if ((ret = _extcss3_fill_string_token(intern, &tmp)) != 0) {
			return ret;
		}

		if (tmp.type == EXTCSS3_TYPE_BAD_STRING) {
			token->type = EXTCSS3_TYPE_BAD_URL;

			if ((ret = _extcss3_consume_bad_url_remnants(intern)) != 0) {
				return ret;
			}

			token->data.len = intern->state.reader - token->data.str;
		} else {
			token->data.str = tmp.data.str;
			token->data.len = tmp.data.len;

			token->flag = EXTCSS3_FLAG_STRING;
			token->info.str = token->data.str;
			token->info.len = 1;

			// Consume all trailing whitespaces
			while (EXTCSS3_IS_WS(*intern->state.reader)) {
				if ((ret = _extcss3_next_char(intern)) != 0) {
					return ret;
				}
			}

			if ((*intern->state.reader == ')') || (*intern->state.reader == '\0')) {
				if ((ret = _extcss3_next_char(intern)) != 0) {
					return ret;
				}
			} else {
				token->type = EXTCSS3_TYPE_BAD_URL;

				if ((ret = _extcss3_consume_bad_url_remnants(intern)) != 0) {
					return ret;
				}

				token->data.len = intern->state.reader - token->data.str;
			}
		}
	} else {
		while (1) {
			token->data.len = intern->state.reader - token->data.str;

			if ((*intern->state.reader == ')') || (*intern->state.reader == '\0')) {
				return _extcss3_next_char(intern);
			} else if (EXTCSS3_IS_WS(*intern->state.reader)) {
				// Consume all trailing whitespaces
				while (EXTCSS3_IS_WS(*intern->state.reader)) {
					if ((ret = _extcss3_next_char(intern)) != 0) {
						return ret;
					}
				}

				if ((*intern->state.reader == ')') || (*intern->state.reader == '\0')) {
					return _extcss3_next_char(intern);
				} else {
					token->type = EXTCSS3_TYPE_BAD_URL;

					if ((ret = _extcss3_consume_bad_url_remnants(intern)) != 0) {
						return ret;
					}

					token->data.len = intern->state.reader - token->data.str;

					return 0;
				}
			} else if (
				(*intern->state.reader == '"')	||
				(*intern->state.reader == '\'')	||
				(*intern->state.reader == '(')	||
				EXTCSS3_NON_PRINTABLE(*intern->state.reader)
			) {
				token->type = EXTCSS3_TYPE_BAD_URL;

				if ((ret = _extcss3_consume_bad_url_remnants(intern)) != 0) {
					return ret;
				}

				token->data.len = intern->state.reader - token->data.str;

				return 0;
			} else if (*intern->state.reader == '\\') {
				if (EXTCSS3_SUCCESS == _extcss3_check_start_valid_escape(intern->state.reader)) {
					if ((ret = _extcss3_consume_escaped(intern)) != 0) {
						return ret;
					}
				} else {
					token->type = EXTCSS3_TYPE_BAD_URL;

					if ((ret = _extcss3_consume_bad_url_remnants(intern)) != 0) {
						return ret;
					}

					token->data.len = intern->state.reader - token->data.str;

					return 0;
				}
			} else {
				if ((ret = _extcss3_next_char(intern)) != 0) {
					return ret;
				}
			}
		}
	}

	return 0;
}

/**
 * https://www.w3.org/TR/css-syntax-3/#consume-a-string-token
 */
static inline int _extcss3_fill_string_token(extcss3_intern *intern, extcss3_token *token)
{
	extcss3_token tmp;
	int ret;

	tmp.data.str = intern->state.reader;

	// Consume the opening '"' or '\''
	if ((ret = _extcss3_next_char(intern)) != 0) {
		return ret;
	}

	token->type = EXTCSS3_TYPE_STRING;
	token->flag = EXTCSS3_FLAG_STRING;
	token->info.str = tmp.data.str;
	token->info.len = 1;

	while (true) {
		if ((*intern->state.reader == *tmp.data.str) || (*intern->state.reader == '\0')) {
			if (*intern->state.reader != '\0') {
				// Consume the closing '"' or '\''
				if ((ret = _extcss3_next_char(intern)) != 0) {
					return ret;
				}
			}

			break;
		} else if (*intern->state.reader == '\n') {
			token->type = EXTCSS3_TYPE_BAD_STRING;

			break;
		} else if (*intern->state.reader == '\\') {
			if (intern->state.reader[1] == '\0') {
				// Consume the '\\'
				if ((ret = _extcss3_next_char(intern)) != 0) {
					return ret;
				}

				break;
			} else if (intern->state.reader[1] == '\n') {
				if ((ret = _extcss3_next_char(intern)) != 0) {
					return ret;
				}
			} else if (EXTCSS3_SUCCESS == _extcss3_check_start_valid_escape(intern->state.reader)) {
				if ((ret = _extcss3_consume_escaped(intern)) != 0) {
					return ret;
				}

				continue; // Do not move the reader pointer to the next character
			}
		}

		if ((ret = _extcss3_next_char(intern)) != 0) {
			return ret;
		}
	}

	token->data.str = tmp.data.str;
	token->data.len = intern->state.reader - token->data.str;

	return 0;
}

/**
 * https://www.w3.org/TR/css-syntax-3/#consume-a-numeric-token
 */
static inline int _extcss3_fill_number_token(extcss3_intern *intern, extcss3_token *token)
{
	int ret;

	token->flag = EXTCSS3_FLAG_INTEGER;
	token->data.str = intern->state.reader;

	if ((*intern->state.reader == '+') || (*intern->state.reader == '-')) {
		if ((ret = _extcss3_next_char(intern)) != 0) {
			return ret;
		}
	}

	while (EXTCSS3_IS_DIGIT(*intern->state.reader)) {
		if ((ret = _extcss3_next_char(intern)) != 0) {
			return ret;
		}
	}

	if ((*intern->state.reader == '.') && EXTCSS3_IS_DIGIT(intern->state.reader[1])) {
		if ((ret = _extcss3_next_char(intern)) != 0) {
			return ret;
		}

		token->flag = EXTCSS3_FLAG_NUMBER;

		while (EXTCSS3_IS_DIGIT(*intern->state.reader)) {
			if ((ret = _extcss3_next_char(intern)) != 0) {
				return ret;
			}
		}
	}

	if (EXTCSS3_CHARS_EQ(*intern->state.reader, 'e')) {
		if (EXTCSS3_IS_DIGIT(intern->state.reader[1])) {
			if ((ret = _extcss3_next_char(intern)) != 0) {
				return ret;
			}

			token->flag = EXTCSS3_FLAG_NUMBER;

			while (EXTCSS3_IS_DIGIT(*intern->state.reader)) {
				if ((ret = _extcss3_next_char(intern)) != 0) {
					return ret;
				}
			}
		} else if ((((intern->state.reader[1] == '+') || (intern->state.reader[1] == '-'))) && EXTCSS3_IS_DIGIT(intern->state.reader[2])) {
			if ((ret = _extcss3_next_char(intern)) != 0) {
				return ret;
			}
			if ((ret = _extcss3_next_char(intern)) != 0) {
				return ret;
			}

			token->flag = EXTCSS3_FLAG_NUMBER;

			while (EXTCSS3_IS_DIGIT(*intern->state.reader)) {
				if ((ret = _extcss3_next_char(intern)) != 0) {
					return ret;
				}
			}
		}
	}

	// !!! The conversion to a number is intentionally skipped !!!

	if (EXTCSS3_SUCCESS == _extcss3_check_start_ident(intern->state.reader)) {
		token->type = EXTCSS3_TYPE_DIMENSION;
		token->info.str = intern->state.reader;

		if ((ret = _extcss3_consume_name(intern)) != 0) {
			return ret;
		}

		token->info.len = intern->state.reader - token->info.str;
	} else if (*intern->state.reader == '%') {
		token->type = EXTCSS3_TYPE_PERCENTAGE;
		token->info.str = intern->state.reader;

		// Consume the '%'
		if ((ret = _extcss3_next_char(intern)) != 0) {
			return ret;
		}

		token->info.len = intern->state.reader - token->info.str;
	} else {
		token->type = EXTCSS3_TYPE_NUMBER;
	}

	token->data.len = intern->state.reader - token->data.str;

	return 0;
}

/* ==================================================================================================== */
/* CONSUMER */

/**
 * https://www.w3.org/TR/css-syntax-3/#consume-an-escaped-code-point
 */
static inline int _extcss3_consume_escaped(extcss3_intern *intern)
{
	unsigned short int i;
	int ret, v;
	char hex[7];

	// Consume '\\'
	if ((ret = _extcss3_next_char(intern)) != 0) {
		return ret;
	} else if (!EXTCSS3_IS_HEX(*intern->state.reader)) {
		if ((ret = _extcss3_next_char(intern)) != 0) {
			return ret;
		}

		return 0;
	}

	for (i = 0; i < 6; i++) {
		if (!EXTCSS3_IS_HEX(*intern->state.reader)) {
			hex[i] = '\0';
			break;
		}

		hex[i] = *intern->state.reader;

		if ((ret = _extcss3_next_char(intern)) != 0) {
			return ret;
		}
	}

	if (EXTCSS3_IS_WS(*intern->state.reader)) {
		if ((ret = _extcss3_next_char(intern)) != 0) {
			return ret;
		}

		i++;
	}

	hex[6] = '\0';
	v = (int)strtol(hex, NULL, 16);

	if ((v <= 0) || (v > EXTCSS3_MAX_ALLOWED_CP) || EXTCSS3_FOR_SURROGATE_CP(v)) {
		if (i < EXTCSS3_REPLACEMENT_LEN) {
			memmove(intern->state.reader + EXTCSS3_REPLACEMENT_LEN - i, intern->state.reader, intern->state.writer - intern->state.reader + 1);
			memcpy(intern->state.reader - i, EXTCSS3_REPLACEMENT_CHR, EXTCSS3_REPLACEMENT_LEN);
			intern->state.writer += EXTCSS3_REPLACEMENT_LEN - i;
			intern->state.reader += EXTCSS3_REPLACEMENT_LEN - i;
		} else if (i > EXTCSS3_REPLACEMENT_LEN) {
			memcpy(intern->state.reader - i, EXTCSS3_REPLACEMENT_CHR, EXTCSS3_REPLACEMENT_LEN);
			memmove(intern->state.reader - i + EXTCSS3_REPLACEMENT_LEN, intern->state.reader, intern->state.writer - intern->state.reader + 1);
			intern->state.reader -= i-EXTCSS3_REPLACEMENT_LEN;
			intern->state.writer -= i-EXTCSS3_REPLACEMENT_LEN;
		} else {
			memcpy(intern->state.reader - i, EXTCSS3_REPLACEMENT_CHR, EXTCSS3_REPLACEMENT_LEN);
		}
	}

	return 0;
}

/**
 * https://www.w3.org/TR/css-syntax-3/#consume-the-remnants-of-a-bad-url
 */
static inline int _extcss3_consume_bad_url_remnants(extcss3_intern *intern)
{
	int ret;

	while (true) {
		if ((ret = _extcss3_next_char(intern)) != 0) {
			return ret;
		}

		if ((*intern->state.reader == ')') || (*intern->state.reader == '\0')) {
			return _extcss3_next_char(intern);
		} else if (EXTCSS3_SUCCESS == _extcss3_check_start_valid_escape(intern->state.reader)) {
			if ((ret = _extcss3_consume_escaped(intern)) != 0) {
				return ret;
			}
		}
	}

	return 0;
}

/**
 * https://www.w3.org/TR/css-syntax-3/#consume-a-name
 */
static inline int _extcss3_consume_name(extcss3_intern *intern)
{
	int ret;

	while (true) {
		if ((ret = _extcss3_next_char(intern)) != 0) {
			return ret;
		}

		if (EXTCSS3_SUCCESS == _extcss3_check_is_name(intern->state.reader)) {
			continue;
		} else if (EXTCSS3_SUCCESS == _extcss3_check_start_valid_escape(intern->state.reader)) {
			if ((ret = _extcss3_consume_escaped(intern)) != 0) {
				return ret;
			}
		} else {
			break;
		}
	}

	return 0;
}

/* ==================================================================================================== */
/* CHECKER */

/**
 * https://www.w3.org/TR/css-syntax-3/#starts-with-a-valid-escape
 */
static inline bool _extcss3_check_start_valid_escape(char *str)
{
	if ((*str != '\\') || (str[1] == '\n')) {
		return false;
	}

	return true;
}

/**
 * https://www.w3.org/TR/css-syntax-3/#name-start-code-point
 */
static inline bool _extcss3_check_start_name(char *str)
{
	if ((*str == '_') || EXTCSS3_IS_LETTER(*str) || (extcss3_char_len(*str) > 1)) {
		return true;
	}

	return false;
}

/**
 * https://www.w3.org/TR/css-syntax-3/#would-start-an-identifier
 */
static inline bool _extcss3_check_start_ident(char *str)
{
	if (*str == '-') {
		if (
			(EXTCSS3_SUCCESS == _extcss3_check_start_name(str + 1)) ||
			(EXTCSS3_SUCCESS == _extcss3_check_start_valid_escape(str + 1))
		) {
			return true;
		}

		return false;
	} else if (
		(EXTCSS3_SUCCESS == _extcss3_check_start_name(str)) ||
		(EXTCSS3_SUCCESS == _extcss3_check_start_valid_escape(str))
	) {
		return true;
	}

	return false;
}

/**
 * https://www.w3.org/TR/css-syntax-3/#starts-with-a-number
 */
static inline bool _extcss3_check_start_number(char *str)
{
	if ((*str == '+') || (*str == '-')) {
		if (EXTCSS3_IS_DIGIT(str[1])) {
			return true;
		} else if ((str[1] == '.') && EXTCSS3_IS_DIGIT(str[2])) {
			return true;
		}

		return false;
	} else if ((*str == '.') && EXTCSS3_IS_DIGIT(str[1])) {
		return true;
	} else if (EXTCSS3_IS_DIGIT(*str)) {
		return true;
	}

	return false;
}

/**
 * https://www.w3.org/TR/css-syntax-3/#name-code-point
 */
static inline bool _extcss3_check_is_name(char *str)
{
	if ((EXTCSS3_SUCCESS == _extcss3_check_start_name(str)) || EXTCSS3_IS_DIGIT(*str) || (*str == '-')) {
		return true;
	}

	return false;
}