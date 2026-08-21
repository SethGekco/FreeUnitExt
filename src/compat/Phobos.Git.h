#pragma once
/*
 * Stub for a header Phobos GENERATES at build time.
 *
 * Phobos/src/Phobos.version.h unconditionally does `#include "Phobos.Git.h"`.
 * That file does not exist in the repository — Phobos.props' ComputeGitInfo
 * target writes it into $(IntDir)Generated during a Phobos build. We compile a
 * handful of Phobos utility sources without importing Phobos.props, so nothing
 * ever generates it and the build dies with:
 *
 *   fatal error C1083: Cannot open include file: 'Phobos.Git.h'
 *
 * Everything that header would define (STR_GIT_COMMIT, STR_GIT_REF,
 * STR_GIT_DIRTY) is consumed behind #ifdef guards, so leaving them undefined is
 * valid and simply means "no git metadata" — which is correct here. This is
 * FreeUnitExt; Phobos' own version resource is not ours to stamp.
 *
 * Resolution works because `#include "Phobos.Git.h"` falls through from Phobos'
 * own src/ directory to the include path, where src/compat lives.
 */
