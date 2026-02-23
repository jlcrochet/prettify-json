#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>

#include "lib/stdio_helpers.h"

static char space = ' ';
static int spaces = 2;
static bool skip_comments = false;

// Output buffer
static char *buf = NULL;
static size_t buf_len = 0;
static size_t buf_cap = 0;

static void buf_putc(char c)
{
	if (buf_len >= buf_cap) {
		if (buf_cap > SIZE_MAX / 2) {
			PUTS_ERR("error: out of memory\n");
			exit(1);
		}
		buf_cap = buf_cap ? buf_cap * 2 : 4096;
		char *new_buf = realloc(buf, buf_cap);
		if (!new_buf) {
			PUTS_ERR("error: out of memory\n");
			exit(1);
		}
		buf = new_buf;
	}
	buf[buf_len++] = c;
}

static void buf_indent(int indent)
{
	for (int i = 0; i < spaces * indent; ++i)
		buf_putc(space);
}

static void skip_line_comment(void)
{
	int c;
	while ((c = GETC()) != EOF && c != '\n');
}

static void skip_block_comment(void)
{
	int c;
	while ((c = GETC()) != EOF) {
		if (c == '*') {
			c = GETC();
			if (c == '/')
				return;
			if (c != EOF)
				UNGETC(c);
		}
	}
}

// Skip whitespace and comments (if enabled), return next meaningful char or EOF
static int next_char(void)
{
	int c;
	while ((c = GETC()) != EOF) {
		if (isspace(c))
			continue;
		if (skip_comments && c == '/') {
			int next = GETC();
			if (next == '/') {
				skip_line_comment();
				continue;
			} else if (next == '*') {
				skip_block_comment();
				continue;
			} else {
				if (next != EOF)
					UNGETC(next);
				return c;
			}
		}
		return c;
	}
	return EOF;
}

static bool parse_value(int indent);

static bool parse_string(void)
{
	int c;
	buf_putc('"');
	while ((c = GETC()) != EOF && c != '"') {
		buf_putc((char)c);
		if (c == '\\') {
			c = GETC();
			if (c == EOF) {
				PUTS_ERR("error: unterminated string\n");
				return false;
			}
			buf_putc((char)c);
		}
	}
	if (c == EOF) {
		PUTS_ERR("error: unterminated string\n");
		return false;
	}
	buf_putc('"');
	return true;
}

static bool parse_object(int indent)
{
	buf_putc('{');

	int c = next_char();
	if (c == '}') {
		buf_putc('}');
		return true;
	}

	indent++;
	buf_putc('\n');
	buf_indent(indent);

	while (true) {
		if (c != '"') {
			PUTS_ERR("error: expected string key in object\n");
			return false;
		}
		if (!parse_string())
			return false;

		c = next_char();
		if (c != ':') {
			PUTS_ERR("error: expected ':' after object key\n");
			return false;
		}
		buf_putc(':');
		buf_putc(' ');

		if (!parse_value(indent))
			return false;

		c = next_char();
		if (c == '}')
			break;
		if (c != ',') {
			PUTS_ERR("error: expected ',' or '}' in object\n");
			return false;
		}
		buf_putc(',');
		buf_putc('\n');
		buf_indent(indent);

		c = next_char();
	}

	indent--;
	buf_putc('\n');
	buf_indent(indent);
	buf_putc('}');
	return true;
}

static bool parse_array(int indent)
{
	buf_putc('[');

	int c = next_char();
	if (c == ']') {
		buf_putc(']');
		return true;
	}

	indent++;
	buf_putc('\n');
	buf_indent(indent);
	UNGETC(c);

	while (true) {
		if (!parse_value(indent))
			return false;

		c = next_char();
		if (c == ']')
			break;
		if (c != ',') {
			PUTS_ERR("error: expected ',' or ']' in array\n");
			return false;
		}
		buf_putc(',');
		buf_putc('\n');
		buf_indent(indent);
	}

	indent--;
	buf_putc('\n');
	buf_indent(indent);
	buf_putc(']');
	return true;
}

static bool parse_keyword(const char *expected)
{
	for (const char *p = expected; *p; p++) {
		int c = GETC();
		if (c != *p) {
			PRINTF_ERR("error: invalid keyword, expected '%s'\n", expected);
			return false;
		}
		buf_putc((char)c);
	}
	return true;
}

