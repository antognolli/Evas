{
   int Cx, Cy, i, j;
   DATA32 *dptr, *sptr, *pix, *pbuf;
   int a, r, g, b, rx, gx, bx, ax;
   int xap, yap, pos;
   int dyy, dxx;
   
   DATA32  **yp; 
   int *xp;
   int w = dst_clip_w;

   dptr = dst_ptr;
   pos = (src_region_y * src_w) + src_region_x;
   dyy = dst_clip_y - dst_region_y;
   dxx = dst_clip_x - dst_region_x;

   xp = xpoints + dxx;
   yp = ypoints + dyy;
   xapp = xapoints + dxx;
   yapp = yapoints + dyy;
   pbuf = buf;
/*#ifndef SCALE_USING_MMX */
/* for now there's no mmx down scaling - so C only */
#if 1
   if (src->flags & RGBA_IMAGE_HAS_ALPHA)
     {
	while (dst_clip_h--)
	  {
	     Cy = *yapp >> 16;
	     yap = *yapp & 0xffff;

	     while (dst_clip_w--)
	       {
		  Cx = *xapp >> 16;
		  xap = *xapp & 0xffff;

		  sptr = *yp + *xp + pos;
		  pix = sptr;
		  sptr += src_w;

		  ax = (A_VAL(pix) * xap) >> 9;
		  rx = (R_VAL(pix) * xap) >> 9;
		  gx = (G_VAL(pix) * xap) >> 9;
		  bx = (B_VAL(pix) * xap) >> 9;
		  pix++;
		  for (i = (1 << 14) - xap; i > Cx; i -= Cx)
		    {
		       ax += (A_VAL(pix) * Cx) >> 9;
		       rx += (R_VAL(pix) * Cx) >> 9;
		       gx += (G_VAL(pix) * Cx) >> 9;
		       bx += (B_VAL(pix) * Cx) >> 9;
		       pix++;
		    }
		  if (i > 0)
		    {
		       ax += (A_VAL(pix) * i) >> 9;
		       rx += (R_VAL(pix) * i) >> 9;
		       gx += (G_VAL(pix) * i) >> 9;
		       bx += (B_VAL(pix) * i) >> 9;
		    }

		  a = (ax * yap) >> 14;
		  r = (rx * yap) >> 14;
		  g = (gx * yap) >> 14;
		  b = (bx * yap) >> 14;

		  for (j = (1 << 14) - yap; j > Cy; j -= Cy)
		    {
		       pix = sptr;
		       sptr += src_w;
		       ax = (A_VAL(pix) * xap) >> 9;
		       rx = (R_VAL(pix) * xap) >> 9;
		       gx = (G_VAL(pix) * xap) >> 9;
		       bx = (B_VAL(pix) * xap) >> 9;
		       pix++;
		       for (i = (1 << 14) - xap; i > Cx; i -= Cx)
			 {
			    ax += (A_VAL(pix) * Cx) >> 9;
			    rx += (R_VAL(pix) * Cx) >> 9;
			    gx += (G_VAL(pix) * Cx) >> 9;
			    bx += (B_VAL(pix) * Cx) >> 9;
			    pix++;
			 }
		       if (i > 0)
			 {
			    ax += (A_VAL(pix) * i) >> 9;
			    rx += (R_VAL(pix) * i) >> 9;
			    gx += (G_VAL(pix) * i) >> 9;
			    bx += (B_VAL(pix) * i) >> 9;
			 }

		       a += (ax * Cy) >> 14;
		       r += (rx * Cy) >> 14;
		       g += (gx * Cy) >> 14;
		       b += (bx * Cy) >> 14;
		    }
		  if (j > 0)
		    {
		       pix = sptr;
		       sptr += src_w;
		       ax = (A_VAL(pix) * xap) >> 9;
		       rx = (R_VAL(pix) * xap) >> 9;
		       gx = (G_VAL(pix) * xap) >> 9;
		       bx = (B_VAL(pix) * xap) >> 9;
		       pix++;
		       for (i = (1 << 14) - xap; i > Cx; i -= Cx)
			 {
			    ax += (A_VAL(pix) * Cx) >> 9;
			    rx += (R_VAL(pix) * Cx) >> 9;
			    gx += (G_VAL(pix) * Cx) >> 9;
			    bx += (B_VAL(pix) * Cx) >> 9;
			    pix++;
			 }
		       if (i > 0)
			 {
			    ax += (A_VAL(pix) * i) >> 9;
			    rx += (R_VAL(pix) * i) >> 9;
			    gx += (G_VAL(pix) * i) >> 9;
			    bx += (B_VAL(pix) * i) >> 9;
			 }

		       a += (ax * j) >> 14;
		       r += (rx * j) >> 14;
		       g += (gx * j) >> 14;
		       b += (bx * j) >> 14;
		    }
		  *pbuf++ = ARGB_JOIN(a >> 5, r >> 5, g >> 5, b >> 5);
		  xp++;  xapp++;
	       }

	     if (dc->mod.use)
	       func_cmod(buf, dptr, w, dc->mod.r, dc->mod.g, dc->mod.b, dc->mod.a);
	     else if (dc->mul.use)
	       func_mul(buf, dptr, w, dc->mul.col);
	     else
	       func(buf, dptr, w);

	     pbuf = buf;
	     dptr += dst_w;   dst_clip_w = w;
	     xp = xpoints + dxx;
	     xapp = xapoints + dxx;
	     yp++;  yapp++;
	  }
     }
   else
     {
	while (dst_clip_h--)
	  {
	     Cy = *yapp >> 16;
	     yap = *yapp & 0xffff;

	     while (dst_clip_w--)
	       {
		  Cx = *xapp >> 16;
		  xap = *xapp & 0xffff;

		  sptr = *yp + *xp + pos;
		  pix = sptr;
		  sptr += src_w;

		  rx = (R_VAL(pix) * xap) >> 9;
		  gx = (G_VAL(pix) * xap) >> 9;
		  bx = (B_VAL(pix) * xap) >> 9;
		  pix++;
		  for (i = (1 << 14) - xap; i > Cx; i -= Cx)
		    {
		       rx += (R_VAL(pix) * Cx) >> 9;
		       gx += (G_VAL(pix) * Cx) >> 9;
		       bx += (B_VAL(pix) * Cx) >> 9;
		       pix++;
		    }
		  if (i > 0)
		    {
		       rx += (R_VAL(pix) * i) >> 9;
		       gx += (G_VAL(pix) * i) >> 9;
		       bx += (B_VAL(pix) * i) >> 9;
		    }

		  r = (rx * yap) >> 14;
		  g = (gx * yap) >> 14;
		  b = (bx * yap) >> 14;

		  for (j = (1 << 14) - yap; j > Cy; j -= Cy)
		    {
		       pix = sptr;
		       sptr += src_w;
		       rx = (R_VAL(pix) * xap) >> 9;
		       gx = (G_VAL(pix) * xap) >> 9;
		       bx = (B_VAL(pix) * xap) >> 9;
		       pix++;
		       for (i = (1 << 14) - xap; i > Cx; i -= Cx)
			 {
			    rx += (R_VAL(pix) * Cx) >> 9;
			    gx += (G_VAL(pix) * Cx) >> 9;
			    bx += (B_VAL(pix) * Cx) >> 9;
			    pix++;
			 }
		       if (i > 0)
			 {
			    rx += (R_VAL(pix) * i) >> 9;
			    gx += (G_VAL(pix) * i) >> 9;
			    bx += (B_VAL(pix) * i) >> 9;
			 }

		       r += (rx * Cy) >> 14;
		       g += (gx * Cy) >> 14;
		       b += (bx * Cy) >> 14;
		    }
		  if (j > 0)
		    {
		       pix = sptr;
		       sptr += src_w;
		       rx = (R_VAL(pix) * xap) >> 9;
		       gx = (G_VAL(pix) * xap) >> 9;
		       bx = (B_VAL(pix) * xap) >> 9;
		       pix++;
		       for (i = (1 << 14) - xap; i > Cx; i -= Cx)
			 {
			    rx += (R_VAL(pix) * Cx) >> 9;
			    gx += (G_VAL(pix) * Cx) >> 9;
			    bx += (B_VAL(pix) * Cx) >> 9;
			    pix++;
			 }
		       if (i > 0)
			 {
			    rx += (R_VAL(pix) * i) >> 9;
			    gx += (G_VAL(pix) * i) >> 9;
			    bx += (B_VAL(pix) * i) >> 9;
			 }

		       r += (rx * j) >> 14;
		       g += (gx * j) >> 14;
		       b += (bx * j) >> 14;
		    }
		  *pbuf++ = ARGB_JOIN(0xff, r >> 5, g >> 5, b >> 5);
		  xp++;  xapp++;
	       }

	     if (dc->mod.use)
	       func_cmod(buf, dptr, w, dc->mod.r, dc->mod.g, dc->mod.b, dc->mod.a);
	     else if (dc->mul.use)
	       func_mul(buf, dptr, w, dc->mul.col);
	     else
	       func(buf, dptr, w);

	     pbuf = buf;
	     dptr += dst_w;   dst_clip_w = w;
	     xp = xpoints + dxx;
	     xapp = xapoints + dxx;
	     yp++;  yapp++;
	  }
     }
#else
   /* MMX scaling down would go here */
#endif
}
