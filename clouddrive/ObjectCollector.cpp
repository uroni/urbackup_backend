#include "ObjectCollector.h"
#include "IKvStoreFrontend.h"
#include <zlib.h>

const char object_collector_sig[] = "OBJECTCOLLECTOR";

bool CompressedWData::compress()
{
	if (decompressed_length != std::string::npos)
	{
		assert(false);
		return false;
	}

	uLongf orig_compressed_data_size = compressBound(static_cast<uLong>(data.size()));
	compressed_data = static_cast<Bytef*>(malloc(orig_compressed_data_size));
	if (compressed_data == nullptr)
		throw std::bad_alloc();

	compressed_data_size = orig_compressed_data_size;

	if (::compress(compressed_data, &compressed_data_size,
		reinterpret_cast<const Bytef*>(data.data()), static_cast<uLong>(data.size())) != Z_OK)
	{
		assert(false);
		return false;
	}

	if (orig_compressed_data_size != compressed_data_size)
	{
		compressed_data = static_cast<Bytef*>(realloc(compressed_data, compressed_data_size));
	}

	decompressed_length = data.size();
	std::string().swap(data);
	rdata.reset();
	return true;
}

bool CompressedWData::decompress()
{
	if (decompressed_length == std::string::npos)
	{
		assert(false);
		return false;
	}

	data.resize(decompressed_length);

	uLongf new_decompressed_length = static_cast<uLongf>(decompressed_length);
	int rc;
	if ((rc = ::uncompress(reinterpret_cast<Bytef*>(&data[0]), &new_decompressed_length,
		compressed_data, compressed_data_size)) != Z_OK)
	{
		assert(false);
		return false;
	}

	free(compressed_data);
	compressed_data = nullptr;
	compressed_data_size = 0;
	decompressed_length = std::string::npos;

	return true;
}

