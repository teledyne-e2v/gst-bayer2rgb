/* GStreamer
 * Copyright (C) 2010 David Schleef <ds@entropywave.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>
#include "gstgray2bayer.h"

#define GST_CAT_DEFAULT gst_gray2bayer_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

static void gst_gray2bayer_finalize (GObject * object);

static GstCaps *gst_gray2bayer_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static gboolean
gst_gray2bayer_get_unit_size (GstBaseTransform * trans, GstCaps * caps,
    gsize * size);
static gboolean
gst_gray2bayer_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps);
static GstFlowReturn gst_gray2bayer_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf);

static GstStaticPadTemplate gst_gray2bayer_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("GRAY8"))
    );

#if 0
/* do these later */
static GstStaticPadTemplate gst_gray2bayer_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_GRAYx ";" GST_VIDEO_CAPS_xGRAY ";"
        GST_VIDEO_CAPS_BGRx ";" GST_VIDEO_CAPS_xBGR ";"
        GST_VIDEO_CAPS_GRAYA ";" GST_VIDEO_CAPS_AGRAY ";"
        GST_VIDEO_CAPS_BGRA ";" GST_VIDEO_CAPS_ABGR)
    );
#endif

static GstStaticPadTemplate gst_gray2bayer_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-bayer,"
        "format=(string){bggr,gbrg,grbg,rggb},"
        "width=[1,MAX],height=[1,MAX]," "framerate=(fraction)[0/1,MAX]")
    );

/* class initialization */

#define gst_gray2bayer_parent_class parent_class
G_DEFINE_TYPE (GstGRAY2Bayer, gst_gray2bayer, GST_TYPE_BASE_TRANSFORM);

static void
gst_gray2bayer_class_init (GstGRAY2BayerClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *base_transform_class =
      GST_BASE_TRANSFORM_CLASS (klass);

  gobject_class->finalize = gst_gray2bayer_finalize;

  gst_element_class_add_static_pad_template (element_class,
      &gst_gray2bayer_src_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_gray2bayer_sink_template);

  gst_element_class_set_static_metadata (element_class,
      "GRAY to Bayer converter",
      "Filter/Converter/Video",
      "Converts video/x-raw to video/x-bayer",
      "David Schleef <ds@entropywave.com>");

  base_transform_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_gray2bayer_transform_caps);
  base_transform_class->get_unit_size =
      GST_DEBUG_FUNCPTR (gst_gray2bayer_get_unit_size);
  base_transform_class->set_caps = GST_DEBUG_FUNCPTR (gst_gray2bayer_set_caps);
  base_transform_class->transform = GST_DEBUG_FUNCPTR (gst_gray2bayer_transform);

  GST_DEBUG_CATEGORY_INIT (gst_gray2bayer_debug, "gray2bayer", 0,
      "gray2bayer element");
}

static void
gst_gray2bayer_init (GstGRAY2Bayer * gray2bayer)
{

}

void
gst_gray2bayer_finalize (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->finalize (object);
}


static GstCaps *
gst_gray2bayer_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstGRAY2Bayer *gray2bayer;
  GstCaps *res_caps, *tmp_caps;
  GstStructure *structure;
  guint i, caps_size;

  gray2bayer = GST_GRAY_2_BAYER (trans);

  res_caps = gst_caps_copy (caps);
  caps_size = gst_caps_get_size (res_caps);
  for (i = 0; i < caps_size; i++) {
    structure = gst_caps_get_structure (res_caps, i);
    if (direction == GST_PAD_SRC) {
      gst_structure_set_name (structure, "video/x-raw");
      gst_structure_remove_field (structure, "format");
    } else {
      gst_structure_set_name (structure, "video/x-bayer");
      gst_structure_remove_fields (structure, "format", "colorimetry",
          "chroma-site", NULL);
    }
  }
  if (filter) {
    tmp_caps = res_caps;
    res_caps =
        gst_caps_intersect_full (filter, tmp_caps, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (tmp_caps);
  }
  GST_DEBUG_OBJECT (gray2bayer, "transformed %" GST_PTR_FORMAT " into %"
      GST_PTR_FORMAT, caps, res_caps);
  return res_caps;
}

