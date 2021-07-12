/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2021 Martin Raiber
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

#include "KvStoreBackendS3.h"
#include <aws/core/client/ClientConfiguration.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <aws/s3/model/ListObjectsRequest.h>
#include <aws/s3/model/ListObjectVersionsRequest.h>
#include <aws/s3/model/Object.h>
#include <aws/s3/model/PutObjectRequest.h>
#include <aws/s3/model/DeleteObjectsRequest.h>
#include <aws/s3/model/DeleteObjectRequest.h>
#include <aws/s3/model/GetBucketLocationRequest.h>
#include <aws/core/Aws.h>
#include <aws/core/utils/Array.h>
#include "../Interface/File.h"
#include "../Interface/Server.h"
#include "../Interface/Types.h"
#include "../Interface/File.h"
#include "../Interface/BackupFileSystem.h"
#include "../stringtools.h"
#include "../urbackupcommon/os_functions.h"
#include "../urbackupcommon/events.h"
#include "../md5.h"
#include <fstream>
#include <cstdarg>
#include <stdlib.h>
#include <assert.h>


static const char* ALLOCATION_TAG = "DumbOnlineKvStoreBackend";
static const char* SHARD_TAG = "__SHARD__";

class ServerAwsLogger : public Aws::Utils::Logging::LogSystemInterface
{
public:
	ServerAwsLogger(Aws::Utils::Logging::LogLevel loglevel)
		: m_loglevel(loglevel) {}

	virtual Aws::Utils::Logging::LogLevel GetLogLevel(void) const
	{
		return m_loglevel;
	}
	
	void Log(Aws::Utils::Logging::LogLevel logLevel, const char* tag, const char* formatStr, ...)
	{
		Aws::StringStream ss;

		std::va_list args;
		va_start(args, formatStr);

		va_list tmp_args; //unfortunately you cannot consume a va_list twice
		va_copy(tmp_args, args); //so we have to copy it
		#ifdef WIN32
			const int requiredLength = _vscprintf(formatStr, tmp_args) + 1;
		#else
			const int requiredLength = vsnprintf(nullptr, 0, formatStr, tmp_args) + 1;
		#endif
		va_end(tmp_args);

		Aws::Utils::Array<char> outputBuff(requiredLength);
		#ifdef WIN32
			vsnprintf_s(outputBuff.GetUnderlyingData(), requiredLength, _TRUNCATE, formatStr, args);
		#else
			vsnprintf(outputBuff.GetUnderlyingData(), requiredLength, formatStr, args);
		#endif // WIN32

		ss << outputBuff.GetUnderlyingData(); 
	  
		Log(logLevel, ss.str());

		va_end(args);
	}
	
	void LogStream(Aws::Utils::Logging::LogLevel logLevel, const char* tag, const Aws::OStringStream &message_stream)
	{
		Log(logLevel, message_stream.rdbuf()->str());
	}
	
	void Log(Aws::Utils::Logging::LogLevel loglevel, const Aws::String& msg)
	{
		int ll;
		switch(loglevel)
		{
			case Aws::Utils::Logging::LogLevel::Error: ll=LL_ERROR; break;
			case Aws::Utils::Logging::LogLevel::Fatal: ll=LL_ERROR; break;
			case Aws::Utils::Logging::LogLevel::Warn: ll=LL_WARNING; break;
			case Aws::Utils::Logging::LogLevel::Info: ll=LL_INFO; break;
			default: ll=LL_DEBUG;
		}

		Server->Log("AWS-Client: "+trim(msg.c_str()), ll);
	}

	void Flush() {}
	
private:
	Aws::Utils::Logging::LogLevel m_loglevel;
};

Aws::String BucketLocationToRegion(Aws::S3::Model::BucketLocationConstraint b)
{
	using namespace Aws::S3::Model;
	Aws::String ret;
	switch(b)
	{
		case BucketLocationConstraint::EU: return Aws::Region::EU_WEST_1;
		case BucketLocationConstraint::NOT_SET: return Aws::Region::US_EAST_1;
		default: ret = Aws::S3::Model::BucketLocationConstraintMapper::GetNameForBucketLocationConstraint(b); break;
	}
	
	if(ret.empty())
	{
		Server->Log("Unknown bucket location constraint "+convert((int)b)+". Returning US_EAST_1", LL_WARNING);
		return Aws::Region::US_EAST_1;
	}
	
	return ret;
}

namespace
{
	std::string base64md5(std::string md5)
	{
		std::string b = hexToBytes(md5);
		return base64_encode(reinterpret_cast<const unsigned char*>(b.data()), static_cast<unsigned int>(b.size()));
	}
	
	size_t getShardIdx(const std::string& key, size_t n_shards)
	{
		std::string md5sum = Server->GenerateBinaryMD5(key);
		size_t idx_ret;
		memcpy(&idx_ret, md5sum.data(), sizeof(idx_ret));
		return idx_ret%n_shards;
	}
}

class offset_buf : public std::streambuf
{
public:
	offset_buf(int64 offset, IFile* f, KvStoreBackendS3* backend, bool own_f)
		: offset(offset), pos(offset), f(f), has_error(false), backend(backend), own_f(own_f),
		size(f->Size())
	{
		buffer.resize(32768);
		char *end = buffer.data() + buffer.size();
		setg(end, end, end);
	}

