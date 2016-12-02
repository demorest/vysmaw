//
// Copyright © 2016 Associated Universities, Inc. Washington DC, USA.
//
// This file is part of vysmaw.
//
// vysmaw is free software: you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version.
//
// vysmaw is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
// A PARTICULAR PURPOSE.  See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along with
// vysmaw.  If not, see <http://www.gnu.org/licenses/>.
//
#ifndef VYS_PRIVATE_H_
#define VYS_PRIVATE_H_

#include <vys.h>

#define SIGNAL_MULTICAST_ADDRESS_KEY "signal_multicast_address"

extern char *config_vys_base(void)
	__attribute__((malloc,returns_nonnull));
extern char *load_config(
	const char *path, struct vys_error_record **error_record)
	__attribute__((malloc,returns_nonnull));

#endif /* VYS_PRIVATE_H_ */
