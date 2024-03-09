// crc.cpp - written and placed in the public domain by Wei Dai

#include "crc.h"
#ifndef _WIN32
#include <signal.h>
#include <setjmp.h>
#endif

#include <cstdint> // for uintptr_t data type

// Visual Studio needs VS2008 (1500)
//  http://msdn.microsoft.com/en-us/library/bb531394%28v=vs.90%29.aspx
#if defined(_MSC_VER) && (_MSC_VER < 1500)
# undef CRYPTOPP_BOOL_SSE4_INTRINSICS_AVAILABLE
#endif

#if !defined(CRYPTOPP_LITTLE_ENDIAN) && !defined(CRYPTOPP_BIG_ENDIAN) && (defined(__BIG_ENDIAN__) || (defined(__s390__) || defined(__s390x__) || defined(__zarch__)) || (defined(__m68k__) || defined(__MC68K__)) || defined(__sparc) || defined(__sparc__) || defined(__hppa__) || defined(__MIPSEB__) || defined(__ARMEB__) || (defined(__MWERKS__) && !defined(__INTEL__)))
#	define CRYPTOPP_BIG_ENDIAN 1
#endif

// define this if running on a little-endian CPU
// big endian will be assumed if CRYPTOPP_LITTLE_ENDIAN is not non-0
#if !defined(CRYPTOPP_BIG_ENDIAN) && !defined(CRYPTOPP_LITTLE_ENDIAN)
#	define CRYPTOPP_LITTLE_ENDIAN 1
#endif

// Intrinsics availible in GCC 4.3 (http://gcc.gnu.org/gcc-4.3/changes.html) and
//   MSVC 2008 (http://msdn.microsoft.com/en-us/library/bb892950%28v=vs.90%29.aspx)
//   SunCC could generate SSE4 at 12.1, but the intrinsics are missing until 12.4.
#if !defined(CRYPTOPP_DISABLE_ASM) && !defined(CRYPTOPP_DISABLE_SSE4) && !defined(_M_ARM) && ((_MSC_VER >= 1500) || (defined(__SSE4_1__) && defined(__SSE4_2__)))
#define CRYPTOPP_BOOL_SSE4_INTRINSICS_AVAILABLE 1
#else
#define CRYPTOPP_BOOL_SSE4_INTRINSICS_AVAILABLE 0
#endif

// Requires ARMv8 and ACLE 2.0. For GCC, requires 4.8 and above.
// Microsoft plans to support ARM-64, but its not clear how to detect it.
// TODO: Add MSC_VER and ARM-64 platform define when available
#if !defined(CRYPTOPP_BOOL_ARM_CRC32_INTRINSICS_AVAILABLE) && !defined(CRYPTOPP_DISABLE_ASM)
# if defined(__ARM_FEATURE_CRC32) || defined(_M_ARM64)
#  define CRYPTOPP_BOOL_ARM_CRC32_INTRINSICS_AVAILABLE 1
#if defined(_MSC_VER) 
#include <intrin.h>
#else
# include <stdint.h>
# include <arm_acle.h>
#endif
# endif
#endif

#if CRYPTOPP_BOOL_SSE4_INTRINSICS_AVAILABLE
#if defined(_MSC_VER) 
#include <intrin.h>
#endif
#endif

#if defined(_MSC_VER) || defined(__BORLANDC__)
# define CRYPTOPP_MS_STYLE_INLINE_ASSEMBLY
#else
# define CRYPTOPP_GNU_STYLE_INLINE_ASSEMBLY
#endif
#if ((__ILP32__ >= 1) || (_ILP32 >= 1)) && defined(__x86_64__)
#define CRYPTOPP_BOOL_X32 1
#else
#define CRYPTOPP_BOOL_X32 0
#endif
// see http://predef.sourceforge.net/prearch.html
#if (defined(_M_IX86) || defined(__i386__) || defined(__i386) || defined(_X86_) || defined(__I86__) || defined(__INTEL__)) && !CRYPTOPP_BOOL_X32
#define CRYPTOPP_BOOL_X86 1
#else
#define CRYPTOPP_BOOL_X86 0
#endif

#if (defined(_M_X64) || defined(__x86_64__)) && !CRYPTOPP_BOOL_X32
#define CRYPTOPP_BOOL_X64 1
#else
#define CRYPTOPP_BOOL_X64 0
#endif

