/**
 * @brief OOBE - Out-of-Box Experience / First-time Setup Wizard
 *
 * Runs on first boot to guide the user through initial system setup:
 * username, password, hostname configuration.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2024 K. Lange
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/time.h>

#include <toaru/graphics.h>
#include <toaru/kbd.h>
#include <toaru/yutani.h>
#include <toaru/text.h>
#include <toaru/trace.h>
#define TRACE_APP_NAME "oobe"

#define OOBE_FLAG_FILE "/etc/oobe_complete"
#define INPUT_SIZE 256

#define WIN_W 600
#define WIN_H 440
#define BOX_PAD 40
#define CORNER_R 12

/* Modern blue-white color scheme */
#define COLOR_BG       rgb(248,250,252)
#define COLOR_PRIMARY  rgb(59,130,246)
#define COLOR_PRIMARY_DARK rgb(37,99,235)
#define COLOR_TEXT     rgb(30,41,59)
#define COLOR_TEXT_LIGHT rgb(100,116,139)
#define COLOR_WHITE    rgb(255,255,255)
#define COLOR_BORDER   rgb(203,213,225)
#define COLOR_INPUT_BG rgb(241,245,249)
#define COLOR_INPUT_BORDER rgb(148,163,184)
#define COLOR_INPUT_FOCUS rgb(59,130,246)
#define COLOR_SUCCESS  rgb(34,197,94)
#define COLOR_SHADOW   rgba(0,0,0,50)

static gfx_context_t * ctx;
static yutani_t * yctx;
static yutani_window_t * window;
static struct TT_Font * font;
static struct TT_Font * font_bold;
static struct TT_Font * font_cjk;

static int screen_w, screen_h;
static int win_w = WIN_W, win_h = WIN_H;

static int step = 0;
#define STEP_WELCOME  0
#define STEP_USERNAME 1
#define STEP_PASSWORD 2
#define STEP_HOSTNAME 3
#define STEP_CONFIRM  4
#define STEP_DONE     5

static char username[INPUT_SIZE] = {0};
static char password[INPUT_SIZE] = {0};
static char hostname_buf[INPUT_SIZE] = {0};

static int focus_input = 0;
static char * active_input = NULL;
static int active_input_len = 0;
static int active_is_password = 0;

static int center_x(int w) { return (screen_w - w) / 2; }
static int center_y(int h) { return (screen_h - h) / 2; }
static int win_center_x(int w) { return (win_w - w) / 2; }

static int buffer_put(char * buf, int max_len, char c) {
	int len = strlen(buf);
	if (c == 8) {
		if (len > 0) { buf[len-1] = '\0'; }
		return 0;
	}
	if (c < 32 || c > 126) return 0;
	if (len >= max_len - 1) return 0;
	buf[len] = c;
	buf[len+1] = '\0';
	return 0;
}

static void draw_input_box(int x, int y, int w, int h, char * text, int is_focused, int is_password, char * placeholder) {
	/* Border */
	draw_rounded_rectangle(ctx, x, y, w, h, 6, is_focused ? COLOR_INPUT_FOCUS : COLOR_INPUT_BORDER);
	/* Background */
	draw_rounded_rectangle(ctx, x+1, y+1, w-2, h-2, 5, COLOR_INPUT_BG);

	tt_set_size(font, 14);
	char display_text[INPUT_SIZE] = {0};
	uint32_t text_color = COLOR_TEXT;

	if (strlen(text) == 0 && !is_focused) {
		strcpy(display_text, placeholder);
		text_color = COLOR_TEXT_LIGHT;
	} else if (is_password) {
		for (unsigned int i = 0; i < strlen(text); ++i) {
			display_text[i] = '*';
			display_text[i+1] = '\0';
		}
	} else {
		strcpy(display_text, text);
	}

	gfx_context_t * clipped = init_graphics_subregion(ctx, x + 8, y + 2, w - 16, h - 4);
	tt_draw_string_cjk(clipped, font, font_cjk, 4, h - 8, display_text, text_color);

	if (is_focused) {
		int tw = tt_string_width_cjk(font, font_cjk, is_password ? display_text : text);
		draw_line(clipped, tw + 6, tw + 6, 4, h - 10, COLOR_PRIMARY);
	}
	free(clipped);
}

