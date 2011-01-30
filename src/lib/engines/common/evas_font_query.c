#include "evas_common.h"
#include "language/evas_bidi_utils.h" /*defines BIDI_SUPPORT if possible */
#include "evas_font_private.h" /* for Frame-Queuing support */
#include "evas_font_ot.h"

EAPI int
evas_common_font_query_kerning(RGBA_Font_Int* fi,
			       FT_UInt left, FT_UInt right,
			       int* kerning)
{
   int *result;
   FT_Vector delta;
   int key[2];
   int error = 1;

//   return 0;
   key[0] = left;
   key[1] = right;

   result = eina_hash_find(fi->kerning, key);
   if (result)
     {
	*kerning = result[2];
	goto on_correct;
     }

   /* NOTE: ft2 seems to have a bug. and sometimes returns bizarre
    * values to kern by - given same font, same size and same
    * prev_index and index. auto/bytecode or none hinting doesn't
    * matter */
   evas_common_font_int_reload(fi);
   FTLOCK();
   if (FT_Get_Kerning(fi->src->ft.face,
		      key[0], key[1],
		      ft_kerning_default, &delta) == 0)
     {
	int *push;

        FTUNLOCK();
	*kerning = delta.x;

	push = malloc(sizeof (int) * 3);
	if (!push) return 1;

	push[0] = key[0];
	push[1] = key[1];
	push[2] = *kerning;

	eina_hash_direct_add(fi->kerning, push, push);

	goto on_correct;
     }

        FTUNLOCK();
   error = 0;

 on_correct:
   return error;
}

/* text x inset */
/* FIXME: should use OT info when available. */
EAPI int
evas_common_font_query_inset(RGBA_Font *fn, const Eina_Unicode *text)
{
   FT_UInt index;
   RGBA_Font_Glyph *fg;
   int gl;
   RGBA_Font_Int *fi;

   fi = fn->fonts->data;

   if (!*text) return 0;
   gl = *text;
   if (gl == 0) return 0;
//   evas_common_font_size_use(fn);
   index = evas_common_font_glyph_search(fn, &fi, gl);
   LKL(fi->ft_mutex);
   evas_common_font_int_reload(fi);
   if (fi->src->current_size != fi->size)
     {
	FTLOCK();
        FT_Activate_Size(fi->ft.size);
	FTUNLOCK();
        fi->src->current_size = fi->size;
     }
   fg = evas_common_font_int_cache_glyph_get(fi, index);
   LKU(fi->ft_mutex);
   if (!fg) return 0;
/*
   INF("fg->glyph_out->left = %i, "
	  "fi->src->ft.face->glyph->bitmap_left = %i, "
	  "fi->src->ft.face->glyph->metrics.horiBearingX = %i, "
	  "fi->src->ft.face->glyph->metrics.horiBearingY = %i, "
	  "fi->src->ft.face->glyph->metrics.horiAdvance = %i"
	  ,
	  (int)fg->glyph_out->left,
	  (int)fi->src->ft.face->glyph->bitmap_left,
	  (int)fi->src->ft.face->glyph->metrics.horiBearingX >> 6,
	  (int)fi->src->ft.face->glyph->metrics.horiBearingY >> 6,
	  (int)fi->src->ft.face->glyph->metrics.horiAdvance >> 6
	  );
 */
  evas_common_font_int_use_trim();
  return fg->glyph_out->left;
}

/* size of the string (width and height) in pixels
 * BiDi handling: We receive the shaped string + other props from text_props,
 * We only care about the size, and the size does not depend on the visual order.
 * As long as we follow the logical string and get kerning data like we should,
 * we are fine.
 */

EAPI void
evas_common_font_query_size(RGBA_Font *fn, const Eina_Unicode *text, const Evas_Text_Props *text_props __UNUSED__, int *w, int *h)
{
   int keep_width = 0;
   int prev_pen_x = 0;
   EVAS_FONT_WALK_TEXT_INIT();

   EVAS_FONT_WALK_TEXT_VISUAL_START()
     {
        EVAS_FONT_WALK_TEXT_WORK();
        if (!visible) continue;
        /* Keep the width because we'll need it for the last char */
        keep_width = EVAS_FONT_WALK_WIDTH +
           EVAS_FONT_WALK_X_OFF +
           EVAS_FONT_WALK_X_BEAR;
        /* Keep the previous EVAS_FONT_WALK_PEN_X, before it's advanced in TEXT_END */
        prev_pen_x = EVAS_FONT_WALK_PEN_X;
     }
   EVAS_FONT_WALK_TEXT_END();

   if (w) *w = prev_pen_x + keep_width;
   if (h) *h = evas_common_font_max_ascent_get(fn) + evas_common_font_max_descent_get(fn);
}

