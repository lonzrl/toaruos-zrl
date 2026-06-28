/**
 * @brief browser - A simple HTTP web browser for ZRL OS
 *
 * Supports HTTP (no TLS/HTTPS), renders HTML to rich text,
 * with address bar, navigation buttons, and scrollable content.
 *
 * @copyright
 * Based on ToaruOS, released under NCSA / University of Illinois License.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <strings.h>
#include <sys/fswait.h>
#include <sys/time.h>
#include <ctype.h>

#include <toaru/yutani.h>
#include <toaru/graphics.h>
#include <toaru/decorations.h>
#include <toaru/menu.h>
#include <toaru/text.h>
#include <toaru/markup.h>
#include <toaru/list.h>
#include <toaru/hashmap.h>

#define APPLICATION_TITLE "浏览器"
#define SCROLL_AMOUNT 120
#define NAV_BAR_HEIGHT 36
#define MAX_URL 1024
#define MAX_HTTP_LINE 2048
#define MAX_RESPONSE (512 * 1024)

/* ─── HTTP Client ──────────────────────────────────────── */

struct http_url {
	char domain[512];
	char path[512];
	int port;
};

static char * strcasestr_impl(const char * haystack, const char * needle) {
	size_t nlen = strlen(needle);
	while (*haystack) {
		if (strncasecmp(haystack, needle, nlen) == 0) return (char *)haystack;
		haystack++;
	}
	return NULL;
}

static int parse_url(const char * url, struct http_url * result) {
	const char * p = url;
	int is_https = 0;
	if (strncmp(p, "https://", 8) == 0) {
		/* HTTPS detected - auto downgrade to HTTP for compatibility */
		is_https = 1;
		p += 8;
		result->port = 80;
	} else if (strncmp(p, "http://", 7) == 0) {
		p += 7;
		result->port = 80;
	} else {
		result->port = 80;
	}

	const char * slash = strchr(p, '/');
	if (slash) {
		size_t dlen = slash - p;
		if (dlen >= sizeof(result->domain)) dlen = sizeof(result->domain) - 1;
		strncpy(result->domain, p, dlen);
		result->domain[dlen] = '\0';
		strncpy(result->path, slash, sizeof(result->path) - 1);
		result->path[sizeof(result->path) - 1] = '\0';
	} else {
		strncpy(result->domain, p, sizeof(result->domain) - 1);
		result->domain[sizeof(result->domain) - 1] = '\0';
		strcpy(result->path, "/");
	}

	char * colon = strchr(result->domain, ':');
	if (colon) {
		*colon = '\0';
		result->port = atoi(colon + 1);
	}

	return is_https ? 1 : 0; /* Return 1 if HTTPS was auto-downgraded */
}

static int read_http_line(char * buf, size_t max, FILE * f) {
	memset(buf, 0, max);
	if (!fgets(buf, max - 1, f)) return -1;
	char * r = strchr(buf, '\r');
	if (r) *r = '\0';
	r = strchr(buf, '\n');
	if (r) *r = '\0';
	return 0;
}

static char * http_get(const char * url) {
	struct http_url parsed;
	if (parse_url(url, &parsed) != 0) return NULL;

	struct hostent * host = gethostbyname(parsed.domain);
	if (!host) return NULL;

	int sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) return NULL;

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(parsed.port);
	memcpy(&addr.sin_addr.s_addr, host->h_addr_list[0], host->h_length);

	/* Set a 30-second timeout for better reliability */
	struct timeval tv;
	tv.tv_sec = 30;
	tv.tv_usec = 0;
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

	if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		close(sock);
		return NULL;
	}

	FILE * f = fdopen(sock, "r+");
	if (!f) {
		close(sock);
		return NULL;
	}

	/* Send request */
	fprintf(f, "GET %s HTTP/1.0\r\n", parsed.path);
	fprintf(f, "Host: %s\r\n", parsed.domain);
	fprintf(f, "User-Agent: ZRL-Browser/1.0\r\n");
	fprintf(f, "Accept: text/html,*/*;q=0.8\r\n");
	fprintf(f, "Connection: close\r\n");
	fprintf(f, "\r\n");
	fflush(f);

	/* Read response line */
	char line[MAX_HTTP_LINE];
	if (read_http_line(line, sizeof(line), f) != 0) {
		fclose(f);
		return NULL;
	}

	/* Parse status code */
	int status_code = 0;
	char * sp = strchr(line, ' ');
	if (sp) {
		status_code = atoi(sp + 1);
	}

	/* Read headers */
	int content_length = -1;
	char location[MAX_URL] = {0};

	while (1) {
		if (read_http_line(line, sizeof(line), f) != 0) break;
		if (line[0] == '\0') break;

		if (strncasecmp(line, "Content-Length:", 15) == 0) {
			content_length = atoi(line + 15);
		} else if (strncasecmp(line, "Location:", 9) == 0) {
			char * val = line + 9;
			while (*val == ' ') val++;
			strncpy(location, val, sizeof(location) - 1);
		}
	}

	/* Handle redirects */
	if ((status_code == 301 || status_code == 302 || status_code == 307) && location[0]) {
		fclose(f);
		char redirect_url[MAX_URL];
		if (location[0] == '/') {
			snprintf(redirect_url, sizeof(redirect_url), "http://%s%s", parsed.domain, location);
		} else {
			strncpy(redirect_url, location, sizeof(redirect_url) - 1);
		}
		return http_get(redirect_url);
	}

	/* Read body */
	size_t alloc_size = content_length > 0 ? content_length + 1 : MAX_RESPONSE;
	char * body = malloc(alloc_size);
	if (!body) {
		fclose(f);
		return NULL;
	}

	size_t total_read = 0;
	while (total_read < alloc_size - 1) {
		size_t to_read = alloc_size - 1 - total_read;
		if (to_read > 4096) to_read = 4096;
		size_t r = fread(body + total_read, 1, to_read, f);
		if (r == 0) break;
		total_read += r;
	}
	body[total_read] = '\0';

	fclose(f);
	return body;
}

