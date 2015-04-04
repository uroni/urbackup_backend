#pragma once
#include <string>

class Bitmap
{
public:
	Bitmap(size_t n)
	{
		resize(n);
	}

	Bitmap()
	{
		bitmap_size = 0;
		bitmap=NULL;
	}

	~Bitmap()
	{
		delete[] bitmap;
	}

	void resize(size_t n)
	{
		if(bitmap!=NULL)
		{
			delete[] bitmap;
		}

		bitmap_size = n/8 + (n%8==0 ? 0 : 1);
		bitmap = new char[bitmap_size];
	}

	void set(size_t i, bool v)
	{
		size_t bitmap_byte=(size_t)(i/8);
		size_t bitmap_bit=i%8;

		unsigned char b=bitmap[bitmap_byte];

		if(v==true)
			b=b|(1<<(7-bitmap_bit));
		else
			b=b&(~(1<<(7-bitmap_bit)));

		bitmap[bitmap_byte]=b;
	}

	bool get(size_t i)
	{
		size_t bitmap_byte=(size_t)(i/8);
		size_t bitmap_bit=i%8;

		unsigned char b=bitmap[bitmap_byte];

		bool has_bit=((b & (1<<(7-bitmap_bit)))>0);

		return has_bit;
	}

	char* raw()
	{
		return bitmap;
	}

	size_t rawSize()
	{
		return bitmap_size;
	}

	void setRaw(char* raw)
	{
		memcpy(bitmap, raw, bitmap_size);
	}
private:
	char* bitmap;
	size_t bitmap_size;
};