static bool parse_number(int first)
{
	int c = first;
	buf_putc((char)c);

	// JSON number grammar:
	// -? (0|[1-9][0-9]*) ([.][0-9]+)? ([eE][+-]?[0-9]+)?
	if (c == '-') {
		c = GETC();
			if (c == EOF || !isdigit(c)) {
				PUTS_ERR("error: invalid number\n");
				return false;
			}
			buf_putc((char)c);
	}

	if (c == '0') {
		c = GETC();
		if (c != EOF) {
			if (isdigit(c)) {
				PUTS_ERR("error: invalid number\n");
				return false;
			}
			UNGETC(c);
		}
		} else {
			while ((c = GETC()) != EOF && isdigit(c))
				buf_putc((char)c);
			if (c != EOF)
				UNGETC(c);
		}

	c = GETC();
	if (c == '.') {
		buf_putc('.');
		c = GETC();
			if (c == EOF || !isdigit(c)) {
				PUTS_ERR("error: invalid number\n");
				return false;
			}
			buf_putc((char)c);
			while ((c = GETC()) != EOF && isdigit(c))
				buf_putc((char)c);
			if (c != EOF)
				UNGETC(c);
		} else if (c != EOF) {
		UNGETC(c);
	}

		c = GETC();
		if (c == 'e' || c == 'E') {
			buf_putc((char)c);
			c = GETC();
			if (c == '+' || c == '-') {
				buf_putc((char)c);
				c = GETC();
			}
			if (c == EOF || !isdigit(c)) {
				PUTS_ERR("error: invalid number\n");
				return false;
			}
			buf_putc((char)c);
			while ((c = GETC()) != EOF && isdigit(c))
				buf_putc((char)c);
			if (c != EOF)
				UNGETC(c);
		} else if (c != EOF) {
		UNGETC(c);
	}

	return true;
}

static bool parse_value(int indent)
{
	int c = next_char();

	switch (c) {
		case EOF:
			PUTS_ERR("error: unexpected end of input\n");
			return false;
		case '{':
			return parse_object(indent);
		case '[':
			return parse_array(indent);
		case '"':
			return parse_string();
		case 't':
			buf_putc('t');
			return parse_keyword("rue");
		case 'f':
			buf_putc('f');
			return parse_keyword("alse");
		case 'n':
			buf_putc('n');
			return parse_keyword("ull");
		default:
			if (isdigit(c) || c == '-')
				return parse_number(c);
			PUTS_ERR("error: unexpected character\n");
			return false;
	}
}

int main(int argc, char **argv)
{
	struct option options[] = {
		{ "tabs", no_argument, 0, 't' },
		{ "size", required_argument, 0, 's' },
		{ "skip-comments", no_argument, 0, 'c' },
		{ "help", no_argument, 0, 'h' },
		{ 0 }
	};

	int c;
	bool custom_size = false;

	while ((c = getopt_long(argc, argv, "ts:ch", options, NULL)) != -1) {
		switch (c) {
			case '?':
				break;
			case 't':
				space = '\t';
				break;
			case 's': {
				char *end = NULL;
				errno = 0;
				long parsed = strtol(optarg, &end, 10);
				if (errno != 0 || end == optarg || *end != '\0' || parsed <= 0 || parsed > 32)
					spaces = 2;
				else
					spaces = (int)parsed;
				custom_size = true;
				break;
			}
			case 'c':
				skip_comments = true;
				break;
			case 'h':
				PUTS(
					"Usage: prettify-json\n"
					"\n"
					"-t, --tabs           Use tabs instead of spaces for indentation\n"
					"-s, --size N         Indent N characters per level (default: 2 spaces, 1 tab)\n"
					"-c, --skip-comments  Skip // and /* */ comments in input\n"
					"-h, --help           Print this help"
				);
				return EXIT_SUCCESS;
		}
	}

	if (optind < argc) {
		PUTS_ERR("Usage: prettify-json\n");
		return EXIT_FAILURE;
	}

	if (!custom_size && space == '\t')
		spaces = 1;

	struct stat st;
	if (fstat(STDIN_FILENO, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0) {
		size_t cap = (size_t)st.st_size + 1;
		buf = malloc(cap);
		if (!buf) {
			PUTS_ERR("error: out of memory\n");
			return EXIT_FAILURE;
		}
		buf_cap = cap;
	}

	// Check for empty input
	c = next_char();
	if (c == EOF) {
		PUTS_ERR("error: empty input\n");
		return EXIT_FAILURE;
	}
	UNGETC(c);

	if (!parse_value(0)) {
		free(buf);
		return EXIT_FAILURE;
	}

	// Check for trailing content
	c = next_char();
	if (c != EOF) {
		PUTS_ERR("error: unexpected content after JSON value\n");
		free(buf);
		return EXIT_FAILURE;
	}

	WRITE(buf, buf_len);
	PUTC('\n');
	free(buf);
	return EXIT_SUCCESS;
}