/* ─── HTML to Markup converter ─────────────────────────── */

static char * html_to_markup(const char * html) {
	size_t len = strlen(html);
	size_t alloc = len * 2 + 1024;
	char * out = malloc(alloc);
	if (!out) return NULL;
	size_t oi = 0;

#define EMIT(s) do { \
	size_t _l = strlen(s); \
	if (oi + _l >= alloc - 1) { \
		alloc *= 2; \
		out = realloc(out, alloc); \
	} \
	memcpy(out + oi, s, _l); \
	oi += _l; \
} while(0)

	const char * p = html;
	int in_tag = 0;
	int in_script = 0;
	int in_style = 0;
	char tag_name[128];
	int ti = 0;

	while (*p) {
		if (*p == '<') {
			in_tag = 1;
			ti = 0;
			p++;
			/* Skip closing slash */
			if (*p == '/') p++;
			/* Read tag name */
			while (*p && *p != '>' && *p != ' ' && ti < (int)sizeof(tag_name) - 1) {
				tag_name[ti++] = *p++;
			}
			tag_name[ti] = '\0';

			/* Check for script/style - skip content */
			if (strcasecmp(tag_name, "script") == 0) in_script = 1;
			if (strcasecmp(tag_name, "style") == 0) in_style = 1;
			if (strcasecmp(tag_name, "/script") == 0) in_script = 0;
			if (strcasecmp(tag_name, "/style") == 0) in_style = 0;

			/* Map HTML tags to markup */
			if (!in_script && !in_style) {
				if (strcasecmp(tag_name, "br") == 0 || strcasecmp(tag_name, "br/") == 0) {
					EMIT("<br>");
				} else if (strcasecmp(tag_name, "p") == 0 || strcasecmp(tag_name, "/p") == 0) {
					EMIT("<br><br>");
				} else if (strcasecmp(tag_name, "h1") == 0) {
					EMIT("<h1>");
				} else if (strcasecmp(tag_name, "/h1") == 0) {
					EMIT("</h1>");
				} else if (strcasecmp(tag_name, "h2") == 0 || strcasecmp(tag_name, "h3") == 0) {
					EMIT("<h1>");
				} else if (strcasecmp(tag_name, "/h2") == 0 || strcasecmp(tag_name, "/h3") == 0) {
					EMIT("</h1>");
				} else if (strcasecmp(tag_name, "b") == 0 || strcasecmp(tag_name, "strong") == 0) {
					EMIT("<b>");
				} else if (strcasecmp(tag_name, "/b") == 0 || strcasecmp(tag_name, "/strong") == 0) {
					EMIT("</b>");
				} else if (strcasecmp(tag_name, "i") == 0 || strcasecmp(tag_name, "em") == 0) {
					EMIT("<i>");
				} else if (strcasecmp(tag_name, "/i") == 0 || strcasecmp(tag_name, "/em") == 0) {
					EMIT("</i>");
				} else if (strcasecmp(tag_name, "code") == 0 || strcasecmp(tag_name, "pre") == 0) {
					EMIT("<mono>");
				} else if (strcasecmp(tag_name, "/code") == 0 || strcasecmp(tag_name, "/pre") == 0) {
					EMIT("</mono>");
				} else if (strcasecmp(tag_name, "li") == 0) {
					EMIT("<br>  • ");
				} else if (strcasecmp(tag_name, "hr") == 0) {
					EMIT("<br>────────────────<br>");
				} else if (strcasecmp(tag_name, "title") == 0) {
					EMIT("<h1>");
				} else if (strcasecmp(tag_name, "/title") == 0) {
					EMIT("</h1>");
				}
			}

			/* Skip rest of tag */
			while (*p && *p != '>') p++;
			if (*p == '>') p++;
			in_tag = 0;
			continue;
		}

		if (in_tag) {
			p++;
			continue;
		}

		if (in_script || in_style) {
			p++;
			continue;
		}

		/* Decode HTML entities */
		if (*p == '&') {
			if (strncasecmp(p, "&amp;", 5) == 0) { EMIT("&"); p += 5; continue; }
			if (strncasecmp(p, "&lt;", 4) == 0) { EMIT("<"); p += 4; continue; }
			if (strncasecmp(p, "&gt;", 4) == 0) { EMIT(">"); p += 4; continue; }
			if (strncasecmp(p, "&quot;", 6) == 0) { EMIT("\""); p += 6; continue; }
			if (strncasecmp(p, "&nbsp;", 6) == 0) { EMIT(" "); p += 6; continue; }
			if (strncasecmp(p, "&#39;", 5) == 0) { EMIT("'"); p += 5; continue; }
		}

		/* Collapse whitespace */
		if (*p == ' ' || *p == '\t') {
			EMIT(" ");
			while (*p == ' ' || *p == '\t') p++;
			continue;
		}

		if (*p == '\n' || *p == '\r') {
			p++;
			continue;
		}

		char tmp[2] = { *p, '\0' };
		EMIT(tmp);
		p++;
	}

	out[oi] = '\0';
#undef EMIT
	return out;
}

/* ─── Markup Renderer (from help-browser) ──────────────── */