	~offset_buf()
	{
		if(own_f)
		{
				std::string tmpfile_path = f->getFilename();
				Server->destroy(f);
				Server->deleteFile(tmpfile_path);
		}
	}
	
	bool has_error;
	
protected:
	
	std::streambuf::int_type underflow() override
	{
		if (gptr() < egptr())
		{
			return traits_type::to_int_type(*gptr());
		}

		bool has_read_error=false;
		size_t toread = (std::min)(static_cast<size_t>(size - pos), buffer.size());
		_u32 read = f->Read(pos, buffer.data(), static_cast<_u32>(toread), &has_read_error);
		
		if(has_read_error)
		{
			has_error=true;
			std::string msg = "Read error while reading from file "
					+ f->getFilename() + " at position " + convert(pos)
					+ " len " + convert(buffer.size()) + " for S3 submission. " + os_last_error_str();
			Server->Log(msg, LL_ERROR);
			addSystemEvent("cache_err",
				"Error reading from file on cache",
				msg, LL_ERROR);
		}
		
		pos+=read;
		if(read==0)
		{
			return traits_type::eof();
		}

		backend->add_uploaded_bytes(read);

		setg(buffer.data(), buffer.data(), buffer.data() + read);

		return traits_type::to_int_type(*gptr());
	}
	
	std::streambuf::pos_type seekoff( std::streambuf::off_type off, std::ios_base::seekdir dir,
                          std::ios_base::openmode which ) override
	{
		int64 npos=pos;
		if(dir==std::ios::beg)
		{
			npos=off + offset;
		}
		else if(dir==std::ios::cur)
		{
			npos+=off;
		}
		else
		{
			npos = f->Size()-off;
		}
		
		if(npos<offset)
		{
			return std::streambuf::pos_type(std::streambuf::off_type(-1));
		}
		
		pos=npos;
		
		char *end = buffer.data() + buffer.size();
		setg(end, end, end);
		
		return std::streambuf::pos_type(pos-offset);
	}
	
	virtual std::streambuf::pos_type seekpos( pos_type pos,
                          std::ios_base::openmode which) override
	{
		return seekoff(pos, std::ios::beg, which);
	}
		
private:
	std::vector<char> buffer;
	
	int64 offset;
	int64 pos;
	int64 size;
	IFile* f;
	bool own_f;
	KvStoreBackendS3* backend;
};

IMutex* KvStoreBackendS3::client_mutex=nullptr;
int64 KvStoreBackendS3::max_request_timems=0;
int64 KvStoreBackendS3::n_requests=0;

KvStoreBackendS3::KvStoreBackendS3(const std::string& encryption_key, const std::string& access_key, const std::string& secret_access_key,
	const std::string& bucket_name, ICompressEncryptFactory* compress_encrypt_factory, const std::string& s3_endpoint, 
	const std::string& s3_region, const std::string& p_storage_class, unsigned int comp_method, unsigned int comp_method_metadata,
	IBackupFileSystem* cachefs)
	: encryption_key(encryption_key),
	  compress_encrypt_factory(compress_encrypt_factory), online_kv_store(nullptr), 
	  s3_endpoint(s3_endpoint), s3_region(s3_region),
	  storage_class(Aws::S3::Model::StorageClass::NOT_SET), comp_method(comp_method),
	  comp_method_metadata(comp_method_metadata),
		uploaded_bytes(0), downloaded_bytes(0), cachefs(cachefs)
{
	if(!access_key.empty())
	{
		credentials_provider.reset(new Aws::Auth::SimpleAWSCredentialsProvider(access_key.c_str(), secret_access_key.c_str()));
	}
	else
	{
		credentials_provider.reset(new Aws::Auth::InstanceProfileCredentialsProvider);
	}

	if (!p_storage_class.empty())
	{
		storage_class = Aws::S3::Model::StorageClassMapper::GetStorageClassForName(p_storage_class.c_str());
	}
	
	std::vector<std::string> toks;
	Tokenize(bucket_name, toks, "/");
	
	if(toks.empty())
	{
		toks.push_back("");
	}
	
	s3_clients.resize(toks.size());
	buckets.resize(toks.size());
	for(size_t i=0;i<toks.size();++i)
	{
		SBucket& bucket = buckets[i];
		bucket.name = toks[i];
		bucket.location = Aws::S3::Model::BucketLocationConstraint();
		if(s3_endpoint.empty())
		{
			Aws::S3::Model::GetBucketLocationRequest getBucketLocationRequest;
			getBucketLocationRequest.SetBucket(bucket.name.c_str());
			auto s3_client = getS3Client(i, false);
			auto getBucketLocationOutcome = s3_client.second->GetBucketLocation(getBucketLocationRequest);
			if(getBucketLocationOutcome.IsSuccess())
			{
				bucket.location = getBucketLocationOutcome.GetResult().GetLocationConstraint();
			}
		}
	}
	
	resetClient();
}

void KvStoreBackendS3::init_mutex()
{
	client_mutex = Server->createMutex();
	
	Aws::SDKOptions options;
	Aws::Utils::Logging::LogLevel logLevel = Aws::Utils::Logging::LogLevel::Warn;
	
	
	if(FileExists("/var/urbackup/trace_aws_s3"))
	{
		logLevel = Aws::Utils::Logging::LogLevel::Trace;
	}
	
	options.loggingOptions.logLevel = logLevel;
	options.loggingOptions.logger_create_fn = [logLevel](){
		return Aws::MakeShared<ServerAwsLogger>(ALLOCATION_TAG, logLevel);
	};
    Aws::InitAPI(options);
}