static void draw_primary_button(int x, int y, int w, int h, char * text, int hover) {
	draw_rounded_rectangle(ctx, x, y, w, h, 6, hover ? COLOR_PRIMARY_DARK : COLOR_PRIMARY);
	tt_set_size(font_bold, 14);
	int tw = tt_string_width_cjk(font_bold, font_cjk, text);
	tt_draw_string_cjk(ctx, font_bold, font_cjk, x + (w - tw)/2, y + h - 10, text, COLOR_WHITE);
}

static void draw_secondary_button(int x, int y, int w, int h, char * text, int hover) {
	draw_rounded_rectangle(ctx, x, y, w, h, 6, hover ? COLOR_BORDER : COLOR_WHITE);
	draw_rounded_rectangle(ctx, x+1, y+1, w-2, h-2, 5, hover ? COLOR_INPUT_BG : COLOR_WHITE);
	tt_set_size(font, 14);
	int tw = tt_string_width_cjk(font, font_cjk, text);
	tt_draw_string_cjk(ctx, font, font_cjk, x + (w - tw)/2, y + h - 10, text, COLOR_TEXT);
}

static void render_welcome(void) {
	/* Title */
	tt_set_size(font_bold, 28);
	char * title = "\xe6\xac\xa2\xe8\xbf\x8e"; /* 欢迎 */
	int tw = tt_string_width_cjk(font_bold, font_cjk, title);
	tt_draw_string_cjk(ctx, font_bold, font_cjk, win_center_x(tw), 60, title, COLOR_PRIMARY);

	/* Subtitle */
	tt_set_size(font, 15);
	char * sub = "\xe8\xae\xa9\xe6\x88\x91\xe4\xbb\xac\xe4\xb8\xba\xe6\x82\xa8\xe7\x9a\x84\xe7\xb3\xbb\xe7\xbb\x9f\xe5\x81\x9a\xe5\xa5\xbd\xe5\x87\x86\xe5\xa4\x87"; /* 让我们为您的系统做好准备 */
	tw = tt_string_width_cjk(font, font_cjk, sub);
	tt_draw_string_cjk(ctx, font, font_cjk, win_center_x(tw), 100, sub, COLOR_TEXT_LIGHT);

	/* Feature list */
	tt_set_size(font, 14);
	char * features[] = {
		"\xe2\x80\xa2 \xe8\xae\xbe\xe7\xbd\xae\xe7\x94\xa8\xe6\x88\xb7\xe8\xb4\xa6\xe6\x88\xb7", /* 设置用户账户 */
		"\xe2\x80\xa2 \xe9\x85\x8d\xe7\xbd\xae\xe7\xb3\xbb\xe7\xbb\x9f\xe4\xb8\xbb\xe6\x9c\xba\xe5\x90\x8d", /* 配置系统主机名 */
		"\xe2\x80\xa2 \xe4\xb8\xaa\xe6\x80\xa7\xe5\x8c\x96\xe6\x82\xa8\xe7\x9a\x84\xe6\x93\x8d\xe4\xbd\x9c\xe7\xb3\xbb\xe7\xbb\x9f", /* 个性化您的操作系统 */
		NULL
	};
	for (int i = 0; features[i]; i++) {
		tt_draw_string_cjk(ctx, font, font_cjk, 80, 150 + i * 30, features[i], COLOR_TEXT);
	}

	/* Start button */
	draw_primary_button(win_center_x(200), 300, 200, 40, "\xe5\xbc\x80\xe5\xa7\x8b\xe8\xae\xbe\xe7\xbd\xae", 0); /* 开始设置 */
}

static void render_username(void) {
	tt_set_size(font_bold, 22);
	char * title = "\xe5\x88\x9b\xe5\xbb\xba\xe7\x94\xa8\xe6\x88\xb7\xe8\xb4\xa6\xe6\x88\xb7"; /* 创建用户账户 */
	int tw = tt_string_width_cjk(font_bold, font_cjk, title);
	tt_draw_string_cjk(ctx, font_bold, font_cjk, win_center_x(tw), 50, title, COLOR_TEXT);

	tt_set_size(font, 13);
	char * sub = "\xe8\xaf\xb7\xe8\xbe\x93\xe5\x85\xa5\xe6\x82\xa8\xe7\x9a\x84\xe7\x94\xa8\xe6\x88\xb7\xe5\x90\x8d"; /* 请输入您的用户名 */
	tw = tt_string_width_cjk(font, font_cjk, sub);
	tt_draw_string_cjk(ctx, font, font_cjk, win_center_x(tw), 80, sub, COLOR_TEXT_LIGHT);

	draw_input_box(80, 120, win_w - 160, 36, username, focus_input == STEP_USERNAME, 0, "\xe7\x94\xa8\xe6\x88\xb7\xe5\x90\x8d"); /* 用户名 */

	draw_primary_button(win_center_x(120), 220, 120, 36, "\xe4\xb8\x8b\xe4\xb8\x80\xe6\xad\xa5", 0); /* 下一步 */
	draw_secondary_button(win_center_x(120) + 140, 220, 100, 36, "\xe4\xb8\x8a\xe4\xb8\x80\xe6\xad\xa5", 0); /* 上一步 */
}