/* h & v advance
 * BiDi handling: We receive the shaped string + other props from text_props,
 * We don't care about the order, as heights will remain the same (we already did
 * shaping) and as long as we go through the logical string and match the kerning
 * this way, we are safe.
 */
EAPI void
evas_common_font_query_advance(RGBA_Font *fn, const Eina_Unicode *text, const Evas_Text_Props *text_props, int *h_adv, int *v_adv)
{
   EVAS_FONT_WALK_TEXT_INIT();

   EVAS_FONT_WALK_TEXT_LOGICAL_START()
     {
        EVAS_FONT_WALK_TEXT_WORK();
        if (!visible) continue;
     }
   EVAS_FONT_WALK_TEXT_END();

   if (v_adv) *v_adv = evas_common_font_get_line_advance(fn);
   if (h_adv) *h_adv = EVAS_FONT_WALK_PEN_X;
}

/* x y w h for char at char pos for null it returns the position right after
 * the last char with 0 as width and height.
 * BiDi handling: We receive the shaped string + other props from text_props,
 * We care about the actual drawing location of the string, this is why we need
 * the visual string. We need to know how it's printed. After that we need to calculate
 * the reverse kerning in case of rtl parts. "pos" passed to this function is an
 * index in bytes, that is the actual byte location of the string, we need to find
 * the index in order to find it in the visual string.
 */

EAPI int
evas_common_font_query_char_coords(RGBA_Font *fn, const Eina_Unicode *in_text, const Evas_Text_Props *text_props, int pos, int *cx, int *cy, int *cw, int *ch)
{
   int asc, desc;
   int position = 0;
   int ret_val = 0;
   EVAS_FONT_WALK_TEXT_INIT();

   asc = evas_common_font_max_ascent_get(fn);
   desc = evas_common_font_max_descent_get(fn);

   position = pos;
   /* If it's the null, choose location according to the direction. */
   if (text_props->len == position)
     {
        /* if it's rtl then the location is the left of the string,
         * otherwise, the right. */
#ifdef BIDI_SUPPORT
        if (text_props->bidi.dir == EVAS_BIDI_DIRECTION_RTL)
          {
             if (cx) *cx = 0;
             if (ch) *ch = asc + desc;
          }
        else
#endif
          {
             evas_common_font_query_advance(fn, in_text, text_props, cx, ch);
          }
        if (cy) *cy = 0;
        if (cw) *cw = 0;
        ret_val = 1;
        goto end;
     }

   Evas_Coord cluster_start, last_end;
   int prev_cluster = -1;
   int found = 0, items = 1, item_pos = 1;
   int last_is_visible;
   EVAS_FONT_WALK_TEXT_VISUAL_START()
     {
        EVAS_FONT_WALK_TEXT_WORK();

        if (prev_cluster != (int) EVAS_FONT_WALK_POS)
          {
             if (found)
               {
                  break;
               }
             else
               {
                  cluster_start = EVAS_FONT_WALK_PEN_X +
                     EVAS_FONT_WALK_X_OFF +
                     EVAS_FONT_WALK_X_BEAR;
               }
          }
        last_is_visible = visible;
        last_end = EVAS_FONT_WALK_PEN_X + EVAS_FONT_WALK_X_OFF +
           EVAS_FONT_WALK_X_BEAR + EVAS_FONT_WALK_WIDTH;
        /* we need to see if the char at the visual position is the char wanted */
        if ((text_props->bidi.dir == EVAS_BIDI_DIRECTION_LTR) &&
              (EVAS_FONT_WALK_POS <= (size_t) position) &&
              ((((size_t) position) < EVAS_FONT_WALK_POS_NEXT) ||
               (EVAS_FONT_WALK_IS_LAST)))
          {
             found = 1;
#ifdef OT_SUPPORT
             if (evas_common_font_ot_is_enabled())
               {
                  items = evas_common_font_ot_cluster_size_get(text_props,
                        char_index);
               }
#endif
             item_pos = position - EVAS_FONT_WALK_POS + 1;
          }
        else if ((text_props->bidi.dir == EVAS_BIDI_DIRECTION_RTL) &&
              ((EVAS_FONT_WALK_POS_PREV > (size_t) position) ||
               (EVAS_FONT_WALK_IS_FIRST)) &&
              (((size_t) position) >= EVAS_FONT_WALK_POS))
          {
             found = 1;
#ifdef OT_SUPPORT
             if (evas_common_font_ot_is_enabled())
               {
                  items = evas_common_font_ot_cluster_size_get(text_props,
                        char_index);
               }
#endif
             item_pos = items - (position - EVAS_FONT_WALK_POS);
          }

        prev_cluster = EVAS_FONT_WALK_POS;
     }
   EVAS_FONT_WALK_TEXT_END();
   if (found)
     {
        Evas_Coord cluster_w;
        cluster_w = last_end - cluster_start;
        if (cy) *cy = -asc;
        if (ch) *ch = asc + desc;
        if (last_is_visible)
          {
             if (cx) *cx = cluster_start +
               (cluster_w / items) *
                  (item_pos - 1);
             if (cw) *cw = (cluster_w / items);
          }
        else
          {
             if (cx) *cx = cluster_start;
             if (cw) *cw = 0;
          }
        ret_val = 1;
        goto end;
     }
end:

   return ret_val;
}