bool KvStoreBackendS3::get( const std::string& key, const std::string& md5sum, 
		unsigned int flags, bool allow_error_event, IFsFile* ret_file, std::string& ret_md5sum, unsigned int& get_status)
{
	assert(ret_file!=nullptr);
	get_status=0;
	
	size_t idx0 = 0;
	if(buckets[0].name==SHARD_TAG)
	{
		idx0 = getShardIdx(key, buckets.size()-1)+1;
	}
	
	std::string expected_md5sum = get_md5sum(md5sum);
	std::string version = get_locinfo(md5sum);
	
	Aws::S3::Model::GetObjectRequest getObjectRequest;
	getObjectRequest.SetBucket(buckets[idx0].name.c_str());
	getObjectRequest.SetKey(key.c_str());
	if(!version.empty())
		getObjectRequest.SetVersionId(version.c_str());

	IFsFile* tmpfile = Server->openTemporaryFile();
	if(tmpfile==nullptr)
	{
		std::string syserr = os_last_error_str();
		if (allow_error_event)
		{
			addSystemEvent("s3_backend",
				"Error opening temporary file",
				"Error opening temporary file. " + syserr, LL_ERROR);
		}
		Server->Log("Error opening temporary file. "+syserr, LL_ERROR);
		return false;
	}
	std::string tmpfile_path = tmpfile->getFilename();
	Server->destroy(tmpfile);
	
	if(!(flags & IKvStoreBackend::GetDecrypted))
	{
		Server->Log("Retrieving object "+ key+" and not decrypting", LL_INFO);
	}
	else
	{
		Server->Log("Retrieving object "+ key, LL_INFO);
	}

	getObjectRequest.SetResponseStreamFactory([&tmpfile_path](){ return Aws::New<Aws::FStream>( ALLOCATION_TAG, tmpfile_path, 
		std::ios_base::out | std::ios_base::in | std::ios_base::trunc | std::ios_base::binary
#ifdef _WIN32
		, _SH_DENYNO
#endif
		); });

	int64 starttime = Server->getTimeMS();
	auto s3_client = getS3Client(idx0);
	auto getObjectOutcome = s3_client.second->GetObject(getObjectRequest);
	releaseS3Client(idx0, s3_client);
	
	for(size_t idx = 1; !getObjectOutcome.IsSuccess() && idx < buckets.size() 
		&& (getObjectOutcome.GetError().GetErrorType() == Aws::S3::S3Errors::NO_SUCH_KEY
			|| (getObjectOutcome.GetError().GetErrorType() == Aws::S3::S3Errors::UNKNOWN 
					&& getObjectOutcome.GetError().GetMessage().find("Response code: 404")!=std::string::npos ) )
		;++idx)
	{
		Server->Log("Key "+key+" not found in bucket idx "+convert(idx0)+". Trying bucket \""+buckets[idx].name+"\"...", LL_INFO);
		getObjectRequest.SetBucket(buckets[idx].name.c_str());
		auto s3_client = getS3Client(idx);
		getObjectOutcome = s3_client.second->GetObject(getObjectRequest);		
		releaseS3Client(idx, s3_client);
	}
	
	int64 passedtime = Server->getTimeMS()-starttime;

	if(getObjectOutcome.IsSuccess())
	{
		{
			IScopedLock lock(client_mutex);
			if(passedtime>max_request_timems)
			{
				max_request_timems=passedtime;
			}
			++n_requests;
		}

		int mode = MODE_RW;
#ifdef _WIN32
		mode = MODE_RW_DEVICE;
#endif // _WIN32

		tmpfile = Server->openFile(tmpfile_path, mode);
		if(tmpfile==nullptr)
		{
			std::string syserr = os_last_error_str();
			if (allow_error_event)
			{
				addSystemEvent("s3_backend",
					"Error opening temporary file ",
					"Error opening temporary file -2. " + syserr, LL_ERROR);
			}
			Server->Log("Error opening temporary file -2. "+syserr, LL_ERROR);
			return false;
		}
		ScopedDeleteFile delete_tmpfile(tmpfile);

		downloaded_bytes+=tmpfile->Size();

		std::unique_ptr<IDecryptAndDecompress> decrypt_and_decompress;
		
		if(flags & IKvStoreBackend::GetDecrypted)
		{
			decrypt_and_decompress.reset(compress_encrypt_factory->createDecryptAndDecompress(encryption_key, ret_file));
		}

		std::vector<char> buffer;
		buffer.resize(32768);

		MD5 md_check;
		tmpfile->Seek(0);
		while(true)
		{
			_u32 read = tmpfile->Read(buffer.data(), static_cast<_u32>(buffer.size()));
			if(read==0)
			{
				break;
			}

			if(decrypt_and_decompress.get()!=nullptr
				&& !decrypt_and_decompress->put(buffer.data(), read))
			{
				if(allow_error_event)
				{
					addSystemEvent("s3_backend",
						"Error decrypting and compressing",
						"Error decrypting and decompressing object "+key+". Last errors:\n"+extractLastLogErrors(), LL_ERROR);
				}
				Server->Log("Error decrypting and decompressing", LL_ERROR);
				return false;
			}
			else if(decrypt_and_decompress.get()==nullptr)
			{
				md_check.update(reinterpret_cast<unsigned char*>(buffer.data()), read);
				if(ret_file->Write(buffer.data(), read)!=read)
				{
					std::string syserr = os_last_error_str();
					Server->Log("Error writing to result file. "+syserr, LL_ERROR);
					if(allow_error_event)
					{
						addSystemEvent("s3_backend",
							"Error writing to result file",
							"Error writing to result file. "+syserr, LL_ERROR);
					}
					return false;
				}
			}
		}

		if (decrypt_and_decompress.get() != nullptr)
		{
			if (!decrypt_and_decompress->finalize())
			{
				Server->Log("Error finalizing decryption of object "+key, LL_ERROR);
				if (allow_error_event)
				{
					addSystemEvent("s3_backend",
						"Error decrypting object",
						"Error finalizing decryption of object " + key, LL_ERROR);
				}
				return false;
			}

			ret_md5sum = hexToBytes(decrypt_and_decompress->md5sum());
		}
		else
		{
			md_check.finalize();
			ret_md5sum.assign(reinterpret_cast<char*>(md_check.raw_digest_int()), 16);
		}

		if (!expected_md5sum.empty()
			&& expected_md5sum != ret_md5sum)
		{
			Server->Log("Calculated md5sum of downloaded object differs from expected md5sum for object " + key 
				+ ". Calculated=" + bytesToHex(ret_md5sum) + " Expected=" + bytesToHex(expected_md5sum), LL_ERROR);
			if (allow_error_event)
			{
				addSystemEvent("s3_backend",
					"Calculated md5sum differs from expected",
					"Calculated md5sum of downloaded object differs from expected md5sum for object " + key
					+ ". Calculated=" + bytesToHex(ret_md5sum) + " Expected=" + bytesToHex(expected_md5sum), LL_ERROR);
			}
			return false;
		}

		return true;
	}
	else
	{
		if(getObjectOutcome.GetError().GetErrorType() == Aws::S3::S3Errors::NO_SUCH_KEY
			|| (getObjectOutcome.GetError().GetErrorType() == Aws::S3::S3Errors::UNKNOWN 
					&& getObjectOutcome.GetError().GetMessage().find("Response code: 404")!=std::string::npos) )
		{
			Server->Log("Key "+key+" not found", LL_INFO);
			get_status|=IKvStoreBackend::GetStatusNotFound;
			
			if(allow_error_event)
			{
				addSystemEvent("s3_backend",
					"Error during S3 download (not found)",
					"S3 download of object "+key+" failed. Object not present/not found.\nLast errors:\n"+extractLastLogErrors(5, "AWS-Client: "), LL_ERROR);
			}
		}
		else if(allow_error_event)
		{
			addSystemEvent("s3_backend",
				"Error during S3 download",
				"S3 download of object "+key+" failed.\nLast errors:\n"+extractLastLogErrors(5, "AWS-Client: ", true), LL_ERROR);
		}
	
		fixError(getObjectOutcome.GetError().GetErrorType());
		return false;
	}
}