static gboolean
gst_gray2bayer_get_unit_size (GstBaseTransform * trans, GstCaps * caps,
    gsize * size)
{
  GstStructure *structure;
  int width;
  int height;
  const char *name;

  structure = gst_caps_get_structure (caps, 0);

  if (gst_structure_get_int (structure, "width", &width) &&
      gst_structure_get_int (structure, "height", &height)) {
    name = gst_structure_get_name (structure);
    /* Our name must be either video/x-bayer video/x-raw */
    if (g_str_equal (name, "video/x-bayer")) {
      *size = GST_ROUND_UP_4 (width) * height;
      return TRUE;
    } else {
      /* For output, calculate according to format */
      *size = width * height;
      return TRUE;
    }

  }

  return FALSE;
}

static gboolean
gst_gray2bayer_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstGRAY2Bayer *gray2bayer = GST_GRAY_2_BAYER (trans);
  GstStructure *structure;
  const char *format;
  GstVideoInfo info;

  GST_DEBUG ("in caps %" GST_PTR_FORMAT " out caps %" GST_PTR_FORMAT, incaps,
      outcaps);

  if (!gst_video_info_from_caps (&info, incaps))
    return FALSE;

  gray2bayer->info = info;

  structure = gst_caps_get_structure (outcaps, 0);

  gst_structure_get_int (structure, "width", &gray2bayer->width);
  gst_structure_get_int (structure, "height", &gray2bayer->height);

  format = gst_structure_get_string (structure, "format");
  if (g_str_equal (format, "bggr")) {
    gray2bayer->format = GST_GRAY_2_BAYER_FORMAT_BGGR;
  } else if (g_str_equal (format, "gbrg")) {
    gray2bayer->format = GST_GRAY_2_BAYER_FORMAT_GBRG;
  } else if (g_str_equal (format, "grbg")) {
    gray2bayer->format = GST_GRAY_2_BAYER_FORMAT_GRBG;
  } else if (g_str_equal (format, "rggb")) {
    gray2bayer->format = GST_GRAY_2_BAYER_FORMAT_RGGB;
  } else {
    return FALSE;
  }

  return TRUE;
}

static GstFlowReturn
gst_gray2bayer_transform (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstGRAY2Bayer *gray2bayer = GST_GRAY_2_BAYER (trans);
  GstMapInfo map;
  guint8 *dest;
  guint8 *src;
  int i, j;
  int height = gray2bayer->height;
  int width = gray2bayer->width;
  GstVideoFrame frame;
  if (!gst_video_frame_map (&frame, &gray2bayer->info, inbuf, GST_MAP_READ))
    goto map_failed;

  if (!gst_buffer_map (outbuf, &map, GST_MAP_READ)) {
    gst_video_frame_unmap (&frame);
    goto map_failed;
  }
  //outbuf->pool->flushing = inbuf->pool->flushing;

  dest = map.data;
  src = GST_VIDEO_FRAME_PLANE_DATA (&frame, 0);
  memcpy(dest,src,height * width * sizeof(guint8));

  gst_buffer_unmap (outbuf, &map);
  gst_video_frame_unmap (&frame);
  return GST_FLOW_OK;

map_failed:
  GST_WARNING_OBJECT (trans, "Could not map buffer, skipping");
  return GST_FLOW_OK;
}



static gboolean
plugin_init (GstPlugin * plugin)
{
  //gst_element_register (plugin, "bayer2rgb", GST_RANK_NONE,
      //gst_bayer2rgb_get_type ());
    GST_DEBUG_CATEGORY_INIT(gst_gray2bayer_debug, "gray2bayer",
                            0, "Template gray2bayer");
  gst_element_register (plugin, "gray2bayer", GST_RANK_NONE,
      gst_gray2bayer_get_type ());

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    gray2bayer,
    "Elements to convert Bayer images",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)


