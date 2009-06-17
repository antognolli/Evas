/*
 * vim:ts=8:sw=3:sts=8:noexpandtab:cino=>5n-3f0^-2{2
 */

#include <assert.h>

#include "evas_common.h"
#include "evas_private.h"
#include "evas_image_private.h"

#define SCALECACHE 1

#define MAX_SCALEITEMS 32
#define MIN_SCALE_USES 3
//#define MIN_SCALE_AGE_GAP 5000
#define MAX_SCALECACHE_DIM 3200
#define FLOP_ADD 4
#define MAX_FLOP_COUNT 16
#define FLOP_DEL 1
#define SCALE_CACHE_SIZE 4 * 1024 * 1024
//#define SCALE_CACHE_SIZE 0

typedef struct _Scaleitem Scaleitem;

struct _Scaleitem
{
   EINA_INLIST;
   unsigned long long usage;
   unsigned long long usage_count;
   RGBA_Image *im, *parent_im;
   int src_x, src_y, src_w, src_h;
   int dst_w, dst_h;
   int flop;
   int size_adjust;
   Eina_Bool forced_unload : 1;
   Eina_Bool smooth : 1;
   Eina_Bool populate_me : 1;
};

#ifdef SCALECACHE
static unsigned long long use_counter = 0;

#ifdef BUILD_PTHREAD
static LK(cache_lock);
#endif
static Eina_Inlist *cache_list = NULL;
static int cache_size = 0;
static int init = 0;

static int max_cache_size = SCALE_CACHE_SIZE;
static int max_dimension = MAX_SCALECACHE_DIM;
static int max_flop_count = MAX_FLOP_COUNT;
static int max_scale_items = MAX_SCALEITEMS;
static int min_scale_uses = MIN_SCALE_USES;
#endif

void
evas_common_scalecache_init(void)
{
#ifdef SCALECACHE
   const char *s;

   init++;
   if (init > 1) return;
   use_counter = 0;
   LKI(cache_lock);
   s = getenv("EVAS_SCALECACHE_SIZE");
   if (s) max_cache_size = atoi(s) * 1024;
   s = getenv("EVAS_SCALECACHE_MAX_DIMENSION");
   if (s) max_dimension = atoi(s);
   s = getenv("EVAS_SCALECACHE_MAX_FLOP_COUNT");
   if (s) max_flop_count = atoi(s);
   s = getenv("EVAS_SCALECACHE_MAX_ITEMS");
   if (s) max_scale_items = atoi(s);
   s = getenv("EVAS_SCALECACHE_MIN_USES");
   if (s) min_scale_uses = atoi(s);
#endif
}

void
evas_common_scalecache_shutdown(void)
{
#ifdef SCALECACHE
   init--;
   LKD(cache_lock);
#endif
}

void
evas_common_rgba_image_scalecache_init(Image_Entry *ie)
{
#ifdef SCALECACHE
   RGBA_Image *im = (RGBA_Image *)ie;
   LKI(im->cache.lock);
#endif
}

void
evas_common_rgba_image_scalecache_shutdown(Image_Entry *ie)
{
#ifdef SCALECACHE
   RGBA_Image *im = (RGBA_Image *)ie;
   evas_common_rgba_image_scalecache_dirty(ie);
   LKD(im->cache.lock);
#endif
}

void
evas_common_rgba_image_scalecache_dirty(Image_Entry *ie)
{
#ifdef SCALECACHE
   RGBA_Image *im = (RGBA_Image *)ie;
   LKL(im->cache.lock);
   while (im->cache.list)
     {
        Scaleitem *sci;
        sci = im->cache.list->data;
        im->cache.list = eina_list_remove(im->cache.list, sci);
        if (sci->im)
          {
//             printf(" 0- %i\n", sci->dst_w * sci->dst_h * 4);
             LKL(cache_lock);
             evas_common_rgba_image_free(&sci->im->cache_entry);
             if (!sci->forced_unload)
               cache_size -= sci->dst_w * sci->dst_h * 4;
             else
               cache_size -= sci->size_adjust;
             cache_list = eina_inlist_remove(cache_list, (Eina_Inlist *)sci);
             LKU(cache_lock);
          }
        free(sci);
     }
   LKU(im->cache.lock);
#endif
}