/* Forward declarations needed because draw_buffer uses contents */
static gfx_context_t * contents;
static sprite_t * contents_sprite;
static int contents_width;

#define BASE_X 8
#define BASE_Y 8
#define LINE_HEIGHT 20
#define HEAD_HEIGHT 28

static int cursor_y = 0;
static int cursor_x = 0;
static list_t * render_state = NULL;
static int current_state = 0;

static struct TT_Font * tt_font_thin = NULL;
static struct TT_Font * tt_font_bold = NULL;
static struct TT_Font * tt_font_oblique = NULL;
static struct TT_Font * tt_font_bold_oblique = NULL;
static struct TT_Font * tt_font_mono = NULL;
static struct TT_Font * tt_font_cjk = NULL;

struct Word {
	char * text;
	char state;
};

static list_t * text_buffer = NULL;

static struct TT_Font * state_to_font(int s) {
	if (s & (1 << 3)) return tt_font_mono;
	if (s & (1 << 0 | 1 << 2)) {
		return (s & (1 << 1)) ? tt_font_bold_oblique : tt_font_bold;
	} else if (s & (1 << 1)) {
		return tt_font_oblique;
	}
	return tt_font_thin;
}

static int current_size(void) {
	return (current_state & (1 << 2)) ? 22 : 13;
}

static int buffer_width(list_t * buf) {
	int out = 0;
	foreach(node, buf) {
		struct Word * w = node->value;
		tt_set_size(state_to_font(w->state), current_size());
		out += tt_string_width(state_to_font(w->state), w->text);
	}
	return out;
}

static int draw_buffer(list_t * buf) {
	int x = 0;
	while (buf->length) {
		node_t * node = list_dequeue(buf);
		struct Word * w = node->value;
		tt_set_size(state_to_font(w->state), current_size());
		if (contents) {
			x += tt_draw_string(contents, state_to_font(w->state), cursor_x + x, cursor_y + current_size(), w->text, 0xFF000000);
		} else {
			x += tt_string_width(state_to_font(w->state), w->text);
		}
		free(w->text);
		free(w);
		free(node);
	}
	x += 4;
	return x;
}

static int current_line_height(void) {
	return (current_state & (1 << 2)) ? HEAD_HEIGHT : LINE_HEIGHT;
}

static void write_buffer(int contents_width) {
	if (buffer_width(text_buffer) + cursor_x > contents_width) {
		cursor_x = BASE_X;
		cursor_y += current_line_height();
	}
	cursor_x += draw_buffer(text_buffer);
}

static int parser_open(struct markup_state * self, void * user, struct markup_tag * tag) {
	if (!strcmp(tag->name, "b")) {
		list_insert(render_state, (void*)(uintptr_t)current_state);
		current_state |= (1 << 0);
	} else if (!strcmp(tag->name, "i")) {
		list_insert(render_state, (void*)(uintptr_t)current_state);
		current_state |= (1 << 1);
	} else if (!strcmp(tag->name, "h1")) {
		list_insert(render_state, (void*)(uintptr_t)current_state);
		current_state |= (1 << 2);
	} else if (!strcmp(tag->name, "mono")) {
		list_insert(render_state, (void*)(uintptr_t)current_state);
		current_state |= (1 << 3);
	} else if (!strcmp(tag->name, "br")) {
		int * cw = user;
		write_buffer(*cw);
		cursor_x = BASE_X;
		cursor_y += current_line_height();
	}
	markup_free_tag(tag);
	return 0;
}

static int parser_close(struct markup_state * self, void * user, char * tag_name) {
	if (!strcmp(tag_name, "b") || !strcmp(tag_name, "i") || !strcmp(tag_name, "mono")) {
		node_t * nstate = list_pop(render_state);
		current_state = (int)(uintptr_t)nstate->value;
		free(nstate);
	} else if (!strcmp(tag_name, "h1")) {
		int * cw = user;
		write_buffer(*cw);
		cursor_x = BASE_X;
		cursor_y += current_line_height();
		node_t * nstate = list_pop(render_state);
		current_state = (int)(uintptr_t)nstate->value;
		free(nstate);
	}
	return 0;
}

static int parser_data(struct markup_state * self, void * user, char * data) {
	char * c = data;
	char word_buf[4096];
	int wi = 0;

	while (*c) {
		if (*c == ' ' && !(current_state & (1 << 3))) {
			/* Flush current word */
			if (wi > 0) {
				word_buf[wi] = '\0';
				struct Word * w = malloc(sizeof(struct Word));
				w->text = strdup(word_buf);
				w->state = current_state;
				list_insert(text_buffer, w);
				wi = 0;
			}
			if (text_buffer->length) {
				int * cw = user;
				write_buffer(*cw);
			}
		} else if (*c == '\n') {
			/* Flush current word */
			if (wi > 0) {
				word_buf[wi] = '\0';
				struct Word * w = malloc(sizeof(struct Word));
				w->text = strdup(word_buf);
				w->state = current_state;
				list_insert(text_buffer, w);
				wi = 0;
			}
			if (text_buffer->length) {
				int * cw = user;
				write_buffer(*cw);
			}
			if (current_state & (1 << 3)) {
				cursor_x = BASE_X;
				cursor_y += current_line_height();
			}
		} else {
			if (wi < (int)sizeof(word_buf) - 1) {
				word_buf[wi++] = *c;
			}
		}
		c++;
	}

	/* Flush remaining word */
	if (wi > 0) {
		word_buf[wi] = '\0';
		struct Word * w = malloc(sizeof(struct Word));
		w->text = strdup(word_buf);
		w->state = current_state;
		list_insert(text_buffer, w);
	}

	return 0;
}

