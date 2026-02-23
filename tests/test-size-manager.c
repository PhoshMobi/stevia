/*
 * Copyright (C) 2025 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include "pos-config.h"

#include "pos-size-manager.c"

#include "phosh-osk-enums.h"
#include "pos-output-priv.h"

#include <glib.h>

#define ALL_FLAGS (PHOSH_OSK_SCALING_AUTO_PORTRAIT |  \
                   PHOSH_OSK_SCALING_AUTO_LANDSCAPE | \
                   PHOSH_OSK_SCALING_BOTTOM_DEAD_ZONE)

/* Display has no physical size specified */
static void
test_size_manager_landscape_display_no_phys_height (void)
{
  g_autoptr (PosOutput) output = g_object_new (POS_TYPE_OUTPUT, NULL);
  guint dz;

  pos_output_set_logical_width (output, 1000);
  pos_output_set_logical_height (output, 100);

  g_assert_false (pos_output_is_portrait (output));
  g_assert_cmpint (calc_height (output, &dz, ALL_FLAGS), ==, POS_INPUT_SURFACE_DEFAULT_HEIGHT);
}

/* Display has too little physical height */
static void
test_size_manager_landscape_display_too_little_phys_height (void)
{
  g_autoptr (PosOutput) output = g_object_new (POS_TYPE_OUTPUT, NULL);
  guint dz;

  pos_output_set_logical_width (output, 1000);
  pos_output_set_logical_height (output, 100);
  pos_output_set_physical_height (output, 90);

  g_assert_false (pos_output_is_portrait (output));
  g_assert_cmpint (calc_height (output, &dz, ALL_FLAGS), ==, POS_INPUT_SURFACE_DEFAULT_HEIGHT);
}


/* OSK has already too large logical height */
static void
test_size_manager_landscape_osk_too_little_logical_height (void)
{
  g_autoptr (PosOutput) output = g_object_new (POS_TYPE_OUTPUT, NULL);
  guint dz;

  pos_output_set_logical_width (output, 1000);
  pos_output_set_logical_height (output, 220);
  pos_output_set_physical_height (output, 130);

  g_assert_false (pos_output_is_portrait (output));
  g_assert_cmpint (calc_height (output, &dz, ALL_FLAGS), ==, POS_INPUT_SURFACE_DEFAULT_HEIGHT);
}


/* Librem 11 at scale 1.5 */
static void
test_size_manager_landscape_osk_scale_up_l11_add_150 (void)
{
  g_autoptr (PosOutput) output = g_object_new (POS_TYPE_OUTPUT, NULL);
  double pixel_size;
  guint dz;

  pos_output_set_logical_width (output, 2560 / 1.5);
  pos_output_set_logical_height (output, 1600 / 1.5);
  pos_output_set_physical_height (output, 150);
  pos_output_set_physical_width (output, 250);
  pixel_size = pos_output_get_logical_pixel_size (output);

  g_assert_false (pos_output_is_portrait (output));
  g_assert_cmpfloat (pixel_size, >, 0.09);
  g_assert_cmpint (calc_height (output, &dz, ALL_FLAGS), ==, 319);
  g_assert_cmpint (dz, ==, 0);
}


/* Use same data as L11 but with a display transform */
static void
test_size_manager_landscape_osk_scale_up_transform (void)
{
  g_autoptr (PosOutput) output = g_object_new (POS_TYPE_OUTPUT, NULL);
  double pixel_size;
  guint dz;

  pos_output_set_logical_width (output, 2560 / 1.5);
  pos_output_set_logical_height (output, 1600 / 1.5);
  pos_output_set_transform (output, WL_OUTPUT_TRANSFORM_90);
  pos_output_set_physical_width (output, 150);
  pos_output_set_physical_height (output, 250);
  pixel_size = pos_output_get_logical_pixel_size (output);

  g_assert_false (pos_output_is_portrait (output));
  g_assert_cmpfloat (pixel_size, >, 0.09);
  g_assert_cmpint (calc_height (output, &dz, ALL_FLAGS), ==, 319);
  g_assert_cmpint (dz, ==, 0);
}


/* Framework 13" at scale 1.5 */
static void
test_size_manager_landscape_osk_scale_up_fw13_add_150 (void)
{
  g_autoptr (PosOutput) output = g_object_new (POS_TYPE_OUTPUT, NULL);
  double pixel_size;
  guint dz;

  pos_output_set_logical_width (output, 2880 / 1.5);
  pos_output_set_logical_height (output, 1920 / 1.5);
  pos_output_set_physical_height (output, 190);
  pos_output_set_physical_width (output, 290);
  pixel_size = pos_output_get_logical_pixel_size (output);

  g_assert_false (pos_output_is_portrait (output));
  g_assert_cmpfloat (pixel_size, >, 0.09);
  g_assert_cmpint (calc_height (output, &dz, ALL_FLAGS), ==, 303);
  g_assert_cmpint (dz, ==, 0);
}