bool KvStoreBackendS3::list( IListCallback* callback )
{
	bool etag_error = false;
	Aws::String key_marker;
	Aws::String version_marker;
	size_t idx=0;
	auto s3_client = getS3Client(idx);
	while(idx<buckets.size())
	{
		if(buckets[idx].name == SHARD_TAG)
		{
			if(idx+1<buckets.size())
			{
				++idx;
				s3_client = getS3Client(idx);
				key_marker.clear();
				version_marker.clear();
				continue;
			}
			else
			{
				return false;
			}
		}

		Aws::S3::Model::ListObjectVersionsRequest listObjectsRequest;
		listObjectsRequest.SetBucket(buckets[idx].name.c_str());

		if(!key_marker.empty())
		{
			listObjectsRequest.WithKeyMarker(key_marker);
			listObjectsRequest.WithVersionIdMarker(version_marker);
		}

		Aws::S3::Model::ListObjectVersionsOutcome listObjectsOutcome = s3_client.second->ListObjectVersions(listObjectsRequest);

		if(!listObjectsOutcome.IsSuccess())
		{
			if (idx == 0
				&& listObjectsOutcome.GetError().GetExceptionName()=="NotImplemented")
			{
				return list_wo_versions(callback);
			}
			Server->Log("Listing objects in bucket \""+buckets[idx].name+"\" with versions failed. "+listObjectsOutcome.GetError().GetMessage().c_str()+" (code: "+convert(static_cast<int64>(listObjectsOutcome.GetError().GetErrorType()))+")", LL_ERROR);
			fixError(listObjectsOutcome.GetError().GetErrorType());
			releaseS3Client(idx, s3_client);
			return false;
		}

		for (const Aws::S3::Model::ObjectVersion& object: listObjectsOutcome.GetResult().GetVersions())
		{			
			key_marker = object.GetKey();
			version_marker = object.GetVersionId();

			std::string md5sum;
			if (version_marker.size()>=16)
			{
				md5sum.resize(16 + version_marker.size());
				md5sum.replace(md5sum.begin() + 16, md5sum.end(), version_marker.data());
			}
			else
			{
				md5sum.assign(version_marker.begin(), version_marker.end());
			}

			if(!callback->onlineItem(object.GetKey().c_str(), md5sum, object.GetSize(), object.GetLastModified().Millis()))
			{
				releaseS3Client(idx, s3_client);
				return false;
			}
		}

		if(!listObjectsOutcome.GetResult().GetIsTruncated())
		{
			releaseS3Client(idx, s3_client);
			
			if(idx+1<buckets.size())
			{
				++idx;
				s3_client = getS3Client(idx);
				key_marker.clear();
				version_marker.clear();
			}
			else
			{
				return true;
			}
		}
		else
		{
			if(!listObjectsOutcome.GetResult().GetNextKeyMarker().empty())
			{
				key_marker = listObjectsOutcome.GetResult().GetNextKeyMarker();
				version_marker = listObjectsOutcome.GetResult().GetNextVersionIdMarker();
			}
		}
	}
	return true;
}