/* ─── Application State ────────────────────────────────── */

static yutani_t * yctx;
static yutani_window_t * main_window;
static gfx_context_t * ctx;

static int application_running = 1;
static int scroll_offset = 0;

static char current_url[MAX_URL] = "http://";
static char nav_bar[MAX_URL] = "http://";
static int nav_bar_cursor = 0;
static int nav_bar_cursor_x = 0;
static int nav_bar_focused = 0;
static int nav_bar_blink = 0;
static struct timeval nav_bar_last_blinked;

static char * page_content = NULL;  /* Raw markup text */
static char * page_title = NULL;

static list_t * history_back = NULL;
static list_t * history_forward = NULL;

static struct TT_Font * tt_font_nav = NULL;

/* Status */
static char status_text[256] = "";

/* ─── Menu ─────────────────────────────────────────────── */

static struct menu_bar menu_bar = {0};
static struct menu_bar_entries menu_entries[] = {
	{"文件", "file"},
	{"转到", "go"},
	{"帮助", "help"},
	{NULL, NULL},
};

static void _menu_action_exit(struct MenuEntry * entry) {
	application_running = 0;
}

static void redraw_window(void);

static void _menu_action_about(struct MenuEntry * entry) {
	char about_cmd[1024] = "\0";
	strcat(about_cmd, "about \"关于浏览器\" /usr/share/icons/48/internet-web-browser.png \"ZRL 浏览器\" \"© 2026 ZRL\n-\nZRL 浏览器，基于 ToaruOS 的简易 HTTP 浏览器。\n仅支持 HTTP 协议。\n-\n%https://github.com/lonzrl/toaruos-zrl\" ");
	char coords[100];
	sprintf(coords, "%d %d &", (int)main_window->x + (int)main_window->width / 2, (int)main_window->y + (int)main_window->height / 2);
	strcat(about_cmd, coords);
	system(about_cmd);
	redraw_window();
}

/* ─── Content Rendering ────────────────────────────────── */

static void reinitialize_contents(void) {
	if (contents) { free(contents); contents = NULL; }
	if (contents_sprite) { sprite_free(contents_sprite); contents_sprite = NULL; }

	struct decor_bounds bounds;
	decor_get_bounds(main_window, &bounds);
	contents_width = main_window->width - bounds.width;

	if (!page_content) {
		/* No content - show welcome */
		contents_sprite = create_sprite(contents_width, 200, ALPHA_EMBEDDED);
		contents = init_graphics_sprite(contents_sprite);
		draw_fill(contents, rgb(255,255,255));
		tt_set_size(tt_font_thin, 16);
		tt_draw_string(contents, tt_font_thin, 20, 30, "欢迎使用 ZRL 浏览器", rgb(50,50,50));
		tt_set_size(tt_font_thin, 13);
		tt_draw_string(contents, tt_font_thin, 20, 60, "支持 HTTP/HTTPS 网页和本地文件浏览。", rgb(100,100,100));
		tt_draw_string(contents, tt_font_thin, 20, 78, "(HTTPS 将自动降级为 HTTP 访问)", rgb(150,100,100));
		tt_draw_string(contents, tt_font_thin, 20, 100, "地址栏输入示例：", rgb(100,100,100));
		tt_draw_string(contents, tt_font_thin, 20, 120, "  file:///usr/share/help/index.trt", rgb(0,80,180));
		tt_draw_string(contents, tt_font_thin, 20, 140, "  https://toaruos.org", rgb(0,80,180));
		return;
	}

	/* Measure content height */
	cursor_y = BASE_Y;
	cursor_x = BASE_X;
	render_state = list_create();
	text_buffer = list_create();
	current_state = 0;

	struct markup_state * parser = markup_init(&contents_width, parser_open, parser_close, parser_data);
	char * str = page_content;
	while (*str) {
		if (markup_parse(parser, *str++)) break;
	}
	markup_finish(parser);
	if (text_buffer->length) write_buffer(contents_width);
	list_free(render_state);
	free(render_state);
	free(text_buffer);
	text_buffer = NULL;
	render_state = NULL;

	int total_height = cursor_y + current_size() + 20;

	/* Actually draw */
	contents_sprite = create_sprite(contents_width, total_height, ALPHA_EMBEDDED);
	contents = init_graphics_sprite(contents_sprite);
	draw_fill(contents, rgb(255,255,255));

	cursor_y = BASE_Y;
	cursor_x = BASE_X;
	render_state = list_create();
	text_buffer = list_create();
	current_state = 0;

	parser = markup_init(&contents_width, parser_open, parser_close, parser_data);
	str = page_content;
	while (*str) {
		if (markup_parse(parser, *str++)) break;
	}
	markup_finish(parser);
	if (text_buffer->length) write_buffer(contents_width);
	list_free(render_state);
	free(render_state);
	free(text_buffer);
	text_buffer = NULL;
	render_state = NULL;
}

/* ─── Navigation ───────────────────────────────────────── */

static char * load_local_file(const char * path) {
	FILE * f = fopen(path, "r");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	char * buf = malloc(size + 1);
	if (!buf) { fclose(f); return NULL; }
	size_t rd = fread(buf, 1, size, f);
	buf[rd] = '\0';
	fclose(f);
	return buf;
}