// Define this to ensure C/C++ standard compliance and respect for GCC aliasing rules and other alignment fodder. If you
// experience a break with GCC at -O3, you should try this first. Guard it in case its set on the command line (and it differs).
#ifndef CRYPTOPP_NO_UNALIGNED_DATA_ACCESS
# define CRYPTOPP_NO_UNALIGNED_DATA_ACCESS
#endif

#if !defined(CRYPTOPP_NO_UNALIGNED_DATA_ACCESS) && !defined(CRYPTOPP_ALLOW_UNALIGNED_DATA_ACCESS)
#if (CRYPTOPP_BOOL_X64 || CRYPTOPP_BOOL_X86 || CRYPTOPP_BOOL_X32 || defined(__powerpc__) || (__ARM_FEATURE_UNALIGNED >= 1))
#define CRYPTOPP_ALLOW_UNALIGNED_DATA_ACCESS
#endif
#endif
#ifdef _MSC_VER
#define CRYPTOPP_MSC_VERSION (_MSC_VER)
#endif
#ifdef __GNUC__
#define CRYPTOPP_GCC_VERSION (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)
#endif
#if (CRYPTOPP_MSC_VERSION >= 1900)
#  define CRYPTOPP_CXX11_ALIGNAS 1
#  define CRYPTOPP_CXX11_ALIGNOF 1
#elif (__INTEL_COMPILER >= 1500)
#  define CRYPTOPP_CXX11_ALIGNAS 1
#  define CRYPTOPP_CXX11_ALIGNOF 1
#elif defined(__clang__)
#  if __has_feature(cxx_alignas)
#  define CRYPTOPP_CXX11_ALIGNAS 1
#  endif
#  if __has_feature(cxx_alignof)
#  define CRYPTOPP_CXX11_ALIGNOF 1
#  endif
#elif (CRYPTOPP_GCC_VERSION >= 40800)
#  define CRYPTOPP_CXX11_ALIGNAS 1
#  define CRYPTOPP_CXX11_ALIGNOF 1
#elif (__SUNPRO_CC >= 0x5130)
#  define CRYPTOPP_CXX11_ALIGNAS 1
#  define CRYPTOPP_CXX11_ALIGNOF 1
#endif // alignof/alignas

// uintptr_t and ptrdiff_t
#if (__cplusplus < 201103L) && (!defined(_MSC_VER) || (_MSC_VER >= 1700))
# include <stdint.h>
#elif defined(_MSC_VER) && (_MSC_VER < 1700)
# include <stddef.h>
#endif

#if CRYPTOPP_BOOL_SSE4_INTRINSICS_AVAILABLE
#include <smmintrin.h>
#endif

#ifndef CRYPTOPP_MS_STYLE_INLINE_ASSEMBLY
extern "C" {
	typedef void(*SigHandler)(int);
};
#endif  // Not CRYPTOPP_MS_STYLE_INLINE_ASSEMBLY

namespace cryptopp_crc
{
	typedef unsigned int word32;

	/// \brief Tests whether a value is a power of 2
	/// \param value the value to test
	/// \returns true if value is a power of 2, false otherwise
	/// \details The function creates a mask of <tt>value - 1</tt> and returns the result
	///   of an AND operation compared to 0. If value is 0 or less than 0, then the function
	///   returns false.
	template <class T>
	inline bool IsPowerOf2(const T &value)
	{
		return value > 0 && (value & (value - 1)) == 0;
	}

	/// \brief Performs a saturating subtract clamped at 0
	/// \tparam T1 class or type
	/// \tparam T2 class or type
	/// \param a the minuend
	/// \param b the subtrahend
	/// \returns the difference produced by the saturating subtract
	/// \details Saturating arithmetic restricts results to a fixed range. Results that are
	///   less than 0 are clamped at 0.
	/// \details Use of saturating arithmetic in places can be advantageous because it can
	///   avoid a branch by using an instruction like a conditional move (<tt>CMOVE</tt>).
	template <class T1, class T2>
	inline T1 SaturatingSubtract(const T1 &a, const T2 &b)
	{
		// Generated ASM of a typical clamp, http://gcc.gnu.org/ml/gcc-help/2014-10/msg00112.html
		return T1((a > b) ? (a - b) : 0);
	}

