#include "SChannelPipe.h"
#include "Server.h"
#include "stringtools.h"
#include <ntsecapi.h>
#include <sspi.h>
#include <schnlsp.h>


PSecurityFunctionTableW SChannelPipe::sec = NULL;

SChannelPipe::SChannelPipe(CStreamPipe * bpipe)
	: bpipe(bpipe), has_cred_handle(false),
	has_ctxt_handle(false), decbuf_pos(0),
	sendbuf_pos(0), last_flush_time(0),
	has_error(false)
{
}

SChannelPipe::~SChannelPipe()
{
	if (has_cred_handle)
	{
		sec->FreeCredentialsHandle(&cred_handle);
	}

	if (has_ctxt_handle)
	{
		sec->DeleteSecurityContext(&ctxt_handle);
	}

	delete bpipe;
}

bool SChannelPipe::ssl_connect(const std::string& p_hostname, int timeoutms)
{
	hostname = p_hostname;
	int64 starttime = Server->getTimeMS();

	SCHANNEL_CRED cred_data = {};

	cred_data.dwVersion = SCHANNEL_CRED_VERSION;
	cred_data.dwFlags = SCH_CRED_AUTO_CRED_VALIDATION | SCH_CRED_REVOCATION_CHECK_CHAIN;
	cred_data.grbitEnabledProtocols = SP_PROT_TLS1_0_CLIENT | SP_PROT_TLS1_1_CLIENT | SP_PROT_TLS1_2_CLIENT;

	HRESULT res = sec->AcquireCredentialsHandleW(NULL, UNISP_NAME_W,
		SECPKG_CRED_OUTBOUND, NULL, &cred_data, NULL, NULL, &cred_handle, &time_stamp);

	if (res != SEC_E_OK)
	{
		Server->Log("AcquireCredentialsHandleW failed with result " + convert((int64)res), LL_ERROR);
		return false;
	}

	has_cred_handle = true;

	SecBuffer outbuf = {};
	outbuf.BufferType = SECBUFFER_EMPTY;
	SecBufferDesc outbuf_desc;
	outbuf_desc.ulVersion = SECBUFFER_VERSION;
	outbuf_desc.cBuffers = 1;
	outbuf_desc.pBuffers = &outbuf;

	std::wstring hostname_w = Server->ConvertToWchar(hostname);

	unsigned long flags = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT |
		ISC_REQ_CONFIDENTIALITY | ISC_REQ_ALLOCATE_MEMORY |
		ISC_REQ_STREAM;

	unsigned long ret_flags = 0;
	res = sec->InitializeSecurityContextW(&cred_handle, NULL, const_cast<wchar_t*>(hostname_w.c_str()), flags,
		0, 0, NULL, 0, &ctxt_handle, &outbuf_desc, &ret_flags, &time_stamp);

	if (res != SEC_I_CONTINUE_NEEDED)
	{
		Server->Log("InitializeSecurityContextW failed with result " + convert((int64)res), LL_ERROR);
		return false;
	}

	has_ctxt_handle = true;

	if (flags != ret_flags)
	{
		Server->Log("Setting security context flags failed " + convert((int64)flags) + "!=" + convert((int64)ret_flags), LL_ERROR);
		return false;
	}

	if (!bpipe->Write(reinterpret_cast<char*>(outbuf.pvBuffer), outbuf.cbBuffer, timeoutms, true))
	{
		return false;
	}

	last_flush_time = Server->getTimeMS();
	int64 passed_time = last_flush_time - starttime;
	int64 remaining_time = timeoutms == -1 ? -1 : (passed_time < timeoutms ? (timeoutms - passed_time) : 0);
	return ssl_connect_negotiate(static_cast<int>(remaining_time), true);
}