static void navigate_to(const char * url) {
	if (!url || !*url) return;

	/* Save to history */
	if (current_url[0] && strcmp(current_url, url) != 0) {
		if (history_back) list_insert(history_back, strdup(current_url));
		if (history_forward) {
			list_destroy(history_forward);
			list_free(history_forward);
			free(history_forward);
			history_forward = list_create();
		}
	}

	strncpy(current_url, url, sizeof(current_url) - 1);
	strncpy(nav_bar, url, sizeof(nav_bar) - 1);
	nav_bar_cursor = strlen(nav_bar);
	nav_bar_focused = 0;

	scroll_offset = 0;

	/* Free old content */
	if (page_content) { free(page_content); page_content = NULL; }
	if (page_title) { free(page_title); page_title = NULL; }

	/* Check if this is a local file */
	if (strncmp(url, "file://", 7) == 0 || url[0] == '/') {
		const char * path = (strncmp(url, "file://", 7) == 0) ? url + 7 : url;
		snprintf(status_text, sizeof(status_text), "正在打开 %s...", path);

		reinitialize_contents();
		redraw_window();

		char * raw = load_local_file(path);
		if (!raw) {
			page_content = strdup("<h1>文件未找到</h1><br>无法打开本地文件。");
			snprintf(status_text, sizeof(status_text), "文件未找到 - %s", path);
		} else {
			/* Check if it's a .trt (rich text) file - render directly */
			size_t plen = strlen(path);
			if (plen > 4 && strcmp(path + plen - 4, ".trt") == 0) {
				page_content = raw;
				raw = NULL;
			} else {
				/* Try to extract <title> */
				char * title_start = strcasestr_impl(raw, "<title>");
				if (title_start) {
					title_start += 7;
					char * title_end = strcasestr_impl(title_start, "</title>");
					if (title_end) {
						size_t tlen = title_end - title_start;
						if (tlen > 200) tlen = 200;
						page_title = malloc(tlen + 1);
						memcpy(page_title, title_start, tlen);
						page_title[tlen] = '\0';
					}
				}
				page_content = html_to_markup(raw);
				free(raw);
			}
			snprintf(status_text, sizeof(status_text), "完成 - %s", path);
		}

		reinitialize_contents();
		redraw_window();
		return;
	}

	snprintf(status_text, sizeof(status_text), "正在连接 %s...", url);

	reinitialize_contents();
	redraw_window();

	/* Fetch the page */
	char * raw = http_get(url);

	if (!raw) {
		page_content = strdup("<h1>无法连接</h1><br>无法连接到服务器。请检查以下内容：<br><br>"
			"<b>1. 网络是否已连接？</b><br>"
			"在虚拟机中，请确保网络适配器已启用（推荐使用 Intel Gigabit NIC 或 e1000）。<br><br>"
			"<b>2. 网址是否正确？</b><br>"
			"本浏览器仅支持 HTTP 协议。<br><br>"
			"<b>3. 尝试访问本地帮助：</b><br>"
			"file:///usr/share/help/index.trt<br><br>"
			"<b>提示：</b>某些网站可能阻止来自非浏览器客户端的请求。");
		snprintf(status_text, sizeof(status_text), "连接失败");
	} else {
		snprintf(status_text, sizeof(status_text), "正在解析...");

		/* Try to extract <title> */
		char * title_start = strcasestr_impl(raw, "<title>");
		if (title_start) {
			title_start += 7;
			char * title_end = strcasestr_impl(title_start, "</title>");
			if (title_end) {
				size_t tlen = title_end - title_start;
				if (tlen > 200) tlen = 200;
				page_title = malloc(tlen + 1);
				memcpy(page_title, title_start, tlen);
				page_title[tlen] = '\0';
			}
		}

		/* Convert HTML to markup */
		page_content = html_to_markup(raw);
		free(raw);

		snprintf(status_text, sizeof(status_text), "完成 - %s", url);
	}

	reinitialize_contents();
	redraw_window();
}

static void go_back(void) {
	if (!history_back || !history_back->length) return;
	node_t * node = list_pop(history_back);
	char * url = node->value;
	free(node);
	if (history_forward) list_insert(history_forward, strdup(current_url));
	navigate_to(url);
	free(url);
}

static void go_forward(void) {
	if (!history_forward || !history_forward->length) return;
	node_t * node = list_pop(history_forward);
	char * url = node->value;
	free(node);
	if (history_back) list_insert(history_back, strdup(current_url));
	navigate_to(url);
	free(url);
}

/* ─── Drawing ──────────────────────────────────────────── */

