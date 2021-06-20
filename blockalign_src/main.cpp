/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2018 Martin Raiber
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
#define __STDC_LIMIT_MACROS
#include <iostream>
#include <limits.h>
#include <fstream>
#include <stdint.h>
#include <string>

#include <map>
#include <vector>
#include <algorithm>
#include <memory>
#include <string.h>
#ifdef _WIN32
#include <fcntl.h>
#include <Windows.h>
#include <io.h>
#else
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#endif
#include <assert.h>
#ifdef BLOCKALIGN_USE_CRYPTOPP
#include "../cryptoplugin/cryptopp_inc.h"
#include "crc32c-adler.cpp"
#if (CRYPTOPP_VERSION < 564)
#include "crc.cpp"
#endif
#else
#include "crc32c-adler.h"
#include "crc.h"
#endif

#if defined(__FreeBSD__) || defined(__APPLE__)
#define lstat64 lstat
#define stat64 stat
#define fstat64 fstat
#define open64 open
#endif

const unsigned int blocksize_min = 64;
const unsigned int blocksize_max = 1024;
const size_t c_buffer_reset = 20;
const unsigned int blocksize_avg = blocksize_min + (blocksize_max - blocksize_min) / 2 + sizeof(unsigned short);
const size_t hash_search_limit = 10000;
const size_t c_fit_off = sizeof(unsigned short);
const std::string magic = "BLOCKALIGN#1";
const unsigned int double_check_lim = 100 * 1024; //100kb
const size_t max_backlog_size = 10 * 1024 * 1024; //10MB

namespace
{


	uint32_t crc32c(
		uint32_t crc,
		const char *input,
		size_t length)
	{
#if !defined(BLOCKALIGN_USE_CRYPTOPP) || (CRYPTOPP_VERSION < 564)
		uint32_t r2 = cryptopp_crc::crc32c_hw(crc, input, length);
#ifndef NDEBUG
		uint32_t r1 = crc32c_sw(input, length, crc);
		assert(r1 == r2);
#endif
		return r2;
#else
		const unsigned int CRC32_NEGL = 0xffffffffL;
		CryptoPP::CRC32C crc_tmp;
		assert(sizeof(crc_tmp)>=sizeof(void*)+sizeof(CRC32_NEGL));
		assert(memcmp((char*)&crc_tmp + sizeof(void*), &CRC32_NEGL, sizeof(CRC32_NEGL)) == 0);
		unsigned int m_crc = crc ^ CRC32_NEGL;
		memcpy((char*)&crc_tmp + sizeof(void*), &m_crc, sizeof(m_crc));
		crc_tmp.Update(reinterpret_cast<const unsigned char*>(input), length);
		crc_tmp.TruncatedFinal(reinterpret_cast<unsigned char*>(&m_crc), sizeof(m_crc));

#ifndef NDEBUG
		uint32_t r1 = crc32c_sw(input, length, crc);
		assert(r1 == m_crc);
#endif
		return m_crc;
#endif
	}
}

class HashDb
{
public:
    HashDb(std::string fn)
#ifdef _WIN32
        : hMap(INVALID_HANDLE_VALUE), view(NULL), has_error(false),
#else
		: hFile(-1), view(reinterpret_cast<int*>(MAP_FAILED)), has_error(false),
#endif
        next_idx(0)
    {
#ifdef _WIN32
        hFile = CreateFileA(fn.c_str(), GENERIC_READ, FILE_SHARE_READ,
            NULL, OPEN_EXISTING, 0, NULL);

        if (hFile == INVALID_HANDLE_VALUE)
        {
            has_error = true;
            return;
        }

		LARGE_INTEGER file_size;
        file_size.LowPart = GetFileSize(hFile, reinterpret_cast<LPDWORD>(&file_size.HighPart));
        num_entries = static_cast<size_t>(file_size.QuadPart / sizeof(int32_t));

        HANDLE hMap = CreateFileMappingA(hFile, NULL, PAGE_READONLY, 0, 0, NULL);

        if (hMap == INVALID_HANDLE_VALUE)
        {
            has_error = true;
            return;
        }

        view = reinterpret_cast<int*>(MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0));

        if (view == NULL)
        {
            has_error = true;
        }
#else
		hFile = open(fn.c_str(), O_RDONLY);

		if (hFile == -1)
		{
			has_error = true;
			return;
		}

		struct stat64 sb;
		if (fstat64(hFile, &sb) == -1)
		{
			has_error = true;
			return;
		}

		fsize = sb.st_size;
		num_entries = static_cast<size_t>(sb.st_size / sizeof(int32_t));

