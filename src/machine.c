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

#include "libbuzztrax-gst/ui.h"

#include <gst/gstbin.h>
#include <gst/audio/audio-format.h>
#include <gst/audio/audio-resampler.h>
#include <gst/base/gstbasetransform.h>

#include <math.h>

#define GFX_WIDTH 64
#define GFX_HEIGHT 64

GST_DEBUG_CATEGORY(GST_CAT_DEFAULT);

G_DECLARE_FINAL_TYPE(BtEdbDistortInternal, btedb_distort_internal, BTEDB, DISTORT_INTERNAL, GstBaseTransform);
G_DECLARE_FINAL_TYPE(BtEdbDistort, btedb_distort, BTEDB, DISTORT, GstBin);

struct _BtEdbDistortInternal {
  GstBaseTransform parent;

  GstElement* resample_in;
  GstElement* resample_out;

  guint oversample;
  gfloat pos_db_pregain;
  gfloat pos_shape_a;
  gfloat pos_shape_b;
  gfloat pos_shape_exp;
  gboolean symmetric;
  gfloat neg_db_pregain;
  gfloat neg_shape_a;
  gfloat neg_shape_b;
  gfloat neg_shape_exp;
  gfloat db_postgain;
  
  gint perf_samples;
  gulong perf_time;
  GstClockTime perf_log_time;
};

G_DEFINE_TYPE(BtEdbDistortInternal, btedb_distort_internal, GST_TYPE_BASE_TRANSFORM);

struct _BtEdbDistort {
  GstBin parent;

  BtEdbDistortInternal* distort;
  GstElement* resample_in;
  GstElement* resample_out;
  BtEdbPropertiesSimple* props;

  BtUiCustomGfx gfx;
  guint32 gfx_data[GFX_WIDTH * GFX_HEIGHT];
};

G_DEFINE_TYPE(BtEdbDistort, btedb_distort, GST_TYPE_BIN);

static guint signal_bt_gfx_invalidated;

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

static inline gfloat plerp(gfloat a, gfloat b, gfloat alpha, gfloat power) {
  return powf(a + (b-a) * MAX(MIN(alpha,1),0), power);
}

static inline void distort(BtEdbDistortInternal* const self, gfloat* data, guint nsamples) {
  const gfloat pos_pregain = db_to_gain(self->pos_db_pregain);
  const gfloat neg_pregain = db_to_gain(self->neg_db_pregain);
  const gfloat postgain = db_to_gain(self->db_postgain);

  for (int i = 0; i < nsamples; i++) {
    const gboolean negative = data[i] < 0;
    const gboolean use_pos_values = self->symmetric || !negative;
    const gfloat shape0 = use_pos_values ? self->pos_shape_a : self->neg_shape_a;
    const gfloat shape1 = use_pos_values ? self->pos_shape_b : self->neg_shape_b;
    const gfloat shape_exp = use_pos_values ? self->pos_shape_exp : self->neg_shape_exp;
    const gfloat pregain = use_pos_values ? pos_pregain : neg_pregain;

    const gfloat data_abs = fabs(data[i]);
    data[i] = (1-exp(-fabs(data_abs * pregain)/plerp(shape0, shape1, data_abs, shape_exp))) * postgain;

    if (negative)
      data[i] *= -1;
  }
}

static const BtUiCustomGfx* on_gfx_request(BtEdbDistort* self) {
  gfloat data_in[GFX_WIDTH];
  guint32* const gfx = self->gfx.data;

  for (int i = 0; i < GFX_WIDTH*GFX_HEIGHT; ++i) {
    gfx[i] = 0x00000000;
  }

  for (int i = 0; i < GFX_WIDTH; ++i) {
    data_in[i] = -1.0f + 2 * ((gfloat)i/GFX_WIDTH);
  }

  distort(self->distort, data_in, GFX_WIDTH);
  
  for (int i = 1; i < GFX_WIDTH; ++i) {
    const gfloat val0 = 1.0f - ((data_in[i-1] + 1) / 2);
    const gfloat val1 = 1.0f - ((data_in[i] + 1) / 2);
    const guint y0 = MAX(MIN((gint)(val0 * (GFX_HEIGHT-1)), GFX_HEIGHT-1), 0);
    const guint y1 = MAX(MIN((gint)(val1 * (GFX_HEIGHT-1)), GFX_HEIGHT-1), 0);
    for (int y = MIN(y0,y1); y <= MAX(y0,y1); ++y) {
      gfx[i + GFX_WIDTH * y] = 0xFF000000;
    }
  }
  
  return &self->gfx;
}