void
evas_common_rgba_image_scalecache_orig_use(Image_Entry *ie)
{
#ifdef SCALECACHE
   RGBA_Image *im = (RGBA_Image *)ie;
   LKL(im->cache.lock);
   use_counter++;
   // FIXME: if orig not loaded, reload
   // FIXME: mark orig with current used counter
   im->cache.orig_usage++;
   im->cache.usage_count = use_counter;
   LKU(im->cache.lock);
#endif
}

int
evas_common_rgba_image_scalecache_usage_get(Image_Entry *ie)
{
#ifdef SCALECACHE
   RGBA_Image *im = (RGBA_Image *)ie;
   int size = 0;
   Eina_List *l;
   Scaleitem *sci;
   LKL(im->cache.lock);
   EINA_LIST_FOREACH(im->cache.list, l, sci)
     {
        if (sci->im) size += sci->dst_w * sci->dst_h * 4;
     }
   LKU(im->cache.lock);
   return size;
#else
   return 0;
#endif
}

#ifdef SCALECACHE
static void
_sci_fix_newest(RGBA_Image *im)
{
   Eina_List *l;
   Scaleitem *sci;
   
   im->cache.newest_usage = 0;
   im->cache.newest_usage_count = 0;
   EINA_LIST_FOREACH(im->cache.list, l, sci)
     {
        if (sci->usage > im->cache.newest_usage)
          im->cache.newest_usage = sci->usage;
        if (sci->usage_count > im->cache.newest_usage_count)
          im->cache.newest_usage_count = sci->usage_count;
     }
//   printf("_sci_fix_newest! -> %i\n", im->cache.newest_usage);
}

static Scaleitem *
_sci_find(RGBA_Image *im,
          RGBA_Draw_Context *dc, int smooth,
          int src_region_x, int src_region_y,
          int src_region_w, int src_region_h,
          int dst_region_w, int dst_region_h)
{
   Eina_List *l;
   Scaleitem *sci;

   EINA_LIST_FOREACH(im->cache.list, l, sci)
     {
        if (
            (sci->src_w == src_region_w) &&
            (sci->src_h == src_region_h) &&
            (sci->dst_w == dst_region_w) &&
            (sci->dst_h == dst_region_h) &&
            (sci->src_x == src_region_x) &&
            (sci->src_y == src_region_y) &&
            (sci->smooth == smooth)
            )
          {
             if (im->cache.list != l)
               {
                  im->cache.list = eina_list_remove_list(im->cache.list, l);
                  im->cache.list = eina_list_prepend(im->cache.list, sci);
               }
             return sci;
          }
     }
   if (eina_list_count(im->cache.list) > max_scale_items)
     {
        l = eina_list_last(im->cache.list);
        sci = l->data;
        im->cache.list = eina_list_remove_list(im->cache.list, l);
        if ((sci->usage == im->cache.newest_usage) ||
            (sci->usage_count == im->cache.newest_usage_count))
          _sci_fix_newest(im);
        if (sci->im)
          {
             evas_common_rgba_image_free(&sci->im->cache_entry);
             if (!sci->forced_unload)
               cache_size -= sci->dst_w * sci->dst_h * 4;
             else
               cache_size -= sci->size_adjust;
//             printf(" 1- %i\n", sci->dst_w * sci->dst_h * 4);
             cache_list = eina_inlist_remove(cache_list, (Eina_Inlist *)sci);
          }
        if (max_scale_items < 1) return NULL;
     }
   else
     {
        if (max_scale_items < 1) return NULL;

        if (eina_list_count(im->cache.list) > (max_scale_items - 1))
          return NULL;
        sci = malloc(sizeof(Scaleitem));
        memset(sci, 0, sizeof(Eina_Inlist));
        sci->parent_im = im;
     }
   sci->usage = 0;
   sci->usage_count = 0;
   sci->populate_me = 0;
   sci->smooth = smooth;
   sci->forced_unload = 0;
   sci->flop = 0;
   sci->im = NULL;
   sci->src_x = src_region_x;
   sci->src_y = src_region_y;
   sci->src_w = src_region_w;
   sci->src_h = src_region_h;
   sci->dst_w = dst_region_w;
   sci->dst_h = dst_region_h;
   im->cache.list = eina_list_prepend(im->cache.list, sci);
   return sci;
}

