#ifndef __BITMASK__
#define __BITMASK__

namespace std_
{
template<typename T> struct bitmask
{
	bitmask():m_flag(T(0)){};
	bitmask(const T& _ty):m_flag(_ty){};

	bitmask & operator<<(const T& flag){
		m_flag |= flag; 
		return *this;
	}
	bitmask & operator>>(const  T& flag){
		m_flag ^= flag;
		return *this;
	}
	
	unsigned int to_dword() const {
		return static_cast<unsigned int> ( m_flag );
	}
	
	bool contains(const  T& flag) const {
		return static_cast<bool>(flag & m_flag);
	}

private:
	T m_flag;
};

}

#endif