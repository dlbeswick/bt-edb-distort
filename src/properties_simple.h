/*
  Distort effect for Buzztrax
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

#pragma once

#include <glib-object.h>

typedef struct _BtEdbPropertiesSimple BtEdbPropertiesSimple;

/*
  This class helps with avoiding some repetitive code around properties setting.
*/
BtEdbPropertiesSimple* btedb_properties_simple_new(GObject* owner);
void btedb_properties_simple_free(BtEdbPropertiesSimple* self);

void btedb_properties_simple_add(BtEdbPropertiesSimple* self, const char* prop_name, void* var);
gboolean btedb_properties_simple_get(const BtEdbPropertiesSimple* self, GParamSpec* pspec, GValue* value);
gboolean btedb_properties_simple_set(const BtEdbPropertiesSimple* self, GParamSpec* pspec, const GValue* value);

