/*
 * Learned samples for the ELAN 04f3:0c7f sensor
 *
 * Copyright (C) 2026 John Hill-Tanner
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/*
 * The sensor sees a patch about 2.6 mm wide, so twelve enrollment scans
 * cover only part of a finger, and a later touch that lands elsewhere is
 * rejected however genuine it is.  The driver therefore keeps touches that
 * matched the enrollment confidently, and matches later touches against
 * those as well.
 *
 * fprintd saves a print only when enrolling, so these cannot be added to
 * the stored print.  They live in a file of their own, one per enrollment,
 * named by a hash of the enrolled samples.
 *
 * File format: the GVariant LEARN_FORMAT, (version, samples), in GVariant
 * normal form.
 */

#include <errno.h>
#include <glib/gstdio.h>

#include "elan0c7f-learn.h"

#define LEARN_FORMAT "(u" ELAN0C7F_SAMPLES_TYPE ")"
#define LEARN_FORMAT_VERSION 1

const char *
elan0c7f_learn_dir (void)
{
  const char *dir = g_getenv ("ELAN0C7F_LEARN_DIR");

  if (!dir)
    return ELAN0C7F_LEARN_DIR_DEFAULT;
  return *dir ? dir : NULL;
}

gchar *
elan0c7f_learn_key (GVariant *enrolled_samples)
{
  g_autoptr(GVariant) normal = g_variant_get_normal_form (enrolled_samples);

  return g_compute_checksum_for_data (G_CHECKSUM_SHA256,
                                      g_variant_get_data (normal),
                                      g_variant_get_size (normal));
}

static gchar *
learn_path (const char *dir, const char *key)
{
  return g_build_filename (dir, key, NULL);
}

GVariant *
elan0c7f_learn_load (const char *dir, const char *key)
{
  g_autofree gchar *path = learn_path (dir, key);

  g_autoptr(GMappedFile) file = NULL;
  g_autoptr(GBytes) bytes = NULL;
  g_autoptr(GVariant) data = NULL;
  g_autoptr(GError) error = NULL;
  guint32 version;
  GVariant *samples;

  file = g_mapped_file_new (path, FALSE, &error);
  if (!file)
    {
      if (!g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
        g_warning ("Could not read learned samples %s: %s", path, error->message);
      return NULL;
    }

  bytes = g_mapped_file_get_bytes (file);
  data = g_variant_ref_sink (g_variant_new_from_bytes (G_VARIANT_TYPE (LEARN_FORMAT),
                                                       bytes, FALSE));
  /* A truncated or foreign file would otherwise read as default values. */
  if (!g_variant_is_normal_form (data))
    {
      g_warning ("Ignoring malformed learned samples %s", path);
      return NULL;
    }

  g_variant_get (data, "(u@" ELAN0C7F_SAMPLES_TYPE ")", &version, &samples);
  if (version != LEARN_FORMAT_VERSION)
    {
      g_variant_unref (samples);
      return NULL;
    }
  return samples;
}

GVariant *
elan0c7f_learn_add (GVariant *learned, GVariant *sample, guint cap)
{
  GVariantBuilder builder;
  gsize n = learned ? g_variant_n_children (learned) : 0;
  gsize skip;

  g_return_val_if_fail (cap > 0, NULL);
  g_return_val_if_fail (g_variant_is_of_type (sample, G_VARIANT_TYPE (ELAN0C7F_SAMPLE_TYPE)), NULL);

  /* Keep the newest cap - 1 old ones, so skin that changes with the
   * seasons replaces itself rather than being outvoted by old touches. */
  skip = n >= cap ? n - (cap - 1) : 0;

  g_variant_builder_init (&builder, G_VARIANT_TYPE (ELAN0C7F_SAMPLES_TYPE));
  for (gsize i = skip; i < n; i++)
    {
      g_autoptr(GVariant) old = g_variant_get_child_value (learned, i);
      g_variant_builder_add_value (&builder, old);
    }
  g_variant_builder_add_value (&builder, sample);
  return g_variant_ref_sink (g_variant_builder_end (&builder));
}

gboolean
elan0c7f_learn_save (const char *dir, const char *key, GVariant *learned,
                     GError **error)
{
  g_autofree gchar *path = learn_path (dir, key);

  g_autoptr(GVariant) data = NULL;
  g_autoptr(GVariant) normal = NULL;

  if (g_mkdir_with_parents (dir, 0700) != 0)
    {
      int saved = errno;

      g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (saved),
                   "Could not create %s: %s", dir, g_strerror (saved));
      return FALSE;
    }

  data = g_variant_ref_sink (g_variant_new ("(u@" ELAN0C7F_SAMPLES_TYPE ")",
                                            LEARN_FORMAT_VERSION, learned));
  normal = g_variant_get_normal_form (data);
  return g_file_set_contents_full (path, g_variant_get_data (normal),
                                   g_variant_get_size (normal),
                                   G_FILE_SET_CONTENTS_CONSISTENT, 0600, error);
}