		view = reinterpret_cast<int*>(mmap(nullptr, sb.st_size, PROT_READ, MAP_PRIVATE, hFile, 0));
		if (view == MAP_FAILED)
		{
			has_error = true;
			return;
		}
#endif
    }

    ~HashDb()
    {
#ifdef _WIN32
        if (view != NULL)
        {
            UnmapViewOfFile(view);
        }

        if (hMap != INVALID_HANDLE_VALUE)
        {
            CloseHandle(hMap);
        }

        if (hFile != INVALID_HANDLE_VALUE)
        {
            CloseHandle(hFile);
        }
#else
		if (view != MAP_FAILED)
		{
			munmap(view, fsize);
		}

		if (hFile != -1)
		{
			close(hFile);
		}
#endif
    }

    bool find(unsigned int crc, int64_t& offset, size_t& idx)
    {
        for (size_t i = next_idx; i < next_idx + hash_search_limit * 2 && i<num_entries; i += 2)
        {
            if (static_cast<unsigned int>(view[i]) == crc)
            {
                int64_t avg_offset = i / 2 * blocksize_avg;
                int64_t b_offset = avg_offset + view[i + 1];

                if (b_offset >= offset)
                {
                    idx = i;
                    offset = b_offset;
                    return true;
                }
            }
        }
        return false;
    }

    bool findAll(unsigned int crc, int64_t& offset, size_t& idx)
    {
        for (size_t i = 0; i < num_entries; i += 2)
        {
            if (static_cast<unsigned int>(view[i]) == crc)
            {
                int64_t avg_offset = i / 2 * blocksize_avg;
                int64_t b_offset = avg_offset + view[i + 1];

                idx = i;
                offset = b_offset;
                return true;
            }
        }
        return false;
    }

    void setNextIdx(size_t idx)
    {
        next_idx = idx;
    }

    bool hasError()
    {
        return has_error;
    }


private:
    bool has_error;
    int* view;
#ifdef _WIN32
    HANDLE hFile;
    HANDLE hMap;
#else
	int hFile;
	size_t fsize;
#endif

    size_t num_entries;
    size_t next_idx;
};

#ifndef _WIN32
int getPageSize()
{
	static int page_size = 0;
	if (page_size == 0)
	{
		page_size = sysconf(_SC_PAGE_SIZE);

		if (page_size <= 0)
		{
			page_size = 4096;
		}
	}
	return page_size;
}
#endif

class BlockMap
{
public:
    BlockMap(std::string fn, int64_t bm_size)
#ifdef _WIN32
        : hMap(INVALID_HANDLE_VALUE), view_start(NULL), has_error(false)
#else
		: hFile(-1), view_start(reinterpret_cast<char*>(MAP_FAILED)), has_error(false)
#endif
    {
#ifdef _WIN32
        hFile = CreateFileA(fn.c_str(), GENERIC_READ, FILE_SHARE_READ,
            NULL, OPEN_EXISTING, 0, NULL);

        if (hFile == INVALID_HANDLE_VALUE)
        {
            has_error = true;
            return;
        }

        HANDLE hMap = CreateFileMappingA(hFile, NULL, PAGE_READONLY, 0, 0, NULL);

        if (hMap == INVALID_HANDLE_VALUE)
        {
            has_error = true;
            return;
        }

        SYSTEM_INFO sys_info;
        GetSystemInfo(&sys_info);

        LARGE_INTEGER file_size;
        file_size.LowPart = GetFileSize(hFile, reinterpret_cast<LPDWORD>(&file_size.HighPart));

        LARGE_INTEGER start_map;
        start_map.QuadPart = file_size.QuadPart - bm_size*sizeof(int32_t) - sizeof(int64_t);

        int64_t actual_offset = start_map.QuadPart;

        if (start_map.QuadPart%sys_info.dwAllocationGranularity != 0)
        {
            start_map.QuadPart = (start_map.QuadPart / sys_info.dwAllocationGranularity)*sys_info.dwAllocationGranularity;
        }

        view_start = reinterpret_cast<char*>(MapViewOfFile(hMap, FILE_MAP_READ, start_map.HighPart, start_map.LowPart, 0));

        if (view_start == NULL)
        {
            has_error = true;
        }

        view = reinterpret_cast<int*>(view_start + (actual_offset - start_map.QuadPart));
#else
		hFile = open(fn.c_str(), O_RDONLY);

		if (hFile == -1)
		{
			has_error = true;
			return;
		}

		struct stat64 sb;
		if (fstat64(hFile, &sb) == -1)
		{
			has_error = true;
			return;
		}

		int64_t fsize = sb.st_size;

		int64_t	start_map = fsize - bm_size * sizeof(int32_t) - sizeof(int64_t);

		int64_t actual_offset = start_map;

		if (start_map % getPageSize() != 0)
		{
			start_map = (start_map / getPageSize())*getPageSize();
		}
		
		map_size = fsize - start_map;
		view_start = reinterpret_cast<char*>(mmap(nullptr, fsize-start_map, PROT_READ, MAP_PRIVATE, hFile, start_map));
		if (view_start == MAP_FAILED)
		{
			has_error = true;
		}
		view = reinterpret_cast<int*>(view_start + (actual_offset - start_map));
#endif
    }