ObjectCollector::ObjectCollector(IKvStoreFrontend* frontend, const std::string& fn)
	: frontend(frontend)
{
	std::unique_ptr<IFile> in(Server->openFile(fn, MODE_READ));

	if (!in)
	{
		has_error = true;
		return;
	}

	std::string sig = in->Read(sizeof(object_collector_sig)-1);

	if (sig != std::string(object_collector_sig))
	{
		Server->Log("Object collector signature wrong: " + sig, LL_ERROR);
		has_error = true;
		return;
	}

	unsigned int header_size;
	if (in->Read(reinterpret_cast<char*>(&header_size), sizeof(header_size)) != sizeof(header_size))
	{
		Server->Log("Error reading object collector header_size", LL_ERROR);
		has_error = true;
		return;
	}

	std::vector<char> header_buf(header_size);
	if (in->Read(0, header_buf.data(), header_size) != header_size)
	{
		Server->Log("Error reading object collector header", LL_ERROR);
		has_error = true;
		return;
	}

	std::string md5 = Server->GenerateBinaryMD5(std::string(header_buf.data(), header_buf.size()));

	CRData header_data(header_buf.data(), header_buf.size());
	header_data.incrementPtr(sizeof(object_collector_sig) - 1 + sizeof(header_size));

	int64 n_trans_ids;
	if (!header_data.getVarInt(&task_id) ||
		!header_data.getVarInt(&n_trans_ids))
	{
		Server->Log("Error reading header -1", LL_ERROR);
		has_error = true;
		return;
	}

	for (int64 i = 0; i < n_trans_ids; ++i)
	{
		int64 trans_id;
		if (!header_data.getVarInt(&trans_id))
		{
			Server->Log("Error reading trans id", LL_ERROR);
			has_error = true;
			return;
		}

		trans_ids.push_back(trans_id);
	}

	char mirrored_info;
	int64 t_stride_size;
	int64 t_global_transid;
	int64 n_backend_keys;
	if (!header_data.getVarInt(&t_global_transid) ||
		!header_data.getVarInt(&n_backend_keys) ||
		!header_data.getVarInt(&t_stride_size) ||
		!header_data.getChar(&mirrored_info) ||
		!header_data.getVarInt(&cd_id) ||
		!header_data.getVarInt(&n_backend_keys) )
	{
		Server->Log("Error reading header -2", LL_ERROR);
		has_error = true;
		return;
	}

	global_transid = t_global_transid;
	stride_size = t_stride_size;
	with_mirrored = mirrored_info != 0;

	std::vector<std::pair<int64, int64> > backend_key_lens;

	for (int64 i = 0; i < n_backend_keys; ++i)
	{
		int64 len;
		int64 dl;
		if (!header_data.getVarInt(&len) ||
			!header_data.getVarInt(&dl))
		{
			Server->Log("Error reading backend_key_lens", LL_ERROR);
			has_error = true;
			return;
		}

		backend_key_lens.push_back(std::make_pair(len, dl));
	}
	
	int64 n_backend_locinfos;
	if (!header_data.getVarInt(&n_backend_locinfos))
	{
		Server->Log("Error reading header -3", LL_ERROR);
		has_error = true;
		return;
	}

	std::vector<std::pair<int64, int64> > backend_locinfo_lens;

	for (int64 i = 0; i < n_backend_locinfos; ++i)
	{
		int64 len;
		int64 dl;
		if (!header_data.getVarInt(&len) ||
			!header_data.getVarInt(&dl) )
		{
			Server->Log("Error reading backend_locinfo_lens", LL_ERROR);
			has_error = true;
			return;
		}

		backend_locinfo_lens.push_back(std::make_pair(len, dl));
	}

	int64 pos = header_size;
	for (auto it : backend_key_lens)
	{
		int64 len = it.first;
		int64 dl = it.second;
		char* buf = reinterpret_cast<char*>(malloc(len));
		if (in->Read(pos, buf, static_cast<_u32>(len)) != len)
		{
			free(buf);
			Server->Log("Error reading backend keys", LL_ERROR);
			has_error = true;
			return;
		}
		auto nd = std::make_unique<CompressedWData>(dl == -1 ? std::string::npos : dl, 
			reinterpret_cast<unsigned char*>(buf), 
			static_cast<unsigned long>(len));
		backend_keys.push_back(std::move(nd));
		pos += len;
	}

	for (auto it : backend_locinfo_lens)
	{
		int64 len = it.first;
		int64 dl = it.second;
		char* buf = reinterpret_cast<char*>(malloc(len));
		if (in->Read(pos, buf, static_cast<_u32>(len)) != len)
		{
			free(buf);
			Server->Log("Error reading backend locinfo", LL_ERROR);
			has_error = true;
			return;
		}
		auto nd = std::make_unique<CompressedWData>(dl == -1 ? std::string::npos : dl,
			reinterpret_cast<unsigned char*>(buf),
			static_cast<unsigned long>(len));
		backend_locinfo.push_back(std::move(nd));
		pos += len;
	}
}

void ObjectCollector::add(int64 transid, const std::string& tkey, const std::string& locinfo, bool mirrored)
{
	if (n_backend_keys >= stride_size
		|| curr_backend_keys == nullptr)
	{
		if (!backend_keys.empty())
		{
			backend_keys[backend_keys.size() - 1]->compress();
		}
		if (!backend_locinfo.empty())
		{
			backend_locinfo[backend_locinfo.size() - 1]->compress();
		}
		backend_keys.push_back(std::unique_ptr<CompressedWData>(new CompressedWData));
		curr_backend_keys = backend_keys[backend_keys.size() - 1].get();
		backend_locinfo.push_back(std::unique_ptr<CompressedWData>(new CompressedWData));
		curr_backend_locinfo = backend_locinfo[backend_locinfo.size() - 1].get();
		n_backend_keys = 0;

		size_t transid_size = 0;
		if (transid >= 1 >> 24)
			transid_size = 4;
		else if (transid >= 1 >> 16)
			transid_size = 3;
		else if (transid >= 1 >> 8)
			transid_size = 2;
		else if (transid > 0)
			transid_size = 1;

		if (with_mirrored)
			transid_size += 1;

		curr_backend_keys->reserve(stride_size * (tkey.size() + 1 + transid_size));
		curr_backend_locinfo->reserve(stride_size * (backend_locinfo.size() + 1));
	}

	++n_backend_keys;

	if (transid >= 0)
	{
		curr_backend_keys->addVarInt(transid);
	}
	curr_backend_keys->addString2(tkey);
	curr_backend_locinfo->addString2(locinfo);
	if (with_mirrored)
	{
		curr_backend_keys->addChar(mirrored ? 1 : 0);
	}
}