	/// \brief Reduces a value to a power of 2
	/// \tparam T1 class or type
	/// \tparam T2 class or type
	/// \param a the first value
	/// \param b the second value
	/// \returns ModPowerOf2() returns <tt>a & (b-1)</tt>. <tt>b</tt> must be a power of 2.
	///   Use IsPowerOf2() to determine if <tt>b</tt> is a suitable candidate.
	/// \sa IsPowerOf2
	template <class T1, class T2>
	inline T2 ModPowerOf2(const T1 &a, const T2 &b)
	{
		// Coverity finding CID 170383 Overflowed return value (INTEGER_OVERFLOW)
		return T2(a) & SaturatingSubtract(b, 1U);
	}

	/// \brief Determines whether ptr is aligned to a minimum value
	/// \param ptr the pointer being checked for alignment
	/// \param alignment the alignment value to test the pointer against
	/// \returns true if <tt>ptr</tt> is aligned on at least <tt>alignment</tt>
	///  boundary, false otherwise
	/// \details Internally the function tests whether alignment is 1. If so,
	///  the function returns true. If not, then the function effectively
	///  performs a modular reduction and returns true if the residue is 0.
	inline bool IsAlignedOn(const void *ptr, unsigned int alignment)
	{
		const uintptr_t x = reinterpret_cast<uintptr_t>(ptr);
		return alignment == 1 || (IsPowerOf2(alignment) ? ModPowerOf2(x, alignment) == 0 : x % alignment == 0);
	}



	//! \brief Returns the minimum alignment requirements of a type
	//! \param dummy an unused Visual C++ 6.0 workaround
	//! \returns the minimum alignment requirements of a type, in bytes
	//! \details Internally the function calls C++11's <tt>alignof</tt> if available. If not available,
	//!   then the function uses compiler specific extensions such as <tt>__alignof</tt> and
	//!   <tt>_alignof_</tt>. If an extension is not available, then the function uses
	//!   <tt>__BIGGEST_ALIGNMENT__</tt> if <tt>__BIGGEST_ALIGNMENT__</tt> is smaller than <tt>sizeof(T)</tt>.
	//!   <tt>sizeof(T)</tt> is used if all others are not available.
	//!   In <em>all</em> cases, if <tt>CRYPTOPP_ALLOW_UNALIGNED_DATA_ACCESS</tt> is defined, then the
	//!   function returns 1.
	template <class T>
	inline unsigned int GetAlignmentOf(T *dummy = nullptr)	// VC60 workaround
	{
		// GCC 4.6 (circa 2008) and above aggressively uses vectorization.
#if defined(CRYPTOPP_ALLOW_UNALIGNED_DATA_ACCESS)
		if (sizeof(T) < 16)
			return 1;
#endif
#if defined(__GNUC__) && !defined(__clang__)
	return __alignof__(T);
#elif defined(CRYPTOPP_CXX11_ALIGNOF)
		return alignof(T);
#elif (_MSC_VER >= 1300)
		return __alignof(T);
#elif CRYPTOPP_BOOL_SLOW_WORD64
		return UnsignedMin(4U, sizeof(T));
#else
# if __BIGGEST_ALIGNMENT__
		if (__BIGGEST_ALIGNMENT__ < sizeof(T))
			return __BIGGEST_ALIGNMENT__;
		else
# endif
			return sizeof(T);
#endif
	}

	/// \brief Determines whether ptr is minimally aligned
	/// \tparam T class or type
	/// \param ptr the pointer to check for alignment
	/// \returns true if <tt>ptr</tt> is aligned to at least <tt>T</tt>
	///  boundary, false otherwise
	/// \details Internally the function calls IsAlignedOn with a second
	///  parameter of GetAlignmentOf<T>.
	template <class T>
	inline bool IsAligned(const void *ptr)
	{
		return IsAlignedOn(ptr, GetAlignmentOf<T>());
	}

#if CRYPTOPP_BOOL_SSE4_INTRINSICS_AVAILABLE
#if _MSC_VER >= 1400 && CRYPTOPP_BOOL_X64

	bool CpuId(word32 input, word32 output[4])
	{
		__cpuid((int *)output, input);
		return true;
	}

#else

#ifndef CRYPTOPP_MS_STYLE_INLINE_ASSEMBLY
	extern "C"
	{
		static jmp_buf s_jmpNoCPUID;
		static void SigIllHandlerCPUID(int)
		{
			longjmp(s_jmpNoCPUID, 1);
		}

		static jmp_buf s_jmpNoSSE2;
		static void SigIllHandlerSSE2(int)
		{
			longjmp(s_jmpNoSSE2, 1);
		}
	}
#endif