bool KvStoreBackendS3::put( const std::string& key, IFsFile* src,
		unsigned int flags, bool allow_error_event, std::string& md5sum, int64& compressed_size)
{
	src->Seek(0);

	unsigned int curr_comp_method = (flags & IKvStoreBackend::GetMetadata) > 0 ? comp_method_metadata
		: comp_method;

	std::string local_md5;
	std::shared_ptr<Aws::IOStream> upload_file;
	std::shared_ptr<offset_buf> offset_buffer;
	int64 local_size=0;
	if(!(flags & IKvStoreBackend::PutAlreadyCompressedEncrypted))
	{
		IFsFile* tmpfile = Server->openTemporaryFile();
		ScopedDeleteFile tmpfile_delete(tmpfile);

		if(tmpfile==nullptr)
		{
			std::string syserr = os_last_error_str();
			Server->Log("Error opening temporary file. "+syserr, LL_ERROR);
			if(allow_error_event)
			{
				addSystemEvent("s3_backend",
					"Error opening temporary file",
					"Error opening temporary file. "+syserr, LL_ERROR);
			}
			return false;
		}
		
		std::unique_ptr<ICompressAndEncrypt> compress_encrypt(compress_encrypt_factory->createCompressAndEncrypt(encryption_key, 
			src, online_kv_store, curr_comp_method));

		std::vector<char> buffer;
		buffer.resize(32768);
		while(true)
		{
			size_t read = compress_encrypt->read(buffer.data(), buffer.size());

			if(read==std::string::npos)
			{
				Server->Log("Error compressing and encrypting (S3)", LL_ERROR);
				break;
			}
			
			if(read==0)
			{
				break;
			}

			if(tmpfile->Write(buffer.data(), static_cast<_u32>(read))!=read)
			{
				std::string syserr = os_last_error_str();
				Server->Log("Error writing to tmp file \""+tmpfile->getFilename()+"\". " + syserr, LL_ERROR);
				if(allow_error_event)
				{
					addSystemEvent("s3_backend",
						"Error writing to temporary file",
						"Error writing to tmp file \""+tmpfile->getFilename()+"\". " + syserr, LL_ERROR);
				}
				return false;
			}
		}

		local_size = tmpfile->Size();
		Server->Log("Uploading object "+ key +"... Uncompressed size="+convert(src->Size())+" Compressed size="+convert(local_size), LL_INFO);
		
		local_md5 = compress_encrypt->md5sum();
		
		tmpfile_delete.release();
		offset_buffer.reset(new offset_buf(0, tmpfile, this, true));
	}
	else
	{
		local_md5.resize(16);
		if(src->Read(0, &local_md5[0], static_cast<_u32>(local_md5.size()))!=local_md5.size())
		{
			local_md5.clear();
		}
		
		local_size = src->Size();
		
		if(local_size>=16)
		{
			local_size-=16;
		}
		
		Server->Log("Uploading object "+ key +"... Compressed size="+convert(local_size), LL_INFO);
			
		offset_buffer.reset(new offset_buf(16, src, this, false));
	}

	upload_file = Aws::MakeShared<Aws::IOStream>(ALLOCATION_TAG, offset_buffer.get());

	size_t idx=0;
	if(buckets[idx].name==SHARD_TAG)
	{
		idx = getShardIdx(key, buckets.size()-1)+1;
	}

	Aws::S3::Model::PutObjectRequest putObjectRequest;
	putObjectRequest.SetBucket(buckets[idx].name.c_str());
	putObjectRequest.SetKey(key.c_str());
	putObjectRequest.SetBody(upload_file);
	putObjectRequest.SetContentMD5(base64_encode(reinterpret_cast<unsigned char*>(local_md5.data()),
		static_cast<unsigned int>(local_md5.size())).c_str());
	if (storage_class != Aws::S3::Model::StorageClass::NOT_SET)
	{
		putObjectRequest.SetStorageClass(storage_class);
	}
	upload_file.reset();

	int64 starttime = Server->getTimeMS();
	auto s3_client = getS3Client(idx);
	Aws::S3::Model::PutObjectOutcome putObjectOutcome = s3_client.second->PutObject(putObjectRequest);
	releaseS3Client(idx, s3_client);
	
	if(offset_buffer.get()!=nullptr
		&& offset_buffer->has_error)
	{
		return false;
	}
	
	int64 passedtime = Server->getTimeMS()-starttime;

	if(putObjectOutcome.IsSuccess())
	{
		{
			IScopedLock lock(client_mutex);
			if(passedtime>max_request_timems)
			{
				max_request_timems=passedtime;
			}
			++n_requests;
		}

		md5sum = local_md5;
		compressed_size = local_size;

		if (del_with_location_info())
		{
			Aws::String version = putObjectOutcome.GetResult().GetVersionId();
			md5sum.resize(16 + version.size());
			md5sum.replace(md5sum.begin() + 16, md5sum.end(), version.data());
		}
	}
	else
	{
		fixError(putObjectOutcome.GetError().GetErrorType());
		
		if(allow_error_event)
		{
			addSystemEvent("s3_backend",
				"Error during S3 upload",
				"S3 upload of object "+key+" failed.\nLast errors:\n"+extractLastLogErrors(5, "AWS-Client: ", true), LL_ERROR);
		}
	}

	return putObjectOutcome.IsSuccess();
}