    ~BlockMap()
    {
#ifdef _WIN32
        if (view_start != NULL)
        {
            UnmapViewOfFile(view_start);
        }

        if (hMap != INVALID_HANDLE_VALUE)
        {
            CloseHandle(hMap);
        }

        if (hFile != INVALID_HANDLE_VALUE)
        {
            CloseHandle(hFile);
        }
#else
		if (view_start != MAP_FAILED)
		{
			munmap(view_start, map_size);
		}

		if (hFile != -1)
		{
			close(hFile);
		}
#endif
    }

    int32_t get(size_t blocknr)
    {
        return view[blocknr];
    }

    bool hasError()
    {
        return has_error;
    }


private:
    bool has_error;
    int32_t* view;
    char* view_start;
#ifdef _WIN32
    HANDLE hFile;
    HANDLE hMap;
#else
	int hFile;
	int64_t map_size;
#endif
};

struct SOutputBufferItem
{
    unsigned int chash;
    std::vector<char> blockdata;
    int64_t offset;
};

class OutputBuffer
{
public:

    typedef std::map<int64_t, SOutputBufferItem>::iterator buffer_it_t;
    typedef std::vector<SOutputBufferItem>::iterator buffer_anywhere_it_t;

    OutputBuffer()
        : buffer_size(0)
    {}

    void add(int64_t offset, unsigned int chash, const char* blockdata, size_t blocklength, int64_t input_pos)
    {
        SOutputBufferItem new_item;
        new_item.chash = chash;
        new_item.offset = input_pos;
        buffer_it_t it = buffer_items.insert(std::make_pair(offset, new_item));

        it->second.blockdata.resize(blocklength);
        memcpy(it->second.blockdata.data(), blockdata, blocklength);

        buffer_size += blocklength;
    }

    void add_anywhere(unsigned int chash, const char* blockdata, size_t blocklength, int64_t input_pos)
    {
        SOutputBufferItem new_item;
        new_item.chash = chash;
        new_item.offset = input_pos;
        anywhere_buffer_items.push_back(new_item);

        SOutputBufferItem& added = anywhere_buffer_items[anywhere_buffer_items.size() - 1];

        added.blockdata.resize(blocklength);
        memcpy(added.blockdata.data(), blockdata, blocklength);

        buffer_size += blocklength;
    }

    buffer_it_t get(int64_t offset)
    {
        buffer_it_t it = buffer_items.find(offset);

        if (it != buffer_items.end())
        {
            return it;
        }

        return buffer_items.end();
    }

    void remove(buffer_it_t item)
    {
        buffer_size -= item->second.blockdata.size();

        buffer_items.erase(item);
    }

    void remove(buffer_anywhere_it_t item)
    {
        buffer_size -= (*item).blockdata.size();

        anywhere_buffer_items.erase(item);
    }


    buffer_anywhere_it_t best_fit(size_t blocklength, size_t fit_off)
    {
        if (blocklength < fit_off)
        {
            return anywhere_buffer_items.end();
        }

        buffer_anywhere_it_t best_fit = anywhere_buffer_items.end();

        for (buffer_anywhere_it_t i = anywhere_buffer_items.begin();
            i!=anywhere_buffer_items.end(); ++i)
        {
            SOutputBufferItem& item = *i;
            if ( (item.blockdata.size() == blocklength-fit_off || item.blockdata.size() + fit_off*2 <= blocklength) &&
                (best_fit==anywhere_buffer_items.end() || item.blockdata.size()>(*best_fit).blockdata.size()) )
            {
                best_fit = i;

                if (blocklength == SIZE_MAX)
                {
                    break;
                }

                if (item.blockdata.size() + fit_off == blocklength)
                {
                    break;
                }
            }
        }

        return best_fit;
    }

    buffer_it_t next(int64_t offset)
    {
        return buffer_items.lower_bound(offset);
    }

    size_t size()
    {
        return buffer_size;
    }

    buffer_it_t end()
    {
        return buffer_items.end();
    }

    buffer_anywhere_it_t best_end()
    {
        return anywhere_buffer_items.end();
    }

    bool empty()
    {
        return buffer_items.empty() && anywhere_buffer_items.empty();
    }

private:

    size_t buffer_size;
    std::multimap<int64_t, SOutputBufferItem> buffer_items;
    std::vector<SOutputBufferItem> anywhere_buffer_items;
};

