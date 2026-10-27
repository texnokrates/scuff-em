/* Copyright (C) 2005-2011 M. T. Homer Reid
 *
 * This file is part of SCUFF-EM.
 *
 * SCUFF-EM is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * SCUFF-EM is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * Owner.h -- pointer-ownership annotations (ISO C++ Core Guidelines R.2/R.3)
 *
 * A raw pointer in SCUFF-EM is either owning or non-owning, and the type
 * system cannot tell the two apart.  gsl::owner<T *> marks the owning
 * case; everything else is borrowed and must not be deleted by its holder.
 *
 * Conventions for new and refactored code:
 *
 *   - Owning resources live in std::unique_ptr / std::vector by default;
 *     use gsl::owner<T *> only where a raw pointer must stay raw (C
 *     interop, LAPACK workspaces, function arguments whose deleters vary).
 *   - A function that creates an object returns std::unique_ptr<T> or
 *     gsl::owner<T *>, and its documentation states who deletes it
 *     (I.11/I.12).  A function that returns a borrowed pointer returns
 *     plain T * and says so.
 *   - gsl::owner<T *> is a vocabulary type with zero runtime footprint;
 *     it exists so that reviewers and tooling (clang-tidy's
 *     cppcoreguidelines-owning-memory) can check resource discipline.
 */

#ifndef OWNER_H
#define OWNER_H

namespace gsl
{
template <typename T>
using owner = T;
}

#endif