	bool CpuId(word32 input, word32 output[4])
	{
#if defined(CRYPTOPP_MS_STYLE_INLINE_ASSEMBLY)
		__try
		{
			__asm
			{
				mov eax, input
				mov ecx, 0
				cpuid
				mov edi, output
				mov[edi], eax
				mov[edi + 4], ebx
				mov[edi + 8], ecx
				mov[edi + 12], edx
			}
		}
		// GetExceptionCode() == EXCEPTION_ILLEGAL_INSTRUCTION
		__except (1)
		{
			return false;
		}

		// function 0 returns the highest basic function understood in EAX
		if (input == 0)
			return !!output[0];

		return true;
#else
		// longjmp and clobber warnings. Volatile is required.
		// http://github.com/weidai11/cryptopp/issues/24 and http://stackoverflow.com/q/7721854
		volatile bool result = true;

		volatile SigHandler oldHandler = signal(SIGILL, SigIllHandlerCPUID);
		if (oldHandler == SIG_ERR)
			return false;

# ifndef __MINGW32__
		volatile sigset_t oldMask;
		if (sigprocmask(0, NULL, (sigset_t*)&oldMask))
			return false;
# endif

		if (setjmp(s_jmpNoCPUID))
			result = false;
		else
		{
			asm volatile
				(
					// save ebx in case -fPIC is being used
					// TODO: this might need an early clobber on EDI.
# if CRYPTOPP_BOOL_X32 || CRYPTOPP_BOOL_X64
					"pushq %%rbx; cpuid; mov %%ebx, %%edi; popq %%rbx"
# else
					"push %%ebx; cpuid; mov %%ebx, %%edi; pop %%ebx"
# endif
					: "=a" (output[0]), "=D" (output[1]), "=c" (output[2]), "=d" (output[3])
					: "a" (input), "c" (0)
					);
		}

# ifndef __MINGW32__
		sigprocmask(SIG_SETMASK, (sigset_t*)&oldMask, NULL);
# endif

		signal(SIGILL, oldHandler);
		return result;
#endif
	}

#endif

	bool g_x86DetectionDone = false;
	bool g_hasSSE4 = false;

	void DetectX86Features()
	{
		unsigned int cpuid[4], cpuid1[4];
		if (!CpuId(0, cpuid))
			return;
		if (!CpuId(1, cpuid1))
			return;

		g_hasSSE4 = ((cpuid1[2] & (1 << 19)) && (cpuid1[2] & (1 << 20)));
		g_x86DetectionDone = true;
	}

	inline bool HasSSE4()
	{
		if (!g_x86DetectionDone)
			DetectX86Features();
		return g_hasSSE4;
	}
#elif (CRYPTOPP_BOOL_ARM_CRC32_INTRINSICS_AVAILABLE) //CRYPTOPP_BOOL_SSE4_INTRINSICS_AVAILABLE
	bool g_ArmDetectionDone = false;
	bool g_hasCRC32 = false;
	static jmp_buf s_jmpNoCRC32;
	static void SigIllHandlerCRC32(int)
	{
		longjmp(s_jmpNoCRC32, 1);
	}
	bool TryCRC32()
	{
# if defined(CRYPTOPP_MS_STYLE_INLINE_ASSEMBLY)
		volatile bool result = true;
		__try
		{
			unsigned int w = 0, x = 1; unsigned short y = 2; unsigned char z = 3;
			w = __crc32cw(w, x);
			w = __crc32ch(w, y);
			w = __crc32cb(w, z);

			result = !!w;
		}
		__except (1)
		{
			return false;
		}
		return result;
# else
		// longjmp and clobber warnings. Volatile is required.
		// http://github.com/weidai11/cryptopp/issues/24 and http://stackoverflow.com/q/7721854
		volatile bool result = true;

		volatile SigHandler oldHandler = signal(SIGILL, SigIllHandlerCRC32);
		if (oldHandler == SIG_ERR)
			return false;

		volatile sigset_t oldMask;
		if (sigprocmask(0, NULL, (sigset_t*)&oldMask))
			return false;

		if (setjmp(s_jmpNoCRC32))
			result = false;
		else
		{
			unsigned int w = 0, x = 1; unsigned short y = 2; unsigned char z = 3;
			w = __crc32cw(w, x);
			w = __crc32ch(w, y);
			w = __crc32cb(w, z);

			// Hack... GCC optimizes away the code and returns true
			result = !!w;
		}

		sigprocmask(SIG_SETMASK, (sigset_t*)&oldMask, NULL);
		signal(SIGILL, oldHandler);
		return result;
# endif
	}
	void DetectArmFeatures()
	{
		g_hasCRC32 = TryCRC32();
		*((volatile bool*)&g_ArmDetectionDone) = true;
	}
	inline bool HasCRC32()
	{
		if (!g_ArmDetectionDone)
			DetectArmFeatures();
		return g_hasCRC32;
	}
#endif //CRYPTOPP_BOOL_ARM_CRC32_INTRINSICS_AVAILABLE