unsigned int next_blockhash(const std::vector<char>& buffer, size_t offset, size_t buffer_len, size_t& length)
{
    if (buffer_len < blocksize_min)
    {
        length = buffer_len;
        return crc32c(0, &buffer[offset], buffer_len);
    }

    unsigned int chash = crc32c(0, &buffer[offset], blocksize_min);
    double prop = 1./(blocksize_max - blocksize_min);
    unsigned int rnd = crc32c(37, &buffer[offset], blocksize_min);
    for (size_t i = offset + blocksize_min; i < offset + buffer_len; ++i)
    {
        rnd = crc32c(rnd, &buffer[i], 1);

        if (rnd/(double)(UINT_MAX) <= prop)
        {
            length = i-offset;
            chash = crc32c(chash, &buffer[offset + blocksize_min], i - (offset + blocksize_min) );
            return chash;
        }
        
        prop = prop / (1. - prop);
    }

    length = buffer_len;

    chash = crc32c(chash, &buffer[offset + blocksize_min], buffer_len - blocksize_min);

    return chash;
}

size_t total_block_size = 0;
size_t n_total_blocks = 0;

unsigned int next_blockhash_stat(const std::vector<char>& buffer, size_t offset, size_t buffer_len, size_t& length)
{
    unsigned int ret = next_blockhash(buffer, offset, buffer_len, length);

    total_block_size += length;
    ++n_total_blocks;

    return ret;
}

size_t fill_buffer(size_t toread, size_t offset, std::vector<char>& buffer, std::istream& in_stream)
{
    size_t read = 0;
    do
    {
		std::streamsize r = in_stream.rdbuf()->sgetn(buffer.data() + offset + read, toread - read);
		in_stream.rdstate();
		in_stream.peek();
		read += static_cast<size_t>(r);

        if (in_stream.eof())
        {
            return read;
        }
    } while (read < toread);

    return read;
}

void write_item(unsigned int chash, size_t blocklength, const char* blockdata,
    uint64_t& nblock, int64_t& output_pos, int64_t input_pos, std::ofstream &blockhash_outf, std::ostream& out_stream,
    std::vector<int32_t>& blockmap)
{
    if (nblock == 17320)
    {
        int abc = 4;
    }

    uint64_t avg_pos = nblock * blocksize_avg;
    unsigned short blen = static_cast<unsigned short>(blocklength);
    int pos_offset_input = static_cast<int>(input_pos - avg_pos);

    blockmap.push_back(pos_offset_input);

    out_stream.write(reinterpret_cast<char*>(&blen), sizeof(blen));
    out_stream.write(blockdata, blocklength);

    assert(blocklength>0);

    int pos_offset_output = static_cast<int>(output_pos- avg_pos);
    blockhash_outf.write(reinterpret_cast<char*>(&chash), sizeof(chash));
    blockhash_outf.write(reinterpret_cast<char*>(&pos_offset_output), sizeof(pos_offset_output));

    output_pos += sizeof(blen);
    output_pos += blocklength;

    ++nblock;
}

void write_zeroes(int64_t& output_pos, int64_t offset,
    std::ofstream &blockhash_outf, std::ostream& out_stream,
    uint64_t& nblock, std::vector<int32_t>& blockmap)
{
    static char zero_buffer[USHRT_MAX] = {};

    while (output_pos < offset)
    {
        unsigned short blen = static_cast<unsigned short>((std::min)(static_cast<int64_t>(USHRT_MAX), offset - output_pos-static_cast<int64_t>(sizeof(unsigned short))));

        out_stream.write(reinterpret_cast<char*>(&blen), sizeof(blen));
        out_stream.write(zero_buffer, blen);

        output_pos += sizeof(blen);
        output_pos += blen;

        blockmap.push_back(INT_MAX);

        ++nblock;
    }
}

void fill_with_backlog(OutputBuffer &output_buffer,
    int64_t& output_pos, size_t blocklength, uint64_t& nblock,
    std::ofstream& blockhash_outf, std::ostream& out_stream, std::vector<int32_t>& blockmap)
{
    OutputBuffer::buffer_it_t next_item = output_buffer.next(output_pos);

    while (true)
    {
        size_t available_space;
        if (next_item != output_buffer.end())
        {
            available_space = static_cast<size_t>(next_item->first - output_pos);
        }
        else
        {
            available_space = blocklength;
        }

        if (available_space == 0)
        {
            break;
        }

        OutputBuffer::buffer_anywhere_it_t output_item = output_buffer.best_fit(available_space, c_fit_off);

        if (output_item != output_buffer.best_end())
        {
            write_item((*output_item).chash, (*output_item).blockdata.size(),
                (*output_item).blockdata.data(), nblock, output_pos, (*output_item).offset, blockhash_outf,
                out_stream, blockmap);

            output_buffer.remove(output_item);
        }
        else
        {
            break;
        }
    }
}