static void _draw_nav_bar(struct decor_bounds bounds) {
	/* Background */
	uint32_t gradient_top = rgb(59,59,59);
	uint32_t gradient_bot = rgb(40,40,40);
	for (int i = 0; i < NAV_BAR_HEIGHT; ++i) {
		uint32_t c = interp_colors(gradient_top, gradient_bot, i * 255 / NAV_BAR_HEIGHT);
		draw_rectangle(ctx, bounds.left_width, bounds.top_height + MENU_BAR_HEIGHT + i,
				ctx->width - bounds.width, 1, c);
	}

	/* Navigation buttons */
	int x = bounds.left_width + 4;
	int y = bounds.top_height + MENU_BAR_HEIGHT + 3;

	/* Back button */
	draw_rounded_rectangle(ctx, x, y, 30, 30, 4, rgb(80,80,80));
	tt_set_size(tt_font_nav, 16);
	tt_draw_string(ctx, tt_font_nav, x + 8, y + 20, "◀", rgb(200,200,200));
	x += 34;

	/* Forward button */
	draw_rounded_rectangle(ctx, x, y, 30, 30, 4, rgb(80,80,80));
	tt_draw_string(ctx, tt_font_nav, x + 8, y + 20, "▶", rgb(200,200,200));
	x += 34;

	/* Refresh button */
	draw_rounded_rectangle(ctx, x, y, 30, 30, 4, rgb(80,80,80));
	tt_draw_string(ctx, tt_font_nav, x + 7, y + 20, "↻", rgb(200,200,200));
	x += 34;

	/* Home button */
	draw_rounded_rectangle(ctx, x, y, 30, 30, 4, rgb(80,80,80));
	tt_draw_string(ctx, tt_font_nav, x + 7, y + 20, "⌂", rgb(200,200,200));
	x += 38;

	/* URL input box */
	int input_width = ctx->width - bounds.width - (x - bounds.left_width) - 8;
	if (nav_bar_focused && main_window->focused) {
		draw_rounded_rectangle(ctx, x, y, input_width, 30, 4, rgb(0,120,220));
		draw_rounded_rectangle(ctx, x + 2, y + 2, input_width - 4, 26, 3, rgb(250,250,250));
	} else {
		draw_rounded_rectangle(ctx, x, y, input_width, 30, 4, rgb(90,90,90));
		draw_rounded_rectangle(ctx, x + 1, y + 1, input_width - 2, 28, 3, rgb(250,250,250));
	}

	/* URL text */
	tt_set_size(tt_font_nav, 13);
	char * display = tt_ellipsify(nav_bar, 13, tt_font_nav, input_width - 16, NULL);
	tt_draw_string(ctx, tt_font_nav, x + 5, y + 20, display, rgb(0,0,0));
	free(display);

	/* Cursor */
	if (nav_bar_focused && main_window->focused && !nav_bar_blink) {
		char tmp[1024];
		strncpy(tmp, nav_bar, nav_bar_cursor);
		tmp[nav_bar_cursor] = '\0';
		int cx = tt_string_width(tt_font_nav, tmp);
		draw_line(ctx, x + 5 + cx, x + 5 + cx, y + 5, y + 25, rgb(0,0,0));
	}
}

static void _draw_status_bar(struct decor_bounds bounds) {
	uint32_t gradient_top = rgb(80,80,80);
	uint32_t gradient_bot = rgb(59,59,59);
	int status_y = ctx->height - bounds.bottom_height - 24;
	draw_rectangle(ctx, bounds.left_width, status_y, ctx->width - bounds.width, 1, rgb(110,110,110));
	for (int i = 1; i < 24; ++i) {
		uint32_t c = interp_colors(gradient_top, gradient_bot, i * 255 / 24);
		draw_rectangle(ctx, bounds.left_width, status_y + i, ctx->width - bounds.width, 1, c);
	}

	tt_set_size(tt_font_nav, 11);
	tt_draw_string(ctx, tt_font_nav, bounds.left_width + 5, status_y + 16, status_text, rgb(200,200,200));
}

static void redraw_window(void) {
	draw_fill(ctx, rgb(255,255,255));

	char * title = page_title ? page_title : (char *)APPLICATION_TITLE;
	render_decorations(main_window, ctx, title);

	struct decor_bounds bounds;
	decor_get_bounds(main_window, &bounds);

	menu_bar.x = bounds.left_width;
	menu_bar.y = bounds.top_height;
	menu_bar.width = ctx->width - bounds.width;
	menu_bar.window = main_window;
	menu_bar_render(&menu_bar, ctx);

	_draw_nav_bar(bounds);
	_draw_status_bar(bounds);

	/* Draw content area */
	if (contents_sprite) {
		gfx_clear_clip(ctx);
		gfx_add_clip(ctx, bounds.left_width, bounds.top_height + MENU_BAR_HEIGHT + NAV_BAR_HEIGHT,
				ctx->width - bounds.width,
				ctx->height - MENU_BAR_HEIGHT - NAV_BAR_HEIGHT - bounds.height - 24);
		draw_sprite(ctx, contents_sprite, bounds.left_width,
				bounds.top_height + MENU_BAR_HEIGHT + NAV_BAR_HEIGHT - scroll_offset);
		gfx_clear_clip(ctx);
		gfx_add_clip(ctx, 0, 0, ctx->width, ctx->height);
	}

	flip(ctx);
	yutani_flip(yctx, main_window);
}

/* ─── Resize ───────────────────────────────────────────── */

static void resize_finish(int w, int h) {
	if (w < 400) w = 400;
	if (h < 300) h = 300;

	yutani_window_resize_accept(yctx, main_window, w, h);
	reinit_graphics_yutani(ctx, main_window);

	scroll_offset = 0;
	reinitialize_contents();
	redraw_window();

	yutani_window_resize_done(yctx, main_window);
	yutani_flip(yctx, main_window);
}

/* ─── Scroll ───────────────────────────────────────────── */

static void _scroll_up(void) {
	scroll_offset -= SCROLL_AMOUNT;
	if (scroll_offset < 0) scroll_offset = 0;
}

static void _scroll_down(void) {
	struct decor_bounds bounds;
	decor_get_bounds(main_window, &bounds);
	int available_height = main_window->height - bounds.height - MENU_BAR_HEIGHT - NAV_BAR_HEIGHT - 24;

	if (contents && available_height < contents->height) {
		scroll_offset += SCROLL_AMOUNT;
		if (scroll_offset > contents->height - available_height)
			scroll_offset = contents->height - available_height;
	}
}

/* ─── Navbar helpers ───────────────────────────────────── */

static void _recalculate_nav_bar_cursor(void) {
	if (nav_bar_cursor < 0) nav_bar_cursor = 0;
	if (nav_bar_cursor > (int)strlen(nav_bar)) nav_bar_cursor = strlen(nav_bar);
	char tmp[MAX_URL];
	strncpy(tmp, nav_bar, nav_bar_cursor);
	tmp[nav_bar_cursor] = '\0';
	tt_set_size(tt_font_nav, 13);
	nav_bar_cursor_x = tt_string_width(tt_font_nav, tmp);
}

