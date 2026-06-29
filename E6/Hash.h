#define HASHTHREADED  1
#define HASHTEST      1
//#define HASHDEBUG     1

// guards! guards!
#ifndef jwhash_h
#define jwhash_h

// needed for size_t
#include <stddef.h>
// #include <stdatomic.h>
#ifdef HASHDEBUG
# define HASH_DEBUG(fmt,args...) printf(fmt, ## args)
#else
# define HASH_DEBUG(fmt,args...) do {} while (0);
#endif

	
typedef struct content content;
struct content{
	void* address;	//内容起始地址
	size_t length;		//内容长度
	// atomic_int ref;
};

typedef struct hashpair hashpair;
struct hashpair
{
	char* key;
	content* cont;
	hashpair* next;
};

typedef struct hashtable hashtable;
struct hashtable
{
	hashpair **bucket;			// pointer to array of buckets
	size_t buckets;
	size_t bucketsinitial;			// if we resize, may need to hash multiple times

#ifdef HASHTHREADED
	volatile int *locks;			// array of locks
	//volatile int lock;				// lock for entire table
#endif
};

// Create/delete hash table
hashtable *create_hash( size_t buckets );
void delete_hash( hashtable *table );		// clean up all memory

// Add to table - keyed by string
int addItem( hashtable *table, char *key, content* cont );

// Delete by string
int delItem( hashtable *table, char *key );

// Get by string
content* getContentByKey( hashtable *table, char *key);

#endif