bool flush_buffer(uint64_t& nblock, OutputBuffer& output_buffer,
    std::ofstream& blockhash_outf, int64_t& output_pos,
    std::ostream& out_stream, std::vector<int32_t>& blockmap)
{
    OutputBuffer::buffer_it_t output_item = output_buffer.next(output_pos);

    if (output_item != output_buffer.end())
    {
        write_item(output_item->second.chash, output_item->second.blockdata.size(),
            output_item->second.blockdata.data(), nblock, output_pos, output_item->second.offset, blockhash_outf,
            out_stream, blockmap);

        output_buffer.remove(output_item);

        return true;
    }
    else
    {
        size_t available_space;
        if (output_item != output_buffer.end())
        {
            available_space = static_cast<size_t>(output_pos - output_item->first);
        }
        else
        {
            available_space = SIZE_MAX;
        }

        OutputBuffer::buffer_anywhere_it_t best_fit = output_buffer.best_fit(available_space, 6);

        if (best_fit != output_buffer.best_end())
        {
            write_item((*best_fit).chash, (*best_fit).blockdata.size(),
                (*best_fit).blockdata.data(), nblock, output_pos, (*best_fit).offset, blockhash_outf,
                out_stream, blockmap);

            output_buffer.remove(best_fit);

            return true;
        }

        return false;
    }
}

void enforce_output_buffer_size(OutputBuffer& output_buffer, uint64_t& nblock, std::ofstream& blockhash_outf,
    int64_t& output_pos, std::ostream& out_stream, std::vector<int32_t>& blockmap)
{
    while (output_buffer.size() > max_backlog_size)
    {
        bool b = flush_buffer(nblock, output_buffer, blockhash_outf, output_pos, out_stream, blockmap);
        OutputBuffer::buffer_it_t buffer_item;
        if (!b
            && output_buffer.size()>max_backlog_size
            && (buffer_item=output_buffer.next(output_pos)) != output_buffer.end() )
        {
            write_zeroes(output_pos, buffer_item->first, blockhash_outf, out_stream, nblock, blockmap);
        }
        else if (!b)
        {
            return;
        }
    }
}

void process_block(uint64_t& nblock, unsigned int chash, const char* blockdata, size_t blocklength,
    HashDb& hashdb, OutputBuffer& output_buffer, std::ofstream& blockhash_outf, int64_t& output_pos,
    int64_t input_pos, std::ostream& out_stream, std::vector<int32_t>& blockmap)
{
    OutputBuffer::buffer_it_t output_item = output_buffer.next(output_pos);

    bool can_write = true;

    if (output_item != output_buffer.end())
    {
        bool write_curr_item = true;

        if (!hashdb.hasError() && output_item->first - output_pos > double_check_lim)
        {
            int64_t offset = output_pos;
            size_t hash_idx;
            bool found = hashdb.find(chash, offset, hash_idx);
            if ( found &&
                offset==output_item->first + output_item->second.blockdata.size() + c_fit_off)
            {
                hashdb.setNextIdx(hash_idx);
                fill_with_backlog(output_buffer, output_pos, blocklength, nblock, blockhash_outf, out_stream, blockmap);
            }
            else
            {
                SOutputBufferItem item = output_item->second;
                output_buffer.remove(output_item);
                output_buffer.add_anywhere(item.chash, item.blockdata.data(), item.blockdata.size(), item.offset);
                write_curr_item = false;
            }
        }

        if (write_curr_item)
        {
            write_zeroes(output_pos, output_item->first, blockhash_outf, out_stream, nblock, blockmap);

            assert(output_pos == output_item->first);
            write_item(output_item->second.chash, output_item->second.blockdata.size(),
                output_item->second.blockdata.data(), nblock, output_pos, output_item->second.offset, blockhash_outf, out_stream,
                blockmap);

            output_buffer.remove(output_item);
        }
    }

    int64_t offset = output_pos;
    size_t hash_idx;
    if (!hashdb.hasError() && hashdb.find(chash, offset, hash_idx))
    {
        assert(offset >= output_pos);

        if (offset == output_pos && can_write)
        {
            hashdb.setNextIdx(hash_idx);

            write_item(chash, blocklength, blockdata, nblock,
                output_pos, input_pos, blockhash_outf, out_stream, blockmap);

            return;
        }
        else
        {
            output_buffer.add(offset, chash, blockdata, blocklength, input_pos);

            if (offset - output_pos > double_check_lim)
            {
                can_write = false;
            }
        }
    }
    else if (hashdb.hasError())
    {
        write_item(chash, blocklength, blockdata, nblock, output_pos,
            input_pos, blockhash_outf, out_stream, blockmap);

        can_write = false;
    }
    else
    {
        output_buffer.add_anywhere(chash, blockdata, blocklength, input_pos);
        can_write = false;
    }

    if (can_write)
    {
        fill_with_backlog(output_buffer, output_pos, blocklength, nblock, blockhash_outf, out_stream, blockmap);
    }
    else
    {
        enforce_output_buffer_size(output_buffer, nblock, blockhash_outf, output_pos, out_stream, blockmap);
    }
}