bool SChannelPipe::ssl_connect_negotiate(int timeoutms, bool do_read)
{
	const size_t encbuf_size_incr = 4096;

	bool connected = false;

	int64 starttime = Server->getTimeMS();

	unsigned long flags = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT |
		ISC_REQ_CONFIDENTIALITY | ISC_REQ_ALLOCATE_MEMORY |
		ISC_REQ_STREAM;

	unsigned long ret_flags;

	std::wstring hostname_w = Server->ConvertToWchar(hostname);

	int64 passed_time;
	encbuf_pos = 0;
	while (timeoutms == -1
		|| (passed_time = Server->getTimeMS() - starttime)<timeoutms)
	{
		if (timeoutms == -1)
			passed_time = Server->getTimeMS() - starttime;

		if (do_read)
		{
			do_read = false;

			if (!bpipe->isReadable(timeoutms == -1 ? timeoutms : (timeoutms - passed_time)))
				return false;

			if (encbuf.size() - encbuf_pos < encbuf_size_incr)
			{
				encbuf.resize(encbuf.size() + encbuf_size_incr);
			}
			size_t read = bpipe->Read(&encbuf[encbuf_pos], encbuf_size_incr, 0);

			if (read == 0)
				return false;

			encbuf_pos += read;
		}

		SecBuffer inbuf2[2] = {};
		inbuf2[0].BufferType = SECBUFFER_TOKEN;
		inbuf2[0].cbBuffer = encbuf_pos;
		inbuf2[0].pvBuffer = encbuf.data();
		inbuf2[1].BufferType = SECBUFFER_EMPTY;

		SecBufferDesc inbuf2_desc;
		inbuf2_desc.cBuffers = 2;
		inbuf2_desc.pBuffers = inbuf2;
		inbuf2_desc.ulVersion = SECBUFFER_VERSION;

		SecBuffer outbuf[3] = {};
		outbuf[0].BufferType = SECBUFFER_TOKEN;
		outbuf[1].BufferType = SECBUFFER_ALERT;
		outbuf[2].BufferType = SECBUFFER_EMPTY;

		SecBufferDesc outbuf_desc;
		outbuf_desc.cBuffers = 3;
		outbuf_desc.pBuffers = outbuf;
		outbuf_desc.ulVersion = SECBUFFER_VERSION;

		HRESULT res = sec->InitializeSecurityContextW(&cred_handle, &ctxt_handle,
			const_cast<wchar_t*>(hostname_w.c_str()), flags, 0, 0, &inbuf2_desc,
			0, NULL, &outbuf_desc, &ret_flags, &time_stamp);

		if (res == SEC_E_INCOMPLETE_MESSAGE)
		{
			do_read = true;
			continue;
		}
		else
		{
			if (inbuf2[1].BufferType == SECBUFFER_EXTRA
				&& inbuf2[1].cbBuffer>0)
			{
				encbuf.erase(encbuf.begin(), encbuf.begin() + (encbuf_pos - inbuf2[1].cbBuffer));
				encbuf_pos -= encbuf_pos - inbuf2[1].cbBuffer;

				if (res == SEC_I_CONTINUE_NEEDED)
				{
					continue;
				}
			}
			else
			{
				encbuf.clear();
				encbuf_pos = 0;
			}
		}

		if (res == SEC_E_OK
			|| res == SEC_I_CONTINUE_NEEDED)
		{
			bool has_error = false;
			for (size_t i = 0; i < outbuf_desc.cBuffers; ++i)
			{
				SecBuffer& buf = outbuf_desc.pBuffers[i];
				if (buf.BufferType == SECBUFFER_TOKEN
					&& buf.cbBuffer > 0
					&& !has_error)
				{
					passed_time = Server->getTimeMS() - starttime;
					int64 remaining_time = timeoutms == -1 ? -1 : (passed_time < timeoutms ? (timeoutms - passed_time) : 0);
					if (!bpipe->Write(reinterpret_cast<char*>(buf.pvBuffer), buf.cbBuffer, static_cast<int>(remaining_time), true))
					{
						has_error = true;
					}
				}

				if (buf.pvBuffer != NULL)
					sec->FreeContextBuffer(buf.pvBuffer);
			}

			if (has_error)
				return false;

			if (res == SEC_E_OK)
			{
				connected = true;
				break;
			}
			else
			{
				do_read = true;
			}
		}
		else
		{
			return false;
		}
	}

	if (connected)
	{
		HRESULT res = sec->QueryContextAttributesW(&ctxt_handle, SECPKG_ATTR_STREAM_SIZES, &stream_sizes);

		if (res != SEC_E_OK)
		{
			return false;
		}

		header_buf.resize(stream_sizes.cbHeader);
		trailer_buf.resize(stream_sizes.cbTrailer);
	}

	return connected;
}


void SChannelPipe::init()
{
	HMODULE mod = LoadLibraryW(L"secur32.dll");

	if (mod == NULL)
	{
		Server->Log("Error loading libarary secur32.dll. Errno: " + convert((int64)GetLastError()), LL_ERROR);
		return;
	}

	INIT_SECURITY_INTERFACE_W init_sec = reinterpret_cast<INIT_SECURITY_INTERFACE_W>(GetProcAddress(mod, "InitSecurityInterfaceW"));

	if (init_sec == NULL)
	{
		Server->Log("Error getting proc InitSecurityInterfaceW in secur32.dll. Errno: " + convert((int64)GetLastError()), LL_ERROR);
		return;
	}

	sec = init_sec();
}

