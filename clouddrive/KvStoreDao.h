#pragma once
#include "../Interface/Database.h"



class KvStoreDao
{
public:
	KvStoreDao(IDatabase *db);
	~KvStoreDao();

	IDatabase* getDb();
	void createTables();

	//@-SQLGenFunctionsBegin
	struct CdDelObject
	{
		int64 trans_id;
		std::string tkey;
	};
	struct CdDelObjectMd5
	{
		int64 trans_id;
		std::string tkey;
		std::string md5sum;
	};
	struct CdIterObject
	{
		int64 trans_id;
		std::string tkey;
		std::string md5sum;
		int64 size;
		int64 last_modified;
	};
	struct CdIterObject2
	{
		int64 id;
		int64 trans_id;
		std::string tkey;
		std::string md5sum;
		int64 size;
	};
	struct CdObject
	{
		bool exists;
		int64 trans_id;
		int64 size;
		std::string md5sum;
	};
	struct CdSingleObject
	{
		bool exists;
		std::string tkey;
		int64 trans_id;
		int64 size;
		std::string md5sum;
	};
	struct CondInt64
	{
		bool exists;
		int64 value;
	};
	struct CondString
	{
		bool exists;
		std::string value;
	};
	struct SCdTrans
	{
		int64 id;
		int completed;
		int active;
	};
	struct SDelItemMd5
	{
		std::string tkey;
		std::string md5sum;
	};
	struct SSize
	{
		bool exists;
		int64 size;
		int64 count;
	};
	struct STransactionProperties
	{
		bool exists;
		int active;
		int completed;
		int64 cd_id;
	};
	struct SUnmirrored
	{
		bool exists;
		int64 tsize;
		int64 tcount;
	};
	struct Task
	{
		bool exists;
		int64 id;
		int task_id;
		int64 trans_id;
		int64 cd_id;
	};