int64_t restore_buffers(std::map<int64_t, std::pair<unsigned short, char*> >& output_buffers, int64_t output_offset, std::ostream& out_stream)
{
    while (true)
    {
        std::map<int64_t, std::pair<unsigned short, char*> >::iterator it = output_buffers.find(output_offset);
        if (it == output_buffers.end())
        {
            break;
        }

        out_stream.write(it->second.second, it->second.first);

        output_offset += it->second.first;

        delete it->second.second;
        output_buffers.erase(it);
    }
    return output_offset;
}

bool restore_stream(const std::string& in_fn, std::istream& in_stream, std::ostream& out_stream)
{
    in_stream.seekg(-8, std::ios::end);
    int64_t bmsize;
    in_stream.read(reinterpret_cast<char*>(&bmsize), sizeof(bmsize));
    if (in_stream.gcount() != sizeof(bmsize))
    {
        return false;
    }

    std::ios::pos_type blockmap_offset_from_end = sizeof(bmsize) + bmsize*sizeof(int32_t);
    std::ios::pos_type blockmap_off = in_stream.tellg() - blockmap_offset_from_end;

    in_stream.seekg(0, std::ios::beg);

    std::string read_magic;
    read_magic.resize(magic.size());

    in_stream.read(&read_magic[0], read_magic.size());

    if (read_magic != magic)
    {
        return false;
    }

    unsigned int read_blocksize_avg;
    in_stream.read(reinterpret_cast<char*>(&read_blocksize_avg), sizeof(read_blocksize_avg));

    BlockMap block_map(in_fn, bmsize);

    int64_t nblock = 0;
    int64_t output_offset = 0;

    std::map<int64_t, std::pair<unsigned short, char*> > output_buffers;

    char* buffer = new char[blocksize_max];

    while (true)
    {
        unsigned short blen;

        if (in_stream.tellg() == blockmap_off)
        {
            return true;
        }

		size_t toread = sizeof(unsigned short);
		size_t read = 0;
		while (read < toread)
		{
			std::streamsize r = in_stream.readsome(reinterpret_cast<char*>(&blen) + read, sizeof(unsigned short) - read);

			if (r == 0)
				in_stream.peek();

			read += r;

			if (in_stream.eof())
			{
				output_offset = restore_buffers(output_buffers, output_offset, out_stream);
				return output_buffers.empty();
			}
		}

        int32_t pos_offset = block_map.get(static_cast<size_t>(nblock));

        if (pos_offset == INT_MAX)
        {
            in_stream.seekg(blen, std::ios::cur);
            ++nblock;
            continue;
        }

        if (blen > blocksize_max)
        {
            return false;
        }

        in_stream.read(buffer, blen);

        int64_t avg_pos = nblock * read_blocksize_avg;
        int64_t block_pos = avg_pos + pos_offset;

        if (nblock == 17320)
        {
            int abc = 45;
        }

        if (block_pos < output_offset)
        {
            return false;
        }
        
        if (block_pos == output_offset)
        {
            out_stream.write(buffer, blen);

            output_offset += blen;

            output_offset = restore_buffers(output_buffers, output_offset, out_stream);
        }
        else
        {
            output_buffers[block_pos] = std::make_pair(blen, buffer);
            buffer = new char[blocksize_max];
        }

        ++nblock;
    }

    delete[] buffer;
    assert(output_buffers.empty());

	return true;
}

#ifndef BLOCKALIGN_NO_MAIN
void show_version()
{
	std::cout << "blockalign v1.0" << std::endl;
	std::cout << "Copyright (C) 2017-2019 Martin Raiber" << std::endl;
	std::cout << "This is free software; see the source for copying conditions. There is NO" << std::endl;
	std::cout << "warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE." << std::endl;
	std::cout << std::endl << "Config:" << std::endl;
	cryptopp_crc::crc32c_alg_type alg_type = cryptopp_crc::get_crc32c_alg_type();
	if (alg_type==cryptopp_crc::crc32c_alg_type_sse4_crc)
	{
		std::cout << "Using SSE4 CRC32C instruction" << std::endl;
	}
	else if (alg_type == cryptopp_crc::crc32c_alg_type_arm64_crc)
	{
		std::cout << "Using ARM64 CRC32C instruction" << std::endl;
	}
	else
	{
		std::cout << "Using software CRC32C" << std::endl;
	}

}

