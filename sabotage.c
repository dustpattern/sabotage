/**
 * sabotage.c
 *
 * This work belongs to the Public Domain. Everyone is free to use, modify,
 * republish, sell or give away this work without prior consent from anybody.
 *
 * This software is provided on an "AS IS" basis, without warranty of any kind.
 * Use at your own risk! Under no circumstances shall the author(s) or
 * contributor(s) be liable for damages resulting directly or indirectly from
 * the use or non-use of this documentation.
 */

// BeginNoSabotage

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sysexits.h>
#include <unistd.h>

/* Sabotage target */
struct sabot {
	char const * file;     // file, such as "src/main.c", or NULL for any
	char const * func;     // function, such as "main", or NULL for any
	double       prob;     // probablity range [0.0, 1.0]
	unsigned     lnoc;     // number of lines, or 0 for any
	unsigned     lnov[16]; // line numbers
	unsigned     cnt;      // # of matches
	unsigned     hit;      // # of failures
};

/* Token types */
enum tokenkind {
	TOK_ERR   = -1,
	TOK_LIT   =  0,       // function, file, or line number
	TOK_END   = ';',      // the end of a target (also '\0')
	TOK_PERC  = '%',
	TOK_PAR   = '(',      // actually "()"
	TOK_COLON = ':',
	TOK_COMMA = ',',
};

/* A token */
struct token {
	char const * s;        // start of token text
	char const * e;        // char immediately following token text
	enum tokenkind k;
};

/* List of sabotage targets */
static struct sabot sabotv[128];
static unsigned sabotc = 0;

/* Seed for the random number generator (controlled by SABOTAGE_SEED env) */
static unsigned int seed = 0;

/** Error number (controlled by SABOTAGE_ERRNO env)*/
static int eno = ENOMEM;

/* Buffer for storing file and function names */
static char strbuf[1024];
static char * strptr = strbuf;

/**
 * Skip whitespace.
 */
static inline const char *
skip(char const * s)
{
	while (*s && isspace(*s))
		s++;
	return s;
}

/**
 * Consume a token of the target specification.
 */
static enum tokenkind
next(char const ** const pp, struct token * const tok)
{
	char const sep[] = ";%:,()";
	enum tokenkind k;
	char const * s;
	char const * e;

	s = skip(*pp);
	e = s;
	switch (*s) {
	case '\0':
		k = TOK_END;
		break;
	case ';':
	case '%':
	case ':':
	case ',':
		k = *s;
		e = s + 1;
		break;
	case '(':
		if (s[1] != ')')
			k = TOK_ERR;
		else {
			k = *s;
			e = s + 2;
		}
		break;
	default:
		if (!isgraph(*s) || strchr(sep, *s))
			k = TOK_ERR;
		else {
			k = TOK_LIT;
			for (; *e && isgraph(*e) && !strchr(sep, *e); e++)
				continue;
		}
	}

	if (tok)
		*tok = k == TOK_ERR
		     ? (struct token) { s, s + 1, k }
		     : (struct token) { s, e, k };

	*pp = e;
	return k;
}

static int
setfile(struct sabot * const t, struct token const * const tok)
{
	size_t const len = tok->e - tok->s;
	size_t const need = len + 1;  // len + '\0'
	size_t const remaining = strbuf + sizeof(strbuf) - strptr;
	if (need > remaining)
		return ENOSPC;
	t->file = strptr;
	memcpy(strptr, tok->s, len);
	strptr += len;
	*strptr++ = '\0';
	return 0;
}

static int
setfunc(struct sabot * const t, struct token const * const tok)
{
	size_t const len = tok->e - tok->s;
	size_t const need = len + 1;  // len + '\0'
	size_t const remaining = strbuf + sizeof(strbuf) - strptr;
	if (need > remaining)
		return ENOSPC;
	t->func = strptr;
	memcpy(strptr, tok->s, len);
	strptr += len;
	*strptr++ = '\0';
	return 0;
}

static int
setprob(struct sabot * const t, struct token const * const tok)
{
	char buf[64];

	size_t const len = tok->e - tok->s;
	if (!len || len >= sizeof(buf))
		return EINVAL;

	memcpy(buf, tok->s, len);
	buf[len] = '\0';

	char * endptr = NULL;
	long long const ll = strtol(buf, &endptr, 10);
	if (*endptr || ll < 0 || ll > 100)
		return EINVAL;

	t->prob = (double) ll / 100.0;
	return 0;
}

static int
setline(struct sabot * const t, struct token const * const tok)
{
	char buf[64];

	if (t->lnoc >= sizeof(t->lnov) / sizeof(t->lnov[0]))
		return ENOSPC;

	size_t const len = tok->e - tok->s;
	if (!len || len >= sizeof(buf))
		return EINVAL;

	memcpy(buf, tok->s, len);
	buf[len] = '\0';

	char * endptr = NULL;
	long long const ll = strtol(buf, &endptr, 10);
	if (*endptr || ll <= 0 || ll > INT_MAX)
		return EINVAL;

	t->lnov[t->lnoc++] = ll;
	return 0;
}

/**
 * A shortcut for checking the return value of a function. used by
 * sabot_parse_one_target().
 */