	void createTransactionTable(void);
	void createTransactionTableCd(void);
	void createObjectTable(void);
	void createObjectTableCd(void);
	void createObjectTransIdIdx(void);
	void createObjectCdTransIdIdx(void);
	bool createObjectLastModifiedIdx(void);
	bool createObjectCdLastModifiedIdx(void);
	void dropObjectLastModifiedIdx(void);
	void dropObjectCdLastModifiedIdx(void);
	void createGenerationTable(void);
	void createGenerationTableCd(void);
	void createTaskTable(void);
	void createMiscTable(void);
	void setTaskActive(int64 id);
	Task getActiveTask(void);
	std::vector<Task> getTasks(int64 created_max, int task_id, int64 cd_id);
	Task getTask(int64 created_max);
	void removeTask(int64 id);
	void addTask(int task_id, int64 trans_id, int64 created, int64 cd_id);
	std::vector<SCdTrans> getTransactionIds(void);
	std::vector<SCdTrans> getTransactionIdsCd(int64 cd_id);
	SSize getSize(void);
	int64 getSizePartial(const std::string& tkey, int64 tans_id);
	int64 getSizePartialLMInit(int64 last_modified_start);
	int64 getSizePartialLM(int64 last_modified_start, int64 last_modified_stop);
	void setTransactionActive(int active, int64 id);
	void setTransactionActiveCd(int active, int64 cd_id, int64 id);
	CondInt64 getMaxCompleteTransaction(void);
	CondInt64 getMaxCompleteTransactionCd(int64 cd_id);
	std::vector<int64> getIncompleteTransactions(int64 max_active);
	std::vector<int64> getIncompleteTransactionsCd(int64 max_active, int64 cd_id);
	bool deleteTransaction(int64 id);
	bool deleteTransactionCd(int64 cd_id, int64 id);
	std::vector<SDelItemMd5> getTransactionObjectsMd5(int64 trans_id);
	std::vector<SDelItemMd5> getTransactionObjectsMd5Cd(int64 cd_id, int64 trans_id);
	std::vector<std::string> getTransactionObjects(int64 trans_id);
	std::vector<std::string> getTransactionObjectsCd(int64 cd_id, int64 trans_id);
	bool deleteTransactionObjects(int64 trans_id);
	bool deleteTransactionObjectsCd(int64 cd_id, int64 trans_id);
	void newTransaction(void);
	void newTransactionCd(int64 cd_id);
	void insertTransaction(int64 id);
	void insertTransactionCd(int64 id, int64 cd_id);
	void setTransactionComplete(int completed, int64 trans_id);
	void setTransactionCompleteCd(int completed, int64 cd_id, int64 trans_id);
	std::vector<int64> getDeletableTransactions(int64 curr_trans_id);
	std::vector<int64> getDeletableTransactionsCd(int64 cd_id, int64 curr_trans_id);
	std::vector<int64> getLastFinalizedTransactions(int64 last_trans_id, int64 curr_complete_trans_id);
	std::vector<int64> getLastFinalizedTransactionsCd(int64 cd_id, int64 last_trans_id, int64 curr_complete_trans_id);
	std::vector<CdDelObjectMd5> getDeletableObjectsMd5Ordered(int64 curr_trans_id);
	std::vector<CdDelObjectMd5> getDeletableObjectsMd5Cd(int64 cd_id, int64 curr_trans_id);
	std::vector<CdDelObjectMd5> getDeletableObjectsMd5(int64 curr_trans_id);
	std::vector<CdDelObject> getDeletableObjectsOrdered(int64 curr_trans_id);
	std::vector<CdDelObject> getDeletableObjects(int64 curr_trans_id);
	bool deleteDeletableObjects(int64 curr_trans_id);
	bool deleteDeletableObjectsCd(int64 cd_id, int64 curr_trans_id);
	void addDelMarkerObject(int64 trans_id, const std::string& tkey);
	void addDelMarkerObjectCd(int64 cd_id, int64 trans_id, const std::string& tkey);
	bool addObject(int64 trans_id, const std::string& tkey, const std::string& md5sum, int64 size);
	bool addObjectCd(int64 cd_id, int64 trans_id, const std::string& tkey, const std::string& md5sum, int64 size);
	bool addObject2(int64 trans_id, const std::string& tkey, const std::string& md5sum, int64 size, int64 last_modified);
	bool addObject2Cd(int64 cd_id, int64 trans_id, const std::string& tkey, const std::string& md5sum, int64 size, int64 last_modified);
	bool addPartialObject(int64 trans_id, const std::string& tkey);
	bool addPartialObjectCd(int64 cd_id, int64 trans_id, const std::string& tkey);
	bool updateObjectSearch(const std::string& md5sum, int64 size, const std::string& tkey, int64 trans_id);
	bool updateObject(const std::string& md5sum, int64 size, int64 rowid);
	bool updateObjectCd(const std::string& md5sum, int64 size, int64 rowid);
	bool updateObject2Cd(const std::string& md5sum, int64 size, int64 last_modified, int64 rowid);
	bool updateObject2(const std::string& md5sum, int64 size, int64 last_modified, int64 rowid);
	void deletePartialObject(int64 rowid);
	void deletePartialObjectCd(int64 rowid);
	void updateGeneration(int64 generation);
	CondInt64 getGeneration(void);
	CondInt64 getGenerationCd(int64 cd_id);
	void insertGeneration(int64 generation);
	CdObject getObjectInTransid(int64 trans_id, const std::string& tkey);
	CdObject getObjectInTransidCd(int64 cd_id, int64 trans_id, const std::string& tkey);
	CdSingleObject getSingleObject(void);
	CdObject getObject(int64 curr_trans_id, const std::string& tkey);
	CdObject getObjectCd(int64 cd_id, int64 curr_trans_id, const std::string& tkey);
	CondInt64 isTransactionActive(int64 trans_id);
	CondInt64 isTransactionActiveCd(int64 cd_id, int64 trans_id);
	bool deleteObject(int64 trans_id, const std::string& tkey);
	CondString getMiscValue(const std::string& key);
	void setMiscValue(const std::string& key, const std::string& value);
	STransactionProperties getTransactionProperties(int64 id);
	STransactionProperties getTransactionPropertiesCd(int64 cd_id, int64 id);
	std::vector<CdIterObject> getInitialObjectsLM(void);
	std::vector<CdIterObject> getInitialObjects(void);
	std::vector<CdIterObject> getIterObjectsLMInit(int64 last_modified_start);
	std::vector<CdIterObject> getIterObjectsLM(int64 last_modified_start, int64 last_modified_stop);
	std::vector<CdIterObject> getIterObjects(const std::string& tkey, int64 tans_id);
	void updateObjectMd5sum(const std::string& md5sum, int64 transid, const std::string& tkey);
	void updateObjectMd5sumCd(const std::string& md5sum, int64 cd_id, int64 transid, const std::string& tkey);
	void insertAllDeletionTasks(void);
	std::vector<CdIterObject2> getUnmirroredObjects(void);
	SUnmirrored getUnmirroredObjectsSize(void);
	void setObjectMirrored(int64 rowid);
	std::vector<SCdTrans> getUnmirroredTransactions(void);
	void setTransactionMirrored(int64 id);
	bool updateGenerationCd(int64 cd_id, int64 generation);
	CondInt64 getLowerTransidObject(const std::string& tkey, int64 transid);
	CondInt64 getLowerTransidObjectCd(int64 cd_id, const std::string& tkey, int64 transid);
	//@-SQLGenFunctionsEnd

