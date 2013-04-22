/*************************************************************************************/
/*      Copyright 2009 Barcelona Supercomputing Center                               */
/*                                                                                   */
/*      This file is part of the NANOS++ library.                                    */
/*                                                                                   */
/*      NANOS++ is free software: you can redistribute it and/or modify              */
/*      it under the terms of the GNU Lesser General Public License as published by  */
/*      the Free Software Foundation, either version 3 of the License, or            */
/*      (at your option) any later version.                                          */
/*                                                                                   */
/*      NANOS++ is distributed in the hope that it will be useful,                   */
/*      but WITHOUT ANY WARRANTY; without even the implied warranty of               */
/*      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the                */
/*      GNU Lesser General Public License for more details.                          */
/*                                                                                   */
/*      You should have received a copy of the GNU Lesser General Public License     */
/*      along with NANOS++.  If not, see <http://www.gnu.org/licenses/>.             */
/*************************************************************************************/
#ifndef _NANOS_VERSION_H_
#define _NANOS_VERSION_H_

#define NANOS_API_MASTER 5022
#define NANOS_API_WORKSHARING 1000
#define NANOS_API_DEPS_API 1001
#define NANOS_API_COPIES_API 1002
#define NANOS_API_OPENMP 6

#ifdef _MERCURIUM
#pragma nanos interface family(master) version(5022)
#pragma nanos interface family(worksharing) version(1000)
#pragma nanos interface family(deps_api) version(1001)
#pragma nanos interface family(copies_api) version(1002)
#pragma nanos interface family(openmp) version(6)
#endif // _MERCURIUM

#endif // _NANOS_VERSION_H_
