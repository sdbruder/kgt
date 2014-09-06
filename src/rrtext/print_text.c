/* $Id$ */

/*
 * RRD -> ASCII renderer
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "../xalloc.h"

#include "../rrd/rrd.h"
#include "../rrd/blist.h"

#include "io.h"

struct render_context {
	struct box_size size;
	char **lines;
	char *scratch;
	int rtl;
	int x, y;
};

static void
bprintf(struct render_context *ctx, const char *fmt, ...)
{
	va_list ap;
	int s;
	char *d;

	va_start(ap, fmt);
	s = vsprintf(ctx->scratch, fmt, ap);
	va_end(ap);
	d = ctx->lines[ctx->y] + ctx->x;

	memcpy(d, ctx->scratch, s);
}

static struct node_walker w_dimension, w_render;

static int
dim_nothing(struct node *n, struct node **np, int depth, void *arg)
{
	(void) np;
	(void) arg;
	(void) depth;

	n->size.w = 0;
	n->size.h = 1;
	n->y = 0;

	return 1;
}

static int
dim_identifier(struct node *n, struct node **np, int depth, void *arg)
{
	(void) np;
	(void) arg;
	(void) depth;

	n->size.w = strlen(n->u.identifier) + 2;
	n->size.h = 1;
	n->y = 0;

	return 1;
}

static int
dim_terminal(struct node *n, struct node **np, int depth, void *arg)
{
	(void) np;
	(void) arg;
	(void) depth;


	n->size.w = strlen(n->u.terminal) + 4;
	n->size.h = 1;
	n->y = 0;

	return 1;
}

static int
dim_sequence(struct node *n, struct node **np, int depth, void *arg)
{
	int w = 0, top = 0, bot = 1;
	struct node *p;

	(void) np;
	(void) arg;

	node_walk_list(&n->u.sequence, &w_dimension, depth + 1, arg);

	for (p = n->u.sequence; p != NULL; p = p->next) {
		w += p->size.w + 2;
		if (p->y > top) {
			top = p->y;
		}
		if (p->size.h - p->y > bot) {
			bot = p->size.h - p->y;
		}
	}

	n->size.w = w - 2;
	n->size.h = bot + top;
	n->y = top;

	return 1;
}

static int
dim_choice(struct node *n, struct node **np, int depth, void *arg)
{
	int w = 0, h = -1;
	struct node *p;

	(void) np;
	(void) arg;

	node_walk_list(&n->u.choice, &w_dimension, depth + 1, arg);

	for (p = n->u.choice; p != NULL; p = p->next) {
		h += 1 + p->size.h;

		if (p->size.w > w) {
			w = p->size.w;
		}

		if (p == n->u.choice) {
			if (p->type == NODE_SKIP && p->next && !p->next->next) {
				n->y = 2 + p->y + p->next->y;
			} else {
				n->y = p->y;
			}
		}
	}

	n->size.w = w + 6;
	n->size.h = h;

	return 1;
}

static int
dim_loop(struct node *n, struct node **np, int depth, void *arg)
{
	int wf, wb;

	(void) np;
	(void) arg;

	node_walk(&n->u.loop.forward, &w_dimension, depth + 1, arg);
	wf = n->u.loop.forward->size.w;

	node_walk(&n->u.loop.backward, &w_dimension, depth + 1, arg);
	wb = n->u.loop.backward->size.w;

	n->size.w = (wf > wb ? wf : wb) + 6;
	n->size.h = n->u.loop.forward->size.h + n->u.loop.backward->size.h + 1;
	n->y = n->u.loop.forward->y;

	return 1;
}

static struct node_walker w_dimension = {
	dim_nothing,
	dim_identifier, dim_terminal,
	dim_choice,     dim_sequence,
	dim_loop
};

static int
render_terminal(struct node *n, struct node **np, int depth, void *arg)
{
	struct render_context *ctx = arg;

	(void) np;
	(void) depth;

	bprintf(ctx, " \"%s\" ", n->u.terminal);

	return 1;
}

static int
render_identifier(struct node *n, struct node **np, int depth, void *arg)
{
	struct render_context *ctx = arg;

	(void) np;
	(void) depth;

	bprintf(ctx, " %s ", n->u.identifier);

	return 1;
}

static void
segment(struct render_context *ctx, struct node *n, int depth, int delim)
{
	int y = ctx->y;
	ctx->y -= n->y;
	node_walk(&n, &w_render, depth, ctx);

	ctx->x += n->size.w;
	ctx->y = y;
	if (delim) {
		bprintf(ctx, "--");
		ctx->x += 2;
	}
}

static int
render_sequence(struct node *n, struct node **np, int depth, void *arg)
{
	/* ->-item1->-item2 */
	struct render_context *ctx = arg;
	struct node *p;
	int x = ctx->x, y = ctx->y;

	(void) np;

	ctx->y += n->y;
	if (!ctx->rtl) {
		for (p = n->u.sequence; p != NULL; p = p->next) {
			segment(ctx, p, depth + 1, !!p->next);
		}
	} else {
		struct bnode *rl;

		rl = NULL;

		for (p = n->u.sequence; p != NULL; p = p->next) {
			b_push(&rl, p);
		}

		while (b_pop(&rl, &p)) {
			segment(ctx, p, depth + 1, !!rl);
		}
	}

	ctx->x = x;
	ctx->y = y;

	return 1;
}