static void render_password(void) {
	tt_set_size(font_bold, 22);
	char * title = "\xe8\xae\xbe\xe7\xbd\xae\xe5\xaf\x86\xe7\xa0\x81"; /* 设置密码 */
	int tw = tt_string_width_cjk(font_bold, font_cjk, title);
	tt_draw_string_cjk(ctx, font_bold, font_cjk, win_center_x(tw), 50, title, COLOR_TEXT);

	tt_set_size(font, 13);
	char * sub = "\xe8\xaf\xb7\xe4\xb8\xba\xe6\x82\xa8\xe7\x9a\x84\xe8\xb4\xa6\xe6\x88\xb7\xe8\xae\xbe\xe7\xbd\xae\xe5\xaf\x86\xe7\xa0\x81"; /* 请为您的账户设置密码 */
	tw = tt_string_width_cjk(font, font_cjk, sub);
	tt_draw_string_cjk(ctx, font, font_cjk, win_center_x(tw), 80, sub, COLOR_TEXT_LIGHT);

	draw_input_box(80, 120, win_w - 160, 36, password, focus_input == STEP_PASSWORD, 1, "\xe5\xaf\x86\xe7\xa0\x81"); /* 密码 */

	draw_primary_button(win_center_x(120), 220, 120, 36, "\xe4\xb8\x8b\xe4\xb8\x80\xe6\xad\xa5", 0);
	draw_secondary_button(win_center_x(120) + 140, 220, 100, 36, "\xe4\xb8\x8a\xe4\xb8\x80\xe6\xad\xa5", 0);
}

static void render_hostname(void) {
	tt_set_size(font_bold, 22);
	char * title = "\xe8\xae\xbe\xe7\xbd\xae\xe4\xb8\xbb\xe6\x9c\xba\xe5\x90\x8d"; /* 设置主机名 */
	int tw = tt_string_width_cjk(font_bold, font_cjk, title);
	tt_draw_string_cjk(ctx, font_bold, font_cjk, win_center_x(tw), 50, title, COLOR_TEXT);

	tt_set_size(font, 13);
	char * sub = "\xe4\xb8\xba\xe6\x82\xa8\xe7\x9a\x84\xe8\xae\xa1\xe7\xae\x97\xe6\x9c\xba\xe8\xae\xbe\xe7\xbd\xae\xe4\xb8\x80\xe4\xb8\xaa\xe5\x90\x8d\xe7\xa7\xb0"; /* 为您的计算机设置一个名称 */
	tw = tt_string_width_cjk(font, font_cjk, sub);
	tt_draw_string_cjk(ctx, font, font_cjk, win_center_x(tw), 80, sub, COLOR_TEXT_LIGHT);

	draw_input_box(80, 120, win_w - 160, 36, hostname_buf, focus_input == STEP_HOSTNAME, 0, "\xe4\xb8\xbb\xe6\x9c\xba\xe5\x90\x8d"); /* 主机名 */

	draw_primary_button(win_center_x(120), 220, 120, 36, "\xe4\xb8\x8b\xe4\xb8\x80\xe6\xad\xa5", 0);
	draw_secondary_button(win_center_x(120) + 140, 220, 100, 36, "\xe4\xb8\x8a\xe4\xb8\x80\xe6\xad\xa5", 0);
}