namespace
{
	bool hasErrors(Aws::S3::Model::DeleteObjectsOutcome& deleteObjectsOutcome, bool& has_missing)
	{
		bool c_error=false;
		for(Aws::S3::Model::Error error: deleteObjectsOutcome.GetResult().GetErrors())
		{
			if(error.GetCode()!="NoSuchKey")
			{
				Server->Log("Deleting object "+std::string(error.GetKey().c_str())+" failed. "+error.GetMessage().c_str()+" (code: "+error.GetCode().c_str()+")", LL_ERROR);
				c_error=true;
			}
			else
			{
				Server->Log("Deleting object "+ std::string(error.GetKey().c_str())+" failed. "+error.GetMessage().c_str()+" (code: "+error.GetCode().c_str()+")", LL_INFO);
				has_missing=true;
			}
		}
		return c_error;
	}
}

bool KvStoreBackendS3::del(key_next_fun_t key_next_fun,
		locinfo_next_fun_t locinfo_next_fun,
		bool background_queue)
{
	//shard_optimized=false for now because of https://tracker.ceph.com/issues/41642 (radosgw does not return NoSuckKey if key is not present)
	return del_int(key_next_fun, locinfo_next_fun, false);
}

bool KvStoreBackendS3::del_int( key_next_fun_t key_next_fun,
	locinfo_next_fun_t locinfo_next_fun, bool shard_optimized)
{
	if(next(s3_endpoint, 0, "https://storage.googleapis.com"))
	{
		//Does not support bulk deletion
		Aws::S3::Model::DeleteObjectRequest deleteObjectRequest;

		bool sharded=false;
		bool has_missing=false;
		for(size_t idx=0;idx<buckets.size();++idx)
		{
			if(idx==0 && buckets[idx].name==SHARD_TAG)
			{
				sharded=true;
				continue;
			}
		
			deleteObjectRequest.SetBucket(buckets[idx].name.c_str());

			std::string key;
			while(key_next_fun(IKvStoreBackend::key_next_action_t::next, &key))
			{
				std::string locinfo;
				if (locinfo_next_fun != nullptr)
				{
					if (locinfo_next_fun(IKvStoreBackend::key_next_action_t::next, &locinfo))
						break;
				}

				if(shard_optimized &&
					sharded &&
					getShardIdx(key, buckets.size()-1)+1!=idx)
				{
					continue;
				}

				deleteObjectRequest.SetKey(key.c_str());
				if (!locinfo.empty())
				{
					deleteObjectRequest.SetVersionId(locinfo.c_str());
				}
				auto s3_client = getS3Client(idx);
				Aws::S3::Model::DeleteObjectOutcome deleteObjectOutcome = s3_client.second->DeleteObject(deleteObjectRequest);
				releaseS3Client(idx, s3_client);
				if (!deleteObjectOutcome.IsSuccess()
					&& deleteObjectOutcome.GetError().GetErrorType()!=Aws::S3::S3Errors::NO_SUCH_KEY  )
				{
					Server->Log("Deleting object "+key+" failed. "+deleteObjectOutcome.GetError().GetMessage().c_str()+" (code: "+convert(static_cast<int64>(deleteObjectOutcome.GetError().GetErrorType()))+")", LL_ERROR);
					fixError(deleteObjectOutcome.GetError().GetErrorType());
					return false;
				}
				else if(!deleteObjectOutcome.IsSuccess() &&
						deleteObjectOutcome.GetError().GetErrorType()==Aws::S3::S3Errors::NO_SUCH_KEY)
				{
					has_missing=true;
				}
			}
			if(idx+1<buckets.size())
			{
				key_next_fun(IKvStoreBackend::key_next_action_t::reset, nullptr);
				if (locinfo_next_fun != nullptr)
					locinfo_next_fun(IKvStoreBackend::key_next_action_t::reset, nullptr);
			}
		}
		
		if(shard_optimized && sharded && has_missing)
		{
			key_next_fun(IKvStoreBackend::key_next_action_t::reset, nullptr);
			if (locinfo_next_fun != nullptr)
				locinfo_next_fun(IKvStoreBackend::key_next_action_t::reset, nullptr);
			return del_int(key_next_fun, locinfo_next_fun, false);
		}

		return true;
	}

	Aws::S3::Model::DeleteObjectsRequest deleteObjectsRequest;
	
	bool sharded=false;
	bool has_missing=false;
	for(size_t idx=0;idx<buckets.size();++idx)
	{
		if(idx==0 && buckets[idx].name==SHARD_TAG)
		{
			sharded=true;
			continue;
		}
			
		deleteObjectsRequest.SetBucket(buckets[idx].name.c_str());

		Aws::S3::Model::Delete deleteList;

		std::string key;
		while(key_next_fun(IKvStoreBackend::key_next_action_t::next, &key))
		{
			std::string locinfo;
			if (locinfo_next_fun != nullptr)
			{
				if (locinfo_next_fun(IKvStoreBackend::key_next_action_t::next, &locinfo))
					break;
			}

			if(shard_optimized && sharded &&
				getShardIdx(key, buckets.size()-1)+1!=idx)
			{
				continue;
			}
			
			Aws::S3::Model::ObjectIdentifier oi;
			oi.SetKey(key.c_str());
			if (!locinfo.empty())
			{
				oi.SetVersionId(locinfo.c_str());
			}
			deleteList.AddObjects(oi);

			if (deleteList.GetObjects().size() > max_del_size() )
			{
				deleteObjectsRequest.SetDelete(deleteList);
				auto s3_client = getS3Client(idx);
				Aws::S3::Model::DeleteObjectsOutcome deleteObjectsOutcome = s3_client.second->DeleteObjects(deleteObjectsRequest);
				releaseS3Client(idx, s3_client);
				if (!deleteObjectsOutcome.IsSuccess()
					|| hasErrors(deleteObjectsOutcome, has_missing) )
				{
					if(!deleteObjectsOutcome.IsSuccess())
					{
						fixError(deleteObjectsOutcome.GetError().GetErrorType());
					}
					return false;
				}
				
				deleteList.SetObjects(Aws::Vector<Aws::S3::Model::ObjectIdentifier>());
			}
		}

		if (!deleteList.GetObjects().empty())
		{
			deleteObjectsRequest.SetDelete(deleteList);

			auto s3_client = getS3Client(idx);
			Aws::S3::Model::DeleteObjectsOutcome deleteObjectsOutcome = s3_client.second->DeleteObjects(deleteObjectsRequest);
			releaseS3Client(idx, s3_client);
			if(!deleteObjectsOutcome.IsSuccess() 
				|| hasErrors(deleteObjectsOutcome, has_missing) )
			{
				if(!deleteObjectsOutcome.IsSuccess())
				{
					fixError(deleteObjectsOutcome.GetError().GetErrorType());
				}
				return false;
			}
		}
		
		if(idx+1<buckets.size())
		{
			key_next_fun(IKvStoreBackend::key_next_action_t::reset, nullptr);
			if (locinfo_next_fun != nullptr)
				locinfo_next_fun(IKvStoreBackend::key_next_action_t::reset, nullptr);
		}
	}
	
	if(shard_optimized && sharded && has_missing)
	{
		Server->Log("Sharded and has missing. Re-running delete on all buckets...", LL_INFO);
		key_next_fun(IKvStoreBackend::key_next_action_t::reset, nullptr);
		if (locinfo_next_fun != nullptr)
			locinfo_next_fun(IKvStoreBackend::key_next_action_t::reset, nullptr);
		return del_int(key_next_fun, locinfo_next_fun, false);
	}
	
	return true;
}