static void set_property (GObject* object, guint prop_id, const GValue* value, GParamSpec* pspec) {
  BtEdbDistort* self = (BtEdbDistort*)object;
  g_assert(self->props);
  btedb_properties_simple_set(self->props, pspec, value);

  g_signal_emit(self, signal_bt_gfx_invalidated, 0);
}

static void get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec) {
  BtEdbDistort* self = (BtEdbDistort*)object;
  btedb_properties_simple_get(self->props, pspec, value);
}

static GstFlowReturn transform_ip(GstBaseTransform* baset, GstBuffer* gstbuf) {
  struct timespec clock_start;
  clock_gettime(CLOCK_MONOTONIC_RAW, &clock_start);

  BtEdbDistortInternal* const self = (BtEdbDistortInternal*)baset;
  
  GstMapInfo info;
  
  if (!gst_buffer_map(gstbuf, &info, GST_MAP_READ | GST_MAP_WRITE)) {
    GST_ERROR_OBJECT(self, "unable to map buffer for read & write");
    return GST_FLOW_ERROR;
  }

  gfloat* data = (gfloat*)info.data;
  guint nsamples = info.size / sizeof(typeof(*data));
  distort(self, data, nsamples);
   
  gst_buffer_unmap (gstbuf, &info);

  struct timespec clock_end;
  clock_gettime(CLOCK_MONOTONIC_RAW, &clock_end);
  self->perf_time += (clock_end.tv_sec - clock_start.tv_sec) * 1e9L + (clock_end.tv_nsec - clock_start.tv_nsec);
  self->perf_samples += nsamples;
  if (self->perf_time >= 1e8f) {
    GST_DEBUG("Avg perf: %f samples/sec\n", self->perf_samples / (self->perf_time / 1e9f));
    self->perf_time = 0;
    self->perf_samples = 0;
  }
  
  return GST_FLOW_OK;
}


static void btedb_distort_internal_init(BtEdbDistortInternal* const self) {
}

static gboolean set_caps(
  GstBaseTransform* const trans,
  GstCaps* incaps,
  GstCaps* outcaps) {

  GST_DEBUG_OBJECT(trans, "incaps %s", gst_caps_to_string(incaps));
  GST_DEBUG_OBJECT(trans, "outcaps %s", gst_caps_to_string(outcaps));

  return TRUE;
}

/*
  This callback is attached to the "sink" pad of the internal Distort effect.
  It's called at the start of playback, when GStreamer is figuring out how to fixate caps for all the machines.
  The goal is to force the upstream audioconvert element to use an oversampled rate on its "src" pad.

  This is called many times during negotiation, with caps becoming more specific as the negotiation progresses.
  Initially, there are only caps with a single structure having ranges.
  Then, there are caps with fixed and non-fixed structures as more information comes from upstream.
  At this point, "distort" can fix its own caps which will determine the oversampling rate from audioconvert.
 */
