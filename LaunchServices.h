/*
 * LaunchServices.h — the umbrella, included as <LaunchServices/LaunchServices.h>.
 *
 * Copyright © 2026 Kyle J. Cardoza.
 * SPDX-FileCopyrightText: 2026 Kyle J. Cardoza
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Portions of this file may derive from GNUstep; see THIRD-PARTY.md.
 *
 * Apple's Launch Services is a CoreServices framework that gathers the type model (Uniform Type
 * Identifiers, in `UTType.h`) and the questions about applications (`LSInfo.h`). This umbrella is those
 * two and nothing else; a consumer includes the header it needs, as it does on the platform being
 * duplicated.
 */
#ifndef LAUNCHSERVICES_LAUNCHSERVICES_H
#define LAUNCHSERVICES_LAUNCHSERVICES_H

#include <LaunchServices/UTType.h>
#include <LaunchServices/LSInfo.h>
#include <LaunchServices/LSOpen.h>

#endif /* LAUNCHSERVICES_LAUNCHSERVICES_H */