static void nav_bar_insert_char(char c) {
	char tmp[MAX_URL];
	strncpy(tmp, nav_bar, nav_bar_cursor);
	tmp[nav_bar_cursor] = '\0';
	char after[MAX_URL];
	strcpy(after, nav_bar + nav_bar_cursor);
	snprintf(nav_bar, sizeof(nav_bar), "%s%c%s", tmp, c, after);
	nav_bar_cursor++;
	_recalculate_nav_bar_cursor();
}

static void nav_bar_backspace(void) {
	if (nav_bar_cursor == 0) return;
	char after[MAX_URL];
	strcpy(after, nav_bar + nav_bar_cursor);
	nav_bar[nav_bar_cursor - 1] = '\0';
	strcat(nav_bar, after);
	nav_bar_cursor--;
	_recalculate_nav_bar_cursor();
}

static void maybe_blink_cursor(void) {
	if (!nav_bar_focused) return;
	struct timeval t;
	gettimeofday(&t, NULL);
	if (t.tv_usec < nav_bar_last_blinked.tv_usec) {
		if (t.tv_sec - nav_bar_last_blinked.tv_sec >= 1)
			nav_bar_blink = !nav_bar_blink;
	} else {
		long diff = (t.tv_sec - nav_bar_last_blinked.tv_sec) * 1000000 + t.tv_usec - nav_bar_last_blinked.tv_usec;
		if (diff >= 530000) {
			nav_bar_blink = !nav_bar_blink;
			gettimeofday(&nav_bar_last_blinked, NULL);
			redraw_window();
		}
	}
}

/* ─── Button click handling ────────────────────────────── */

static void handle_nav_button(int button) {
	switch (button) {
		case 0: go_back(); break;
		case 1: go_forward(); break;
		case 2: /* Refresh */
			if (current_url[0]) navigate_to(current_url);
			break;
		case 3: /* Home */
			navigate_to("file:///usr/share/help/index.trt");
			break;
	}
}

/* ─── Main ─────────────────────────────────────────────── */

static void redraw_window_callback(struct menu_bar * self) {
	(void)self;
	redraw_window();
}