#define CHECK(f, msg) \
  do { \
    switch (f) { \
    case 0: \
      break; \
    case ENOSPC: \
      errmsg = "too many entries"; \
      goto fail_syntax_err; \
    default: \
      errmsg = (msg); \
      goto fail_syntax_err; \
    } \
  } while (0)

/**
 * Expect a specific token next. Used by sabot_parse_one_target().
 */
#define EXPECT(pp, tokknd, tokptr) \
  do { \
    if (next((pp), (tokptr)) != (tokknd)) \
      goto fail_syntax_err; \
  } while (0)

/* Frequent error messages */
#define BAD_FILE_ERRMSG "invalid file name"
#define BAD_FUNC_ERRMSG "invalid function name"
#define BAD_LINE_ERRMSG "invalid line number"
#define BAD_PROB_ERRMSG "invalid probability value"

/**
 * Parse one sabotage target.
 */
static int
sabot_parse_one_target(
	char const   * const s,    // the SABOTAGE string
	char const  ** const pp,   // current parse location in SABOTAGE
	struct sabot * const t )
{
	// Grammar:
	//
	//   TARGET := [PROB] WHERE [ ";" TARGET ]
	//
	//   PROB   := literal "%"
	//
	//   WHERE  := FILE | FUNC
	//
	//   FILE   := literal [ ":" LINES | FUNC ]
	//
	//   LINES  := literal [ "," LINES ]
	//
	//   FUNC   := literal "()"
	//

	char const * errptr;  // points to error location in SABOTAGE string
	char const * errmsg;  // error message
	struct token tok;

	memset(t, 0, sizeof(*t));
	t->prob = 1.0;        // probability defaults to 100%

	/* To keep the code cleaner, we make use of a couple of macros: CHECK()
	 * and EXPECT(). These expand to a "goto fail_syntax_err" on error.
	 */

	/* probability, file, or function */
	errmsg = "expected probabilty, file name, or function";
	errptr = *pp;
	EXPECT(pp, TOK_LIT, &tok);
	switch (next(pp, NULL)) {  // \0 | ; | : | % | ()
	case TOK_END:   // file
		CHECK(setfile(t, &tok), BAD_FILE_ERRMSG);
		return 0;
	case TOK_COLON: // file:
		CHECK(setfile(t, &tok), BAD_FILE_ERRMSG);
		goto func_or_line;
	case TOK_PAR:   // func()
		CHECK(setfunc(t, &tok), BAD_FUNC_ERRMSG);
		goto eol;
	case TOK_PERC:  // prob% ...
		CHECK(setprob(t, &tok), BAD_PROB_ERRMSG);
		break;
	default:
		goto fail_syntax_err;
	}

	/* file name, a function name, or the end of target */
	errmsg = "expected file name or function";
	errptr = *pp;
	switch (next(pp, &tok)) {
	case TOK_END:
		return 0;
	case TOK_LIT:
		break;
	default:
		goto fail_syntax_err;
	}
	switch (next(pp, NULL)) {  // \0 | ; | () | :
	case TOK_END:
		CHECK(setfile(t, &tok), BAD_FILE_ERRMSG);
		return 0;
	case TOK_PAR:
		CHECK(setfunc(t, &tok), BAD_FUNC_ERRMSG);
		goto eol;
	case TOK_COLON:
		CHECK(setfile(t, &tok), BAD_FILE_ERRMSG);
		break;
	default:
		goto fail_syntax_err;
	}

func_or_line:
	/* function name or line number */
	errmsg = "expected function name or line number";
	errptr = *pp;
	EXPECT(pp, TOK_LIT, &tok);
	switch (next(pp, NULL)) {
	case TOK_PAR:    // func()
		CHECK(setfunc(t, &tok), BAD_FUNC_ERRMSG);
		goto eol;
	case TOK_END:    // line
		CHECK(setline(t, &tok), BAD_LINE_ERRMSG);
		return 0;
	case TOK_COMMA:  // line, line, ...
		CHECK(setline(t, &tok), BAD_LINE_ERRMSG);
		errmsg = "expected a line number";
	L:	errptr = *pp;
		EXPECT(pp, TOK_LIT, &tok);
		CHECK(setline(t, &tok), BAD_LINE_ERRMSG);
		switch (next(pp, NULL)) {
		case TOK_END:
			return 0;
		case TOK_COMMA:
			goto L;
		default:
			goto fail_syntax_err;
		}
	default:
		goto fail_syntax_err;
	}
eol:
	errmsg = "expected `;'";
	errptr = *pp;
	EXPECT(pp, TOK_END, NULL);
	return 0;

fail_syntax_err:;
	int const l = skip(errptr) - s;
	fprintf(stderr, "sabotage: syntax error: \"%s\"\n", s);
	fprintf(stderr, "sabotage: syntax error:  %*s^ %s\n", l, "", errmsg);
	return EINVAL;
}

/**
 * Parse all sabotage targets.
 */