static void
_cache_prune(Scaleitem *notsci, Eina_Bool copies_only)
{
   Scaleitem *sci;
   while (cache_size > max_cache_size)
     {
        if (!cache_list) break;
        sci = (Scaleitem *)(cache_list);
        if (copies_only)
          {
             while ((sci) && (!sci->parent_im->image.data))
               sci = (Scaleitem *)(((Eina_Inlist *)sci)->next);
             if (!sci) return;
          }
        if (sci == notsci) return;
        if (sci->im)
          {
             evas_common_rgba_image_free(&sci->im->cache_entry);
             sci->im = NULL;
             sci->usage = 0;
             sci->usage_count = 0;
             sci->flop += FLOP_ADD;
             if (!sci->forced_unload)
               cache_size -= sci->dst_w * sci->dst_h * 4;
             else
               cache_size -= sci->size_adjust;
//             printf(" 2- %i\n", sci->dst_w * sci->dst_h * 4);
             cache_list = eina_inlist_remove(cache_list, (Eina_Inlist *)sci);
             memset(sci, 0, sizeof(Eina_Inlist));
          }
//        printf("FLUSH %i > %i\n", cache_size, max_cache_size);
      }
}
#endif

EAPI void
evas_common_rgba_image_scalecache_size_set(int size)
{
#ifdef SCALECACHE
   LKL(cache_lock);
   if (size != max_cache_size)
     {
        max_cache_size = size;
        _cache_prune(NULL, 1);
     }
   LKU(cache_lock);
#endif   
}

EAPI int
evas_common_rgba_image_scalecache_size_get(void)
{
#ifdef SCALECACHE
   int t;
   LKL(cache_lock);
   t = max_cache_size;
   LKU(cache_lock);
   return t;
#else
   return 0;
#endif   
}

EAPI void
evas_common_rgba_image_scalecache_flush(void)
{
#ifdef SCALECACHE
   int t;
   LKL(cache_lock);
   t = max_cache_size;
   max_cache_size = 0;
   _cache_prune(NULL, 1);
   max_cache_size = t;
   LKU(cache_lock);
#endif   
}

