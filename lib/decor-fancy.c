/**
 * @file lib/decor-fancy.c
 * @brief Modern "Fancy" decoration theme with rounded corners and blue-white color scheme.
 *
 * Provides a modern flat-design window decoration with rounded corners,
 * blue-white color scheme, and clean typography.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2016-2024 K. Lange
 */
#include <stdint.h>
#include <dlfcn.h>

#include <toaru/yutani.h>
#include <toaru/graphics.h>
#include <toaru/decorations.h>
#include <toaru/text.h>
#include <toaru/icon_cache.h>

#define TITLEBAR_HEIGHT 34
#define BASE_SIZE 10
#define TOTAL_SCALE 1
#define OUTER_SIZE 6
#define CORNER_RADIUS 10

/* Modern blue-white color scheme */
#define TITLEBAR_ACTIVE_BG     rgb(255,255,255)
#define TITLEBAR_INACTIVE_BG   rgb(245,247,250)
#define TITLEBAR_ACTIVE_BORDER rgb(59,130,246)
#define TITLEBAR_INACTIVE_BORDER rgb(180,195,215)
#define TITLEBAR_ACTIVE_TEXT   rgb(30,41,59)
#define TITLEBAR_INACTIVE_TEXT rgb(148,163,184)
#define BORDER_COLOR_ACTIVE    rgb(59,130,246)
#define BORDER_COLOR_INACTIVE  rgb(203,213,225)
#define SHADOW_COLOR           rgba(0,0,0,40)

/* Button and title colors */
#define ACTIVE_COLOR   rgb(59,130,246)
#define INACTIVE_COLOR rgb(148,163,184)

static int u_height = TITLEBAR_HEIGHT * TOTAL_SCALE;
static int ul_width = BASE_SIZE * TOTAL_SCALE;
static int ur_width = BASE_SIZE * TOTAL_SCALE;
static int ml_width = BASE_SIZE * TOTAL_SCALE;
static int mr_width = BASE_SIZE * TOTAL_SCALE;
static int l_height = BASE_SIZE * TOTAL_SCALE;
static int ll_width = BASE_SIZE * TOTAL_SCALE;
static int lr_width = BASE_SIZE * TOTAL_SCALE;

static struct TT_Font * _tt_font = NULL;
static struct TT_Font * _tt_font_cjk = NULL;
static int always_left_align = 0;
static int use_window_icons = 1;

#define BUTTON_CLOSE 0
#define BUTTON_MAXIMIZE 1
#define BUTTON_MINIMIZE 2
#define BUTTON_UNMAXIMIZE 3
static sprite_t * sprites[4];

#define TEXT_OFFSET ((window->decorator_flags & DECOR_FLAG_TILED) ? 5 : 10)
#define BUTTON_OFFSET ((window->decorator_flags & DECOR_FLAG_TILED) ? 5 : 0)

/**
 * Load a button sprite.
 */
static void init_sprite(int id, char * path) {
	sprites[id] = malloc(sizeof(sprite_t));
	load_sprite(sprites[id], path);
}

static int get_bounds_fancy(yutani_window_t * window, struct decor_bounds * bounds) {
	if (window == NULL || !(window->decorator_flags & DECOR_FLAG_TILED)) {
		bounds->top_height    = TITLEBAR_HEIGHT * TOTAL_SCALE;
		bounds->bottom_height = OUTER_SIZE * TOTAL_SCALE;
		bounds->left_width    = OUTER_SIZE * TOTAL_SCALE;
		bounds->right_width   = OUTER_SIZE * TOTAL_SCALE;
	} else {
		bounds->top_height = 27 * TOTAL_SCALE + !(window->decorator_flags & DECOR_FLAG_TILE_UP);
		bounds->bottom_height   = !(window->decorator_flags & DECOR_FLAG_TILE_DOWN);
		bounds->left_width      = !(window->decorator_flags & DECOR_FLAG_TILE_LEFT);
		bounds->right_width     = !(window->decorator_flags & DECOR_FLAG_TILE_RIGHT);
	}

	bounds->width = bounds->left_width + bounds->right_width;
	bounds->height = bounds->top_height + bounds->bottom_height;
	return 0;
}

