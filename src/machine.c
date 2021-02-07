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

#include <gst/gstbin.h>
#include <gst/audio/audio-format.h>
#include <gst/audio/audio-resampler.h>
#include <gst/base/gstbasetransform.h>

#include <math.h>

GST_DEBUG_CATEGORY(GST_CAT_DEFAULT);

G_DECLARE_FINAL_TYPE(BtEdbDistortInternal, btedb_distort_internal, BTEDB, DISTORT_INTERNAL, GstBaseTransform);
G_DECLARE_FINAL_TYPE(BtEdbDistort, btedb_distort, BTEDB, DISTORT, GstBin);

struct _BtEdbDistortInternal {
  GstBaseTransform parent;

  GstElement* resample_in;
  GstElement* resample_out;

  guint oversample;
  gfloat pos_db_pregain;
  gfloat pos_scale;
  gfloat pos_bias;
  gfloat pos_exponent;
  gboolean symmetric;
  gfloat neg_db_pregain;
  gfloat neg_scale;
  gfloat neg_bias;
  gfloat neg_exponent;
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
};

G_DEFINE_TYPE(BtEdbDistort, btedb_distort, GST_TYPE_BIN);

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
  g_assert(self->props);
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

  BtEdbDistortInternal* const self = (BtEdbDistortInternal*)baset;
  
  GstMapInfo info;
  
  if (!gst_buffer_map(gstbuf, &info, GST_MAP_READ | GST_MAP_WRITE)) {
    GST_ERROR_OBJECT(self, "unable to map buffer for read & write");
    return GST_FLOW_ERROR;
  }

  gfloat* data = (gfloat*)info.data;
  const guint nsamples = info.size / sizeof(typeof(*data));
  const gfloat pos_pregain = db_to_gain(self->pos_db_pregain);
  const gfloat neg_pregain = db_to_gain(self->neg_db_pregain);
  const gfloat postgain = db_to_gain(self->db_postgain);

  for (int i = 0; i < nsamples; i++) {
    const gboolean negative = data[i] < 0;
    const gboolean use_pos_values = self->symmetric || !negative;
    const gfloat exponent = use_pos_values ? self->pos_exponent : self->neg_exponent;
    const gfloat pregain = use_pos_values ? pos_pregain : neg_pregain;
    const gfloat scale = use_pos_values ? self->pos_scale : self->neg_scale;
    const gfloat bias = use_pos_values ? self->pos_bias : self->neg_bias;
    
    data[i] = 1.f/(1 + expf(-powf(fabs(data[i]*pregain), exponent)*scale + bias)) * postgain;
    
    if (negative)
      data[i] *= -1;
  }
   
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
    
    // TBD: docs
    /*gst_element_class_add_metadata (element_class, GST_ELEMENT_METADATA_DOC_URI,
    "file://" DATADIR "" G_DIR_SEPARATOR_S "gtk-doc" G_DIR_SEPARATOR_S "html"
    G_DIR_SEPARATOR_S "" PACKAGE "-gst" G_DIR_SEPARATOR_S "GstBtSimSyn.html");*/
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
      g_param_spec_uint("oversample", "Oversample", "Oversample", 1, 64, 8, flags ^ GST_PARAM_CONTROLLABLE));
    
    g_object_class_install_property(
      aclass, idx++,
      g_param_spec_float("pos-db-pregain", "+ve Pregain dB", "Positive Pregain dB", -144, 144, 20, flags));

    g_object_class_install_property(
      aclass, idx++,
      g_param_spec_float("pos-scale", "+ve Scale", "Positive Scale", -50, 50, 15, flags));
    
    g_object_class_install_property(
      aclass, idx++,
      g_param_spec_float("pos-bias", "+ve Bias", "Positive Bias", -50, 50, 15, flags));

    g_object_class_install_property(
      aclass, idx++,
      g_param_spec_float("pos-exponent", "+ve Exp", "Positive Exponent", 0, 10, 0.5, flags));

    g_object_class_install_property(
      aclass, idx++,
      g_param_spec_boolean("symmetric", "Symmetric?", "Symmetric? (Use +ve values for -ve)", TRUE, flags));

    g_object_class_install_property(
      aclass, idx++,
      g_param_spec_float("neg-db-pregain", "-ve Pregain dB", "Negative Pregain dB", -144, 144, 20, flags));

    g_object_class_install_property(
      aclass, idx++,
      g_param_spec_float("neg-scale", "-ve Scale", "Negative Scale", -50, 50, 15, flags));
    
    g_object_class_install_property(
      aclass, idx++,
      g_param_spec_float("neg-bias", "-ve Bias", "Negative Bias", -50, 50, 15, flags));

    g_object_class_install_property(
      aclass, idx++,
      g_param_spec_float("neg-exponent", "-ve Exp", "Negative Exponent", 0, 10, 1, flags));

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
  }
}

static void btedb_distort_init(BtEdbDistort* const self) {
  self->distort = (BtEdbDistortInternal*)g_object_new(btedb_distort_internal_get_type(), NULL);
  
  self->props = btedb_properties_simple_new((GObject*)self);
  btedb_properties_simple_add(self->props, "oversample", &self->distort->oversample);
  btedb_properties_simple_add(self->props, "pos-db-pregain", &self->distort->pos_db_pregain);
  btedb_properties_simple_add(self->props, "pos-scale", &self->distort->pos_scale);
  btedb_properties_simple_add(self->props, "pos-bias", &self->distort->pos_bias);
  btedb_properties_simple_add(self->props, "pos-exponent", &self->distort->pos_exponent);
  btedb_properties_simple_add(self->props, "symmetric", &self->distort->symmetric);
  btedb_properties_simple_add(self->props, "neg-db-pregain", &self->distort->neg_db_pregain);
  btedb_properties_simple_add(self->props, "neg-scale", &self->distort->neg_scale);
  btedb_properties_simple_add(self->props, "neg-bias", &self->distort->neg_bias);
  btedb_properties_simple_add(self->props, "neg-exponent", &self->distort->neg_exponent);
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
}