void show_help()
{
	std::cout << std::endl << "USAGE:" << std::endl << std::endl;
	std::cout << "\tblockalign [--version] [--help] [-r] [input file] [output file] [hash file]" << std::endl << std::endl;
	std::cout << "blockalign takes [input file] and writes it to [output file]" << std::endl
		<< "and uses hashes in [hash file] to align blocks in [output file] such" << std::endl
		<< "that subsequent runs have the same blocks at the same positions." << std::endl
		<< "Use it with \"-r\" to remove the alignment. [input file] and [output file]" << std::endl
		<< "can be \"-\" in which case the input is read from stdin and output is" << std::endl
		<< "written to stdout respectively." << std::endl << std::endl;
	std::cout << "Where:" << std::endl << std::endl;
	std::cout << "\t-r, --restore" << std::endl;
	std::cout << "\t  Restore file to its original layout" << std::endl;
	std::cout << "\t[input file]" << std::endl;
	std::cout << "\t  File to read and align. If \"-\" input is read from stdin." << std::endl;
	std::cout << "\t[output file]" << std::endl;
	std::cout << "\t  File where the block aligned output is written." <<std::endl
		<< "\t  If \"-\" output is written to stdout." << std::endl;
	std::cout << "\t[hash file]" << std::endl;
	std::cout << "\t  File where the block hashes from the last run are stored and" << std::endl
		<< "\t  where the block hashes of this run will be written." << std::endl
		<< "\t  This file is not necessary for the -r/--restore operation." << std::endl;
}

#endif

int blockalign(const std::string& name, std::istream& in_stream, std::ostream& out_stream, bool verbose)
{
	std::vector<char> buffer;
	buffer.resize(blocksize_max*c_buffer_reset);

	uint64_t nblock = 0;

	size_t hashes_found = 0;
	size_t hashes_total = 0;

	{
		std::ios::pos_type start_pos = out_stream.tellp();

		out_stream.write(magic.c_str(), magic.size());
		out_stream.write(reinterpret_cast<const char*>(&blocksize_avg), sizeof(blocksize_avg));

		if (&out_stream != &std::cout && out_stream.tellp() - start_pos != magic.size() + sizeof(blocksize_avg))
		{
			std::cerr << "Out stream position wrong after writing magic" << std::endl;
			return 1;
		}

		std::ofstream blockhash_outf((name + ".new").c_str(), std::ios::binary | std::ios::out);

		HashDb blockhash_in(name);
		OutputBuffer output_buffer;

		int64_t output_pos = 0;
		int64_t input_pos = 0;

		size_t buffer_start = 0;
		size_t buffer_offset = 0;
		size_t reset_counter = 0;

		std::vector<int32_t> blockmap;

		while (true)
		{
			size_t toread = blocksize_max;

			if (buffer_offset > buffer_start)
			{
				if (buffer_offset - buffer_start < toread)
				{
					toread = toread + buffer_start - buffer_offset;
				}
				else
				{
					toread = 0;
				}
			}

			size_t read;
			if (toread>0)
			{
				read = fill_buffer(toread, buffer_offset, buffer, in_stream);
			}
			else
			{
				read = 0;
			}

			buffer_offset += read;

			size_t blocklength;
			unsigned int chash = next_blockhash_stat(buffer, buffer_start, buffer_offset - buffer_start, blocklength);

			++hashes_total;

			int64_t offset;
			size_t idx;
			if (!blockhash_in.hasError() && blockhash_in.findAll(chash, offset, idx))
			{
				++hashes_found;
			}

			assert(blocklength > 0);

			process_block(nblock, chash, buffer.data() + buffer_start, blocklength, blockhash_in,
				output_buffer, blockhash_outf, output_pos, input_pos, out_stream, blockmap);

			input_pos += blocklength;
			buffer_start += blocklength;

			if (buffer_start == buffer_offset)
			{
				buffer_start = 0;
				buffer_offset = 0;
				reset_counter = 0;
			}
			else
			{
				++reset_counter;
			}

			if (reset_counter == c_buffer_reset)
			{
				size_t len = buffer_offset - buffer_start;
				memcpy(buffer.data(), &buffer[buffer_start], len);
				buffer_start = 0;
				buffer_offset = len;
				reset_counter = 0;
			}

			if (read < toread)
			{
				break;
			}
		}

		while (buffer_start < buffer_offset)
		{
			size_t blocklength;
			unsigned int chash = next_blockhash_stat(buffer, buffer_start, buffer_offset - buffer_start, blocklength);

			process_block(nblock, chash, buffer.data() + buffer_start, blocklength, blockhash_in,
				output_buffer, blockhash_outf, output_pos, input_pos, out_stream, blockmap);

			input_pos += blocklength;
			buffer_start += blocklength;
		}

		while (!output_buffer.empty())
		{
			flush_buffer(nblock, output_buffer, blockhash_outf, output_pos, out_stream, blockmap);
		}

		//Alignment
		if (output_pos % sizeof(int32_t) != 0)
		{
			int64_t offset = (output_pos + c_fit_off) + (sizeof(int64_t) - (output_pos + c_fit_off) % sizeof(int32_t));
			write_zeroes(output_pos, offset, blockhash_outf, out_stream, nblock, blockmap);
		}

		std::ios::pos_type ppos = out_stream.tellp();
		out_stream.write(reinterpret_cast<char*>(blockmap.data()), blockmap.size() * sizeof(int32_t));
		int64_t bmsize = blockmap.size();
		out_stream.write(reinterpret_cast<char*>(&bmsize), sizeof(bmsize));

		if (&out_stream != &std::cout && out_stream.tellp() - ppos != blockmap.size() * sizeof(int32_t) + sizeof(bmsize))
		{
			std::cerr << "Out stream position wrong after writing block map" << std::endl;
			return 1;
		}
	}

	if (verbose)
	{
		double avg_blocksize = (double)total_block_size / n_total_blocks + sizeof(unsigned short);
		double hashes_found_pc = ((double)(hashes_found)*100.) / ((double)hashes_total);

		std::cerr << "Block align finished. Average block size " << avg_blocksize << ". Hashes found " << hashes_found_pc << "%" << std::endl;
	}

	return 0;
}