#define BUTTON_PAD 5

static void render_decorations_fancy(yutani_window_t * window, gfx_context_t * ctx, char * title, int decors_active) {
	int width = window->width;
	int height = window->height;

	struct decor_bounds bounds;
	get_bounds_fancy(window, &bounds);

	int is_active = (decors_active != DECOR_INACTIVE);
	uint32_t titlebar_bg   = is_active ? TITLEBAR_ACTIVE_BG : TITLEBAR_INACTIVE_BG;
	uint32_t border_color  = is_active ? BORDER_COLOR_ACTIVE : BORDER_COLOR_INACTIVE;
	uint32_t title_color   = is_active ? TITLEBAR_ACTIVE_TEXT : TITLEBAR_INACTIVE_TEXT;
	uint32_t top_border    = is_active ? TITLEBAR_ACTIVE_BORDER : TITLEBAR_INACTIVE_BORDER;

	if ((window->decorator_flags & DECOR_FLAG_TILED)) {
		/* Tiled mode: Flat titlebar with colored top accent line */
		for (int j = 0; j < (int)bounds.top_height; ++j) {
			for (int i = 0; i < width; ++i) {
				GFX(ctx,i,j) = titlebar_bg;
			}
		}
		/* Colored top accent line */
		for (int i = 0; i < width; ++i) {
			GFX(ctx,i,0) = top_border;
		}

		uint32_t edge_color = border_color;
		if (!(window->decorator_flags & DECOR_FLAG_TILE_DOWN)) {
			for (int i = 0; i < (int)window->width; ++i) {
				GFX(ctx,i,window->height-1) = edge_color;
			}
		}
		if (!(window->decorator_flags & DECOR_FLAG_TILE_LEFT)) {
			for (int i = 0; i < (int)window->height; ++i) {
				GFX(ctx,0,i) = edge_color;
			}
		}
		if (!(window->decorator_flags & DECOR_FLAG_TILE_RIGHT)) {
			for (int i = 0; i < (int)window->height; ++i) {
				GFX(ctx,window->width-1,i) = edge_color;
			}
		}
	} else {
		/* Floating mode: Full rounded rectangle with shadow */
		/* Draw outer shadow */
		draw_rounded_rectangle(ctx, 1, 1, width - 2, height - 2, CORNER_RADIUS + 1, SHADOW_COLOR);
		draw_rounded_rectangle(ctx, 2, 2, width - 4, height - 4, CORNER_RADIUS, SHADOW_COLOR);

		/* Draw border */
		draw_rounded_rectangle(ctx, 3, 3, width - 6, height - 6, CORNER_RADIUS, border_color);

		/* Draw white background for the window body */
		draw_rounded_rectangle(ctx, 4, 4, width - 8, height - 8, CORNER_RADIUS - 1, rgb(255,255,255));

		/* Draw titlebar area with rounded top corners */
		draw_rounded_rectangle(ctx, 4, 4, width - 8, bounds.top_height + 4, CORNER_RADIUS - 1, titlebar_bg);
		/* Square off the bottom of the titlebar (fill the rounded bottom part) */
		for (int j = bounds.top_height; j < bounds.top_height + CORNER_RADIUS; ++j) {
			for (int i = CORNER_RADIUS; i < width - CORNER_RADIUS; ++i) {
				GFX(ctx,i,j) = titlebar_bg;
			}
		}

		/* Bottom accent line on titlebar */
		for (int i = CORNER_RADIUS; i < width - CORNER_RADIUS; ++i) {
			GFX(ctx,i,bounds.top_height + 3) = top_border;
		}

		/* Fill window body with white */
		for (int j = bounds.top_height + CORNER_RADIUS; j < height - CORNER_RADIUS; ++j) {
			for (int i = CORNER_RADIUS; i < width - CORNER_RADIUS; ++i) {
				GFX(ctx,i,j) = rgb(255,255,255);
			}
		}
	}

	uint32_t button_color = is_active ? ACTIVE_COLOR : INACTIVE_COLOR;

	int buttons_width = (!(window->decorator_flags & DECOR_FLAG_NO_MAXIMIZE)) ? 72 : 28;
	int usable_width = width - bounds.width - (2 * buttons_width + 10) * TOTAL_SCALE;
	int left_width = 0;

	if (use_window_icons) {
		sprite_t * icon = icon_get_16(window->icon ?: "applications-generic");
		if (icon) {
			int x = bounds.left_width + 8 * TOTAL_SCALE;
			int y = (TEXT_OFFSET + 2) * TOTAL_SCALE;
			draw_sprite_scaled_alpha(ctx, icon, x, y, 16, 16, is_active ? 1.0 : 0.7);
			left_width = 18 * TOTAL_SCALE;
			usable_width -= left_width;
		}
	}

	tt_set_size(_tt_font, 12 * TOTAL_SCALE);
	int title_width = tt_string_width_cjk(_tt_font, _tt_font_cjk, title);
	if (title_width > usable_width || always_left_align) {
		usable_width += buttons_width * TOTAL_SCALE;
		if (usable_width > 0) {
			char * tmp_title = tt_ellipsify(title, 12 * TOTAL_SCALE, _tt_font, usable_width, &title_width);
			int title_offset = bounds.left_width + 10 * TOTAL_SCALE + left_width;
			tt_draw_string_cjk(ctx, _tt_font, _tt_font_cjk, title_offset, (TEXT_OFFSET + 14) * TOTAL_SCALE, tmp_title, title_color);
			free(tmp_title);
		}
	} else {
		int title_offset = buttons_width * TOTAL_SCALE + bounds.left_width + 10 * TOTAL_SCALE + (usable_width / 2) - (title_width / 2);
		tt_draw_string_cjk(ctx, _tt_font, _tt_font_cjk, title_offset, (TEXT_OFFSET + 14) * TOTAL_SCALE, title, title_color);
	}

	/* Window control buttons */
	uint32_t btn_hover_bg = is_active ? rgba(59,130,246,30) : rgba(148,163,184,30);
	uint32_t btn_down_bg  = is_active ? rgba(59,130,246,60) : rgba(148,163,184,60);
	uint32_t btn_close_hover = rgb(239,68,68);
	uint32_t btn_close_down  = rgb(220,38,38);
	uint32_t i_color = (decor_hover_window == window && decor_hover_button) ? ACTIVE_COLOR : button_color;

	if (width + (BUTTON_OFFSET - 28) * TOTAL_SCALE > bounds.left_width) {
		if (decor_hover_window == window && decor_hover_button == DECOR_CLOSE) {
			draw_rounded_rectangle(ctx,
				width + (BUTTON_OFFSET - 28 - BUTTON_PAD) * TOTAL_SCALE,
				(16 - BUTTON_OFFSET - BUTTON_PAD) * TOTAL_SCALE, 8 + BUTTON_PAD * 2, 8 + BUTTON_PAD * 2, 4,
				(decor_down_button == DECOR_CLOSE) ? btn_close_down : btn_close_hover);
		}
		draw_sprite_alpha_paint(ctx, sprites[BUTTON_CLOSE],
			width + (BUTTON_OFFSET - 28) * TOTAL_SCALE,
			(16 - BUTTON_OFFSET) * TOTAL_SCALE, 1.0,
			(decor_hover_window == window && decor_hover_button == DECOR_CLOSE) ? rgb(255,255,255) : i_color);

		if (width + (BUTTON_OFFSET - 50) * TOTAL_SCALE > bounds.left_width) {
			if (!(window->decorator_flags & DECOR_FLAG_NO_MAXIMIZE)) {
				if (decor_hover_window == window && decor_hover_button == DECOR_MAXIMIZE) {
					draw_rounded_rectangle(ctx,
						width + (BUTTON_OFFSET - 50 - BUTTON_PAD) * TOTAL_SCALE,
						(16 - BUTTON_OFFSET - BUTTON_PAD) * TOTAL_SCALE, 8 + BUTTON_PAD * 2, 8 + BUTTON_PAD * 2, 4,
						(decor_down_button == DECOR_MAXIMIZE) ? btn_down_bg : btn_hover_bg);
				}
				draw_sprite_alpha_paint(ctx, sprites[(window->decorator_flags & DECOR_FLAG_TILED) ? BUTTON_UNMAXIMIZE : BUTTON_MAXIMIZE],
					width + (BUTTON_OFFSET - 50) * TOTAL_SCALE,
					(16 - BUTTON_OFFSET) * TOTAL_SCALE, 1.0, i_color);

				if (width + (BUTTON_OFFSET - 72) * TOTAL_SCALE > bounds.left_width) {
					if (decor_hover_window == window && decor_hover_button == DECOR_MINIMIZE) {
						draw_rounded_rectangle(ctx,
							width + (BUTTON_OFFSET - 72 - BUTTON_PAD) * TOTAL_SCALE,
							(16 - BUTTON_OFFSET - BUTTON_PAD) * TOTAL_SCALE, 8 + BUTTON_PAD * 2, 8 + BUTTON_PAD * 2, 4,
							(decor_down_button == DECOR_MINIMIZE) ? btn_down_bg : btn_hover_bg);
					}
					draw_sprite_alpha_paint(ctx, sprites[BUTTON_MINIMIZE],
						width + (BUTTON_OFFSET - 72) * TOTAL_SCALE,
						(16 - BUTTON_OFFSET) * TOTAL_SCALE, 1.0, i_color);
				}
			}
		}
	}
}