static gboolean query_distort_sink(GstPad* pad, GstObject* parent, GstQuery* query) {
  BtEdbDistortInternal* self = (BtEdbDistortInternal*)parent;
  
  switch(GST_QUERY_TYPE (query)) {
  case GST_QUERY_CAPS: {
    gboolean result = FALSE;
    GstCaps* caps_in;
    
    gst_query_parse_caps(query, &caps_in);
    GST_DEBUG_OBJECT(parent, "query caps_in %s", gst_caps_to_string(caps_in));

    GstCaps* caps_sink = gst_pad_get_current_caps(pad);
    GST_DEBUG_OBJECT(parent, "query caps_sink %s", gst_caps_to_string(caps_sink));

    GstPad* pad_src = gst_element_get_static_pad((GstElement*)parent, "src");
    GstCaps* caps_src = gst_pad_get_current_caps(pad_src);
    GST_DEBUG_OBJECT(parent, "query caps_src %s", gst_caps_to_string(caps_src));
    
    // If there are no caps in the query, then there is no information on which to act.
    // If the incoming caps are already fixed, then the final oversampled rate has already been presented upstream
    // and there is nothing to do.
    if (caps_in && !gst_caps_is_fixed(caps_in)) {
      // At this point the incoming caps will present one or more structures that may or may not be fixed.
      // Find the first structure having a fixed "rate" value. This is the sample rate coming from upstream.
      // If found, then present the fixed caps with oversampled rate to the upstream audioresample element.
      // If there is no fixed rate value, then negotiation still hasn't progressed far enough to be able to fix
      // "distort"'s caps.
      for (gint i = 0; i < gst_caps_get_size(caps_in); i++) {
        GstStructure* structure = gst_caps_get_structure(caps_in, i);

        int rate;
        if (gst_structure_get_int(structure, "rate", &rate)) {
          GST_DEBUG_OBJECT(parent, "query fixed caps_in rate is %d in structure idx %d", rate, i);
          GST_DEBUG_OBJECT(parent, "query oversample factor is %d", self->oversample);

          g_assert(self->oversample != 0);
          
          GstCaps* caps_out = gst_caps_copy_nth(caps_in, i);
          GST_DEBUG_OBJECT(parent, "query caps_out before %s", gst_caps_to_string(caps_out));
          gst_structure_set(gst_caps_get_structure(caps_out, 0), "rate", G_TYPE_INT, rate * self->oversample, NULL);
          GST_DEBUG_OBJECT(parent, "query caps_out %s", gst_caps_to_string(caps_out));
          gst_query_set_caps_result(query, caps_out);
          gst_caps_unref(caps_out);
          result = TRUE;
        }
      }
    } else {
      result = gst_pad_query_default(pad, parent, query);
    }

    if (caps_sink) gst_caps_unref(caps_sink);
    if (caps_src) gst_caps_unref(caps_src);
    gst_clear_object(&pad_src);
    return result;
  }
  default:
    return gst_pad_query_default(pad, parent, query);
  }
}

static void dispose(GObject* object) {
  BtEdbDistort* self = (BtEdbDistort*)object;
  btedb_properties_simple_free(self->props);
  self->props = 0;
}

static void btedb_distort_internal_class_init(BtEdbDistortInternalClass* const klass) {
  {
    GstElementClass* const aclass = (GstElementClass*)klass;

    gst_element_class_add_pad_template(aclass, gst_static_pad_template_get(&src_template));
    gst_element_class_add_pad_template(aclass, gst_static_pad_template_get(&sink_template));
  }

  {
    GstBaseTransformClass* aclass = (GstBaseTransformClass*)klass;
    aclass->transform_ip = transform_ip;
    aclass->set_caps = set_caps;
  }
}

static void btedb_distort_class_init(BtEdbDistortClass* const klass) {
  {
    GObjectClass* const aclass = (GObjectClass*)klass;
    aclass->set_property = set_property;
    aclass->get_property = get_property;
    aclass->dispose = dispose;

    // Note: variables will not be set to default values unless G_PARAM_CONSTRUCT is given.
    const GParamFlags flags =
      (GParamFlags)(G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);

    guint idx = 1;
    g_object_class_install_property(
      aclass, idx++,
      g_param_spec_uint("oversample", "Oversample", "Oversample", 1, 64, 2, flags ^ GST_PARAM_CONTROLLABLE));
    
    g_object_class_install_property(
      aclass, idx++,
      g_param_spec_float("pos-db-pregain", "+ve Pregain dB", "Positive Pregain dB", -144, 144, 20, flags));

    g_object_class_install_property(
      aclass, idx++,
      g_param_spec_float("pos-shape-a", "+ve Shape A", "Positive Shape Interp Point A", 0, 10, 1, flags));

    g_object_class_install_property(
      aclass, idx++,
      g_param_spec_float("pos-shape-b", "+ve Shape B", "Positive Shape Interp Point B", 0, 10, 1, flags));

    g_object_class_install_property(
      aclass, idx++,
      g_param_spec_float("pos-shape-exp", "+ve Shape Exp", "Positive Shape Interp Point Exponent", 0, 10, 1, flags));
    
    g_object_class_install_property(
      aclass, idx++,
      g_param_spec_boolean("symmetric", "Symmetric?", "Symmetric? (Use +ve values for -ve)", TRUE, flags));

    g_object_class_install_property(
      aclass, idx++,
      g_param_spec_float("neg-db-pregain", "-ve Pregain dB", "Negative Pregain dB", -144, 144, 20, flags));

    g_object_class_install_property(
      aclass, idx++,
      g_param_spec_float("neg-shape-a", "-ve Shape A", "Negative Shape Interp Point A", 0, 10, 1, flags));

    g_object_class_install_property(
      aclass, idx++,
      g_param_spec_float("neg-shape-b", "-ve Shape B", "Negative Shape Interp Point B", 0, 10, 1, flags));
    
    g_object_class_install_property(
      aclass, idx++,
      g_param_spec_float("neg-shape-exp", "-ve Shape Exp", "Negative Shape Interp Point Exponent", 0, 10, 1, flags));
    
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

    gst_element_class_add_metadata (
      aclass,
      GST_ELEMENT_METADATA_DOC_URI,
      "file://" DATADIR "" G_DIR_SEPARATOR_S "Gear" G_DIR_SEPARATOR_S "" PACKAGE ".html");
  }

  signal_bt_gfx_invalidated =
    g_signal_new (
      "bt-gfx-invalidated",
      G_TYPE_FROM_CLASS(klass),
      G_SIGNAL_ACTION | G_SIGNAL_RUN_LAST,
      0 /* offset */,
      NULL /* accumulator */,
      NULL /* accumulator data */,
      NULL /* C marshaller */,
      G_TYPE_NONE /* return_type */,
      0     /* n_params */);
  
  g_signal_new_class_handler (
    "bt-gfx-request",
    G_TYPE_FROM_CLASS(klass),
    G_SIGNAL_ACTION | G_SIGNAL_RUN_LAST,
    G_CALLBACK (on_gfx_request),
    NULL /* accumulator */,
    NULL /* accumulator data */,
    NULL /* C marshaller */,
    G_TYPE_POINTER /* return_type */,
    0     /* n_params */);
}