EAPI void
evas_common_rgba_image_scalecache_prepare(Image_Entry *ie, RGBA_Image *dst,
                                          RGBA_Draw_Context *dc, int smooth,
                                          int src_region_x, int src_region_y,
                                          int src_region_w, int src_region_h,
                                          int dst_region_x, int dst_region_y,
                                          int dst_region_w, int dst_region_h)
{
#ifdef SCALECACHE
   RGBA_Image *im = (RGBA_Image *)ie;
   Scaleitem *sci;
   if (!im->image.data) return;
   if ((dst_region_w == 0) || (dst_region_h == 0) ||
       (src_region_w == 0) || (src_region_h == 0)) return;
   LKL(im->cache.lock);
   use_counter++;
   if ((src_region_w == dst_region_w) && (src_region_h == dst_region_h))
     {
        // 1:1 scale.
        im->cache.orig_usage++;
        im->cache.usage_count = use_counter;
        LKU(im->cache.lock);
        return;
     }
   if ((!im->cache_entry.flags.alpha) && (!smooth))
     {
        // solid nearest scaling - it's actually the same speed cached or not,
        // or in some cases faster not cached
        im->cache.orig_usage++;
        im->cache.usage_count = use_counter;
        LKU(im->cache.lock);
        return;
     }
   LKL(cache_lock);
   sci = _sci_find(im, dc, smooth, 
                   src_region_x, src_region_y, src_region_w, src_region_h, 
                   dst_region_w, dst_region_h);
   if (!sci)
     {
        LKU(cache_lock);
        LKU(im->cache.lock);
        return;
     }
//   printf("%10i | %4i %4i %4ix%4i -> %4i %4i %4ix%4i | %i\n",
//          (int)use_counter,
//          src_region_x, src_region_y, src_region_w, src_region_h,
//          dst_region_x, dst_region_y, dst_region_w, dst_region_h,
//          smooth);
   if ((sci->usage >= min_scale_uses)
       && (ie->scale_hint != EVAS_IMAGE_SCALE_HINT_DYNAMIC)
//       && (sci->usage_count > (use_counter - MIN_SCALE_AGE_GAP))
       )
     {
        if (!sci->im)
          {
             if ((sci->dst_w < max_dimension) && 
                 (sci->dst_h < max_dimension))
               {
                  if (sci->flop <= max_flop_count)
                    {
                       sci->populate_me = 1;
                       im->cache.populate_count++;
                    }
               }
          }
     }
   sci->usage++;
   sci->usage_count = use_counter;
   LKU(cache_lock);
   if (sci->usage > im->cache.newest_usage) 
     im->cache.newest_usage = sci->usage;
//   printf("newset? %p %i > %i\n", im, 
//          (int)sci->usage, 
//          (int)im->cache.newest_usage);
   if (sci->usage_count > im->cache.newest_usage_count) 
     im->cache.newest_usage_count = sci->usage_count;
//   printf("  -------------- used %8i#, %8i@\n", (int)sci->usage, (int)sci->usage_count);
   LKU(im->cache.lock);
#endif
}

#ifdef SCALECACHE
//static int pops = 0;
//static int hits = 0;
//static int misses = 0;
//static int noscales = 0;
#endif