static void
justify(struct render_context *ctx, int depth, struct node *n, int space, int arrows)
{
	int x = ctx->x;
	int off = (space - n->size.w) / 2;
	int arrow = off / 2;
	const char *as = !arrows ? "-" : ctx->rtl ? "<" : ">";

	for (; ctx->x < x + off; ctx->x++) {
		bprintf(ctx, ctx->x - x != arrow ? "-" : as);
	}

	ctx->y -= n->y;
	node_walk(&n, &w_render, depth, ctx);

	ctx->y += n->y;
	ctx->x += n->size.w;
	for (; ctx->x < x + space; ctx->x++) {
		bprintf(ctx, space - 1 - ctx->x + x != arrow ? "-" : as);
	}

	ctx->x = x;
}

static int
render_choice(struct node *n, struct node **np, int depth, void *arg)
{
	struct render_context *ctx = arg;
	struct node *p;
	int x = ctx->x, y = ctx->y;
	int line = y + n->y;
	char *a_in	= (n->y - n->u.choice->y) ? "v" : "^";
	char *a_out = (n->y - n->u.choice->y) ? "^" : "v";

	ctx->y += n->u.choice->y;

	(void) np;

	for (p = n->u.choice; p != NULL; p = p->next) {
		int i, flush = ctx->y == line;

		ctx->x = x;
		if (!ctx->rtl) {
			bprintf(ctx, flush ? a_out : ">");
		} else {
			bprintf(ctx, flush ? "<" : a_in);
		}

		ctx->x += 1;
		justify(ctx, depth + 1, p, n->size.w - 2, 0);

		ctx->x = x + n->size.w - 1;
		if (!ctx->rtl) {
			bprintf(ctx, flush ? ">" : a_in);
		} else {
			bprintf(ctx, flush ? a_out : "<");
		}
		ctx->y++;

		if (p->next) {
			for (i = 0; i < p->size.h - p->y + p->next->y; i++) {
				ctx->x = x;
				bprintf(ctx, "|");
				ctx->x = x + n->size.w - 1;
				bprintf(ctx, "|");
				ctx->y++;
			}
			/*ctx->y -= p->next->y;*/
		}
	};

	ctx->x = x;
	ctx->y = y;

	return 1;
}

static int
render_loop(struct node *n, struct node **np, int depth, void *arg)
{
	struct render_context *ctx = arg;
	int x = ctx->x, y = ctx->y;
	int i;

	(void) np;

	ctx->y += n->y;
	bprintf(ctx, !ctx->rtl ? ">" : "v");
	ctx->x += 1;

	justify(ctx, depth + 1, n->u.loop.forward, n->size.w - 2, 0);
	ctx->x = x + n->size.w - 1;
	bprintf(ctx, !ctx->rtl ? "v" : "<");
	ctx->y++;

	for (i = 0; i < n->u.loop.forward->size.h - n->u.loop.forward->y + n->u.loop.backward->y; i++) {
		ctx->x = x;
		bprintf(ctx, "|");
		ctx->x = x + n->size.w - 1;
		bprintf(ctx, "|");
		ctx->y++;
	}

	ctx->x = x;
	bprintf(ctx, !ctx->rtl ? "^" : ">");
	ctx->x += 1;
	ctx->rtl = !ctx->rtl;
	justify(ctx, depth + 1, n->u.loop.backward, n->size.w - 2, 0);

	ctx->rtl = !ctx->rtl;
	ctx->x = x + n->size.w - 1;
	bprintf(ctx, !ctx->rtl ? "<" : "^");

	ctx->x = x;
	ctx->y = y;

	return 1;
}

static struct node_walker w_render = {
	0,
	render_terminal, render_identifier,
	render_choice,   render_sequence,
	render_loop
};

void
rrd_print_text(struct node **rrd)
{
	struct render_context ctx;
	int i;

	node_walk(rrd, &w_dimension, 0, 0);

	ctx.size = (**rrd).size;
	ctx.size.w += 8;

	ctx.lines = xmalloc(sizeof *ctx.lines * ctx.size.h + 1);
	for (i = 0; i < ctx.size.h; i++) {
		ctx.lines[i] = xmalloc(ctx.size.w + 1);
		memset(ctx.lines[i], ' ', ctx.size.w);
		ctx.lines[i][ctx.size.w] = '\0';
	}

	ctx.rtl = 0;
	ctx.x = ctx.y = 0;
	ctx.scratch = xmalloc(ctx.size.w + 1);

	ctx.y = (**rrd).y;
	bprintf(&ctx, "||--");

	ctx.x = ctx.size.w - 4;
	bprintf(&ctx, "--||");

	ctx.x = 4;
	ctx.y = 0;
	node_walk(rrd, &w_render, 0, &ctx);

	for (i = 0; i < ctx.size.h; i++) {
		printf("	%s\n", ctx.lines[i]);
		free(ctx.lines[i]);
	}

	free(ctx.lines);
}