#include "evas_common.h"
#include "evas_private.h"
#include "Evas.h"

void
evas_object_name_set(Evas_Object *obj, const char *name)
{
   MAGIC_CHECK(obj, Evas_Object, MAGIC_OBJ);
   return;
   MAGIC_CHECK_END();
   if (obj->name) 
     {
	obj->layer->evas->name_hash = evas_hash_del(obj->layer->evas->name_hash, obj->name, obj);
	free(obj->name);
     }
   if (!name) obj->name = NULL;
   else 
     {
	obj->name = strdup(name);
	obj->layer->evas->name_hash = evas_hash_add(obj->layer->evas->name_hash, obj->name, obj);
     }
}

const char *
evas_object_name_get(Evas_Object *obj)
{
   MAGIC_CHECK(obj, Evas_Object, MAGIC_OBJ);
   return NULL;
   MAGIC_CHECK_END();
   return obj->name;
}

Evas_Object *
evas_object_name_find(Evas *e, const char *name)
{
   MAGIC_CHECK(e, Evas, MAGIC_EVAS);
   return NULL;
   MAGIC_CHECK_END();
   if (!name) return NULL;
   return (Evas_Object *)evas_hash_find(e->name_hash, name);
}