size_t SChannelPipe::Read(char * buffer, size_t bsize, int timeoutms)
{
	const size_t encbuf_size_incr = 1024;

	if (has_error)
		return 0;

	if (decbuf_pos>0)
	{
		size_t toread = (std::min)(decbuf_pos, bsize);
		memcpy(buffer, decbuf.data(), toread);
		decbuf.erase(decbuf.begin(), decbuf.begin() + toread);
		decbuf_pos -= toread;
		return toread;
	}

	int64 starttime = Server->getTimeMS();

	if (encbuf.size() - encbuf_pos < bsize)
	{
		encbuf.resize(encbuf.size() + bsize);
	}

	size_t read = bpipe->Read(&encbuf[encbuf_pos], bsize, timeoutms);

	if (read == 0)
		return 0;

	encbuf_pos += read;

	size_t orig_bsize = bsize;

	HRESULT res = SEC_E_OK;
	while ( (res == SEC_E_OK || res== SEC_E_INCOMPLETE_MESSAGE)
		&& encbuf_pos > 0 && bsize>0)
	{
		if (res == SEC_E_INCOMPLETE_MESSAGE)
		{
			if (encbuf.size() - encbuf_pos < encbuf_size_incr)
			{
				encbuf.resize(encbuf.size() + encbuf_size_incr);
			}

			int64 passed_time = Server->getTimeMS() - starttime;
			int remaining_time = timeoutms == -1 ? -1 : ((timeoutms - passed_time) < 0 ? 0 : (timeoutms - passed_time));

			size_t read = bpipe->Read(&encbuf[encbuf_pos], encbuf_size_incr, remaining_time);

			if (read == 0)
				return 0;

			encbuf_pos += read;
		}

		SecBuffer inbuf[4] = {};
		inbuf[0].BufferType = SECBUFFER_DATA;
		inbuf[0].pvBuffer = encbuf.data();
		inbuf[0].cbBuffer = encbuf_pos;
		inbuf[1].BufferType = SECBUFFER_EMPTY;
		inbuf[2].BufferType = SECBUFFER_EMPTY;
		inbuf[3].BufferType = SECBUFFER_EMPTY;

		SecBufferDesc inbuf_desc;
		inbuf_desc.ulVersion = SECBUFFER_VERSION;
		inbuf_desc.cBuffers = 4;
		inbuf_desc.pBuffers = inbuf;

		res = sec->DecryptMessage(&ctxt_handle, &inbuf_desc, 0, NULL);

		if (res == SEC_E_OK
			|| res== SEC_I_RENEGOTIATE)
		{
			if (inbuf[1].BufferType == SECBUFFER_DATA
				&& inbuf[1].cbBuffer>0)
			{
				size_t toread = 0;
				if (bsize > 0)
				{
					toread = (std::min)(bsize, (size_t)inbuf[1].cbBuffer);
					memcpy(buffer, inbuf[1].pvBuffer, toread);
					inbuf[1].cbBuffer -= toread;
					bsize -= toread;
					buffer += toread;
				}

				if (inbuf[1].cbBuffer > 0)
				{
					if (decbuf.size() - decbuf_pos < inbuf[1].cbBuffer)
					{
						decbuf.resize(decbuf.size() + inbuf[1].cbBuffer);
					}

					memcpy(&decbuf[decbuf_pos], reinterpret_cast<char*>(inbuf[1].pvBuffer) + toread, inbuf[1].cbBuffer);
					decbuf_pos += inbuf[1].cbBuffer;
				}
			}

			if (inbuf[3].BufferType == SECBUFFER_EXTRA
				&& inbuf[3].cbBuffer>0)
			{
				encbuf.erase(encbuf.begin(), encbuf.begin() + (encbuf_pos - inbuf[3].cbBuffer));
				encbuf_pos -= encbuf_pos - inbuf[3].cbBuffer;
			}
			else
			{
				encbuf.clear();
				encbuf_pos = 0;
			}
		}

		if (res == SEC_I_RENEGOTIATE)
		{
			if (!ssl_connect_negotiate(timeoutms, false))
			{
				return 0;
			}
		}

		if (res == SEC_I_CONTEXT_EXPIRED)
		{
			return 0;
		}
	}

	return orig_bsize-bsize;
}