	// Castagnoli CRC32C (iSCSI)

	const unsigned int CRC32C::m_tab[] = {
	#ifdef IS_LITTLE_ENDIAN
		0x00000000L, 0xf26b8303L, 0xe13b70f7L, 0x1350f3f4L, 0xc79a971fL,
		0x35f1141cL, 0x26a1e7e8L, 0xd4ca64ebL, 0x8ad958cfL, 0x78b2dbccL,
		0x6be22838L, 0x9989ab3bL, 0x4d43cfd0L, 0xbf284cd3L, 0xac78bf27L,
		0x5e133c24L, 0x105ec76fL, 0xe235446cL, 0xf165b798L, 0x030e349bL,
		0xd7c45070L, 0x25afd373L, 0x36ff2087L, 0xc494a384L, 0x9a879fa0L,
		0x68ec1ca3L, 0x7bbcef57L, 0x89d76c54L, 0x5d1d08bfL, 0xaf768bbcL,
		0xbc267848L, 0x4e4dfb4bL, 0x20bd8edeL, 0xd2d60dddL, 0xc186fe29L,
		0x33ed7d2aL, 0xe72719c1L, 0x154c9ac2L, 0x061c6936L, 0xf477ea35L,
		0xaa64d611L, 0x580f5512L, 0x4b5fa6e6L, 0xb93425e5L, 0x6dfe410eL,
		0x9f95c20dL, 0x8cc531f9L, 0x7eaeb2faL, 0x30e349b1L, 0xc288cab2L,
		0xd1d83946L, 0x23b3ba45L, 0xf779deaeL, 0x05125dadL, 0x1642ae59L,
		0xe4292d5aL, 0xba3a117eL, 0x4851927dL, 0x5b016189L, 0xa96ae28aL,
		0x7da08661L, 0x8fcb0562L, 0x9c9bf696L, 0x6ef07595L, 0x417b1dbcL,
		0xb3109ebfL, 0xa0406d4bL, 0x522bee48L, 0x86e18aa3L, 0x748a09a0L,
		0x67dafa54L, 0x95b17957L, 0xcba24573L, 0x39c9c670L, 0x2a993584L,
		0xd8f2b687L, 0x0c38d26cL, 0xfe53516fL, 0xed03a29bL, 0x1f682198L,
		0x5125dad3L, 0xa34e59d0L, 0xb01eaa24L, 0x42752927L, 0x96bf4dccL,
		0x64d4cecfL, 0x77843d3bL, 0x85efbe38L, 0xdbfc821cL, 0x2997011fL,
		0x3ac7f2ebL, 0xc8ac71e8L, 0x1c661503L, 0xee0d9600L, 0xfd5d65f4L,
		0x0f36e6f7L, 0x61c69362L, 0x93ad1061L, 0x80fde395L, 0x72966096L,
		0xa65c047dL, 0x5437877eL, 0x4767748aL, 0xb50cf789L, 0xeb1fcbadL,
		0x197448aeL, 0x0a24bb5aL, 0xf84f3859L, 0x2c855cb2L, 0xdeeedfb1L,
		0xcdbe2c45L, 0x3fd5af46L, 0x7198540dL, 0x83f3d70eL, 0x90a324faL,
		0x62c8a7f9L, 0xb602c312L, 0x44694011L, 0x5739b3e5L, 0xa55230e6L,
		0xfb410cc2L, 0x092a8fc1L, 0x1a7a7c35L, 0xe811ff36L, 0x3cdb9bddL,
		0xceb018deL, 0xdde0eb2aL, 0x2f8b6829L, 0x82f63b78L, 0x709db87bL,
		0x63cd4b8fL, 0x91a6c88cL, 0x456cac67L, 0xb7072f64L, 0xa457dc90L,
		0x563c5f93L, 0x082f63b7L, 0xfa44e0b4L, 0xe9141340L, 0x1b7f9043L,
		0xcfb5f4a8L, 0x3dde77abL, 0x2e8e845fL, 0xdce5075cL, 0x92a8fc17L,
		0x60c37f14L, 0x73938ce0L, 0x81f80fe3L, 0x55326b08L, 0xa759e80bL,
		0xb4091bffL, 0x466298fcL, 0x1871a4d8L, 0xea1a27dbL, 0xf94ad42fL,
		0x0b21572cL, 0xdfeb33c7L, 0x2d80b0c4L, 0x3ed04330L, 0xccbbc033L,
		0xa24bb5a6L, 0x502036a5L, 0x4370c551L, 0xb11b4652L, 0x65d122b9L,
		0x97baa1baL, 0x84ea524eL, 0x7681d14dL, 0x2892ed69L, 0xdaf96e6aL,
		0xc9a99d9eL, 0x3bc21e9dL, 0xef087a76L, 0x1d63f975L, 0x0e330a81L,
		0xfc588982L, 0xb21572c9L, 0x407ef1caL, 0x532e023eL, 0xa145813dL,
		0x758fe5d6L, 0x87e466d5L, 0x94b49521L, 0x66df1622L, 0x38cc2a06L,
		0xcaa7a905L, 0xd9f75af1L, 0x2b9cd9f2L, 0xff56bd19L, 0x0d3d3e1aL,
		0x1e6dcdeeL, 0xec064eedL, 0xc38d26c4L, 0x31e6a5c7L, 0x22b65633L,
		0xd0ddd530L, 0x0417b1dbL, 0xf67c32d8L, 0xe52cc12cL, 0x1747422fL,
		0x49547e0bL, 0xbb3ffd08L, 0xa86f0efcL, 0x5a048dffL, 0x8ecee914L,
		0x7ca56a17L, 0x6ff599e3L, 0x9d9e1ae0L, 0xd3d3e1abL, 0x21b862a8L,
		0x32e8915cL, 0xc083125fL, 0x144976b4L, 0xe622f5b7L, 0xf5720643L,
		0x07198540L, 0x590ab964L, 0xab613a67L, 0xb831c993L, 0x4a5a4a90L,
		0x9e902e7bL, 0x6cfbad78L, 0x7fab5e8cL, 0x8dc0dd8fL, 0xe330a81aL,
		0x115b2b19L, 0x020bd8edL, 0xf0605beeL, 0x24aa3f05L, 0xd6c1bc06L,
		0xc5914ff2L, 0x37faccf1L, 0x69e9f0d5L, 0x9b8273d6L, 0x88d28022L,
		0x7ab90321L, 0xae7367caL, 0x5c18e4c9L, 0x4f48173dL, 0xbd23943eL,
		0xf36e6f75L, 0x0105ec76L, 0x12551f82L, 0xe03e9c81L, 0x34f4f86aL,
		0xc69f7b69L, 0xd5cf889dL, 0x27a40b9eL, 0x79b737baL, 0x8bdcb4b9L,
		0x988c474dL, 0x6ae7c44eL, 0xbe2da0a5L, 0x4c4623a6L, 0x5f16d052L,
		0xad7d5351L
	#else
		0x00000000L, 0x03836bf2L, 0xf7703be1L, 0xf4f35013L, 0x1f979ac7L,
		0x1c14f135L, 0xe8e7a126L, 0xeb64cad4L, 0xcf58d98aL, 0xccdbb278L,
		0x3828e26bL, 0x3bab8999L, 0xd0cf434dL, 0xd34c28bfL, 0x27bf78acL,
		0x243c135eL, 0x6fc75e10L, 0x6c4435e2L, 0x98b765f1L, 0x9b340e03L,
		0x7050c4d7L, 0x73d3af25L, 0x8720ff36L, 0x84a394c4L, 0xa09f879aL,
		0xa31cec68L, 0x57efbc7bL, 0x546cd789L, 0xbf081d5dL, 0xbc8b76afL,
		0x487826bcL, 0x4bfb4d4eL, 0xde8ebd20L, 0xdd0dd6d2L, 0x29fe86c1L,
		0x2a7ded33L, 0xc11927e7L, 0xc29a4c15L, 0x36691c06L, 0x35ea77f4L,
		0x11d664aaL, 0x12550f58L, 0xe6a65f4bL, 0xe52534b9L, 0x0e41fe6dL,
		0x0dc2959fL, 0xf931c58cL, 0xfab2ae7eL, 0xb149e330L, 0xb2ca88c2L,
		0x4639d8d1L, 0x45bab323L, 0xaede79f7L, 0xad5d1205L, 0x59ae4216L,
		0x5a2d29e4L, 0x7e113abaL, 0x7d925148L, 0x8961015bL, 0x8ae26aa9L,
		0x6186a07dL, 0x6205cb8fL, 0x96f69b9cL, 0x9575f06eL, 0xbc1d7b41L,
		0xbf9e10b3L, 0x4b6d40a0L, 0x48ee2b52L, 0xa38ae186L, 0xa0098a74L,
		0x54fada67L, 0x5779b195L, 0x7345a2cbL, 0x70c6c939L, 0x8435992aL,
		0x87b6f2d8L, 0x6cd2380cL, 0x6f5153feL, 0x9ba203edL, 0x9821681fL,
		0xd3da2551L, 0xd0594ea3L, 0x24aa1eb0L, 0x27297542L, 0xcc4dbf96L,
		0xcfced464L, 0x3b3d8477L, 0x38beef85L, 0x1c82fcdbL, 0x1f019729L,
		0xebf2c73aL, 0xe871acc8L, 0x0315661cL, 0x00960deeL, 0xf4655dfdL,
		0xf7e6360fL, 0x6293c661L, 0x6110ad93L, 0x95e3fd80L, 0x96609672L,
		0x7d045ca6L, 0x7e873754L, 0x8a746747L, 0x89f70cb5L, 0xadcb1febL,
		0xae487419L, 0x5abb240aL, 0x59384ff8L, 0xb25c852cL, 0xb1dfeedeL,
		0x452cbecdL, 0x46afd53fL, 0x0d549871L, 0x0ed7f383L, 0xfa24a390L,
		0xf9a7c862L, 0x12c302b6L, 0x11406944L, 0xe5b33957L, 0xe63052a5L,
		0xc20c41fbL, 0xc18f2a09L, 0x357c7a1aL, 0x36ff11e8L, 0xdd9bdb3cL,
		0xde18b0ceL, 0x2aebe0ddL, 0x29688b2fL, 0x783bf682L, 0x7bb89d70L,
		0x8f4bcd63L, 0x8cc8a691L, 0x67ac6c45L, 0x642f07b7L, 0x90dc57a4L,
		0x935f3c56L, 0xb7632f08L, 0xb4e044faL, 0x401314e9L, 0x43907f1bL,
		0xa8f4b5cfL, 0xab77de3dL, 0x5f848e2eL, 0x5c07e5dcL, 0x17fca892L,
		0x147fc360L, 0xe08c9373L, 0xe30ff881L, 0x086b3255L, 0x0be859a7L,
		0xff1b09b4L, 0xfc986246L, 0xd8a47118L, 0xdb271aeaL, 0x2fd44af9L,
		0x2c57210bL, 0xc733ebdfL, 0xc4b0802dL, 0x3043d03eL, 0x33c0bbccL,
		0xa6b54ba2L, 0xa5362050L, 0x51c57043L, 0x52461bb1L, 0xb922d165L,
		0xbaa1ba97L, 0x4e52ea84L, 0x4dd18176L, 0x69ed9228L, 0x6a6ef9daL,
		0x9e9da9c9L, 0x9d1ec23bL, 0x767a08efL, 0x75f9631dL, 0x810a330eL,
		0x828958fcL, 0xc97215b2L, 0xcaf17e40L, 0x3e022e53L, 0x3d8145a1L,
		0xd6e58f75L, 0xd566e487L, 0x2195b494L, 0x2216df66L, 0x062acc38L,
		0x05a9a7caL, 0xf15af7d9L, 0xf2d99c2bL, 0x19bd56ffL, 0x1a3e3d0dL,
		0xeecd6d1eL, 0xed4e06ecL, 0xc4268dc3L, 0xc7a5e631L, 0x3356b622L,
		0x30d5ddd0L, 0xdbb11704L, 0xd8327cf6L, 0x2cc12ce5L, 0x2f424717L,
		0x0b7e5449L, 0x08fd3fbbL, 0xfc0e6fa8L, 0xff8d045aL, 0x14e9ce8eL,
		0x176aa57cL, 0xe399f56fL, 0xe01a9e9dL, 0xabe1d3d3L, 0xa862b821L,
		0x5c91e832L, 0x5f1283c0L, 0xb4764914L, 0xb7f522e6L, 0x430672f5L,
		0x40851907L, 0x64b90a59L, 0x673a61abL, 0x93c931b8L, 0x904a5a4aL,
		0x7b2e909eL, 0x78adfb6cL, 0x8c5eab7fL, 0x8fddc08dL, 0x1aa830e3L,
		0x192b5b11L, 0xedd80b02L, 0xee5b60f0L, 0x053faa24L, 0x06bcc1d6L,
		0xf24f91c5L, 0xf1ccfa37L, 0xd5f0e969L, 0xd673829bL, 0x2280d288L,
		0x2103b97aL, 0xca6773aeL, 0xc9e4185cL, 0x3d17484fL, 0x3e9423bdL,
		0x756f6ef3L, 0x76ec0501L, 0x821f5512L, 0x819c3ee0L, 0x6af8f434L,
		0x697b9fc6L, 0x9d88cfd5L, 0x9e0ba427L, 0xba37b779L, 0xb9b4dc8bL,
		0x4d478c98L, 0x4ec4e76aL, 0xa5a02dbeL, 0xa623464cL, 0x52d0165fL,
		0x51537dadL
	#endif
	};