bool KvStoreBackendS3::list_wo_versions(IListCallback* callback)
{
	bool etag_error = false;
	Aws::String key_marker;
	size_t idx = 0;
	auto s3_client = getS3Client(idx);
	while (idx < buckets.size())
	{
		if (buckets[idx].name == SHARD_TAG)
		{
			if (idx + 1 < buckets.size())
			{
				++idx;
				s3_client = getS3Client(idx);
				key_marker.clear();
				continue;
			}
			else
			{
				return false;
			}
		}

		Aws::S3::Model::ListObjectsRequest listObjectsRequest;
		listObjectsRequest.SetBucket(buckets[idx].name.c_str());

		if (!key_marker.empty())
		{
			listObjectsRequest.WithMarker(key_marker);
		}

		Aws::S3::Model::ListObjectsOutcome listObjectsOutcome = s3_client.second->ListObjects(listObjectsRequest);

		if (!listObjectsOutcome.IsSuccess())
		{
			Server->Log("Listing objects in bucket \"" + buckets[idx].name + "\" failed. " + listObjectsOutcome.GetError().GetMessage().c_str() + " (code: " + convert(static_cast<int64>(listObjectsOutcome.GetError().GetErrorType())) + ")", LL_ERROR);
			fixError(listObjectsOutcome.GetError().GetErrorType());
			releaseS3Client(idx, s3_client);
			return false;
		}

		for (const Aws::S3::Model::Object& object : listObjectsOutcome.GetResult().GetContents())
		{
			key_marker = object.GetKey();

			std::string md5sum;
			if (!callback->onlineItem(object.GetKey().c_str(), md5sum, object.GetSize(), object.GetLastModified().Millis()))
			{
				releaseS3Client(idx, s3_client);
				return false;
			}
		}

		if (!listObjectsOutcome.GetResult().GetIsTruncated())
		{
			releaseS3Client(idx, s3_client);

			if (idx + 1 < buckets.size())
			{
				++idx;
				s3_client = getS3Client(idx);
				key_marker.clear();
			}
			else
			{
				return true;
			}
		}
		else
		{
			if (!listObjectsOutcome.GetResult().GetNextMarker().empty())
			{
				key_marker = listObjectsOutcome.GetResult().GetNextMarker();
			}
		}
	}
	return true;
}

