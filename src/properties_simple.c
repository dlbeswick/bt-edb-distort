/*
  Waveshaper effect for Buzztrax
  Copyright (C) 2020 David Beswick

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
  
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "properties_simple.h"
#include <stdio.h>

struct _BtEdbPropertiesSimple {
  GObject parent;
  GObject* owner;
  GArray* props;
};

G_DEFINE_TYPE(BtEdbPropertiesSimple, btedb_properties_simple, G_TYPE_OBJECT);

typedef struct {
  GParamSpec* pspec;
  void* var;
} PspecVar;

gboolean btedb_properties_simple_get(const BtEdbPropertiesSimple* self, GParamSpec* pspec, GValue* value) {
  for (guint i = 0; i < self->props->len; ++i) {
	PspecVar* const pspec_var = &g_array_index(self->props, PspecVar, i);
	
	if (pspec_var->pspec->name == pspec->name) {
	  switch (pspec_var->pspec->value_type) {
	  case G_TYPE_BOOLEAN:
		g_value_set_boolean(value, *(gint*)pspec_var->var);
		break;
	  case G_TYPE_INT:
		g_value_set_int(value, *(gint*)pspec_var->var);
		break;
	  case G_TYPE_UINT:
		g_value_set_uint(value, *(guint*)pspec_var->var);
		break;
	  case G_TYPE_FLOAT:
		g_value_set_float(value, *(gfloat*)pspec_var->var);
		break;
	  case G_TYPE_DOUBLE:
		g_value_set_double(value, *(gdouble*)pspec_var->var);
		break;
	  default:
		if (g_type_is_a(pspec_var->pspec->value_type, G_TYPE_ENUM))
		  g_value_set_enum(value, *(guint*)pspec_var->var);
		else
		  g_assert(FALSE);
	  }
	  return TRUE;
	}
  }
  return FALSE;
}

gboolean btedb_properties_simple_set(const BtEdbPropertiesSimple* self, GParamSpec* pspec, const GValue* value) {
  for (guint i = 0; i < self->props->len; ++i) {
	PspecVar* const pspec_var = &g_array_index(self->props, PspecVar, i);
	
	if (pspec_var->pspec == pspec) {
	  switch (pspec_var->pspec->value_type) {
	  case G_TYPE_BOOLEAN:
		(*(gint*)pspec_var->var) = g_value_get_boolean(value);
		break;
	  case G_TYPE_INT:
		(*(gint*)pspec_var->var) = g_value_get_int(value);
		break;
	  case G_TYPE_UINT:
		(*(guint*)pspec_var->var) = g_value_get_uint(value);
		break;
	  case G_TYPE_FLOAT:
		(*(gfloat*)pspec_var->var) = g_value_get_float(value);
		break;
	  case G_TYPE_DOUBLE:
		(*(gdouble*)pspec_var->var) = g_value_get_double(value);
		break;
	  default:
		if (g_type_is_a(pspec_var->pspec->value_type, G_TYPE_ENUM))
		  (*(guint*)pspec_var->var) = g_value_get_enum(value);
		else
		  g_assert(FALSE);
	  }
	  return TRUE;
	}
  }
  return FALSE;
}
	
void btedb_properties_simple_add(BtEdbPropertiesSimple* self, const char* prop_name, void* var) {
  PspecVar pspec_var;

  pspec_var.pspec = g_object_class_find_property(G_OBJECT_GET_CLASS(self->owner), prop_name);
  g_assert(pspec_var.pspec);
  
  pspec_var.var = var;
  
  g_array_append_val(self->props, pspec_var);
}

void btedb_properties_simple_finalize(GObject* const obj) {
  BtEdbPropertiesSimple* const self = (BtEdbPropertiesSimple*)obj;
  
  g_array_unref(self->props);
  
  G_OBJECT_CLASS(btedb_properties_simple_parent_class)->finalize(obj);
}

void btedb_properties_simple_class_init(BtEdbPropertiesSimpleClass* const klass) {
  GObjectClass* const gobject_class = (GObjectClass*)klass;
  gobject_class->finalize = btedb_properties_simple_finalize;
}

void btedb_properties_simple_init(BtEdbPropertiesSimple* const self) {
  self->props = g_array_new(FALSE, FALSE, sizeof(PspecVar));
}

BtEdbPropertiesSimple* btedb_properties_simple_new(GObject* owner) {
  BtEdbPropertiesSimple* const self = (BtEdbPropertiesSimple*)g_object_new(btedb_properties_simple_get_type(), NULL);
  self->owner = owner;
  return self;
}