bool SChannelPipe::Write(const char * buffer, size_t bsize, int timeoutms, bool flush)
{
	if (has_error)
		return false;

	if (sendbuf_pos + bsize > sendbuf.size())
	{
		sendbuf.resize(sendbuf_pos + bsize);
	}

	memcpy(&sendbuf[sendbuf_pos], buffer, bsize);
	sendbuf_pos += bsize;

	if ((flush || sendbuf_pos>128 * 1024 || sendbuf_pos>= stream_sizes.cbMaximumMessage || (Server->getTimeMS() - last_flush_time)>200)
		&& sendbuf_pos>0)
	{
		return Flush(timeoutms);
	}

	return true;
}

size_t SChannelPipe::Read(std::string * ret, int timeoutms)
{
	if (has_error)
		return 0;

	char buf[1024];
	size_t read = Read(buf, sizeof(buf), timeoutms);
	if (read > 0)
		ret->assign(buf, read);
	return read;
}

bool SChannelPipe::Write(const std::string & str, int timeoutms, bool flush)
{
	return Write(str.data(), str.size(), timeoutms, flush);
}

bool SChannelPipe::Flush(int timeoutms)
{
	if (has_error)
		return false;

	size_t sendbuf_off = 0;
	while (sendbuf_pos- sendbuf_off> 0)
	{
		size_t toflush = (std::min)((size_t)stream_sizes.cbMaximumMessage, sendbuf_pos- sendbuf_off);
		
		SecBuffer outbuf[4] = {};
		outbuf[0].BufferType = SECBUFFER_STREAM_HEADER;
		outbuf[0].cbBuffer = header_buf.size();
		outbuf[0].pvBuffer = header_buf.data();
		outbuf[1].BufferType = SECBUFFER_DATA;
		outbuf[1].cbBuffer = toflush;
		outbuf[1].pvBuffer = &sendbuf[sendbuf_off];
		outbuf[2].BufferType = SECBUFFER_STREAM_TRAILER;
		outbuf[2].cbBuffer = trailer_buf.size();
		outbuf[2].pvBuffer = trailer_buf.data();
		outbuf[3].BufferType = SECBUFFER_EMPTY;

		SecBufferDesc outbuf_desc;
		outbuf_desc.ulVersion = SECBUFFER_VERSION;
		outbuf_desc.cBuffers = 4;
		outbuf_desc.pBuffers = outbuf;

		HRESULT res = sec->EncryptMessage(&ctxt_handle, 0, &outbuf_desc, 0);

		if (res != SEC_E_OK)
		{
			return false;
		}

		if (outbuf[0].cbBuffer > 0)
		{
			if (!bpipe->Write(reinterpret_cast<char*>(outbuf[0].pvBuffer), outbuf[0].cbBuffer, timeoutms, false))
			{
				has_error = true;
				return false;
			}
		}

		if (outbuf[1].cbBuffer > 0)
		{
			if (!bpipe->Write(reinterpret_cast<char*>(outbuf[1].pvBuffer), outbuf[1].cbBuffer, timeoutms, false))
			{
				has_error = true;
				return false;
			}
		}

		if (outbuf[2].cbBuffer > 0)
		{
			if (!bpipe->Write(reinterpret_cast<char*>(outbuf[2].pvBuffer), outbuf[2].cbBuffer, timeoutms, false))
			{
				has_error = true;
				return false;
			}
		}

		sendbuf_off += toflush;
	}

	sendbuf_pos -= sendbuf_off;

	return bpipe->Flush(timeoutms);
}

bool SChannelPipe::isWritable(int timeoutms)
{
	if (has_error)
		return false;

	return bpipe->isWritable(timeoutms);
}

bool SChannelPipe::isReadable(int timeoutms)
{
	if (has_error)
		return false;

	return bpipe->isReadable(timeoutms);
}

bool SChannelPipe::hasError(void)
{
	return has_error || bpipe->hasError();
}

void SChannelPipe::shutdown(void)
{
	bpipe->shutdown();
}

size_t SChannelPipe::getNumWaiters() {
	return bpipe->getNumWaiters();
}

size_t SChannelPipe::getNumElements(void)
{
	return bpipe->getNumElements();
}

void SChannelPipe::addThrottler(IPipeThrottler * throttler)
{
	bpipe->addThrottler(throttler);
}

void SChannelPipe::addOutgoingThrottler(IPipeThrottler * throttler)
{
	bpipe->addOutgoingThrottler(throttler);
}

void SChannelPipe::addIncomingThrottler(IPipeThrottler * throttler)
{
	bpipe->addIncomingThrottler(throttler);
}

_i64 SChannelPipe::getTransferedBytes(void)
{
	return bpipe->getTransferedBytes();
}

void SChannelPipe::resetTransferedBytes(void)
{
	bpipe->resetTransferedBytes();
}