static void
test_size_manager_portrait_osk_too_little_logical_height (void)
{
  g_autoptr (PosOutput) output = g_object_new (POS_TYPE_OUTPUT, NULL);
  guint dz;

  pos_output_set_logical_width (output, 100);
  pos_output_set_logical_height (output, 220);
  pos_output_set_physical_height (output, 130);

  g_assert_true (pos_output_is_portrait (output));
  g_assert_cmpint (calc_height (output, &dz, ALL_FLAGS), ==, POS_INPUT_SURFACE_DEFAULT_HEIGHT);
}


static void
test_size_manager_portrait_osk_too_little_physical_height (void)
{
  g_autoptr (PosOutput) output = g_object_new (POS_TYPE_OUTPUT, NULL);
  double pixel_size;
  guint dz;

  pos_output_set_logical_width (output, 360);
  pos_output_set_logical_height (output, 720);
  pos_output_set_physical_height (output, 60);
  pixel_size = pos_output_get_logical_pixel_size (output);

  g_assert_true (pos_output_is_portrait (output));
  g_assert_cmpfloat (pixel_size, >, 0.08);
  g_assert_cmpint (calc_height (output, &dz, ALL_FLAGS), ==, POS_INPUT_SURFACE_DEFAULT_HEIGHT);
}

/* OnePlus 6T at scale 2.5 */
static void
test_size_manager_portrait_osk_scale_up_op6t_at_250 (void)
{
  g_autoptr (PosOutput) output = g_object_new (POS_TYPE_OUTPUT, NULL);
  double pixel_size;
  guint height, dz;

  pos_output_set_logical_width (output, 1080 / 2.5);
  pos_output_set_logical_height (output, 2340 / 2.5);
  pos_output_set_physical_height (output, 145);
  pixel_size = pos_output_get_logical_pixel_size (output);

  g_assert_true (pos_output_is_portrait (output));
  g_assert_cmpfloat (pixel_size, >, 0.06);
  height = calc_height (output, &dz, ALL_FLAGS);

  g_assert_cmpint (height, ==, 258);
  g_assert_cmpint (dz, ==, 51);
}

/* Librem 5 at scale 2 */
static void
test_size_manager_portrait_osk_scale_up_l5_at_200 (void)
{
  g_autoptr (PosOutput) output = g_object_new (POS_TYPE_OUTPUT, NULL);
  double pixel_size;
  guint height, dz;

  pos_output_set_logical_width (output, 720 / 2.0);
  pos_output_set_logical_height (output, 1440 / 2.0);
  pos_output_set_physical_height (output, 130);
  pixel_size = pos_output_get_logical_pixel_size (output);

  g_assert_true (pos_output_is_portrait (output));
  g_assert_cmpfloat (pixel_size, >, 0.09);
  height = calc_height (output, &dz, ALL_FLAGS);
  g_assert_cmpint (height, ==, 221);
  g_assert_cmpint (dz, ==, 44);
}


int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);

  /* Landscape scale up */
  g_test_add_func ("/pos/size-manager/landscape/display/no_phys_height",
                   test_size_manager_landscape_display_no_phys_height);
  g_test_add_func ("/pos/size-manager/landscape/display/too_little_phys_height",
                   test_size_manager_landscape_display_too_little_phys_height);
  g_test_add_func ("/pos/size-manager/landscape/osk/too_little_logical_height",
                   test_size_manager_landscape_osk_too_little_logical_height);
  g_test_add_func ("/pos/size-manager/landscape/osk/scale_up/l11@150",
                   test_size_manager_landscape_osk_scale_up_l11_add_150);
  g_test_add_func ("/pos/size-manager/landscape/osk/scale_up/transform",
                   test_size_manager_landscape_osk_scale_up_transform);
  g_test_add_func ("/pos/size-manager/landscape/osk/scale_up/fw13@150",
                   test_size_manager_landscape_osk_scale_up_fw13_add_150);

  /* Portrait scale up */
  g_test_add_func ("/pos/size-manager/portrait/osk/too_little_logical_height",
                   test_size_manager_portrait_osk_too_little_logical_height);
  g_test_add_func ("/pos/size-manager/portrait/osk/too_little_physical_height",
                   test_size_manager_portrait_osk_too_little_physical_height);
  g_test_add_func ("/pos/size-manager/portrait/osk/scale_up/op6t@250",
                   test_size_manager_portrait_osk_scale_up_op6t_at_250);
  g_test_add_func ("/pos/size-manager/portrait/osk/scale_up/l5@200",
                   test_size_manager_portrait_osk_scale_up_l5_at_200);

  return g_test_run ();
}