/* x y w h for pen at char pos for null it returns the position right after
 * the last char with 0 as width and height. This is the same as char_coords
 * but it returns the pen_x and adv instead of x and w.
 * BiDi handling: We receive the shaped string + other props from text_props,
 * We care about the actual drawing location of the string, this is why we need
 * the visual string. We need to know how it's printed. After that we need to calculate
 * the reverse kerning in case of rtl parts. "pos" passed to this function is an
 * index in bytes, that is the actual byte location of the string, we need to find
 * the index in order to find it in the visual string.
 */

EAPI int
evas_common_font_query_pen_coords(RGBA_Font *fn, const Eina_Unicode *in_text, const Evas_Text_Props *text_props, int pos, int *cpen_x, int *cy, int *cadv, int *ch)
{
   int asc, desc;
   int position = 0;
   int ret_val = 0;
   EVAS_FONT_WALK_TEXT_INIT();

   asc = evas_common_font_max_ascent_get(fn);
   desc = evas_common_font_max_descent_get(fn);

   position = pos;
   /* If it's the null, choose location according to the direction. */
   if (text_props->len == position)
     {
        /* if it's rtl then the location is the left of the string,
         * otherwise, the right. */
#ifdef BIDI_SUPPORT
        if (text_props->bidi.dir == EVAS_BIDI_DIRECTION_RTL)
          {
             if (cpen_x) *cpen_x = 0;
             if (ch) *ch = asc + desc;
          }
        else
#endif
          {
             evas_common_font_query_advance(fn, in_text, text_props, cpen_x, ch);
          }
        if (cy) *cy = 0;
        if (cadv) *cadv = 0;
        ret_val = 1;
        goto end;
     }
   Evas_Coord cluster_start;
   int prev_cluster = -1;
   int found = 0, items = 1, item_pos = 1;
   int last_is_visible = 1;
   EVAS_FONT_WALK_TEXT_VISUAL_START()
     {
        EVAS_FONT_WALK_TEXT_WORK();

        if (prev_cluster != (int) EVAS_FONT_WALK_POS)
          {
             if (found)
               {
                  break;
               }
             else
               {
                  cluster_start = EVAS_FONT_WALK_PEN_X;
               }
          }
        last_is_visible = visible;

        if ((text_props->bidi.dir == EVAS_BIDI_DIRECTION_LTR) &&
              (EVAS_FONT_WALK_POS <= (size_t) position) &&
              ((((size_t) position) < EVAS_FONT_WALK_POS_NEXT) ||
               (EVAS_FONT_WALK_IS_LAST)))
          {
             found = 1;
#ifdef OT_SUPPORT
             if (evas_common_font_ot_is_enabled())
               {
                  items = evas_common_font_ot_cluster_size_get(text_props,
                        char_index);
               }
#endif
             item_pos = position - EVAS_FONT_WALK_POS + 1;
          }
        else if ((text_props->bidi.dir == EVAS_BIDI_DIRECTION_RTL) &&
              ((EVAS_FONT_WALK_POS_PREV > (size_t) position) ||
               (EVAS_FONT_WALK_IS_FIRST)) &&
              (((size_t) position) >= EVAS_FONT_WALK_POS))
          {
             found = 1;
#ifdef OT_SUPPORT
             if (evas_common_font_ot_is_enabled())
               {
                  items = evas_common_font_ot_cluster_size_get(text_props,
                        char_index);
               }
#endif
             item_pos = items - (position - EVAS_FONT_WALK_POS);
          }

        prev_cluster = EVAS_FONT_WALK_POS;
     }
   EVAS_FONT_WALK_TEXT_END();

   if (found)
     {
        Evas_Coord cluster_adv;
        cluster_adv = EVAS_FONT_WALK_PEN_X - cluster_start;
        if (cy) *cy = -asc;
        if (ch) *ch = asc + desc;
        if (last_is_visible)
          {
             if (cpen_x) *cpen_x = cluster_start +
               (cluster_adv / items) *
                  (item_pos - 1);
             if (cadv) *cadv = (cluster_adv / items);
          }
        else
          {
             if (cpen_x) *cpen_x = EVAS_FONT_WALK_PEN_X;
             if (cadv) *cadv = 0;
          }
        ret_val = 1;
        goto end;
     }
end:

   return ret_val;
}

