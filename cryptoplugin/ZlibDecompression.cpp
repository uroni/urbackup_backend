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

#include "ZlibDecompression.h"
#include "../Interface/Server.h"


size_t ZlibDecompression::decompress(const char *input, size_t input_size, std::vector<char> *output, bool flush, size_t output_off, bool *error)
{
	try
	{
		decomp.Put((const byte*)input, input_size);
		decomp.Flush(true);
		size_t rc=(size_t)decomp.MaxRetrievable();
		if(rc>0)
		{
			if(output->size()<rc+output_off)
			{
				output->resize(rc+output_off);
			}
			return decomp.Get((byte*)(&(*output)[output_off]), rc);
		}
		else
		{
			return 0;
		}
	}
	catch(const CryptoPP::ZlibDecompressor::Err& err)
	{
		Server->Log("Error during ZLib decompression: "+err.GetWhat(), LL_WARNING);
		if(error!=NULL)
		{
			*error=true;
		}
		return 0;
	}
}

size_t ZlibDecompression::decompress( const char *input, size_t input_size, char* output, size_t output_size, bool flush, bool *error)
{
	try
	{
		decomp.Put((const byte*)input, input_size);
		if(flush)
		{
			decomp.Flush(true);
		}
		return decomp.Get(reinterpret_cast<byte*>(output), output_size);
	}
	catch(const CryptoPP::ZlibDecompressor::Err& err)
	{
		Server->Log("Error during ZLib decompression: "+err.GetWhat(), LL_WARNING);
		if(error!=NULL)
		{
			*error=true;
		}
		return 0;
	}
}
