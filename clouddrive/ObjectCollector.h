#pragma once
#include <memory>
#include <string>
#include <memory.h>
#include "../common/data.h"
#include "IKvStoreBackend.h"
#include <assert.h>

class IKvStoreFrontend;

class CompressedWData : public CWData
{
	unsigned char* compressed_data;
	unsigned long compressed_data_size;
	size_t decompressed_length;
	std::unique_ptr<CRData> rdata;
public:
	CompressedWData()
		: decompressed_length(std::string::npos), compressed_data(nullptr), compressed_data_size(0) {}

	CompressedWData(size_t decompressed_length, unsigned char* compressed_data, unsigned long compressed_data_size)
		: decompressed_length(decompressed_length), compressed_data(compressed_data), compressed_data_size(compressed_data_size) {}

	~CompressedWData() {
		free(compressed_data);
	}

	bool compress();

	size_t getDecompressedDataLength() {
		return decompressed_length;
	}

	const char* getCompressedDataPtr() {
		return reinterpret_cast<char*>(compressed_data);
	}

	size_t getCompressedDataLength() {
		return compressed_data_size;
	}

	bool decompress();

	CRData* getRData()
	{
		if (rdata.get() != nullptr)
		{
			return rdata.get();
		}

		if (decompressed_length != std::string::npos)
		{
			if (!decompress())
			{
				assert(false);
				return nullptr;
			}
		}

		assert(decompressed_length == std::string::npos);
		rdata.reset(new CRData(data.data(), data.size()));
		return rdata.get();
	}

	int64 compressed_capacity()
	{
		if (decompressed_length == std::string::npos)
		{
			assert(compressed_data_size == 0);
			return capacity();
		}
		else
		{
			assert(data.capacity() < 100);
			return compressed_data_size;
		}
	}

	int64 uncompressed_capacity()
	{
		if (decompressed_length == std::string::npos)
			return capacity();
		else
			return decompressed_length;
	}

	void clear_all()
	{
		if (decompressed_length == std::string::npos)
		{
			std::string().swap(data);
		}
		else
		{
			free(compressed_data);
			compressed_data = nullptr;
			compressed_data_size = 0;
			decompressed_length = std::string::npos;
		}
	}
};

class ObjectCollector
{
	std::vector<std::unique_ptr<CompressedWData> > backend_keys;
	std::vector<std::unique_ptr<CompressedWData> > backend_locinfo;
	CWData* curr_backend_keys;
	size_t n_backend_keys;
	CWData* curr_backend_locinfo;
	size_t stride_size;
	IKvStoreFrontend* frontend;
	int64 global_transid;
	bool with_mirrored;
	bool has_error;
public:
	int64 completed;
	int64 active;
	int64 task_id;
	int64 cd_id;
	std::vector<int64> trans_ids;

	int64 memsize;
	int64 uncompressed_memsize;
	std::vector<IKvStoreBackend::key_next_fun_t> key_next_funs;
	std::vector<IKvStoreBackend::locinfo_next_fun_t> locinfo_next_funs;

	ObjectCollector(size_t stride_size, IKvStoreFrontend* frontend,
		int64 global_transid, bool with_mirrored, int64 cd_id)
		: curr_backend_keys(nullptr),
		curr_backend_locinfo(nullptr),
		n_backend_keys(0),
		stride_size(stride_size),
		frontend(frontend),
		global_transid(global_transid),
		memsize(0),
		uncompressed_memsize(0),
		with_mirrored(with_mirrored),
		cd_id(cd_id)
	{

	}

	ObjectCollector(IKvStoreFrontend* frontend, const std::string& fn);

	bool get_has_error() {
		return has_error;
	}

	void add(int64 transid, const std::string& tkey, const std::string& locinfo, bool mirrored);

	void add(int64 transid, const std::string& tkey, bool mirrored);

	bool persist(int task_id, int64 completed, int64 active, 
		const std::vector<int64>& trans_ids, const std::string& fn);

	void finalize();

	bool has_locinfo() {
		return key_next_funs.size() == locinfo_next_funs.size();
	}
};