static void btedb_distort_init(BtEdbDistort* const self) {
  self->distort = (BtEdbDistortInternal*)g_object_new(btedb_distort_internal_get_type(), NULL);
  
  self->props = btedb_properties_simple_new((GObject*)self);
  btedb_properties_simple_add(self->props, "oversample", &self->distort->oversample);
  btedb_properties_simple_add(self->props, "pos-db-pregain", &self->distort->pos_db_pregain);
  btedb_properties_simple_add(self->props, "pos-shape-a", &self->distort->pos_shape_a);
  btedb_properties_simple_add(self->props, "pos-shape-b", &self->distort->pos_shape_b);
  btedb_properties_simple_add(self->props, "pos-shape-exp", &self->distort->pos_shape_exp);
  btedb_properties_simple_add(self->props, "symmetric", &self->distort->symmetric);
  btedb_properties_simple_add(self->props, "neg-db-pregain", &self->distort->neg_db_pregain);
  btedb_properties_simple_add(self->props, "neg-shape-a", &self->distort->neg_shape_a);
  btedb_properties_simple_add(self->props, "neg-shape-b", &self->distort->neg_shape_b);
  btedb_properties_simple_add(self->props, "neg-shape-exp", &self->distort->neg_shape_exp);
  btedb_properties_simple_add(self->props, "db-postgain", &self->distort->db_postgain);

  // GST_AUDIO_RESAMPLER_FILTER_MODE_FULL is fastest, but uses the most memory.
  self->resample_in = gst_element_factory_make("audioresample", NULL);
  g_object_set(self->resample_in, "sinc-filter-mode", GST_AUDIO_RESAMPLER_FILTER_MODE_FULL, NULL);
  self->resample_out = gst_element_factory_make("audioresample", NULL);
  g_object_set(self->resample_out, "sinc-filter-mode", GST_AUDIO_RESAMPLER_FILTER_MODE_FULL, NULL);

  self->distort->resample_in = self->resample_in;
  self->distort->resample_out = self->resample_out;
  
  gst_bin_add_many(GST_BIN(self), self->resample_in, (GstElement*)self->distort, self->resample_out, NULL);

  gst_element_link(self->resample_in, (GstElement*)self->distort);
  gst_element_link((GstElement*)self->distort, self->resample_out);

  {
    GstPad* pad = gst_element_get_static_pad(self->resample_in, "sink");
    GstPad* ghost = gst_ghost_pad_new("sink", pad);
    gst_element_add_pad((GstElement*)self, ghost);
    gst_object_unref(pad);
  }

  {
    GstPad* pad = gst_element_get_static_pad((GstElement*)self->distort, "sink");
    gst_pad_set_query_function(pad, query_distort_sink);
    gst_object_unref(pad);
  }
  
  {
    GstPad* pad = gst_element_get_static_pad(self->resample_out, "src");
    GstPad* ghost = gst_ghost_pad_new("src", pad);
    gst_element_add_pad((GstElement*)self, ghost);
    gst_object_unref(pad);
  }

  self->gfx = (struct BtUiCustomGfx){0, GFX_WIDTH, GFX_HEIGHT, self->gfx_data};
}