static void render_confirm(void) {
	tt_set_size(font_bold, 22);
	char * title = "\xe7\xa1\xae\xe8\xae\xa4\xe8\xae\xbe\xe7\xbd\xae"; /* 确认设置 */
	int tw = tt_string_width_cjk(font_bold, font_cjk, title);
	tt_draw_string_cjk(ctx, font_bold, font_cjk, win_center_x(tw), 50, title, COLOR_TEXT);

	/* Summary card */
	draw_rounded_rectangle(ctx, 60, 80, win_w - 120, 180, 8, COLOR_BORDER);
	draw_rounded_rectangle(ctx, 61, 81, win_w - 122, 178, 7, COLOR_WHITE);

	tt_set_size(font, 14);
	char buf[512];
	snprintf(buf, 512, "\xe7\x94\xa8\xe6\x88\xb7\xe5\x90\x8d: %s", username); /* 用户名 */
	tt_draw_string_cjk(ctx, font, font_cjk, 90, 120, buf, COLOR_TEXT);

	snprintf(buf, 512, "\xe5\xaf\x86\xe7\xa0\x81: ******"); /* 密码: ****** */
	tt_draw_string_cjk(ctx, font, font_cjk, 90, 150, buf, COLOR_TEXT);

	snprintf(buf, 512, "\xe4\xb8\xbb\xe6\x9c\xba\xe5\x90\x8d: %s", hostname_buf); /* 主机名 */
	tt_draw_string_cjk(ctx, font, font_cjk, 90, 180, buf, COLOR_TEXT);

	draw_primary_button(win_center_x(120), 290, 120, 40, "\xe5\xba\x94\xe7\x94\xa8\xe8\xae\xbe\xe7\xbd\xae", 0); /* 应用设置 */
	draw_secondary_button(win_center_x(120) + 140, 290, 100, 40, "\xe4\xb8\x8a\xe4\xb8\x80\xe6\xad\xa5", 0);
}

static void render_done(void) {
	tt_set_size(font_bold, 28);
	char * title = "\xe8\xae\xbe\xe7\xbd\xae\xe5\xae\x8c\xe6\x88\x90!"; /* 设置完成! */
	int tw = tt_string_width_cjk(font_bold, font_cjk, title);
	tt_draw_string_cjk(ctx, font_bold, font_cjk, win_center_x(tw), 80, title, COLOR_SUCCESS);

	tt_set_size(font, 15);
	char * sub = "\xe6\x82\xa8\xe7\x9a\x84\xe7\xb3\xbb\xe7\xbb\x9f\xe5\xb7\xb2\xe5\x87\x86\xe5\xa4\x87\xe5\xb0\xb1\xe7\xbb\xaa"; /* 您的系统已准备就绪 */
	tw = tt_string_width_cjk(font, font_cjk, sub);
	tt_draw_string_cjk(ctx, font, font_cjk, win_center_x(tw), 120, sub, COLOR_TEXT_LIGHT);

	char buf[256];
	snprintf(buf, 256, "\xe7\x94\xa8\xe6\x88\xb7: %s", username); /* 用户: */
	tt_draw_string_cjk(ctx, font, font_cjk, win_center_x(tt_string_width_cjk(font, font_cjk, buf)), 160, buf, COLOR_TEXT);

	draw_primary_button(win_center_x(200), 240, 200, 44, "\xe8\xbf\x9b\xe5\x85\xa5\xe7\xb3\xbb\xe7\xbb\x9f", 0); /* 进入系统 */
}

static void render(void) {
	/* Background */
	draw_fill(ctx, COLOR_BG);

	/* Shadow */
	draw_rounded_rectangle(ctx, 3, 3, win_w - 6, win_h - 6, CORNER_R + 2, COLOR_SHADOW);
	draw_rounded_rectangle(ctx, 4, 4, win_w - 8, win_h - 8, CORNER_R + 1, COLOR_SHADOW);

	/* Card */
	draw_rounded_rectangle(ctx, 5, 5, win_w - 10, win_h - 10, CORNER_R, COLOR_WHITE);

	/* Step indicator */
	int total_steps = 5;
	int step_x_start = win_center_x(total_steps * 40 - 10);
	for (int i = 0; i < total_steps; i++) {
		int sx = step_x_start + i * 40;
		int sy = 15;
		if (i <= step) {
			draw_rounded_rectangle(ctx, sx, sy, 12, 12, 6, COLOR_PRIMARY);
		} else {
			draw_rounded_rectangle(ctx, sx, sy, 12, 12, 6, COLOR_BORDER);
		}
	}

	switch (step) {
		case STEP_WELCOME:  render_welcome(); break;
		case STEP_USERNAME: render_username(); break;
		case STEP_PASSWORD: render_password(); break;
		case STEP_HOSTNAME: render_hostname(); break;
		case STEP_CONFIRM:  render_confirm(); break;
		case STEP_DONE:     render_done(); break;
	}

	flip(ctx);
	yutani_flip(yctx, window);
}

