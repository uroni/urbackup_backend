// crc.h - written and placed in the public domain by Wei Dai

//! \file
//! \headerfile crc.h
//! \brief Classes for CRC-32 and CRC-32C checksum algorithm

#pragma once

#include <string>
#include <cstdint>

namespace cryptopp_crc
{

	const unsigned int CRC32_NEGL = 0xffffffffL;

#ifdef IS_LITTLE_ENDIAN
#define CRC32_INDEX(c) (c & 0xff)
#define CRC32_SHIFTED(c) (c >> 8)
#else
#define CRC32_INDEX(c) (c >> 24)
#define CRC32_SHIFTED(c) (c << 8)
#endif

	//! \brief CRC-32C Checksum Calculation
	//! \details Uses CRC polynomial 0x82F63B78
	//! \since Crypto++ 5.6.4
	class CRC32C
	{
	public:
		CRC32C();
		CRC32C(unsigned int icrc);
		void Update(const char *input, size_t length);
		void TruncatedFinal(char *hash, size_t size);
		unsigned int Final();
		virtual void Final(char* digest) {
			TruncatedFinal(digest, DigestSize());
		}
		unsigned int DigestSize() const { return 4; }
		static const char *StaticAlgorithmName() { return "CRC32C"; }
		std::string AlgorithmName() const { return StaticAlgorithmName(); }

		void UpdateByte(char b) { m_crc = m_tab[CRC32_INDEX(m_crc) ^ b] ^ CRC32_SHIFTED(m_crc); }
		char GetCrcByte(size_t i) const { return ((char*)&(m_crc))[i]; }

	protected:
		void Reset() { m_crc = CRC32_NEGL; }

	private:
		static const unsigned int m_tab[256];
		unsigned int m_crc;
	};

	unsigned int crc32c_hw(
		unsigned int crc,
		const char *input,
		size_t length);

	enum crc32c_alg_type
	{
		crc32c_alg_type_sse4_crc,
		crc32c_alg_type_arm64_crc,
		crc32c_alg_type_software
	};

	crc32c_alg_type get_crc32c_alg_type();
}
