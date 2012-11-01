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

class ObjectScope
{
public:
	ObjectScope(IObject *obj)
		: obj(obj) {}
	~ObjectScope(void){
		if(obj!=0) obj->Remove();
	}
	void clear(){
		obj->Remove(); obj=0;
	}
private:
	IObject *obj;
};

#endif //IOBJECT_H
