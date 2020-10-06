#ifdef WITH_OPENSSL

#include "OpenSSLPipe.h"
#include <openssl/ssl.h>
#include <openssl/conf.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>
#include <openssl/bio.h>
#include "Server.h"
#include <sys/types.h>
#include <sys/stat.h>
#ifndef _WIN32
#include <unistd.h>
#endif

OpenSSLPipe::OpenSSLPipe(CStreamPipe * bpipe)
	: bpipe(bpipe), bbio(NULL), ctx(NULL),
	has_error(false)
{
	
}

OpenSSLPipe::~OpenSSLPipe()
{
	if (bbio != NULL)
		BIO_free_all(bbio);

	if (ctx != NULL)
		SSL_CTX_free(ctx);
}


namespace
{
	void log_ssl_err()
	{
		unsigned long err = ERR_get_error();

		if (err != 0)
		{
			const char* const str = ERR_reason_error_string(err);
			if (str)
			{
				Server->Log(std::string("OpenSSL error: ") + str, LL_WARNING);
			}
		}
	}

	const char* const PREFERRED_CIPHERS = "HIGH:!aNULL:!kRSA:!SRP:!PSK:!CAMELLIA:!RC4:!MD5:!DSS";

	//ALL that shit just to set MSG_NOSIGNAL :(
	BIO_METHOD* meth_socket_nosigpipe;

	int bio_read(BIO *b, char * buf, int len)
	{
		if (buf == NULL || len <= 0)
			return 0;

		BIO_clear_retry_flags(b);

		int fd = BIO_get_fd(b, NULL);

		int rc = recv(fd, buf, len, MSG_NOSIGNAL);
		if (rc <= 0)
		{
#ifdef _WIN32
			DWORD err = WSAGetLastError();
			if (err == WSAEWOULDBLOCK || err == WSAEINTR)
			{
				BIO_set_retry_read(b);
			}
#else
			if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
			{
				BIO_set_retry_read(b);
			}
#endif
		}
		return rc;
	}

	int bio_read_ex(BIO *b, char * buf, size_t len, size_t *readbytes)
	{
		int rc = bio_read(b, buf, static_cast<int>(len));
		if (rc > 0)
		{
			*readbytes = rc;
			return 1;
		}
		else
		{
			return 0;
		}
	}

	int bio_write(BIO* b, const char* buf, int len)
	{
		if (buf == NULL || len <= 0)
			return 0;

		BIO_clear_retry_flags(b);

		int fd = BIO_get_fd(b, NULL);

		int rc = send(fd, buf, len, MSG_NOSIGNAL);
		if(rc<0)
		{
#ifdef _WIN32
			DWORD err = WSAGetLastError();
			if (err == EINTR || err== WSAEWOULDBLOCK)
			{
				BIO_set_retry_write(b);
			}
#else
			if (errno == EINTR || errno == EAGAIN || errno== EWOULDBLOCK)
			{
				BIO_set_retry_write(b);
			}
#endif
			return rc;
		}
		return rc;
	}

	int bio_write_ex(BIO* b, const char* buf, size_t len, size_t *written)
	{
		int rc = bio_write(b, buf, static_cast<int>(len));
		if (rc > 0)
		{
			*written = rc;
			return 1;
		}
		else
		{
			return 0;
		}
	}
}

void OpenSSLPipe::init()
{
#if OPENSSL_VERSION_NUMBER < 0x10100000L
	SSL_library_init();

	SSL_load_error_strings();

	OPENSSL_config(NULL);
#endif

#if OPENSSL_VERSION_NUMBER >= 0x10101000L
	const BIO_METHOD* smeth = BIO_s_socket();
#else
	BIO_METHOD* smeth = (BIO_METHOD*)BIO_s_socket();
#endif

	int s_socket_nosignpipe = BIO_get_new_index();

	meth_socket_nosigpipe = BIO_meth_new(s_socket_nosignpipe, "socket_nosigpipe");

	BIO_meth_set_callback_ctrl(meth_socket_nosigpipe, BIO_meth_get_callback_ctrl(smeth));
	BIO_meth_set_create(meth_socket_nosigpipe, BIO_meth_get_create(smeth));
	BIO_meth_set_ctrl(meth_socket_nosigpipe, BIO_meth_get_ctrl(smeth));
	BIO_meth_set_destroy(meth_socket_nosigpipe, BIO_meth_get_destroy(smeth));
	BIO_meth_set_gets(meth_socket_nosigpipe, BIO_meth_get_gets(smeth));
	BIO_meth_set_puts(meth_socket_nosigpipe, BIO_meth_get_puts(smeth));
	BIO_meth_set_read(meth_socket_nosigpipe, bio_read);
	BIO_meth_set_write(meth_socket_nosigpipe, bio_write);
#if OPENSSL_VERSION_NUMBER >= 0x10101000L
	BIO_meth_set_read_ex(meth_socket_nosigpipe, bio_read_ex);
	BIO_meth_set_write_ex(meth_socket_nosigpipe, bio_write_ex);
#endif
}