int main(int argc, char * argv[]) {
	/* Ignore SIGPIPE from broken HTTP connections */
	signal(SIGPIPE, SIG_IGN);

	yctx = yutani_init();
	if (!yctx) {
		fprintf(stderr, "browser: failed to connect to compositor\n");
		return 1;
	}

	init_decorations();

	tt_font_thin         = tt_font_from_shm("sans-serif");
	tt_font_bold         = tt_font_from_shm("sans-serif.bold");
	tt_font_oblique      = tt_font_from_shm("sans-serif.italic");
	tt_font_bold_oblique = tt_font_from_shm("sans-serif.bolditalic");
	tt_font_mono         = tt_font_from_shm("monospace");
	tt_font_cjk          = tt_font_from_shm("cjk");
	tt_font_nav          = tt_font_from_shm("sans-serif");

	struct decor_bounds bounds;
	decor_get_bounds(NULL, &bounds);

	main_window = yutani_window_create(yctx, 800, 600);
	yutani_window_move(yctx, main_window, yctx->display_width / 2 - 400, yctx->display_height / 2 - 300);
	ctx = init_graphics_yutani_double_buffer(main_window);

	yutani_window_advertise_icon(yctx, main_window, APPLICATION_TITLE, "internet-web-browser");

	history_back = list_create();
	history_forward = list_create();

	/* Menu setup */
	menu_bar.entries = menu_entries;
	menu_bar.redraw_callback = redraw_window_callback;
	menu_bar.set = menu_set_create();

	struct MenuList * m = menu_create();
	menu_insert(m, menu_create_normal("exit", NULL, "退出", _menu_action_exit));
	menu_set_insert(menu_bar.set, "file", m);

	m = menu_create();
	menu_insert(m, menu_create_normal("back", NULL, "后退", NULL));
	menu_insert(m, menu_create_normal("forward", NULL, "前进", NULL));
	menu_insert(m, menu_create_normal("refresh", NULL, "刷新", NULL));
	menu_insert(m, menu_create_normal("home", NULL, "主页", NULL));
	menu_set_insert(menu_bar.set, "go", m);

	m = menu_create();
	menu_insert(m, menu_create_normal("help", NULL, "内容", NULL));
	menu_insert(m, menu_create_separator());
	menu_insert(m, menu_create_normal("star", NULL, "关于浏览器", _menu_action_about));
	menu_set_insert(menu_bar.set, "help", m);

	/* Initial content */
	reinitialize_contents();
	redraw_window();

	/* Navigate to URL if provided */
	if (argc > 1 && argv[1][0]) {
		navigate_to(argv[1]);
	}

	while (application_running) {
		int fds[1] = {fileno(yctx->sock)};
		int index = fswait2(1, fds, 100);

		maybe_blink_cursor();

		if (index != 0) continue;

		yutani_msg_t * msg = yutani_poll(yctx);
		while (msg) {
			if (menu_process_event(yctx, msg)) {
				redraw_window();
			}

			switch (msg->type) {
				case YUTANI_MSG_KEY_EVENT: {
					struct yutani_msg_key_event * ke = (void*)msg->data;
					if (ke->event.action == KEY_ACTION_DOWN && ke->wid == main_window->wid) {
						if (nav_bar_focused) {
							switch (ke->event.keycode) {
								case KEY_ESCAPE:
									nav_bar_focused = 0;
									redraw_window();
									break;
								case KEY_BACKSPACE:
									nav_bar_backspace();
									nav_bar_blink = 0;
									gettimeofday(&nav_bar_last_blinked, NULL);
									redraw_window();
									break;
								case '\n':
									nav_bar_focused = 0;
									navigate_to(nav_bar);
									break;
								default:
									if (isgraph(ke->event.key)) {
										nav_bar_insert_char(ke->event.key);
										nav_bar_blink = 0;
										gettimeofday(&nav_bar_last_blinked, NULL);
										redraw_window();
									}
									break;
							}
						} else {
							switch (ke->event.keycode) {
								case 'l':
									if (ke->event.modifiers & YUTANI_KEY_MODIFIER_CTRL) {
										nav_bar_focused = 1;
										nav_bar_blink = 0;
										gettimeofday(&nav_bar_last_blinked, NULL);
										nav_bar_cursor = strlen(nav_bar);
										redraw_window();
									}
									break;
								case 'q':
									_menu_action_exit(NULL);
									break;
								case KEY_ARROW_LEFT:
									if (ke->event.modifiers & YUTANI_KEY_MODIFIER_ALT) go_back();
									break;
								case KEY_ARROW_RIGHT:
									if (ke->event.modifiers & YUTANI_KEY_MODIFIER_ALT) go_forward();
									break;
								case KEY_F5:
									if (current_url[0]) navigate_to(current_url);
									break;
							}
						}
					}
					break;
				}
				case YUTANI_MSG_WINDOW_FOCUS_CHANGE: {
					struct yutani_msg_window_focus_change * wf = (void*)msg->data;
					yutani_window_t * win = hashmap_get(yctx->windows, (void*)(uintptr_t)wf->wid);
					if (win == main_window) {
						win->focused = wf->focused;
						redraw_window();
					}
					break;
				}
				case YUTANI_MSG_RESIZE_OFFER: {
					struct yutani_msg_window_resize * wr = (void*)msg->data;
					if (wr->wid == main_window->wid) {
						resize_finish(wr->width, wr->height);
					}
					break;
				}
				case YUTANI_MSG_WINDOW_MOUSE_EVENT: {
					struct yutani_msg_window_mouse_event * me = (void*)msg->data;
					yutani_window_t * win = hashmap_get(yctx->windows, (void*)(uintptr_t)me->wid);
					if (win == main_window) {
						struct decor_bounds bounds;
						decor_get_bounds(main_window, &bounds);
						int result = decor_handle_event(yctx, msg);
						switch (result) {
							case DECOR_CLOSE:
								_menu_action_exit(NULL);
								break;
							case DECOR_RIGHT:
								decor_show_default_menu(main_window, main_window->x + me->new_x, main_window->y + me->new_y);
								break;
						}

						menu_bar_mouse_event(yctx, main_window, &menu_bar, me, me->new_x, me->new_y);

						/* Check nav bar area */
						int nav_y_start = bounds.top_height + MENU_BAR_HEIGHT;
						int nav_y_end = nav_y_start + NAV_BAR_HEIGHT;

						if (me->new_y >= nav_y_start && me->new_y < nav_y_end &&
							me->new_x >= bounds.left_width &&
							me->new_x < (int)(main_window->width - bounds.right_width)) {

							int rel_x = me->new_x - bounds.left_width - 4;
							if (rel_x >= 0 && rel_x < 34 && me->command == YUTANI_MOUSE_EVENT_CLICK) {
								handle_nav_button(0);
							} else if (rel_x >= 34 && rel_x < 68 && me->command == YUTANI_MOUSE_EVENT_CLICK) {
								handle_nav_button(1);
							} else if (rel_x >= 68 && rel_x < 102 && me->command == YUTANI_MOUSE_EVENT_CLICK) {
								handle_nav_button(2);
							} else if (rel_x >= 102 && rel_x < 136 && me->command == YUTANI_MOUSE_EVENT_CLICK) {
								handle_nav_button(3);
							} else if (rel_x >= 136 && me->command == YUTANI_MOUSE_EVENT_DOWN) {
								nav_bar_focused = 1;
								nav_bar_blink = 0;
								gettimeofday(&nav_bar_last_blinked, NULL);
								nav_bar_cursor = strlen(nav_bar);
								redraw_window();
							}
						}

						/* Scroll in content area */
						if (me->new_y >= nav_y_end && me->new_y < (int)(main_window->height - bounds.bottom_height - 24)) {
							if (me->buttons & YUTANI_MOUSE_SCROLL_UP) {
								_scroll_up();
								redraw_window();
							} else if (me->buttons & YUTANI_MOUSE_SCROLL_DOWN) {
								_scroll_down();
								redraw_window();
							}
						}

						/* Click outside nav bar to unfocus */
						if (me->command == YUTANI_MOUSE_EVENT_DOWN && me->new_y < nav_y_start) {
							if (nav_bar_focused) {
								nav_bar_focused = 0;
								redraw_window();
							}
						}
					}
					break;
				}
				case YUTANI_MSG_WINDOW_CLOSE:
				case YUTANI_MSG_SESSION_END:
					_menu_action_exit(NULL);
					break;
			}

			free(msg);
			msg = yutani_poll_async(yctx);
		}
	}

	/* Cleanup */
	if (page_content) free(page_content);
	if (page_title) free(page_title);
	if (contents) free(contents);
	if (contents_sprite) sprite_free(contents_sprite);
	yutani_close(yctx, main_window);

	return 0;
}
