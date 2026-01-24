// Meatloaf - A Commodore 64/128 multi-device emulator
// https://github.com/idolpx/meatloaf
// Copyright(C) 2020 James Johnston
//
// Meatloaf is free software : you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Meatloaf is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Meatloaf. If not, see <http://www.gnu.org/licenses/>.

//
// .CAR - C64OS archive file
//
// https://c64os.com/c64os/c64archiver/
// https://github.com/OpCoders-Inc/c64os-dev/blob/main/include/v1.08/os/s/c64archive.t
// https://github.com/RebeccaRGB/c64os-devtools/blob/main/tools/c64archive.py

// The V1 format can be completely ignored, it was only used during the betas of C64 OS, pre v1.0 of the OS's release. 
// The V2 format was and is used now, and the V3 format is identical to the V2 format, except that a 4-byte CRC32 of the entire file is tacked on the end of the file.

