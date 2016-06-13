/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2016 Martin Raiber
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU Affero General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU Affero General Public License for more details.
*
*    You should have received a copy of the GNU Affero General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**************************************************************************/

#include "ZlibCompression.h"


ZlibCompression::ZlibCompression(int compression_level)
{
	comp.SetDeflateLevel(compression_level);
}

ZlibCompression::~ZlibCompression(void)
{
}

size_t ZlibCompression::compress(const char *input, size_t input_length, std::vector<char> *output, bool flush, size_t output_off)
{
	if(input_length>0)
	{
		comp.Put((const byte*)input, input_length);
	}
	if(flush)
	{
		comp.Flush(true);
	}
	size_t rc=(size_t)comp.MaxRetrievable();

	if(output->size()<rc+output_off)
	{
		output->resize(rc+output_off);
	}

	return comp.Get((byte*)(&(*output)[output_off]), rc);
}