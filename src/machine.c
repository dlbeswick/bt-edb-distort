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

#include "config.h"
#include "src/debug.h"
#include "src/properties_simple.h"

#include <gst/audio/audio-format.h>
#include <gst/base/gstbasetransform.h>

#include <math.h>

GST_DEBUG_CATEGORY(GST_CAT_DEFAULT);

G_DECLARE_FINAL_TYPE(BtEdbDistort, btedb_distort, BTEDB, DISTORT, GstBaseTransform);

struct _BtEdbDistort
{
  GstBaseTransform parent;

  gfloat power_pos;
  gfloat power_neg;
  gfloat db_pregain;
  gfloat db_postgain;
  gfloat clamp;
  gfloat clampsmooth;
  
  BtEdbPropertiesSimple* props;
  
  gint perf_samples;
  gulong perf_time;
  GstClockTime perf_log_time;
};

G_DEFINE_TYPE(BtEdbDistort, btedb_distort, GST_TYPE_BASE_TRANSFORM);

static gboolean plugin_init(GstPlugin* plugin) {
  GST_DEBUG_CATEGORY_INIT(
    GST_CAT_DEFAULT,
    G_STRINGIFY(GST_CAT_DEFAULT),
    GST_DEBUG_FG_WHITE | GST_DEBUG_BG_BLACK,
    GST_MACHINE_DESC);

  return gst_element_register(
    plugin,
    G_STRINGIFY(GST_MACHINE_NAME),
    GST_RANK_NONE,
    btedb_distort_get_type());
}

GST_PLUGIN_DEFINE(
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  GST_PLUGIN_NAME,
  GST_PLUGIN_DESC,
  plugin_init, VERSION, "GPL", PACKAGE_NAME, PACKAGE_BUGREPORT)

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw, "
        "format = (string) " GST_AUDIO_NE (F32) ", "
        "layout = (string) interleaved, "
        "rate = (int) [1, MAX], "
        "channels = (int) [1, MAX]")
    );

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw, "
        "format = (string) " GST_AUDIO_NE (F32) ", "
        "layout = (string) interleaved, "
        "rate = (int) [1, MAX], "
        "channels = (int) [1, MAX]")
    );

static gfloat db_to_gain(gfloat db) {
  return powf(10.0f, db / 20.0f);
}

static void set_property (GObject* object, guint prop_id, const GValue* value, GParamSpec* pspec) {
  BtEdbDistort* self = (BtEdbDistort*)object;
  btedb_properties_simple_set(self->props, pspec, value);
}

static void get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec) {
  BtEdbDistort* self = (BtEdbDistort*)object;
  btedb_properties_simple_get(self->props, pspec, value);
}

static inline gfloat plerp(gfloat a, gfloat b, gfloat alpha, gfloat power) {
  return powf(a + (b-a) * MAX(MIN(alpha,1),0), power);
}

static GstFlowReturn transform_ip(GstBaseTransform* baset, GstBuffer* gstbuf) {
  struct timespec clock_start;
  clock_gettime(CLOCK_MONOTONIC_RAW, &clock_start);

  BtEdbDistort* const self = (BtEdbDistort*)baset;
  
  GstMapInfo info;
  
  if (!gst_buffer_map(gstbuf, &info, GST_MAP_READ | GST_MAP_WRITE)) {
    GST_ERROR_OBJECT(self, "unable to map buffer for read & write");
    return GST_FLOW_ERROR;
  }

  gfloat* data = (gfloat*)info.data;
  const guint nsamples = info.size / sizeof(typeof(*data));
  const gfloat pregain = db_to_gain(self->db_pregain);
  const gfloat postgain = db_to_gain(self->db_postgain);
  const gfloat power_neg = self->power_neg;
  const gfloat power_pos = self->power_pos;

  for (int i = 0; i < nsamples; i++) {
    const gboolean negative = data[i] < 0;
    const gfloat abs_in = fabs(data[i]);
    gfloat out = powf(abs_in * pregain, negative ? power_neg : power_pos);

    if (self->clamp == 1) {
      out = MIN(out, 1);
    } else if (self->clampsmooth != 0) {
      out += (1 - out) * plerp(0, 1, (abs_in - self->clamp) / (1 - self->clamp), self->clampsmooth);
    }
    
    if (negative)
      out *= -1;
    
    data[i] = out * postgain;
  }
   
  gst_buffer_unmap (gstbuf, &info);

  struct timespec clock_end;
  clock_gettime(CLOCK_MONOTONIC_RAW, &clock_end);
  self->perf_time += (clock_end.tv_sec - clock_start.tv_sec) * 1e9L + (clock_end.tv_nsec - clock_start.tv_nsec);
  self->perf_samples += nsamples;
  if (self->perf_time >= 1e8f) {
    GST_INFO("Avg perf: %f samples/sec\n", self->perf_samples / (self->perf_time / 1e9f));
    self->perf_time = 0;
    self->perf_samples = 0;
  }
  
  return GST_FLOW_OK;
}