EAPI void
evas_common_rgba_image_scalecache_do(Image_Entry *ie, RGBA_Image *dst,
                                     RGBA_Draw_Context *dc, int smooth,
                                     int src_region_x, int src_region_y,
                                     int src_region_w, int src_region_h,
                                     int dst_region_x, int dst_region_y,
                                     int dst_region_w, int dst_region_h)
{
#ifdef SCALECACHE
   RGBA_Image *im = (RGBA_Image *)ie;
   Scaleitem *sci;
   int didpop = 0;
   int dounload = 0;
/*
   static int i = 0;

   i++;
   if (i > 2000)
     {
        printf("p: %6i, h: %6i, m: %6i, n: %6i\n",
               pops, hits, misses, noscales);
        i = 0;
     }
 */
   if ((dst_region_w == 0) || (dst_region_h == 0) ||
       (src_region_w == 0) || (src_region_h == 0)) return;
   LKL(im->cache.lock);
   if ((src_region_w == dst_region_w) && (src_region_h == dst_region_h))
     {
        if (im->cache_entry.space == EVAS_COLORSPACE_ARGB8888)
          evas_cache_image_load_data(&im->cache_entry);
        evas_common_image_colorspace_normalize(im);
//        noscales++;
        if (im->image.data)
          {
             if (smooth)
               evas_common_scale_rgba_in_to_out_clip_smooth(im, dst, dc,
                                                            src_region_x, src_region_y, 
                                                            src_region_w, src_region_h,
                                                            dst_region_x, dst_region_y, 
                                                            dst_region_w, dst_region_h);
             else
               evas_common_scale_rgba_in_to_out_clip_sample(im, dst, dc,
                                                            src_region_x, src_region_y, 
                                                            src_region_w, src_region_h,
                                                            dst_region_x, dst_region_y, 
                                                            dst_region_w, dst_region_h);
          }
        LKU(im->cache.lock);
        return;
     }
   LKL(cache_lock);
   sci = _sci_find(im, dc, smooth,
                   src_region_x, src_region_y, src_region_w, src_region_h,
                   dst_region_w, dst_region_h);
   LKU(cache_lock);
   if (!sci)
     {
        if (im->cache_entry.space == EVAS_COLORSPACE_ARGB8888)
          evas_cache_image_load_data(&im->cache_entry);
        evas_common_image_colorspace_normalize(im);
//        misses++;
        LKU(im->cache.lock);
        if (im->image.data)
          {
             if (smooth)
               evas_common_scale_rgba_in_to_out_clip_smooth(im, dst, dc,
                                                            src_region_x, src_region_y, 
                                                            src_region_w, src_region_h,
                                                            dst_region_x, dst_region_y, 
                                                            dst_region_w, dst_region_h);
             else
               evas_common_scale_rgba_in_to_out_clip_sample(im, dst, dc,
                                                            src_region_x, src_region_y, 
                                                            src_region_w, src_region_h,
                                                            dst_region_x, dst_region_y, 
                                                            dst_region_w, dst_region_h);
          }
        return;
     }
   if (sci->populate_me)
     {
        int size, osize, used;
        
        size = dst_region_w * dst_region_h;
        if (((((dst_region_w > 640) || (dst_region_h > 640)) &&
             (size > (480 * 480))) ||
             (ie->scale_hint == EVAS_IMAGE_SCALE_HINT_STATIC)) &&
            (ie->scale_hint != EVAS_IMAGE_SCALE_HINT_DYNAMIC))
          {
             Eina_List *l;
             Scaleitem *sci2;
             
             dounload = 1;
             osize = sci->parent_im->cache_entry.w * sci->parent_im->cache_entry.h;
             used = 0;
             EINA_LIST_FOREACH(im->cache.list, l, sci2)
               {
                  if (sci2->im) used += sci2->dst_w * sci2->dst_h;
               }
             if ((size < osize) && (used == 0))
               sci->size_adjust = 0;
             else
               {
                  osize -= used;
                  if (osize < 0) osize = 0;
                  size -= osize;
                  sci->size_adjust = size * 4; 
               }
          }
        else
          {
             size *= sizeof(DATA32);
             if ((cache_size + size) > max_cache_size)
               {
                  sci->populate_me = 0;
                  im->cache.populate_count--;
               }
          }
     }
   if (sci->populate_me)
     {
//        printf("##! populate!\n");
        sci->im = evas_common_image_new
          (dst_region_w, dst_region_h, im->cache_entry.flags.alpha);
        if (sci->im)
          {
             static RGBA_Draw_Context *ct = NULL;
        
             LKL(cache_lock);
             im->cache.orig_usage++;
             im->cache.usage_count = use_counter;
             im->cache.populate_count--;
//             pops++;
             if (!ct)
               {
                  // FIXME: static ct - nver can free on shutdown? not a leak
                  // or real harm - just annoying valgrind bitch
                  ct = evas_common_draw_context_new();
                  evas_common_draw_context_set_render_op(ct, _EVAS_RENDER_COPY);
               }
             if (im->cache_entry.space == EVAS_COLORSPACE_ARGB8888)
               evas_cache_image_load_data(&im->cache_entry);
             evas_common_image_colorspace_normalize(im);
             if (im->image.data)
               {
                  if (smooth)
                    evas_common_scale_rgba_in_to_out_clip_smooth
                    (im, sci->im, ct,
                     src_region_x, src_region_y, 
                     src_region_w, src_region_h,
                     0, 0,
                     dst_region_w, dst_region_h);
                  else
                    evas_common_scale_rgba_in_to_out_clip_sample
                    (im, sci->im, ct,
                     src_region_x, src_region_y, 
                     src_region_w, src_region_h,
                     0, 0,
                     dst_region_w, dst_region_h);
                  sci->populate_me = 0;
#if 0 // visual debug of cached images                  
                    {
                       int xx, yy;
                       DATA32 *pp;
                       
                       pp = sci->im->image.data;
                       for (yy = 0; yy < dst_region_h; yy++)
                         {
                            
                            for (xx = 0; xx < dst_region_w; xx++)
                              {
                                 if (yy & 0x1)
                                   {
                                      if (xx & 0x1) *pp = 0x882288ff;
                                   }
                                 else
                                   { 
                                      if (!(xx & 0x1)) *pp = 0x882288ff;
                                  }
                                 pp++;
                              }
                         }
                    }
#endif                  
               }
             if (dounload)
               {
                  sci->forced_unload = 1;
                  cache_size += sci->size_adjust;
               }
             else
               {
                  cache_size += sci->dst_w * sci->dst_h * 4;
               }
//             printf(" + %i @ flop: %i (%ix%i)\n", 
//                    sci->dst_w * sci->dst_h * 4, sci->flop, 
//                    sci->dst_w, sci->dst_h);
             cache_list = eina_inlist_append(cache_list, (Eina_Inlist *)sci);
             _cache_prune(sci, 0);
             LKU(cache_lock);
             didpop = 1;
          }
     }
   if (sci->im)
     {
        if (!didpop)
          {
             cache_list = eina_inlist_remove(cache_list, (Eina_Inlist *)sci);
             cache_list = eina_inlist_append(cache_list, (Eina_Inlist *)sci);
          }
        else
          {
             if (sci->flop > 0) sci->flop -= FLOP_DEL;
          }
//        printf("use cached!\n");
        evas_common_scale_rgba_in_to_out_clip_sample
          (sci->im, dst, dc,
           0, 0,
           dst_region_w, dst_region_h,
           dst_region_x, dst_region_y, 
           dst_region_w, dst_region_h);
//        hits++;
//        printf("check %p %i < %i\n", 
//               im,
//               (int)im->cache.orig_usage, 
//               (int)im->cache.newest_usage);
        if ((dounload) || 
            ((im->cache_entry.flags.loaded) && 
             ((!im->cs.no_free) 
#ifdef EVAS_CSERVE             
             || (ie->data1)
#endif             
              )  &&
             (im->cache_entry.space == EVAS_COLORSPACE_ARGB8888)))
          {
             if ((dounload) || (im->cache.orig_usage < 
                                (im->cache.newest_usage / 20)))
               {
                  evas_common_rgba_image_unload(&im->cache_entry);
               }
          }
        LKU(im->cache.lock);
     }
   else
     {
        if (im->cache_entry.space == EVAS_COLORSPACE_ARGB8888)
          evas_cache_image_load_data(&im->cache_entry);
        evas_common_image_colorspace_normalize(im);
//        misses++;
        LKU(im->cache.lock);
        if (im->image.data)
          {
             if (smooth)
               evas_common_scale_rgba_in_to_out_clip_smooth(im, dst, dc,
                                                            src_region_x, src_region_y, 
                                                            src_region_w, src_region_h,
                                                            dst_region_x, dst_region_y, 
                                                            dst_region_w, dst_region_h);
             else
               evas_common_scale_rgba_in_to_out_clip_sample(im, dst, dc,
                                                            src_region_x, src_region_y, 
                                                            src_region_w, src_region_h,
                                                            dst_region_x, dst_region_y, 
                                                            dst_region_w, dst_region_h);
          }
     }
#else   
   RGBA_Image *im = (RGBA_Image *)ie;
   if (im->cache_entry.space == EVAS_COLORSPACE_ARGB8888)
     evas_cache_image_load_data(&im->cache_entry);
   evas_common_image_colorspace_normalize(im);
   if (im->image.data)
     {
        if (smooth)
          evas_common_scale_rgba_in_to_out_clip_smooth(im, dst, dc,
                                                       src_region_x, src_region_y, 
                                                       src_region_w, src_region_h,
                                                       dst_region_x, dst_region_y, 
                                                       dst_region_w, dst_region_h);
        else
          evas_common_scale_rgba_in_to_out_clip_sample(im, dst, dc,
                                                       src_region_x, src_region_y, 
                                                       src_region_w, src_region_h,
                                                       dst_region_x, dst_region_y, 
                                                       dst_region_w, dst_region_h);
     }
#endif
}

#if 0
// to be done
void
evas_common_rgba_image_scalecache_XXX(Image_Entry *ie)
{
#ifdef SCALECACHE
   RGBA_Image *im = (RGBA_Image *)ie;
   LKL(im->cache.lock);
   // FIXME: XXX
   LKU(im->cache.lock);
#endif
}
#endif