#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif

bool OpenSSLPipe::ssl_connect(const std::string & p_hostname, int timeoutms)
{
	const SSL_METHOD* method = SSLv23_method();

	if (method == NULL)
	{
		log_ssl_err();
		return false;
	}

	ctx = SSL_CTX_new(method);

	if (ctx == NULL)
	{
		log_ssl_err();
		return false;
	}

	SSL_CTX_set_verify_depth(ctx, 5);

	const long flags = SSL_OP_ALL | SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_COMPRESSION;
	SSL_CTX_set_options(ctx, flags);

	std::string capath = Server->getServerParameter("capath");
	if (!capath.empty())
	{
		struct stat sbuf;
		if (stat(capath.c_str(), &sbuf)==0 &&
			!S_ISDIR(sbuf.st_mode))
		{
			long res = SSL_CTX_load_verify_locations(ctx, capath.c_str(), NULL);

			if (res != 1)
			{
				log_ssl_err();
				return false;
			}
		}
		else
		{
			long res = SSL_CTX_load_verify_locations(ctx, NULL, capath.c_str());

			if (res != 1)
			{
				log_ssl_err();
				return false;
			}
		}
	}
	else
	{
#ifndef OPENSSL_SEARCH_CA
		long res = SSL_CTX_set_default_verify_paths(ctx);

		if (res != 1)
		{
			log_ssl_err();
			return false;
		}
#else
		char* capaths[] = {
			"/etc/pki/tls/certs/ca-bundle.crt",
			"/etc/ssl/certs/ca-certificates.crt",			
			"/var/lib/ca-certificates/ca-bundle.pem",
			"/system/etc/security/cacerts",
			"/etc/ssl/certs",
			"/etc/pki/tls/certs",
			"/etc/openssl/certs",
			"/var/ssl/certs",
			NULL
		};

		bool found_ca = false;

		for (size_t i=0;capaths[i]!=NULL;++i)
		{
			char* cafn = capaths[i];
			struct stat sbuf;
			if (stat(cafn, &sbuf) == 0)
			{
				if (S_ISDIR(sbuf.st_mode))
				{
					long res = SSL_CTX_load_verify_locations(ctx, NULL, cafn);

					if (res == 1)
					{
						found_ca = true;
						break;
					}
				}
				else
				{
					long res = SSL_CTX_load_verify_locations(ctx, cafn, NULL);

					if (res == 1)
					{
						found_ca = true;
						break;
					}
				}
			}
		}		

		if (!found_ca)
		{
			Server->Log("No ca certificates found. Please specify certificate bundle path via capath parameter", LL_WARNING);
			return false;
		}
#endif
	}

	bbio = BIO_new_ssl(ctx, 1);

	if (bbio == NULL)
	{
		log_ssl_err();
		return false;
	}

	BIO* sbio = BIO_new(meth_socket_nosigpipe);

	if (sbio == NULL)
	{
		log_ssl_err();
		return false;
	}

	BIO_set_fd(sbio, static_cast<int>(bpipe->getSocket()), BIO_NOCLOSE);

	BIO_push(bbio, sbio);

	BIO_set_nbio(bbio, 1);

	SSL *ssl = NULL;
	BIO_get_ssl(bbio, &ssl);
	if (ssl == NULL)
	{
		log_ssl_err();
		return false;
	}

	long res = SSL_set_cipher_list(ssl, PREFERRED_CIPHERS);
	if (res!=1)
	{
		log_ssl_err();
		return false;
	}

	res = SSL_set_tlsext_host_name(ssl, p_hostname.c_str());
	if (res != 1)
	{
		log_ssl_err();
		return false;
	}

#if OPENSSL_VERSION_NUMBER < 0x10100000L
	X509_VERIFY_PARAM* param = SSL_get0_param(ssl);
	X509_VERIFY_PARAM_set_hostflags(param, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
	res = X509_VERIFY_PARAM_set1_host(param, p_hostname.c_str(), p_hostname.size());
#else
	SSL_set_hostflags(ssl, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
	res = SSL_set1_host(ssl, p_hostname.c_str());
#endif
	if (res != 1)
	{
		log_ssl_err();
		return false;
	}

	SSL_set_verify(ssl, SSL_VERIFY_PEER, NULL);

	int64 starttime = Server->getTimeMS();
	do
	{
		res = BIO_do_handshake(bbio);
		if (res != 1
			&& !BIO_should_retry(bbio) )
		{
			log_ssl_err();
			return false;
		}

		if (res == 1)
			break;

		int64 rtime = timeoutms - (Server->getTimeMS() - starttime);

		if (rtime < 0)
			rtime = 0;
		if (timeoutms < 0)
			rtime = -1;

		if (!bpipe->isReadOrWritable(static_cast<int>(rtime)))
			break;

	} while (Server->getTimeMS() - starttime < timeoutms);

	if (res != 1)
	{
		Server->Log("SSL connect timeout", LL_WARNING);
		return false;
	}

	X509* cert = SSL_get_peer_certificate(ssl);
	if (cert)
	{
		X509_free(cert);
	}

	if (cert == NULL)
	{
		Server->Log("Getting server certificate failed", LL_WARNING);
		return false;
	}

	res = SSL_get_verify_result(ssl);
	if (res != X509_V_OK)
	{
		const char* const str = ERR_reason_error_string(res);
		if (str)
		{
			Server->Log("Verifying certificate of hostname " + p_hostname + " failed with OpenSSL error code " + str, LL_WARNING);
		}
		return false;
	}

	return true;
}

size_t OpenSSLPipe::Read(char * buffer, size_t bsize, int timeoutms)
{
	bool retry=false;
	do
	{
		if (BIO_pending(bbio)<=0)
		{
			if (!bpipe->isReadable(timeoutms))
			{
				return 0;
			}
		}

		int rc = BIO_read(bbio, buffer, static_cast<int>(bsize));

		if (rc <= 0)
		{
			if (!BIO_should_retry(bbio))
			{
				has_error = true;
				return 0;
			}
			else
			{
				retry=true;
			}
		}
		else
		{
			bpipe->doThrottle(rc, false, true);
			return rc;
		}
	} while(retry);

	return 0;
}

bool OpenSSLPipe::Write(const char * buffer, size_t bsize, int timeoutms, bool flush)
{
	if (bsize == 0)
		return true;

	if (!bpipe->isWritable(timeoutms))
	{
		return false;
	}

	int rc = BIO_write(bbio, buffer, static_cast<int>(bsize));

	if (rc <= 0)
	{
		if (!BIO_should_retry(bbio))
		{
			has_error = true;
		}
		return false;
	}
	else
	{
		if (rc < bsize)
		{
			bpipe->doThrottle(rc, true, true);

			return Write(buffer + rc, bsize - rc, -1, flush);
		}

		return true;
	}
}

size_t OpenSSLPipe::Read(std::string * ret, int timeoutms)
{
	char buffer[8192];
	size_t l = Read(buffer, 8192, timeoutms);
	if (l>0)
	{
		ret->assign(buffer, l);
	}
	else
	{
		return 0;
	}
	return l;
}

bool OpenSSLPipe::Write(const std::string & str, int timeoutms, bool flush)
{
	return Write(&str[0], str.size(), timeoutms, flush);
}

bool OpenSSLPipe::Flush(int timeoutms)
{
	return bpipe->Flush(timeoutms);
}

bool OpenSSLPipe::isWritable(int timeoutms)
{
	return bpipe->isWritable(timeoutms);
}

bool OpenSSLPipe::isReadable(int timeoutms)
{
	return BIO_pending(bbio)>0 || bpipe->isReadable(timeoutms);
}

bool OpenSSLPipe::hasError(void)
{
	return has_error || bpipe->hasError();
}

void OpenSSLPipe::shutdown(void)
{
	bpipe->shutdown();
}

size_t OpenSSLPipe::getNumWaiters()
{
	return bpipe->getNumWaiters();
}

size_t OpenSSLPipe::getNumElements(void)
{
	return bpipe->getNumElements();
}

void OpenSSLPipe::addThrottler(IPipeThrottler * throttler)
{
	bpipe->addThrottler(throttler);
}

void OpenSSLPipe::addOutgoingThrottler(IPipeThrottler * throttler)
{
	bpipe->addOutgoingThrottler(throttler);
}

void OpenSSLPipe::addIncomingThrottler(IPipeThrottler * throttler)
{
	bpipe->addIncomingThrottler(throttler);
}

_i64 OpenSSLPipe::getTransferedBytes(void)
{
	return bpipe->getTransferedBytes();
}

void OpenSSLPipe::resetTransferedBytes(void)
{
	bpipe->resetTransferedBytes();
}

#endif //WITH_OPENSSL