#ifndef BLOCKALIGN_NO_MAIN
int main(int argc, char* argv[])
{
    bool restore = false;
	bool has_input_param = false;
	bool verbose = false;

    std::string input_fn;
    std::string output_fn;
    std::string name;

    for (int i = 1; i < argc; ++i)
    {
        if ( (std::string(argv[i]) == "-r"
			|| std::string(argv[i]) == "--restore")
			&& !has_input_param)
        {
            restore = true;
        }
		else if (std::string(argv[i]) == "--help"
			&& !has_input_param)
		{
			show_help();
			return 0;
		}
		else if (std::string(argv[i]) == "--version"
			&& !has_input_param)
		{
			show_version();
			return 0;
		}
		else if (std::string(argv[i]) == "-v"
			&& !has_input_param)
		{
			verbose = true;
		}
        else if (input_fn.empty())
        {
            input_fn = argv[i];
			has_input_param = true;
        }
        else if (output_fn.empty())
        {
            output_fn = argv[i];
			has_input_param = true;
        }
        else if (name.empty() && !restore)
        {
            name = argv[i];
			has_input_param = true;
        }
        else
        {
            std::cerr << "Too many arguments" << std::endl;
            return 1;
        }
    }


    if (input_fn.empty())
    {
        std::cerr << "Input filename not given as argument" << std::endl;
		show_help();
        return 1;
    }

    if (input_fn.empty())
    {
        std::cerr << "Output filename not given as argument" << std::endl;
		show_help();
        return 1;
    }

    if (name.empty() && !restore)
    {
        std::cerr << "Hash output name not given as an argument" << std::endl;
		show_help();
        return 1;
    }

	std::istream* input;
	std::fstream in_stream;

	if (input_fn == "-")
	{
		if (restore)
		{
			std::cerr << "Restore from stdin not supported. Please provide seekable file." << std::endl;
			return 2;
		}

#ifdef _WIN32
		_setmode(_fileno(stdin), _O_BINARY);
#endif
		input = &std::cin;
	}
	else
	{
		in_stream.open(input_fn.c_str(), std::ios::binary | std::ios::in);
		if (!in_stream.is_open())
		{
			std::cerr << "Cannot open input stream \"" << input_fn << "\"" << std::endl;
			return 1;
		}

		input = &in_stream;
	}

	std::ostream* output;
	std::fstream out_stream;

	if (output_fn == "-")
	{
#ifdef _WIN32
		_setmode(_fileno(stdout), _O_BINARY);
#endif
		output = &std::cout;
	}
	else
	{
		out_stream.open(output_fn.c_str(), std::ios::binary | std::ios::out);
		if (!out_stream.is_open())
		{
			std::cerr << "Cannot open output stream \"" << output_fn << "\"" << std::endl;
			return 1;
		}

		output = &out_stream;
	}

	input->exceptions(std::istream::failbit | std::istream::badbit);
	output->exceptions(std::istream::failbit | std::istream::badbit);

	int ret;
	try
	{
		if (restore)
		{
			return restore_stream(input_fn, *input, *output) ? 0 : 1;
		}

		ret = blockalign(name, *input, *output, verbose);
	}
	catch (const std::ios_base::failure& e)
	{
		std::cerr << "Input/output error. " << e.what() << std::endl;
		ret = 2;
	}

	if (ret==0)
	{
		out_stream.close();

		if (rename((name + ".new").c_str(), name.c_str()) != 0)
		{
			std::cerr << "Renaming \"" << name << ".new\" to \"" << name << "\" failed" << std::endl;
			return 1;
		}
	}

    return ret;
}

#endif //BLOCKALIGN_NO_MAIN