static int check_button_press_fancy(yutani_window_t * window, int x, int y) {
	if (y >= (16 - BUTTON_OFFSET - BUTTON_PAD) * TOTAL_SCALE && y <= (16 - BUTTON_OFFSET + 8 + BUTTON_PAD) * TOTAL_SCALE ) {
		if (x >= (int)window->width + (BUTTON_OFFSET - 28 - BUTTON_PAD) * TOTAL_SCALE &&
			x <= (int)window->width + (BUTTON_OFFSET - 28 + 8 + BUTTON_PAD) * TOTAL_SCALE) {
			return DECOR_CLOSE;
		}

		if (!(window->decorator_flags & DECOR_FLAG_NO_MAXIMIZE)) {
			if (x >= (int)window->width + (BUTTON_OFFSET - 50 - BUTTON_PAD) * TOTAL_SCALE &&
				x <= (int)window->width + (BUTTON_OFFSET - 50 + 8 + BUTTON_PAD) * TOTAL_SCALE) {
				return DECOR_MAXIMIZE;
			}

			if (x >= (int)window->width + (BUTTON_OFFSET - 72 - BUTTON_PAD) * TOTAL_SCALE &&
				x <= (int)window->width + (BUTTON_OFFSET - 72 + 8 + BUTTON_PAD) * TOTAL_SCALE) {
				return DECOR_MINIMIZE;
			}
		}

		if (x >= (int)window->width + (BUTTON_OFFSET - 72 - BUTTON_PAD) * TOTAL_SCALE &&
			x <= (int)window->width + (BUTTON_OFFSET - 28 + 8 + BUTTON_PAD) * TOTAL_SCALE) {
			return DECOR_OTHER;
		}
	}

	return 0;
}

void decor_init() {
	init_sprite(BUTTON_CLOSE, "/usr/share/ttk/fancy/button-close.png");
	init_sprite(BUTTON_MAXIMIZE, "/usr/share/ttk/fancy/button-maximize.png");
	init_sprite(BUTTON_MINIMIZE, "/usr/share/ttk/fancy/button-minimize.png");
	init_sprite(BUTTON_UNMAXIMIZE, "/usr/share/ttk/fancy/button-unmaximize.png");

	decor_render_decorations = render_decorations_fancy;
	decor_check_button_press = check_button_press_fancy;
	decor_get_bounds = get_bounds_fancy;

	_tt_font = tt_font_from_shm("sans-serif.bold");
	_tt_font_cjk = tt_font_from_shm("cjk");

	/* TODO: Replace these environment variables with a more robust
	 *       (and runtime-modifiable) configuration system. */
	always_left_align = !!getenv("WM_LEFT_ALIGN"); /* Opt-in config. */
	use_window_icons = !getenv("WM_NO_ICONS"); /* Opt-out config. */
}