void ObjectCollector::add(int64 transid, const std::string& tkey, bool mirrored)
{
	if (n_backend_keys >= stride_size
		|| curr_backend_keys == nullptr)
	{
		if (!backend_keys.empty())
		{
			backend_keys[backend_keys.size() - 1]->compress();
		}
		backend_keys.push_back(std::unique_ptr<CompressedWData>(new CompressedWData));
		curr_backend_keys = backend_keys[backend_keys.size() - 1].get();
		backend_locinfo.push_back(nullptr);
		n_backend_keys = 0;

		size_t transid_size = 0;
		if (transid >= 1 >> 24)
			transid_size = 4;
		else if (transid >= 1 >> 16)
			transid_size = 3;
		else if (transid >= 1 >> 8)
			transid_size = 2;
		else if (transid > 0)
			transid_size = 1;

		if (with_mirrored)
			transid_size += 1;

		curr_backend_keys->reserve(stride_size * (tkey.size() + 1 + transid_size));
	}

	++n_backend_keys;

	if (transid >= 0)
	{
		curr_backend_keys->addVarInt(transid);
	}
	curr_backend_keys->addString2(tkey);

	if (with_mirrored)
	{
		curr_backend_keys->addChar(mirrored ? 1 : 0);
	}
}

bool ObjectCollector::persist(int task_id, int64 completed, int64 active,
	const std::vector<int64>& trans_ids, const std::string& fn)
{
	std::unique_ptr<IFile> out_f(Server->openFile(fn, MODE_RW_CREATE));

	if (!out_f)
		return false;

	CWData wd;
	wd.addBuffer(object_collector_sig, sizeof(object_collector_sig)-1);
	wd.addUInt(0);
	wd.addVarInt(task_id);
	wd.addVarInt(completed);
	wd.addVarInt(active);
	wd.addVarInt(trans_ids.size());
	for (size_t trans_id : trans_ids)
	{
		wd.addVarInt(trans_id);
	}
	wd.addVarInt(global_transid);
	wd.addVarInt(n_backend_keys);
	wd.addVarInt(stride_size);
	wd.addChar(with_mirrored ? 1 : 0);
	wd.addVarInt(cd_id);
	wd.addVarInt(backend_keys.size());

	for (auto& it : backend_keys)
	{
		wd.addVarInt(it->getCompressedDataLength());

		size_t dl = it->getDecompressedDataLength();
		wd.addVarInt(dl == std::string::npos ? -1 : dl);
	}

	wd.addVarInt(backend_locinfo.size());

	for (auto& it : backend_locinfo)
	{
		wd.addVarInt(it->getCompressedDataLength());

		size_t dl = it->getDecompressedDataLength();
		wd.addVarInt(dl == std::string::npos ? -1 : dl);
	}

	unsigned int header_size = wd.getDataSize() + 16;
	memcpy(wd.getDataPtr(), &header_size, sizeof(header_size));

	std::string md5 = Server->GenerateBinaryMD5(std::string(wd.getDataPtr(), wd.getDataSize()));
	wd.addBuffer(md5.data(), md5.size());

	int64 pos = out_f->Size();
	if (pos < 0)
		return false;

	if (out_f->Write(pos, wd.getDataPtr(), wd.getDataSize()) != wd.getDataSize())
		return false;

	pos += wd.getDataSize();

	for (auto& it : backend_keys)
	{
		if (out_f->Write(pos, it->getCompressedDataPtr(),
			static_cast<_u32>(it->getCompressedDataLength())) != it->getCompressedDataLength())
			return false;

		pos += it->getCompressedDataLength();
	}

	for (auto& it : backend_locinfo)
	{
		if (out_f->Write(pos, it->getCompressedDataPtr(),
			static_cast<_u32>(it->getCompressedDataLength())) != it->getCompressedDataLength())
			return false;

		pos += it->getCompressedDataLength();
	}

	return out_f->Sync();
}