/* char pos of text at xy pos
 * BiDi handling: Since we are looking for the char at the specific coords,
 * we have to get the visual string (to which the coords relate to), do
 * reverse kerning query because we are working on the visual string, and then
 * we need to get the logical position of the char we found from the visual string.
 */

EAPI int
evas_common_font_query_char_at_coords(RGBA_Font *fn, const Eina_Unicode *in_text, const Evas_Text_Props *text_props, int x, int y, int *cx, int *cy, int *cw, int *ch)
{
   int asc, desc;
   int ret_val = -1;
   EVAS_FONT_WALK_TEXT_INIT();

   asc = evas_common_font_max_ascent_get(fn);
   desc = evas_common_font_max_descent_get(fn);
   Evas_Coord cluster_start;
   int prev_cluster = -1;
   int found = 0, items = 1;
   EVAS_FONT_WALK_TEXT_VISUAL_START()
     {
        EVAS_FONT_WALK_TEXT_WORK();
        if (prev_cluster != (int) EVAS_FONT_WALK_POS)
          {
             if (found)
               {
                  break;
               }
             else
               {
                  cluster_start = EVAS_FONT_WALK_PEN_X;
               }
          }

        if (!visible) continue;

        /* we need to see if the char at the visual position is the char,
         * we check that by checking if it's before the current pen
         * position and the next */
        if ((x >= EVAS_FONT_WALK_PEN_X) && (x <= (EVAS_FONT_WALK_PEN_X + EVAS_FONT_WALK_X_ADV)) &&
              (y >= -asc) && (y <= desc))
          {
#ifdef OT_SUPPORT
             if (evas_common_font_ot_is_enabled())
               {
                  items = evas_common_font_ot_cluster_size_get(text_props,
                        char_index);
               }
#endif
             found = 1;
          }

        prev_cluster = EVAS_FONT_WALK_POS;
     }
   EVAS_FONT_WALK_TEXT_END();
   if (found)
     {
        int item_pos;
        Evas_Coord cluster_adv;
        cluster_adv = EVAS_FONT_WALK_PEN_X - cluster_start;

        if (text_props->bidi.dir == EVAS_BIDI_DIRECTION_LTR)
          {
             double part;
             part = cluster_adv / items;
             item_pos = (int) ((x - cluster_start) / part);
          }
        else
          {
             double part;
             part = cluster_adv / items;
             item_pos = items - ((int) ((x - cluster_start) / part)) - 1;
          }
        if (cx) *cx = EVAS_FONT_WALK_PEN_X +
          ((cluster_adv / items) * (item_pos - 1));
        if (cy) *cy = -asc;
        if (cw) *cw = (cluster_adv / items);
        if (ch) *ch = asc + desc;
        ret_val = prev_cluster + item_pos;
        goto end;
     }
end:

   return ret_val;
}

/* position of the char after the last char in the text that will fit in xy.
 * BiDi handling: We receive the shaped string + other props from text_props,
 * All we care about is char sizes + kerning so we only really need to get the
 * shaped string to utf8, and then just go through it like in english, as it's
 * just the logical string, nothing special about that.
 */

EAPI int
evas_common_font_query_last_up_to_pos(RGBA_Font *fn, const Eina_Unicode *in_text, const Evas_Text_Props *text_props __UNUSED__, int x, int y)
{
   int asc, desc;
   int ret=-1;
   EVAS_FONT_WALK_TEXT_INIT();

   asc = evas_common_font_max_ascent_get(fn);
   desc = evas_common_font_max_descent_get(fn);

   EVAS_FONT_WALK_TEXT_LOGICAL_START()
     {
        EVAS_FONT_WALK_TEXT_WORK();
        if (!visible) continue;

        if ((x >= EVAS_FONT_WALK_PEN_X) && (x <= (EVAS_FONT_WALK_PEN_X + EVAS_FONT_WALK_X_ADV)) &&
              (y >= -asc) && (y <= desc))
          {
             ret = EVAS_FONT_WALK_POS;
             goto end;
          }
     }
   EVAS_FONT_WALK_TEXT_END();

end:

  return ret;
}