static int apply_settings(void) {
	/* Create home directory */
	char home[512];
	snprintf(home, 512, "/home/%s", username);
	mkdir(home, 0755);

	/* Create user in master.passwd */
	FILE * mp = fopen("/etc/master.passwd", "a");
	if (!mp) return -1;
	fprintf(mp, "%s:%s:1000:1000:%s:%s:/bin/esh:fancy\n",
		username, password, username, home);
	fclose(mp);

	/* Create user in passwd */
	FILE * pw = fopen("/etc/passwd", "a");
	if (!pw) return -1;
	fprintf(pw, "%s:x:1000:1000:%s:%s:/bin/esh:fancy\n",
		username, username, home);
	fclose(pw);

	/* Set hostname */
	FILE * hn = fopen("/etc/hostname", "w");
	if (hn) {
		fprintf(hn, "%s\n", hostname_buf);
		fclose(hn);
	}

	/* Mark OOBE as complete */
	FILE * flag = fopen(OOBE_FLAG_FILE, "w");
	if (flag) {
		fprintf(flag, "complete\n");
		fclose(flag);
	}

	return 0;
}

static int button_hit_test(int bx, int by, int bw, int bh, int mx, int my) {
	return mx >= bx && mx <= bx + bw && my >= by && my <= by + bh;
}

static void handle_click(int mx, int my) {
	switch (step) {
		case STEP_WELCOME: {
			int bx = win_center_x(200);
			if (button_hit_test(bx, 300, 200, 40, mx, my)) {
				step = STEP_USERNAME;
				focus_input = STEP_USERNAME;
			}
			break;
		}
		case STEP_USERNAME: {
			int bx = win_center_x(120);
			if (button_hit_test(bx, 220, 120, 36, mx, my)) {
				if (strlen(username) > 0) {
					step = STEP_PASSWORD;
					focus_input = STEP_PASSWORD;
				}
			} else if (button_hit_test(bx + 140, 220, 100, 36, mx, my)) {
				step = STEP_WELCOME;
				focus_input = 0;
			} else if (button_hit_test(80, 120, win_w - 160, 36, mx, my)) {
				focus_input = STEP_USERNAME;
			}
			break;
		}
		case STEP_PASSWORD: {
			int bx = win_center_x(120);
			if (button_hit_test(bx, 220, 120, 36, mx, my)) {
				if (strlen(password) > 0) {
					step = STEP_HOSTNAME;
					focus_input = STEP_HOSTNAME;
				}
			} else if (button_hit_test(bx + 140, 220, 100, 36, mx, my)) {
				step = STEP_USERNAME;
				focus_input = STEP_USERNAME;
			} else if (button_hit_test(80, 120, win_w - 160, 36, mx, my)) {
				focus_input = STEP_PASSWORD;
			}
			break;
		}
		case STEP_HOSTNAME: {
			int bx = win_center_x(120);
			if (button_hit_test(bx, 220, 120, 36, mx, my)) {
				if (strlen(hostname_buf) > 0) {
					step = STEP_CONFIRM;
					focus_input = 0;
				}
			} else if (button_hit_test(bx + 140, 220, 100, 36, mx, my)) {
				step = STEP_PASSWORD;
				focus_input = STEP_PASSWORD;
			} else if (button_hit_test(80, 120, win_w - 160, 36, mx, my)) {
				focus_input = STEP_HOSTNAME;
			}
			break;
		}
		case STEP_CONFIRM: {
			int bx = win_center_x(120);
			if (button_hit_test(bx, 290, 120, 40, mx, my)) {
				apply_settings();
				step = STEP_DONE;
				focus_input = 0;
			} else if (button_hit_test(bx + 140, 290, 100, 40, mx, my)) {
				step = STEP_HOSTNAME;
				focus_input = STEP_HOSTNAME;
			}
			break;
		}
		case STEP_DONE: {
			int bx = win_center_x(200);
			if (button_hit_test(bx, 240, 200, 44, mx, my)) {
				/* Exit OOBE - will be replaced by login */
				exit(0);
			}
			break;
		}
	}
}