	IQuery* getUpdateGenerationQuery();
	void setUpdateGenerationQuery(IQuery* q);

private:
	//@-SQLGenVariablesBegin
	IQuery* q_createTransactionTable;
	IQuery* q_createTransactionTableCd;
	IQuery* q_createObjectTable;
	IQuery* q_createObjectTableCd;
	IQuery* q_createObjectTransIdIdx;
	IQuery* q_createObjectCdTransIdIdx;
	IQuery* q_createObjectLastModifiedIdx;
	IQuery* q_createObjectCdLastModifiedIdx;
	IQuery* q_dropObjectLastModifiedIdx;
	IQuery* q_dropObjectCdLastModifiedIdx;
	IQuery* q_createGenerationTable;
	IQuery* q_createGenerationTableCd;
	IQuery* q_createTaskTable;
	IQuery* q_createMiscTable;
	IQuery* q_setTaskActive;
	IQuery* q_getActiveTask;
	IQuery* q_getTasks;
	IQuery* q_getTask;
	IQuery* q_removeTask;
	IQuery* q_addTask;
	IQuery* q_getTransactionIds;
	IQuery* q_getTransactionIdsCd;
	IQuery* q_getSize;
	IQuery* q_getSizePartial;
	IQuery* q_getSizePartialLMInit;
	IQuery* q_getSizePartialLM;
	IQuery* q_setTransactionActive;
	IQuery* q_setTransactionActiveCd;
	IQuery* q_getMaxCompleteTransaction;
	IQuery* q_getMaxCompleteTransactionCd;
	IQuery* q_getIncompleteTransactions;
	IQuery* q_getIncompleteTransactionsCd;
	IQuery* q_deleteTransaction;
	IQuery* q_deleteTransactionCd;
	IQuery* q_getTransactionObjectsMd5;
	IQuery* q_getTransactionObjectsMd5Cd;
	IQuery* q_getTransactionObjects;
	IQuery* q_getTransactionObjectsCd;
	IQuery* q_deleteTransactionObjects;
	IQuery* q_deleteTransactionObjectsCd;
	IQuery* q_newTransaction;
	IQuery* q_newTransactionCd;
	IQuery* q_insertTransaction;
	IQuery* q_insertTransactionCd;
	IQuery* q_setTransactionComplete;
	IQuery* q_setTransactionCompleteCd;
	IQuery* q_getDeletableTransactions;
	IQuery* q_getDeletableTransactionsCd;
	IQuery* q_getLastFinalizedTransactions;
	IQuery* q_getLastFinalizedTransactionsCd;
	IQuery* q_getDeletableObjectsMd5Ordered;
	IQuery* q_getDeletableObjectsMd5Cd;
	IQuery* q_getDeletableObjectsMd5;
	IQuery* q_getDeletableObjectsOrdered;
	IQuery* q_getDeletableObjects;
	IQuery* q_deleteDeletableObjects;
	IQuery* q_deleteDeletableObjectsCd;
	IQuery* q_addDelMarkerObject;
	IQuery* q_addDelMarkerObjectCd;
	IQuery* q_addObject;
	IQuery* q_addObjectCd;
	IQuery* q_addObject2;
	IQuery* q_addObject2Cd;
	IQuery* q_addPartialObject;
	IQuery* q_addPartialObjectCd;
	IQuery* q_updateObjectSearch;
	IQuery* q_updateObject;
	IQuery* q_updateObjectCd;
	IQuery* q_updateObject2Cd;
	IQuery* q_updateObject2;
	IQuery* q_deletePartialObject;
	IQuery* q_deletePartialObjectCd;
	IQuery* q_updateGeneration;
	IQuery* q_getGeneration;
	IQuery* q_getGenerationCd;
	IQuery* q_insertGeneration;
	IQuery* q_getObjectInTransid;
	IQuery* q_getObjectInTransidCd;
	IQuery* q_getSingleObject;
	IQuery* q_getObject;
	IQuery* q_getObjectCd;
	IQuery* q_isTransactionActive;
	IQuery* q_isTransactionActiveCd;
	IQuery* q_deleteObject;
	IQuery* q_getMiscValue;
	IQuery* q_setMiscValue;
	IQuery* q_getTransactionProperties;
	IQuery* q_getTransactionPropertiesCd;
	IQuery* q_getInitialObjectsLM;
	IQuery* q_getInitialObjects;
	IQuery* q_getIterObjectsLMInit;
	IQuery* q_getIterObjectsLM;
	IQuery* q_getIterObjects;
	IQuery* q_updateObjectMd5sum;
	IQuery* q_updateObjectMd5sumCd;
	IQuery* q_insertAllDeletionTasks;
	IQuery* q_getUnmirroredObjects;
	IQuery* q_getUnmirroredObjectsSize;
	IQuery* q_setObjectMirrored;
	IQuery* q_getUnmirroredTransactions;
	IQuery* q_setTransactionMirrored;
	IQuery* q_updateGenerationCd;
	IQuery* q_getLowerTransidObject;
	IQuery* q_getLowerTransidObjectCd;
	//@-SQLGenVariablesEnd

	void prepareQueries();

	IDatabase* db;
};