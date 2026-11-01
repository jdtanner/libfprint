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

#pragma once

#include <glib.h>

G_BEGIN_DECLS

/* One sample, as stored in a print: (positions, descriptors). */
#define ELAN0C7F_SAMPLE_TYPE "(aday)"
#define ELAN0C7F_SAMPLES_TYPE "a(aday)"

/* Where learned samples are kept: $ELAN0C7F_LEARN_DIR if set (an empty
 * value turns learning off, returning NULL), otherwise
 * ELAN0C7F_LEARN_DIR_DEFAULT, which fprintd's sandbox allows it to write. */
#define ELAN0C7F_LEARN_DIR_DEFAULT "/var/lib/fprint/elan0c7f-learned"
const char *elan0c7f_learn_dir (void);

/* A key naming one enrollment: the SHA-256 of its samples.  Re-enrolling
 * gives new samples and so a new key, which starts learning afresh. */
gchar *elan0c7f_learn_key (GVariant *enrolled_samples);

/* The samples learned for @key, an ELAN0C7F_SAMPLES_TYPE array, or NULL if
 * there are none or the file is unreadable, of another format version, or
 * not in the expected shape.  Never an error: a bad file means "nothing
 * learned yet", and the next save replaces it. */
GVariant *elan0c7f_learn_load (const char *dir,
                               const char *key);

/* @learned (may be NULL) with @sample appended, keeping at most @cap
 * samples by dropping the oldest.  Returns a new (non-floating) reference. */
GVariant *elan0c7f_learn_add (GVariant *learned,
                              GVariant *sample,
                              guint     cap);

/* Write @learned for @key, creating @dir (0700) if needed.  The file is
 * replaced atomically and is readable by its owner only: learned samples
 * are fingerprint data, exactly as sensitive as the enrolled print. */
gboolean elan0c7f_learn_save (const char *dir,
                              const char *key,
                              GVariant   *learned,
                              GError    **error);

G_END_DECLS