int main(int argc, char ** argv) {
	if (getuid() != 0) {
		fprintf(stderr, "OOBE must be run as root.\n");
		return 1;
	}

	/* Check if OOBE has already been completed */
	if (!access(OOBE_FLAG_FILE, F_OK)) {
		TRACE("OOBE already completed, skipping.");
		return 0;
	}

	TRACE("OOBE starting up...");

	yctx = yutani_init();
	if (!yctx) {
		fprintf(stderr, "OOBE: Failed to connect to compositor, retrying...\n");
		/* Retry a few times in case compositor is still starting up */
		for (int retry = 0; retry < 50; retry++) {
			usleep(100000); /* 100ms */
			yctx = yutani_init();
			if (yctx) break;
		}
		if (!yctx) {
			fprintf(stderr, "OOBE: Giving up, could not connect to compositor.\n");
			return 1;
		}
	}
	TRACE("OOBE connected to compositor.");

	screen_w = yctx->display_width;
	screen_h = yctx->display_height;

	/* Adjust window size if screen is small */
	if (screen_w < WIN_W) win_w = screen_w - 20;
	if (screen_h < WIN_H) win_h = screen_h - 20;

	window = yutani_window_create_flags(yctx, win_w, win_h,
		YUTANI_WINDOW_FLAG_DISALLOW_RESIZE | YUTANI_WINDOW_FLAG_DISALLOW_DRAG);
	yutani_window_move(yctx, window, center_x(win_w), center_y(win_h));
	yutani_set_stack(yctx, window, YUTANI_ZORDER_OVERLAY);

	ctx = init_graphics_yutani_double_buffer(window);

	font = tt_font_from_shm("sans-serif");
	font_bold = tt_font_from_shm("sans-serif.bold");
	font_cjk = tt_font_from_shm("cjk");

	/* Set default hostname */
	gethostname(hostname_buf, INPUT_SIZE - 1);

	render();

	while (1) {
		yutani_msg_t * m = yutani_poll(yctx);
		while (m) {
			switch (m->type) {
				case YUTANI_MSG_KEY_EVENT: {
					struct yutani_msg_key_event * ke = (void*)m->data;
					if (ke->event.action == KEY_ACTION_DOWN) {
						if (ke->event.keycode == '\n') {
							/* Enter key - advance step */
							if (step == STEP_USERNAME && strlen(username) > 0) {
								step = STEP_PASSWORD;
								focus_input = STEP_PASSWORD;
							} else if (step == STEP_PASSWORD && strlen(password) > 0) {
								step = STEP_HOSTNAME;
								focus_input = STEP_HOSTNAME;
							} else if (step == STEP_HOSTNAME && strlen(hostname_buf) > 0) {
								step = STEP_CONFIRM;
								focus_input = 0;
							}
						} else if (ke->event.keycode == '\t') {
							/* Tab - cycle through steps */
							if (step == STEP_USERNAME) {
								step = STEP_PASSWORD;
								focus_input = STEP_PASSWORD;
							} else if (step == STEP_PASSWORD) {
								step = STEP_HOSTNAME;
								focus_input = STEP_HOSTNAME;
							} else if (step == STEP_HOSTNAME) {
								step = STEP_USERNAME;
								focus_input = STEP_USERNAME;
							}
						} else if (ke->event.key) {
							if (focus_input == STEP_USERNAME) {
								buffer_put(username, INPUT_SIZE, ke->event.key);
							} else if (focus_input == STEP_PASSWORD) {
								buffer_put(password, INPUT_SIZE, ke->event.key);
							} else if (focus_input == STEP_HOSTNAME) {
								buffer_put(hostname_buf, INPUT_SIZE, ke->event.key);
							}
						}
					}
					break;
				}
				case YUTANI_MSG_WINDOW_MOUSE_EVENT: {
					struct yutani_msg_window_mouse_event * me = (void*)m->data;
					if (me->command == YUTANI_MOUSE_EVENT_DOWN && me->buttons & YUTANI_MOUSE_BUTTON_LEFT) {
						handle_click(me->new_x, me->new_y);
					}
					break;
				}
				case YUTANI_MSG_SESSION_END:
					exit(0);
				default:
					break;
			}
			free(m);
			m = yutani_poll_async(yctx);
		}
		render();
	}

	return 0;
}