static int
sabot_parse_all_targets(char const * s)
{
	char const * const s0 = s;
	size_t const max = sizeof(sabotv) / sizeof(sabotv[0]);
	struct sabot const * const T = sabotv + max;
	struct sabot * t = sabotv;
	int err;

	for (s = skip(s); *s && t < T; t++)
		if ((err = sabot_parse_one_target(s0, &s, t)))
			return err;
	if (*s) {
		fputs("sabotage: error: too many targets\n", stderr);
		return ENOSPC;
	}

	sabotc = t - sabotv;
	return 0;
}

static int
sabot_parse_seed(char const * const s, unsigned * const seed)
{
	char * endptr = NULL;
	long long const ll = strtoll(s, &endptr, 10);
	if (*endptr || ll < 0 || ll > UINT_MAX)
		return EINVAL;
	*seed = (unsigned) ll;
	return 0;
}

static int
sabot_parse_errno(char const * const s, int * const err)
{
	char * endptr = NULL;
	long long const ll = strtoll(s, &endptr, 10);
	if (*endptr || ll < INT_MIN || ll > INT_MAX)
		return EINVAL;
	*err = (int) ll;
	return 0;
}

/**
 * Initialization
 */
__attribute__ ((constructor))
void
sabot_init(void)
{
	char const * env = NULL;

	/* read targets */
	if ((env = getenv("SABOTAGE")) == NULL)
		return;
	if (sabot_parse_all_targets(env))
		_exit(64);
	if (!sabotc)
		return;

	/* seed the random number generator */
	if ((env = getenv("SABOTAGE_SEED")) == NULL) {
		struct timeval tv;
		gettimeofday(&tv, NULL);
		seed = tv.tv_sec * 1000000 + tv.tv_usec;
	}
	else if (sabot_parse_seed(env, &seed)) {
		fprintf(stderr, "sabotage: bad SABOTAGE_SEED: \"%s\"\n", env);
		return;
	}

	/* read error number */
	if ((env = getenv("SABOTAGE_ERRNO"))
	   && sabot_parse_errno(env, &eno)) {
		fprintf(stderr, "sabotage: bad SABOTAGE_ERRNO: \"%s\"\n", env);
		return;
	}

	fprintf(stderr, " __    __    __    __    __    __    __    __    __    __    __    __    __ \n"
	                "_\\/_  _\\/_  _\\/_  _\\/_  _\\/_  _\\/_  _\\/_  _\\/_  _\\/_  _\\/_  _\\/_  _\\/_  _\\/_\n"
	                "\\/\\/  \\/\\/  \\/\\/  \\/\\/  \\/\\/  \\/\\/  \\/\\/  \\/\\/  \\/\\/  \\/\\/  \\/\\/  \\/\\/  \\/\\/\n\n");

	fprintf(stderr, "Process %d is being sabotaged:\n", (int) getpid());
	fprintf(stderr, "\"%s\"\n\n", getenv("SABOTAGE"));
	fprintf(stderr, "Seed: %u\n", seed);
	fprintf(stderr, "Errno: %d\n", eno);
	fprintf(stderr, "\nTargets:\n");
	struct sabot const * t = sabotv;
	for (unsigned i = 0; i < sabotc; i++, t++) {
		fprintf(stderr, "  %3d%%", (int) (t->prob * 100.0));
		if (t->file)
			fprintf(stderr, " %s", t->file);
		if (t->func)
			fprintf(stderr, " %s()", t->func);
		if (t->lnoc)
			for (unsigned j = 0; j < t->lnoc; j++)
				fprintf(stderr, "%s%u",
				        j ? ", " : ": ",
				        t->lnov[j]);
		fputc('\n', stderr);
	}

	fprintf(stderr, " __    __    __    __    __    __    __    __    __    __    __    __    __ \n"
	                "_\\/_  _\\/_  _\\/_  _\\/_  _\\/_  _\\/_  _\\/_  _\\/_  _\\/_  _\\/_  _\\/_  _\\/_  _\\/_\n"
	                "\\/\\/  \\/\\/  \\/\\/  \\/\\/  \\/\\/  \\/\\/  \\/\\/  \\/\\/  \\/\\/  \\/\\/  \\/\\/  \\/\\/  \\/\\/\n\n");
}

/**
 * Do sabotage.
 */
int
__sabotage(
	char const   * const file,
	char const   * const func,
	unsigned int   const line )
{
	struct sabot * t = sabotv;
	for (unsigned i = 0; i < sabotc; i++, t++) {
		if (t->file && strcmp(file, t->file))
			continue;
		if (t->func && strcmp(func, t->func))
			continue;
		if (t->lnoc) {
			bool matched = false;
			for (unsigned j = 0; j < t->lnoc && !matched; j++)
				matched = line == t->lnov[j];
			if (!matched)
				continue;
		}

		t->cnt++;

		if (  t->prob == 0.0
		   || (  t->prob != 1.0
		      && rand_r(&seed) >= t->prob * ((double) RAND_MAX + 1)))
			return 0;

		t->hit++;

		fprintf(stderr,
		    "sabotage: hit: file `%s', line %u, func %s() [%d%%]\n",
		    file, line, func,
		    (int) (100.0 * t->hit / t->cnt));

		return eno;
	}

	return 0;
}

// EndNoSabotage
