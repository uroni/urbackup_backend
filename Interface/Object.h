#ifndef IOBJECT_H
#define IOBJECT_H

class IObject 
{
public:
	virtual ~IObject(void)
	{
	}

	virtual void Remove(void)
	{
		delete this;
	}
};

#endif //IOBJECT_H