void ObjectCollector::finalize()
{
	for (size_t i = 0; i < backend_keys.size(); ++i)
	{
		memsize += backend_keys[i]->compressed_capacity();
		uncompressed_memsize += backend_keys[i]->uncompressed_capacity();
		if (backend_locinfo[i] != nullptr)
		{
			memsize += backend_locinfo[i]->compressed_capacity();
			uncompressed_memsize += backend_locinfo[i]->uncompressed_capacity();
		}
	}

	int64 l_cd_id = cd_id;
	for (size_t i = 0; i < backend_keys.size(); ++i)
	{
		CompressedWData* rdata = backend_keys[i].get();
		IKvStoreFrontend* cfrontend = frontend;
		bool c_with_mirrored = with_mirrored;
		if (global_transid >= 0)
		{
			int64 transid = global_transid;
			key_next_funs.push_back(
				[cfrontend, c_with_mirrored, transid, rdata, l_cd_id](IKvStoreBackend::key_next_action_t action, std::string* key) {

					if (action == IKvStoreBackend::key_next_action_t::clear) {
						rdata->clear_all();
						assert(key == nullptr);
						return true;
					}

					if (action == IKvStoreBackend::key_next_action_t::reset) {
						rdata->getRData()->setStreampos(0);
						assert(key == nullptr);
						return true;
					}

					assert(action == IKvStoreBackend::key_next_action_t::next);

					std::string tkey;
					if (!rdata->getRData()->getStr2(&tkey))
					{
						return false;
					}

					if (cfrontend != nullptr)
					{
						cfrontend->incr_total_del_ops();
						*key = cfrontend->prefixKey(cfrontend->encodeKey(l_cd_id, tkey, transid));
					}

					if (c_with_mirrored)
					{
						char mirrored;
						if (!rdata->getRData()->getChar(&mirrored))
						{
							return false;
						}

						if (mirrored == 1)
						{
							cfrontend->log_del_mirror(*key);
						}
					}
					return true;
				});
		}
		else
		{
			key_next_funs.push_back(
				[cfrontend, c_with_mirrored, rdata, l_cd_id](IKvStoreBackend::key_next_action_t action, std::string* key) {

					if (action == IKvStoreBackend::key_next_action_t::clear) {
						rdata->clear_all();
						assert(key == nullptr);
						return true;
					}

					if (action == IKvStoreBackend::key_next_action_t::reset) {
						rdata->getRData()->setStreampos(0);
						assert(key == nullptr);
						return true;
					}

					assert(action == IKvStoreBackend::key_next_action_t::next);

					int64 transid;
					if (!rdata->getRData()->getVarInt(&transid))
					{
						return false;
					}

					std::string tkey;
					if (!rdata->getRData()->getStr2(&tkey))
					{
						rdata->compress();
						return false;
					}

					cfrontend->incr_total_del_ops();
					*key = cfrontend->prefixKey(cfrontend->encodeKey(l_cd_id, tkey, transid));

					if (c_with_mirrored)
					{
						char mirrored;
						if (!rdata->getRData()->getChar(&mirrored))
						{
							return false;
						}

						if (mirrored == 1)
						{
							cfrontend->log_del_mirror(*key);
						}
					}
					return true;
				});
		}

		if (backend_locinfo[i] != nullptr)
		{
			CompressedWData* rdata = backend_locinfo[i].get();
			locinfo_next_funs.push_back(
				[rdata](IKvStoreBackend::key_next_action_t action, std::string* locinfo) {

					if (action == IKvStoreBackend::key_next_action_t::clear) {
						assert(locinfo == nullptr);
						rdata->clear_all();
						return true;
					}

					if (action == IKvStoreBackend::key_next_action_t::reset) {
						assert(locinfo == nullptr);
						rdata->getRData()->setStreampos(0);
						return true;
					}

					assert(action == IKvStoreBackend::key_next_action_t::next);

					if (!rdata->getRData()->getStr2(locinfo))
					{
						return false;
					}

					return true;
				});
		}
		else
		{
			locinfo_next_funs.push_back(nullptr);
		}
	}
}
