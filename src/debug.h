#pragma once
#include <gst/gstinfo.h>

// Note: this must match the generated .so file name.
#define GST_PLUGIN_NAME bt_edb_distort
#define GST_PLUGIN_DESC "Distort effect"
#define GST_MACHINE_NAME GST_PLUGIN_NAME
#define GST_MACHINE_DESC GST_PLUGIN_DESC
#define GST_MACHINE_CATEGORY "Filter/Effect/Audio"

#define GST_CAT_DEFAULT G_PASTE(GST_MACHINE_NAME, _debug)
GST_DEBUG_CATEGORY_EXTERN(GST_CAT_DEFAULT);