static void btedb_distort_init(BtEdbDistort* const self) {
  self->props = btedb_properties_simple_new((GObject*)self);
  btedb_properties_simple_add(self->props, "power-pos", &self->power_pos);
  btedb_properties_simple_add(self->props, "power-neg", &self->power_neg);
  btedb_properties_simple_add(self->props, "db-pregain", &self->db_pregain);
  btedb_properties_simple_add(self->props, "db-postgain", &self->db_postgain);
  btedb_properties_simple_add(self->props, "clamp", &self->clamp);
  btedb_properties_simple_add(self->props, "clamp-smooth", &self->clampsmooth);
}

static void dispose(GObject* object) {
  BtEdbDistort* self = (BtEdbDistort*)object;
  g_clear_object(&self->props);
}

static void btedb_distort_class_init(BtEdbDistortClass* const klass) {
  {
    GObjectClass* const aclass = (GObjectClass*)klass;
    aclass->set_property = set_property;
    aclass->get_property = get_property;
    aclass->dispose = dispose;

    const GParamFlags flags =
      (GParamFlags)(G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS);

    guint idx = 1;
    g_object_class_install_property(
      aclass, idx++,
      g_param_spec_float("db-pregain", "Pregain dB", "Pregain dB", -144, 144, 0, flags));

    g_object_class_install_property(
      aclass, idx++,
      g_param_spec_float("clamp", "Clamp", "Clamp", 0, 1, 0.5f, flags));

    g_object_class_install_property(
      aclass, idx++,
      g_param_spec_float("clamp-smooth", "Clamp Smooth", "Clamp Smooth", 0, 10, 2, flags));
    
    g_object_class_install_property(
      aclass, idx++,
      g_param_spec_float("power-pos", "+ve Power", "Power (Positive values)", 0, 5, 1, flags));

    g_object_class_install_property(
      aclass, idx++,
      g_param_spec_float("power-neg", "-ve Power", "Power (Negative values)", 0, 5, 1, flags));
    
    g_object_class_install_property(
      aclass, idx++,
      g_param_spec_float("db-postgain", "Postgain dB", "Postgain dB", -144, 144, 0, flags));
  }

  {
    GstElementClass* const aclass = (GstElementClass*)klass;
    gst_element_class_set_static_metadata(
      aclass,
      G_STRINGIFY(GST_MACHINE_NAME),
      GST_MACHINE_CATEGORY,
      GST_MACHINE_DESC,
      PACKAGE_BUGREPORT);
    
    gst_element_class_add_pad_template(aclass, gst_static_pad_template_get(&src_template));
    gst_element_class_add_pad_template(aclass, gst_static_pad_template_get(&sink_template));
    
    // TBD: docs
    /*gst_element_class_add_metadata (element_class, GST_ELEMENT_METADATA_DOC_URI,
    "file://" DATADIR "" G_DIR_SEPARATOR_S "gtk-doc" G_DIR_SEPARATOR_S "html"
    G_DIR_SEPARATOR_S "" PACKAGE "-gst" G_DIR_SEPARATOR_S "GstBtSimSyn.html");*/
  }

  {
    GstBaseTransformClass* aclass = (GstBaseTransformClass*)klass;
    aclass->transform_ip = transform_ip;
  }
}