	CRC32C::CRC32C()
	{
		Reset();
	}

	cryptopp_crc::CRC32C::CRC32C(unsigned int icrc)
		: m_crc(icrc ^ CRC32_NEGL)
	{
	}

	void CRC32C::Update(const char *s, size_t n)
	{
#if CRYPTOPP_BOOL_SSE4_INTRINSICS_AVAILABLE
		if (HasSSE4())
		{
			for (; !IsAligned<word32>(s) && n > 0; s++, n--)
				m_crc = _mm_crc32_u8(m_crc, *s);

			for (; n > 4; s += 4, n -= 4)
				m_crc = _mm_crc32_u32(m_crc, *(const word32 *)(void*)s);

			for (; n > 0; s++, n--)
				m_crc = _mm_crc32_u8(m_crc, *s);

			return;
		}
#elif (CRYPTOPP_BOOL_ARM_CRC32_INTRINSICS_AVAILABLE)
		if (HasCRC32())
		{
			for (; !IsAligned<word32>(s) && n > 0; s++, n--)
				m_crc = __crc32cb(m_crc, *s);

			for (; n > 4; s += 4, n -= 4)
				m_crc = __crc32cw(m_crc, *(const word32 *)(void*)s);

			for (; n > 0; s++, n--)
				m_crc = __crc32cb(m_crc, *s);

			return;
		}
#endif

		unsigned int crc = m_crc;

		for (; !IsAligned<unsigned int>(s) && n > 0; n--)
			crc = m_tab[CRC32_INDEX(crc) ^ *s++] ^ CRC32_SHIFTED(crc);

		while (n >= 4)
		{
			crc ^= *(const unsigned int *)(void*)s;
			crc = m_tab[CRC32_INDEX(crc)] ^ CRC32_SHIFTED(crc);
			crc = m_tab[CRC32_INDEX(crc)] ^ CRC32_SHIFTED(crc);
			crc = m_tab[CRC32_INDEX(crc)] ^ CRC32_SHIFTED(crc);
			crc = m_tab[CRC32_INDEX(crc)] ^ CRC32_SHIFTED(crc);
			n -= 4;
			s += 4;
		}

		while (n--)
			crc = m_tab[CRC32_INDEX(crc) ^ *s++] ^ CRC32_SHIFTED(crc);

		m_crc = crc;
	}

	void CRC32C::TruncatedFinal(char *hash, size_t size)
	{
		m_crc ^= CRC32_NEGL;
		for (size_t i = 0; i < size; i++)
			hash[i] = GetCrcByte(i);

		Reset();
	}

	unsigned int cryptopp_crc::CRC32C::Final()
	{
		m_crc ^= CRC32_NEGL;
		return m_crc;
	}

	crc32c_alg_type get_crc32c_alg_type()
	{
#if CRYPTOPP_BOOL_SSE4_INTRINSICS_AVAILABLE
		if (HasSSE4())
		{
			return crc32c_alg_type_sse4_crc;
		}
#elif (CRYPTOPP_BOOL_ARM_CRC32_INTRINSICS_AVAILABLE)
		if (HasCRC32())
		{
			return crc32c_alg_type_arm64_crc;
		}
#endif
		return crc32c_alg_type_software;
	}

	unsigned int crc32c_hw(
		unsigned int crc,
		const char *input,
		size_t length)
	{
		CRC32C icrc(crc);
		icrc.Update(input, length);
		return icrc.Final();
	}

} //namsepace cryptopp_crc
