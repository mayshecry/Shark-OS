#ifndef BROWSER_H
#define BROWSER_H

#include "kernel.h"
#include "desktop.h"

/* SharkOS Browser ("Shark Navigator")
 *
 * A small but real web engine:
 *   - HTML tokenizer + tree builder (void elements, implied </p>/</li>,
 *     entities, <script>/<style> raw text)
 *   - CSS: <style> blocks, style="" attributes, a UA stylesheet; selectors
 *     by tag / .class / #id / descendant; properties: color, background(-color),
 *     font-size, font-weight, text-align, display, margin*, padding*,
 *     border(-color), width, text-decoration, list-style
 *   - Layout: block + inline flow with word wrapping, lists, tables (simple),
 *     images (PNG via the kernel decoder)
 *   - JavaScript: an interpreter (var/let/const, functions, closures, objects,
 *     arrays, strings, if/for/while, operators) with a DOM subset:
 *     document.getElementById/querySelector/createElement/body/title,
 *     element.innerHTML/textContent/style/onclick/addEventListener/
 *     appendChild/setAttribute/classList, console.log, alert, setTimeout,
 *     Math, JSON.stringify, parseInt/parseFloat, Date.now
 *   - Navigation: http:// over the kernel TCP stack, about: pages, file: from
 *     the in-memory filesystem, links, back/forward/reload/home, a bookmark
 *     drop-down and a status line.
 */

void app_window_draw_browser(window_t* w);
void app_window_mouse_browser(window_t* w, int mx, int my, int buttons);
void app_window_keyboard_browser(window_t* w, char c);
void browser_close(void);

/* Programmatic navigation (used by the `browser <url>` shell command and by
 * the File Manager for .html files). Opens the window if needed. */
void browser_open_url(const char* url);

/* Called from the desktop main loop so JS timers and the mouse wheel work
 * while the window is idle. Cheap when nothing is pending. */
void browser_tick(void);

#endif