std::pair<int64, std::shared_ptr<Aws::S3::S3Client> > KvStoreBackendS3::getS3Client(size_t idx, bool useVirtualAdressing)
{
	if(!s3_endpoint.empty())
	{
		useVirtualAdressing=false;
	}

	IScopedLock lock(client_mutex);
	
	int64 curr_requesttimeout;
	if(n_requests>100)
	{
		curr_requesttimeout = max_request_timems + 60000;
	}
	else
	{
		curr_requesttimeout = 10 * 60000;
	}
	
	if(s3_clients[idx].empty())
	{
		lock.relock(nullptr);
		return newS3Client(idx, curr_requesttimeout, useVirtualAdressing);
	}

	std::pair<int64, std::shared_ptr<Aws::S3::S3Client> > ret = s3_clients[idx].top();
	s3_clients[idx].pop();
	
	lock.relock(nullptr);

	int64 tdiff = ret.first - curr_requesttimeout;
	if(tdiff<0) tdiff*=-1;
	
	if(tdiff<10000)
	{
		return ret;
	}
	
	return newS3Client(idx, curr_requesttimeout, useVirtualAdressing);
}

std::pair<int64, std::shared_ptr<Aws::S3::S3Client> > KvStoreBackendS3::newS3Client(size_t idx, int64 curr_requesttimeout, bool useVirtualAdressing)
{
	Aws::Client::ClientConfiguration clientConfiguration;
	clientConfiguration.region = BucketLocationToRegion(buckets[idx].location);
	clientConfiguration.followRedirects = Aws::Client::FollowRedirectsPolicy::ALWAYS;
	clientConfiguration.requestTimeoutMs = static_cast<long>(curr_requesttimeout);
	clientConfiguration.connectTimeoutMs = 3000;
	if(!s3_endpoint.empty())
	{
		std::string scheme = getuntil("://", s3_endpoint);			
		std::string endpoint;
		if(scheme.empty())
		{
			endpoint = s3_endpoint;
		}
		else
		{
			endpoint = getafter("://", s3_endpoint);
			
			clientConfiguration.scheme = Aws::Http::SchemeMapper::FromString(scheme.c_str());
		}
		
		if(!endpoint.empty() && endpoint[endpoint.size()-1]=='/')
		{
			endpoint.erase(endpoint.size()-1, 1);
		}
		
		clientConfiguration.endpointOverride = endpoint;
		
		std::string scaleway_tld = ".scw.cloud";
		std::string ovh_tld = ".cloud.ovh.net";
		if(endpoint.find(scaleway_tld)==endpoint.size()-scaleway_tld.size())
		{
			clientConfiguration.region = getbetween("s3.", scaleway_tld, endpoint);
		}
		else if(endpoint.find(ovh_tld)==endpoint.size()-ovh_tld.size())
		{
			clientConfiguration.region = getbetween("storage.", ovh_tld, endpoint);
		}
	}
	if(!s3_region.empty())
	{
		clientConfiguration.region = s3_region;
	}
	auto new_client = Aws::MakeShared<Aws::S3::S3Client>(ALLOCATION_TAG, credentials_provider, clientConfiguration, Aws::Client::AWSAuthV4Signer::PayloadSigningPolicy::Never, useVirtualAdressing);
	return std::make_pair(curr_requesttimeout, new_client);
}

void KvStoreBackendS3::releaseS3Client(size_t idx, std::pair<int64, std::shared_ptr<Aws::S3::S3Client> > client)
{
	IScopedLock lock(client_mutex);
	s3_clients[idx].push(client);
}

void KvStoreBackendS3::resetClient()
{
	IScopedLock lock(client_mutex);
	
	for(size_t idx=0;idx<s3_clients.size();++idx)
	{
		while(!s3_clients[idx].empty())
			s3_clients[idx].pop();
	}
}

void KvStoreBackendS3::setFrontend( IOnlineKvStore* online_kv_store , bool do_init)
{
	this->online_kv_store = online_kv_store;
}

void KvStoreBackendS3::fixError(Aws::S3::S3Errors error)
{
	if(error==Aws::S3::S3Errors::REQUEST_TIME_TOO_SKEWED)
	{
		Server->Log("Fixing system time...", LL_INFO);
		writestring("", "/tmp/time_sync");
		if(system("/root/update_time_often.sh > /dev/null 2>&1")==0)
		{
			Server->Log("...done. Please retry.", LL_INFO);
		}
		else
		{
			Server->Log("...failed.", LL_INFO);
		}
	}
}

std::string KvStoreBackendS3::meminfo()
{
	IScopedLock lock(client_mutex);
	std::string ret = "##KvStoreBackendS3:\n";
	for(size_t idx=0;idx<s3_clients.size();++idx)
	{
		ret+= "  s3_clients["+convert(idx)+"]: "+convert(s3_clients[idx].size())+" * "+PrettyPrintBytes(sizeof(int64) + sizeof(std::shared_ptr<Aws::S3::S3Client>))+"\n";
	}
	return ret;
}
