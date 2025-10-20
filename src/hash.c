/*****
 *
 * Description: Hash Functions
 *
 * Copyright (c) 2008-2023, Ron Dilley
 * All rights reserved.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 ****/

/****
 *
 * defines
 *
 ****/

/****
 *
 * includes
 *
 ****/

#include "hash.h"
#include "mem.h"

/****
 *
 * local variables
 *
 ****/

/* force selection of good primes */
size_t hashPrimes[] = {
    53, 97, 193, 389, 769, 1543, 3079,
    6151, 12289, 24593, 49157, 98317, 196613, 393241,
    786433, 1572869, 3145739, 6291469, 12582917, 25165843, 50331653,
    100663319, 201326611, 402653189, 805306457, 1610612741, 0};

/****
 *
 * external global variables
 *
 ****/

extern Config_t *config;

/****
 *
 * functions
 *
 ****/

/****
 *
 * Calculate hash value for key string using ELF hash algorithm
 *
 * DESCRIPTION:
 *   Computes hash value for a key string using the ELF hash algorithm,
 *   which provides good distribution properties for string keys.
 *   The result is modulo the hash table size for array indexing.
 *
 * PARAMETERS:
 *   hashSize - Size of hash table (for modulo operation)
 *   keyString - String to hash
 *
 * RETURNS:
 *   Hash value in range [0, hashSize-1]
 *
 * SIDE EFFECTS:
 *   None (pure function)
 *
 * ALGORITHM:
 *   ELF hash with shifting and XOR operations for good distribution
 *
 * PERFORMANCE:
 *   O(n) where n is string length
 *   Critical path for hash table operations
 *
 ****/

uint32_t calcHash(uint32_t hashSize, const char *keyString)
{
  uint32_t val = 0;
  uint32_t tmp;
  int i, keyLen = (int)(strlen(keyString) + 1);

#ifdef DEBUG
  if (config->debug >= 3)
    printf("DEBUG - Calculating hash\n");
#endif

  /* generate the lookup hash */
  for (i = 0; i < keyLen; i++)
  {
    val = (val << 4) + (keyString[i] & 0xff);
    if ((tmp = (val & 0xf0000000)))
    {
      val = val ^ (tmp >> 24);
      val = val ^ tmp;
    }
  }

#ifdef DEBUG
  if (config->debug >= 4)
    printf("DEBUG - hash: %u\n", (uint32_t)val % hashSize);
#endif

  return (uint32_t)val % hashSize;
}

/****
 *
 * Initialize hash table with optimal prime size
 *
 * DESCRIPTION:
 *   Creates and initializes a new hash table with size selected from
 *   a predefined list of prime numbers. Uses the smallest prime that
 *   is greater than or equal to the requested size for optimal
 *   distribution properties.
 *
 * PARAMETERS:
 *   hashSize - Desired minimum hash table size
 *
 * RETURNS:
 *   Pointer to initialized hash structure
 *   NULL if memory allocation fails
 *
 * SIDE EFFECTS:
 *   - Allocates memory for hash structure
 *   - Allocates array of hash record list pointers
 *   - Initializes all list pointers to NULL
 *   - Sets initial statistics (size, count, etc.)
 *
 * SIZE SELECTION:
 *   Chooses optimal prime size from predefined list for
 *   best hash distribution characteristics
 *
 * PERFORMANCE:
 *   O(1) initialization time
 *   Memory usage: O(prime_size) where prime_size >= hashSize
 *
 ****/

struct hash_s *initHash(uint32_t hashSize)
{
  struct hash_s *tmpHash;
  int i;

  if ((tmpHash = (struct hash_s *)XMALLOC((int)sizeof(struct hash_s))) EQ NULL)
  {
    fprintf(stderr, "ERR - Unable to allocate hash\n");
    return NULL;
  }
  XMEMSET(tmpHash, 0, (int)sizeof(struct hash_s));

  /* pick a good hash size */
  for (i = 0; ((hashSize > (uint32_t)hashPrimes[i]) && (hashPrimes[i] > 0)); i++)
    ;

  if (hashPrimes[i] EQ 0)
  {
    /* size too large */
    fprintf(stderr, "ERR - Hash size too large\n");
    XFREE(tmpHash);
    return NULL;
  }

  tmpHash->primeOff = (uint8_t)i;
  tmpHash->size = (uint32_t)hashPrimes[i];

  if ((tmpHash->lists = (struct hashRecList_s **)XMALLOC(
           (int)(sizeof(struct hashRecList_s *) * tmpHash->size))) EQ NULL)
  {
    fprintf(stderr, "ERR - Unable to allocate hash record list\n");
    XFREE(tmpHash);
    return NULL;
  }
  XMEMSET(tmpHash->lists, 0, (int)(sizeof(struct hashRecList_s *) * tmpHash->size));

#ifdef DEBUG
  if (config->debug >= 4)
    printf("DEBUG - Hash initialized [%u]\n", tmpHash->size);
#endif

  return tmpHash;
}

/****
 *
 * Free all hash table memory
 *
 * DESCRIPTION:
 *   Recursively frees all hash records, lists, and hash structure.
 *
 * PARAMETERS:
 *   hash - Hash table to destroy
 *
 * RETURNS:
 *   void
 *
 * SIDE EFFECTS:
 *   Frees all allocated memory for hash and contents
 *
 ****/

void freeHash(struct hash_s *hash)
{
  size_t i, key;

  if (hash != NULL)
  {
    for (key = 0; key < hash->size; key++)
    {
      if (hash->lists[key] != NULL)
      {
        for (i = 0; i < (int)hash->lists[key]->count; i++)
        {
          if (hash->lists[key]->records[i] != NULL)
          {
            XFREE(hash->lists[key]->records[i]->keyString);
            hash->lists[key]->records[i]->keyString = NULL;
            XFREE(hash->lists[key]->records[i]);
            hash->lists[key]->records[i] = NULL;
          }
        }
        if (hash->lists[key]->records != NULL)
        {
          XFREE(hash->lists[key]->records);
          hash->lists[key]->records = NULL;
        }
        XFREE(hash->lists[key]);
        hash->lists[key] = NULL;
      }
    }
    if (hash->lists != NULL)
    {
      XFREE(hash->lists);
      hash->lists = NULL;
    }
    XFREE(hash);
    hash = NULL;
  }
}

/****
 *
 * Apply function to all hash records
 *
 * DESCRIPTION:
 *   Iterates through all hash records and calls provided function for each.
 *
 * PARAMETERS:
 *   hash - Hash table to traverse
 *   fn - Function to call for each record
 *
 * RETURNS:
 *   TRUE on success, FAILED if any function call returns non-zero
 *
 ****/

int traverseHash(const struct hash_s *hash, int (*fn)(const struct hashRec_s *hashRec))
{
  size_t i, key;

#ifdef DEBUG
  if (config->debug >= 3)
    printf("DEBUG - Traversing hash\n");
#endif

  for (key = 0; key < hash->size; key++)
  {
    if (hash->lists[key] != NULL)
    {
      for (i = 0; i < (int)hash->lists[key]->count; i++)
      {
        if (hash->lists[key]->records[i] != NULL)
        {
          if (fn(hash->lists[key]->records[i]))
            return (FAILED);
        }
      }
    }
  }

  return (TRUE);
}

/****
 *
 * Create and add new hash record
 *
 * DESCRIPTION:
 *   Adds new record to hash using binary search insertion to maintain sorted order
 *   within collision lists. Returns NULL if key already exists.
 *
 * PARAMETERS:
 *   hash - Hash table
 *   keyString - Key (may be binary data)
 *   keyLen - Length of key (0 for null-terminated string)
 *   data - User data pointer
 *
 * RETURNS:
 *   Pointer to new record, or NULL if duplicate or allocation fails
 *
 * SIDE EFFECTS:
 *   Allocates memory for record and key copy
 *
 ****/

struct hashRec_s *addUniqueHashRec(struct hash_s *hash, const char *keyString, int keyLen, void *data)
{
  uint32_t key;
  uint32_t val = 0;
  uint32_t tmp;
#ifdef DEBUG
  char nBuf[4096];
#endif
  int i, ret, low, high;
  register int mid;
  struct hashRec_s **tmpHashArrayPtr;
  struct hashRec_s *tmpHashRecPtr = NULL;

  if (keyLen EQ 0)
    keyLen = (int)(strlen(keyString) + 1);

  /* generate the lookup hash */
  for (i = 0; i < keyLen; i++)
  {
    val = (val << 4) + (keyString[i] & 0xff);
    if ((tmp = (val & 0xf0000000)))
    {
      val = val ^ (tmp >> 24);
      val = val ^ tmp;
    }
  }
  key = (uint32_t)val % hash->size;

  if (key > hash->size)
  {
    fprintf(stderr, "ERR - Key outside of valid record range [%u]\n", key);
  }

#ifdef DEBUG
  if (config->debug >= 3)
    printf("DEBUG - Adding hash [%u] (%s)\n", key,
           hexConvert(keyString, keyLen, nBuf, (int)sizeof(nBuf)));
#endif

  if (hash->lists[key] EQ NULL)
  {
    /* add new list entry to hash pointer buffer */
    if ((hash->lists[key] = (struct hashRecList_s *)XMALLOC((int)sizeof(struct hashRecList_s))) EQ NULL)
    {
      fprintf(stderr, "ERR - Unable to allocate space for hash list record\n");
      return NULL;
    }
    /* add pointer buffer */
    hash->lists[key]->count = 1;
    if ((hash->lists[key]->records = (struct hashRec_s **)XMALLOC((int)(sizeof(struct hashRec_s *) * hash->lists[key]->count))) EQ NULL)
    {
      fprintf(stderr, "ERR - Unable to allocate space for hash record list\n");
      hash->lists[key]->count--;
      XFREE(hash->lists[key]);
      hash->lists[key] = NULL;
      return NULL;
    }
    /* add record to pointer buffer */
    if ((hash->lists[key]->records[0] = (struct hashRec_s *)XMALLOC((int)sizeof(struct hashRec_s))) EQ NULL)
    {
      fprintf(stderr, "ERR - Unable to allocate space for hash record\n");
      XFREE(hash->lists[key]->records);
      XFREE(hash->lists[key]);
      hash->lists[key] = NULL;
      return NULL;
    }
    XMEMSET((struct hashRec_s *)hash->lists[key]->records[0], 0,
            (int)sizeof(struct hashRec_s));
    if ((hash->lists[key]->records[0]->keyString = (char *)XMALLOC(keyLen)) EQ NULL)
    {
      fprintf(stderr, "ERR - Unable to allocate space for hash label\n");
      XFREE(hash->lists[key]);
      hash->lists[key] = NULL;
      return NULL;
    }
    XMEMCPY(hash->lists[key]->records[0]->keyString, keyString, keyLen);
    hash->lists[key]->records[0]->keyLen = keyLen;
    if (data != NULL)
      hash->lists[key]->records[0]->data = data;
    hash->lists[key]->records[0]->lastSeen = hash->lists[key]->records[0]->createTime = config->current_time;
  }
  else
  {
    /* search for keyString and insert in sorted hash list if not found */
    low = 0;
    high = (int)hash->lists[key]->count;
    mid = high / 2;

    do
    {
#ifdef DEBUG
      if (config->debug >= 1)
        printf("DEBUG - snoop hashrec Count: %lu L: %d M: %d H: %d\n", hash->lists[key]->count, low, mid, high);
#endif
      if ((ret = strcmp(keyString, hash->lists[key]->records[mid]->keyString)) > 0)
        low = mid + 1;
      else if (ret < 0)
        high = mid;
      else
      {
        /* existing record found */
#ifdef DEBUG
        if (config->debug >= 4)
        {
          if (keyString[keyLen - 1] EQ 0) // it is a null terminated key string
            printf("DEBUG - Found (%s) in hash table at [%u] at depth [%d]\n", (char *)keyString, key, mid);
          else
            printf("DEBUG - Found (%s) in hash table at [%u] at depth [%d]\n", hexConvert(keyString, keyLen, nBuf, (int)sizeof(nBuf)), key, mid);
        }
#endif
        return NULL;
      }
      mid = low + ((high - low) / 2);
    } while (low < high);

    /* grow the hash list buffer */
    if ((tmpHashArrayPtr = (struct hashRec_s **)XREALLOC(hash->lists[key]->records, (int)(sizeof(struct hashRec_s *) * (hash->lists[key]->count + 1)))) EQ NULL)
    {
      /* return without adding record, keep existing list */
      fprintf(stderr, "ERR - Unable to allocate space for hash record list\n");
      return NULL;
    }
    hash->lists[key]->records = tmpHashArrayPtr;

    /* create hash record */
    if ((tmpHashRecPtr = (struct hashRec_s *)XMALLOC((int)sizeof(struct hashRec_s))) EQ NULL)
    {
      /* XXX need better cleanup than this */
      fprintf(stderr, "ERR - Unable to allocate space for hash record\n");
      return NULL;
    }
    XMEMSET((struct hashRec_s *)tmpHashRecPtr, 0, (int)sizeof(struct hashRec_s));
    if ((tmpHashRecPtr->keyString = (char *)XMALLOC(keyLen)) EQ NULL)
    {
      /* XXX need better cleanup than this */
      /* remove the partial record from the list */
      fprintf(stderr, "ERR - Unable to allocate space for hash label\n");
      return NULL;
    }
    XMEMCPY(tmpHashRecPtr->keyString, keyString, keyLen);
    tmpHashRecPtr->keyLen = keyLen;
    tmpHashRecPtr->data = data;
    tmpHashRecPtr->lastSeen = tmpHashRecPtr->createTime = config->current_time;

    if (mid < (int)hash->lists[key]->count)
    {
      hash->lists[key]->records[hash->lists[key]->count] = NULL;
      memmove(&hash->lists[key]->records[mid + 1], &hash->lists[key]->records[mid], sizeof(struct hashRec_s *) * (hash->lists[key]->count - (size_t)mid));
      hash->lists[key]->records[mid] = tmpHashRecPtr;
      hash->lists[key]->count++;
    }
    else
      hash->lists[key]->records[hash->lists[key]->count++] = tmpHashRecPtr;
  }

#ifdef DEBUG
  if (config->debug >= 4)
  {
    if (keyString[keyLen - 1] EQ 0) // it is a null terminated key string
      printf("DEBUG - Added hash [%u] (%s) in record list [%lu]\n", key, keyString, hash->lists[key]->count);
    else
      printf("DEBUG - Added hash [%u] (%s) in record list [%lu]\n", key, hexConvert(keyString, keyLen, nBuf, (int)sizeof(nBuf)), hash->lists[key]->count);
  }
#endif

  hash->totalRecords++;

#ifdef DEBUG
  if (config->debug >= 3)
    printf("DEBUG - Record Count: %u\n", hash->totalRecords);
#endif

  return tmpHashRecPtr;
}

/****
 *
 * Insert pre-allocated record into hash
 *
 * DESCRIPTION:
 *   Inserts existing hash record into table (used during resize operations).
 *   Maintains sorted order within collision lists.
 *
 * PARAMETERS:
 *   hash - Hash table
 *   hashRec - Pre-allocated record to insert
 *
 * RETURNS:
 *   TRUE on success, FAILED/FALSE on duplicate or allocation error
 *
 ****/

int insertUniqueHashRec(struct hash_s *hash, struct hashRec_s *hashRec)
{
  uint32_t key;
  uint32_t val = 0;
  uint32_t tmp;
#ifdef DEBUG
  char nBuf[4096];
#endif
  int i, ret, low, high;
  register int mid;
  struct hashRec_s **tmpHashArrayPtr;

  /* generate the lookup hash */
  for (i = 0; i < hashRec->keyLen; i++)
  {
    val = (val << 4) + (hashRec->keyString[i] & 0xff);
    if ((tmp = (val & 0xf0000000)))
    {
      val = val ^ (tmp >> 24);
      val = val ^ tmp;
    }
  }
  key = (uint32_t)val % hash->size;

  if (key > hash->size)
  {
    fprintf(stderr, "ERR - Key outside of valid record range [%u]\n", key);
  }

#ifdef DEBUG
  if (config->debug >= 3)
  {
    if (hashRec->keyString[hashRec->keyLen - 1] EQ 0) // it is a null terminated key string
      printf("DEBUG - Inserting hash [%u] (%s)\n", key, hashRec->keyString);
    else
      printf("DEBUG - Inserting hash [%u] (%s)\n", key, hexConvert(hashRec->keyString, hashRec->keyLen, nBuf, (int)sizeof(nBuf)));
  }
#endif

  if (hash->lists[key] EQ NULL)
  {
    /* add new list entry to hash pointer buffer */
    if ((hash->lists[key] = (struct hashRecList_s *)XMALLOC((int)sizeof(struct hashRecList_s))) EQ NULL)
    {
      fprintf(stderr, "ERR - Unable to allocate space for hash list record\n");
      return FAILED;
    }
    /* add pointer buffer */
    hash->lists[key]->count = 1;
    if ((hash->lists[key]->records = (struct hashRec_s **)XMALLOC((int)(sizeof(struct hashRec_s *) * hash->lists[key]->count))) EQ NULL)
    {
      fprintf(stderr, "ERR - Unable to allocate space for hash record list\n");
      hash->lists[key]->count--;
      XFREE(hash->lists[key]);
      hash->lists[key] = NULL;
      return FAILED;
    }
    hash->lists[key]->records[0] = hashRec;
  }
  else
  {
    /* search for keyString and insert in sorted hash list if not found */
    low = 0;
    high = (int)hash->lists[key]->count;
    mid = high / 2;

    do
    {
#ifdef DEBUG
      if (config->debug >= 1)
        printf("DEBUG - snoop hashrec Count: %lu L: %d M: %d H: %d\n", hash->lists[key]->count, low, mid, high);
#endif

      if ((ret = strcmp(hashRec->keyString, hash->lists[key]->records[mid]->keyString)) > 0)
        low = mid + 1;
      else if (ret < 0)
        high = mid;
      else
      {
        /* strings match */
        if (hashRec->keyString[hashRec->keyLen - 1] EQ 0) // it is a null terminated key string
          fprintf(stderr, "ERR - Found duplicate hash record [%s][%s] at [%u] in record list [%d]\n", hashRec->keyString, hash->lists[key]->records[mid]->keyString, key, mid);
        else
          fprintf(stderr, "ERR - Found duplicate hash record at [%u] in record list [%d]\n", key, mid);

        return FALSE;
      }
      mid = low + ((high - low) / 2);
    } while (low < high);

    /* grow the hash list buffer */
    if ((tmpHashArrayPtr = (struct hashRec_s **)XREALLOC(hash->lists[key]->records, (int)(sizeof(struct hashRec_s *) * (hash->lists[key]->count + 1)))) EQ NULL)
    {
      /* return without adding record, keep existing list */
      fprintf(stderr, "ERR - Unable to allocate space for hash record list\n");
      return FAILED;
    }
    hash->lists[key]->records = tmpHashArrayPtr;

    if (mid < (int)hash->lists[key]->count)
    {
      hash->lists[key]->records[hash->lists[key]->count] = NULL;
      memmove(&hash->lists[key]->records[mid + 1], &hash->lists[key]->records[mid], sizeof(struct hashRec_s *) * (hash->lists[key]->count - (size_t)mid));
      hash->lists[key]->records[mid] = hashRec;
      hash->lists[key]->count++;
    }
    else
      hash->lists[key]->records[hash->lists[key]->count++] = hashRec;
  }

#ifdef DEBUG
  if (config->debug >= 4)
  {
    if (hashRec->keyString[hashRec->keyLen - 1] EQ 0) // it is a null terminated key string
      printf("DEBUG - Added hash [%u] (%s) in record list [%lu]\n", key, hashRec->keyString, hash->lists[key]->count);
    else
      printf("DEBUG - Added hash [%u] (%s) in record list [%lu]\n", key, hexConvert(hashRec->keyString, hashRec->keyLen, nBuf, (int)sizeof(nBuf)), hash->lists[key]->count);
  }
#endif

  hash->totalRecords++;

#ifdef DEBUG
  if (config->debug >= 3)
    printf("DEBUG - Record Count: %u\n", hash->totalRecords);
#endif

  return TRUE;
}

/****
 *
 * Retrieve hash record by key
 *
 * DESCRIPTION:
 *   Searches for record with exact key match using binary search. Updates lastSeen
 *   timestamp on successful lookup.
 *
 * PARAMETERS:
 *   hash - Hash table
 *   keyString - Key to find
 *   keyLen - Key length (0 for null-terminated)
 *
 * RETURNS:
 *   Pointer to record, or NULL if not found
 *
 * SIDE EFFECTS:
 *   Updates lastSeen field on match
 *
 ****/

struct hashRec_s *getHashRecord(struct hash_s *hash, const char *keyString, int keyLen)
{
  uint32_t key;
  uint32_t val = 0;
  uint32_t tmp;
  int i, ret, low, high;
  register int mid;

  if (keyLen EQ 0)
    keyLen = (int)(strlen(keyString) + 1);

  /* generate the lookup hash */
  for (i = 0; i < keyLen; i++)
  {
    val = (val << 4) + (keyString[i] & 0xff);
    if ((tmp = (val & 0xf0000000)))
    {
      val = val ^ (tmp >> 24);
      val = val ^ tmp;
    }
  }
  key = (uint32_t)val % hash->size;

#ifdef DEBUG
  if (config->debug >= 3)
    printf("DEBUG - Getting record from hash table\n");
#endif

  if (hash->lists[key] != NULL)
  {
    /* search for keyString and hash record if found */
    low = 0;
    high = (int)hash->lists[key]->count;
    mid = high / 2;

    do
    {

#ifdef DEBUG
      if (config->debug >= 1)
        printf("DEBUG - snoop hashrec Count: %lu L: %d M: %d H: %d\n", hash->lists[key]->count, low, mid, high);
#endif

      if ((ret = strcmp(keyString, hash->lists[key]->records[mid]->keyString)) > 0)
        low = mid + 1;
      else if (ret < 0)
        high = mid;
      else
      {
#ifdef DEBUG
        if (config->debug >= 4)
          printf("DEBUG - Found (%s) in hash table at [%u] in record list [%d]\n",
                 (char *)keyString, key, mid);
#endif
        hash->lists[key]->records[mid]->lastSeen = config->current_time;
        return hash->lists[key]->records[mid];
      }
      mid = low + ((high - low) / 2);
    } while (low < high);
  }

  return NULL;
}

/****
 *
 * Retrieve hash record without updating timestamp
 *
 * DESCRIPTION:
 *   Same as getHashRecord but does not modify lastSeen field.
 *
 * PARAMETERS:
 *   hash - Hash table
 *   keyString - Key to find
 *   keyLen - Key length (0 for null-terminated)
 *
 * RETURNS:
 *   Pointer to record, or NULL if not found
 *
 ****/

struct hashRec_s *snoopHashRecord(struct hash_s *hash, const char *keyString, int keyLen)
{
  uint32_t key;
  uint32_t val = 0;
  uint32_t tmp;
  int i, ret, cmpLen, low, high;
  register int mid;

  if (keyLen EQ 0)
    keyLen = (int)(strlen(keyString) + 1);

  /* generate the lookup hash */
  for (i = 0; i < keyLen; i++)
  {
    val = (val << 4) + (keyString[i] & 0xff);
    if ((tmp = (val & 0xf0000000)))
    {
      val = val ^ (tmp >> 24);
      val = val ^ tmp;
    }
  }
  key = (uint32_t)val % hash->size;

#ifdef DEBUG
  if (config->debug >= 3)
    printf("DEBUG - Snooping record from hash table\n");
#endif

  if (hash->lists[key] != NULL)
  {
    /* search for keyString and hash record if found */
    low = 0;
    high = (int)hash->lists[key]->count;
    mid = high / 2;

    /* use the shortest string */
    if (hash->lists[key]->records[mid]->keyLen < keyLen)
      cmpLen = (int)hash->lists[key]->records[mid]->keyLen;
    else
      cmpLen = keyLen;

#ifdef DEBUG
    if (config->debug >= 1)
      printf("DEBUG - snoop hashrec L: %d M: %d H: %d\n", low, mid, high);
#endif

    do
    {
      if ((ret = XMEMCMP(keyString, hash->lists[key]->records[mid]->keyString, (size_t)cmpLen)) > 0)
        low = mid + 1;
      else if (ret < 0)
        high = mid;
      else
      {
        if (keyLen > hash->lists[key]->records[mid]->keyLen)
          low = mid + 1;
        else if (keyLen < hash->lists[key]->records[mid]->keyLen)
          high = mid;
        else
        {
#ifdef DEBUG
          if (config->debug >= 4)
            printf("DEBUG - Found (%s) in hash table at [%u] in record list [%d]\n", (char *)keyString, key, mid);
#endif
          return hash->lists[key]->records[mid];
        }
      }
      mid = low + ((high - low) / 2);
    } while (low < high);
  }

  return NULL;
}

/****
 *
 * Retrieve user data from hash record
 *
 * DESCRIPTION:
 *   Looks up record and returns associated data pointer. Updates lastSeen timestamp.
 *
 * PARAMETERS:
 *   hash - Hash table
 *   keyString - Key to find
 *   keyLen - Key length (0 for null-terminated)
 *
 * RETURNS:
 *   Data pointer, or NULL if not found
 *
 * SIDE EFFECTS:
 *   Updates lastSeen field on match
 *
 ****/

void *getHashData(struct hash_s *hash, const char *keyString, int keyLen)
{
  uint32_t key;
  uint32_t val = 0;
  uint32_t tmp;
  int i, ret, cmpLen, low, high;
  register int mid;

  if (keyLen EQ 0)
    keyLen = (int)(strlen(keyString) + 1);

  /* generate the lookup hash */
  for (i = 0; i < keyLen; i++)
  {
    val = (val << 4) + (keyString[i] & 0xff);
    if ((tmp = (val & 0xf0000000)))
    {
      val = val ^ (tmp >> 24);
      val = val ^ tmp;
    }
  }
  key = (uint32_t)val % hash->size;

#ifdef DEBUG
  if (config->debug >= 3)
    printf("DEBUG - Getting data from hash table\n");
#endif

  if (hash->lists[key] != NULL)
  {
    /* search for keyString and hash record if found */
    low = 0;
    high = (int)hash->lists[key]->count;
    mid = high / 2;

    /* use the shortest string */
    if (hash->lists[key]->records[mid]->keyLen < keyLen)
      cmpLen = (int)hash->lists[key]->records[mid]->keyLen;
    else
      cmpLen = keyLen;

#ifdef DEBUG
    if (config->debug >= 1)
      printf("DEBUG - snoop hashrec L: %d M: %d H: %d\n", low, mid, high);
#endif

    do
    {
      if ((ret = XMEMCMP(keyString, hash->lists[key]->records[mid]->keyString, (size_t)cmpLen)) > 0)
        low = mid + 1;
      else if (ret < 0)
        high = mid;
      else
      {
        if (keyLen > hash->lists[key]->records[mid]->keyLen)
          low = mid + 1;
        else if (keyLen < hash->lists[key]->records[mid]->keyLen)
          high = mid;
        else
        {
#ifdef DEBUG
          if (config->debug >= 4)
            printf("DEBUG - Found (%s) in hash table at [%u] in record list [%d]\n", (char *)keyString, key, mid);
#endif
          hash->lists[key]->records[mid]->lastSeen = config->current_time;
          return hash->lists[key]->records[mid]->data;
        }
      }
      mid = low + ((high - low) / 2);
    } while (low < high);
  }

  return NULL;
}

/****
 *
 * Retrieve user data without updating timestamp
 *
 * DESCRIPTION:
 *   Same as getHashData but does not modify lastSeen field.
 *
 * PARAMETERS:
 *   hash - Hash table
 *   keyString - Key to find
 *   keyLen - Key length (0 for null-terminated)
 *
 * RETURNS:
 *   Data pointer, or NULL if not found
 *
 ****/

void *snoopHashData(struct hash_s *hash, const char *keyString, int keyLen)
{
  uint32_t key;
  uint32_t val = 0;
  uint32_t tmp;
  int i, ret, cmpLen, low, high;
  register int mid;

  if (keyLen EQ 0)
    keyLen = (int)(strlen(keyString) + 1);

  /* generate the lookup hash */
  for (i = 0; i < keyLen; i++)
  {
    val = (val << 4) + (keyString[i] & 0xff);
    if ((tmp = (val & 0xf0000000)))
    {
      val = val ^ (tmp >> 24);
      val = val ^ tmp;
    }
  }
  key = (uint32_t)val % hash->size;

#ifdef DEBUG
  if (config->debug >= 3)
    printf("DEBUG - Snooping data from hash table\n");
#endif

  if (hash->lists[key] != NULL)
  {
    /* search for keyString and hash record if found */
    low = 0;
    high = (int)hash->lists[key]->count;
    mid = high / 2;

    /* use the shortest string */
    if (hash->lists[key]->records[mid]->keyLen < keyLen)
      cmpLen = (int)hash->lists[key]->records[mid]->keyLen;
    else
      cmpLen = keyLen;

#ifdef DEBUG
    if (config->debug >= 1)
      printf("DEBUG - snoop hashrec L: %d M: %d H: %d\n", low, mid, high);
#endif

    do
    {
      if ((ret = XMEMCMP(keyString, hash->lists[key]->records[mid]->keyString, (size_t)cmpLen)) > 0)
        low = mid + 1;
      else if (ret < 0)
        high = mid;
      else
      {
        if (keyLen > hash->lists[key]->records[mid]->keyLen)
          low = mid + 1;
        else if (keyLen < hash->lists[key]->records[mid]->keyLen)
          high = mid;
        else
        {
#ifdef DEBUG
          if (config->debug >= 4)
            printf("DEBUG - Found (%s) in hash table at [%u] in record list [%d]\n", (char *)keyString, key, mid);
#endif
          return hash->lists[key]->records[mid]->data;
        }
      }
      mid = low + ((high - low) / 2);
    } while (low < high);
  }

  return NULL;
}

/****
 *
 * Remove record from hash table
 *
 * DESCRIPTION:
 *   Finds and deletes record, shrinking collision list. Returns user data pointer
 *   for caller to free.
 *
 * PARAMETERS:
 *   hash - Hash table
 *   keyString - Key to delete
 *   keyLen - Key length (0 for null-terminated)
 *
 * RETURNS:
 *   Data pointer from deleted record, or NULL if not found
 *
 * SIDE EFFECTS:
 *   Frees record and key memory, reallocates collision list
 *
 ****/

void *deleteHashRecord(struct hash_s *hash, const char *keyString, int keyLen)
{
  uint32_t key;
  uint32_t val = 0;
  uint32_t tmp;
  int i, ret, low, high, cmpLen;
  register int mid;
  void *tmpDataPtr;

  if (keyLen EQ 0)
    keyLen = (int)(strlen(keyString) + 1);

  /* generate the lookup hash */
  for (i = 0; i < keyLen; i++)
  {
    val = (val << 4) + (keyString[i] & 0xff);
    if ((tmp = (val & 0xf0000000)))
    {
      val = val ^ (tmp >> 24);
      val = val ^ tmp;
    }
  }
  key = (uint32_t)val % hash->size;

#ifdef DEBUG
  if (config->debug >= 3)
    printf("DEBUG - Searching for record to delete from the hash table\n");
#endif

  if (hash->lists[key] != NULL)
  {
    /* search for keyString and hash record if found */
    low = 0;
    high = (int)hash->lists[key]->count;
    mid = high / 2;

    /* use the shortest string */
    if (hash->lists[key]->records[mid]->keyLen < keyLen)
      cmpLen = (int)hash->lists[key]->records[mid]->keyLen;
    else
      cmpLen = keyLen;

#ifdef DEBUG
    if (config->debug >= 1)
      printf("DEBUG - snoop hashrec L: %d M: %d H: %d\n", low, mid, high);
#endif

    do
    {
      if ((ret = XMEMCMP(keyString, hash->lists[key]->records[mid]->keyString, (size_t)cmpLen)) > 0)
        low = mid + 1;
      else if (ret < 0)
        high = mid;
      else
      {
        if (keyLen > hash->lists[key]->records[mid]->keyLen)
          low = mid + 1;
        else if (keyLen < hash->lists[key]->records[mid]->keyLen)
          high = mid;
        else
        {
#ifdef DEBUG
          if (config->debug >= 4)
            printf("DEBUG - Found (%s) in hash table at [%u] in record list [%d]\n", (char *)keyString, key, mid);
#endif
          tmpDataPtr = hash->lists[key]->records[mid]->data;
          XFREE(hash->lists[key]->records[mid]->keyString);
          XFREE(hash->lists[key]->records[mid]);

          if (hash->lists[key]->count EQ 1)
          {
            /* last record in list */
            XFREE(hash->lists[key]->records);
            XFREE(hash->lists[key]);
            hash->lists[key] = NULL;
            return tmpDataPtr;
          }
          else if (mid < hash->lists[key]->count)
          {
            /* move mem up to fill the hole */
            /* XXX need to add a wrapper in mem.c for memmove */
            memmove(&hash->lists[key]->records[mid], &hash->lists[key]->records[mid + 1], sizeof(struct hashRec_s *) * (hash->lists[key]->count - (size_t)(mid + 1)));
          }
          hash->lists[key]->count--;
          /* shrink the buffer */
          if ((hash->lists[key]->records = (struct hashRec_s **)XREALLOC(hash->lists[key]->records, (int)(sizeof(struct hashRec_s *) * (hash->lists[key]->count)))) EQ NULL)
          {
            /* XXX need better cleanup than this */
            fprintf(stderr, "ERR - Unable to allocate space for hash record list\n");
            return NULL;
          }
          /* return the data */
          return tmpDataPtr;
        }
      }
      mid = low + ((high - low) / 2);
    } while (low < high);
  }

  return NULL;
}

/****
 *
 * Grow hash table to next prime size
 *
 * DESCRIPTION:
 *   Allocates larger hash table and rehashes all records when load factor exceeds
 *   0.8. Returns new hash or original if at maximum size.
 *
 * PARAMETERS:
 *   oldHash - Hash table to grow
 *
 * RETURNS:
 *   Pointer to new hash (or original if growth not needed/possible)
 *
 * SIDE EFFECTS:
 *   Frees old hash structure (but not user data)
 *
 ****/

struct hash_s *dyGrowHash(struct hash_s *oldHash)
{
  struct hash_s *tmpHash;
  int i;
  uint32_t tmpKey;

  if (((float)oldHash->totalRecords / (float)oldHash->size) > 0.8)
  {
    /* the hash should be grown */

#ifdef DEBUG
    if (config->debug >= 3)
      printf("DEBUG - R: %d T: %d\n", oldHash->totalRecords, oldHash->size);
#endif

    if (hashPrimes[oldHash->primeOff + 1] EQ 0)
    {
      fprintf(stderr, "ERR - Hash at maximum size already\n");
      return oldHash;
    }
#ifdef DEBUG
    if (config->debug >= 4)
      printf("DEBUG - HASH: Growing\n");
#endif
    if ((tmpHash = initHash((uint32_t)hashPrimes[oldHash->primeOff + 1])) EQ NULL)
    {
      fprintf(stderr, "ERR - Unable to allocate new hash\n");
      return oldHash;
    }

    for (tmpKey = 0; tmpKey < oldHash->size; tmpKey++)
    {
      if (oldHash->lists[tmpKey] != NULL)
      {
        for (i = 0; i < (int)oldHash->lists[tmpKey]->count; i++)
        {
          if (insertUniqueHashRec(tmpHash, oldHash->lists[tmpKey]->records[i]) != TRUE)
          {
            fprintf(stderr, "ERR - Failed to insert hash record while growing\n");
            /* XXX need to do properly handle this error */
          }
        }
        /* free old hash record list buffer */
        XFREE(oldHash->lists[tmpKey]->records);
        XFREE(oldHash->lists[tmpKey]);
      }
    }

#ifdef DEBUG
    if (config->debug >= 5)
      printf("DEBUG - Old [RC: %u T: %u] New [RC: %u T: %u]\n",
             oldHash->totalRecords, oldHash->size, tmpHash->totalRecords,
             tmpHash->size);
#endif

    if (tmpHash->totalRecords != oldHash->totalRecords)
    {
      fprintf(stderr,
              "ERR - New hash is not the same size as the old hash [%u->%u]\n",
              oldHash->totalRecords, tmpHash->totalRecords);
    }

    /* free the rest of the old hash buffers */
    XFREE(oldHash->lists);
    XFREE(oldHash);

#ifdef DEBUG
    if (config->debug >= 5)
      printf("DEBUG - HASH: Grew\n");
#endif
    return tmpHash;
  }

  return oldHash;
}

/****
 *
 * Shrink hash table to smaller prime size
 *
 * DESCRIPTION:
 *   Allocates smaller hash table and rehashes all records when load factor below
 *   0.3. Returns new hash or original if at minimum size.
 *
 * PARAMETERS:
 *   oldHash - Hash table to shrink
 *
 * RETURNS:
 *   Pointer to new hash (or original if shrink not needed/possible)
 *
 * SIDE EFFECTS:
 *   Frees old hash structure (but not user data)
 *
 ****/

struct hash_s *dyShrinkHash(struct hash_s *oldHash)
{
  struct hash_s *tmpHash;
  int i;
  uint32_t tmpKey;

  if ((oldHash->totalRecords / oldHash->size) < 0.3)
  {
    /* the hash should be shrunk */

#ifdef DEBUG
    if (config->debug >= 3)
      printf("DEBUG - R: %d T: %d\n", oldHash->totalRecords, oldHash->size);
#endif

    if (oldHash->primeOff EQ 0)
    {
      fprintf(stderr, "ERR - Hash at minimum size already\n");
      return oldHash;
    }
#ifdef DEBUG
    if (config->debug >= 4)
      printf("DEBUG - HASH: Shrinking\n");
#endif
    if ((tmpHash = initHash((uint32_t)hashPrimes[oldHash->primeOff - 1])) EQ NULL)
    {
      fprintf(stderr, "ERR - Unable to allocate new hash\n");
      return oldHash;
    }

    for (tmpKey = 0; tmpKey < oldHash->size; tmpKey++)
    {
      if (oldHash->lists[tmpKey] != NULL)
      {
        for (i = 0; i < (int)oldHash->lists[tmpKey]->count; i++)
        {
          if (insertUniqueHashRec(tmpHash, oldHash->lists[tmpKey]->records[i]) EQ FAILED)
          {
            fprintf(stderr, "ERR - Failed to insert hash record while shrinking\n");
            /* XXX need to do properly handle this error */
          }
        }
        /* free old hash record list buffer */
        XFREE(oldHash->lists[tmpKey]->records);
      }
    }

#ifdef DEBUG
    if (config->debug >= 5)
      printf("DEBUG - Old [RC: %u T: %u] New [RC: %u T: %u]\n",
             oldHash->totalRecords, oldHash->size, tmpHash->totalRecords,
             tmpHash->size);
#endif

    if (tmpHash->totalRecords != oldHash->totalRecords)
    {
      fprintf(stderr,
              "ERR - New hash is not the same size as the old hash [%u->%u]\n",
              oldHash->totalRecords, tmpHash->totalRecords);
    }

    /* free the rest of the old hash buffers */
    XFREE(oldHash->lists);
    XFREE(oldHash);

#ifdef DEBUG
    if (config->debug >= 5)
      printf("DEBUG - HASH: Grew\n");
#endif
    return tmpHash;
  }

  return oldHash;
}

/****
 *
 * Remove records older than specified age
 *
 * DESCRIPTION:
 *   Scans hash and removes records with lastSeen older than age threshold.
 *   Returns array of user data pointers from purged records.
 *
 * PARAMETERS:
 *   hash - Hash table
 *   age - Cutoff time (records with lastSeen < age are purged)
 *   dataList - Existing data list to append to (or NULL for new list)
 *
 * RETURNS:
 *   NULL-terminated array of data pointers from purged records
 *
 * SIDE EFFECTS:
 *   Frees purged records, reallocates collision lists
 *
 ****/

void **purgeOldHashRecords(struct hash_s *hash, time_t age, void **dataList)
{
  uint32_t i, key;
  size_t count = 0;

  if (dataList EQ NULL)
  {
    if ((dataList = (void **)XMALLOC((int)(sizeof(void *) * (count + 1)))) EQ NULL)
    {
      fprintf(stderr, "ERR - Unable to allocate memory for purged data list\n");
      return NULL;
    }
  }
  dataList[count] = NULL;

#ifdef DEBUG
  if (config->debug >= 3)
    printf("DEBUG - Purging hash records older than [%lld]\n", (long long)age);
#endif

  for (key = 0; key < hash->size; key++)
  {
    if (hash->lists[key] != NULL)
    {
      for (i = 0; i < (int)hash->lists[key]->count; i++)
      {
        if (hash->lists[key]->records[i] != NULL)
        {
          if (hash->lists[key]->records[i]->lastSeen < age)
          {
            /* old record, remove it */
            count++;
            if ((dataList = (void **)XREALLOC(dataList, (int)count)) EQ NULL)
            {
              fprintf(stderr, "ERR - Unable to grow memory for purged data list\n");
              return NULL;
            }
            dataList[count - 1] = hash->lists[key]->records[i]->data;
            dataList[count] = NULL;
            XFREE(hash->lists[key]->records[i]->keyString);
            XFREE(hash->lists[key]->records[i]);

            if (hash->lists[key]->count EQ 1)
            {
              /* last record in list */
              XFREE(hash->lists[key]->records);
              XFREE(hash->lists[key]);
              hash->lists[key] = NULL;
            }
            else if (i < (int)hash->lists[key]->count)
            {
              /* move mem up to fill the hole */
              /* XXX need to add a wrapper in mem.c for memmove */
              memmove(&hash->lists[key]->records[i], &hash->lists[key]->records[i + 1], sizeof(struct hashRec_s *) * (hash->lists[key]->count - (size_t)(i + 1)));
            }
            hash->lists[key]->count--;
            /* shrink the buffer */
            if ((hash->lists[key]->records = (struct hashRec_s **)XREALLOC(hash->lists[key]->records, sizeof(struct hashRec_s *) * (hash->lists[key]->count))) EQ NULL)
            {
              /* XXX need better cleanup than this */
              fprintf(stderr, "ERR - Unable to allocate space for hash record list\n");
              return NULL;
            }
          }
        }
      }
    }
  }
  return (dataList);
}

/****
 *
 * Convert binary key to hex string
 *
 * DESCRIPTION:
 *   Formats potentially binary key as hex string for debug output.
 *
 * PARAMETERS:
 *   keyString - Key data (may contain non-printable bytes)
 *   keyLen - Length of key
 *   buf - Output buffer
 *   bufLen - Size of output buffer
 *
 * RETURNS:
 *   Pointer to output buffer
 *
 ****/

char *hexConvert(const char *keyString, int keyLen, char *buf,
                 const int bufLen)
{
  int i;
  int max_chars;
  char *ptr = buf;

  /* Calculate maximum characters to avoid overflow warning in loop */
  max_chars = (bufLen > 1) ? ((bufLen / 2) - 1) : 0;

  for (i = 0; (i < keyLen) && (i < max_chars); i++)
  {
    snprintf(ptr, (size_t)bufLen, "%02x", keyString[i] & 0xff);
    ptr += 2;
  }
  return buf;
}

/****
 *
 * Convert UTF-16 string to ASCII (naive implementation)
 *
 * DESCRIPTION:
 *   Simple UTF-16 to ASCII conversion by dropping high bytes. Does not properly
 *   handle Unicode - only works for ASCII subset.
 *
 * PARAMETERS:
 *   keyString - UTF-16 encoded string
 *   keyLen - Length in bytes
 *   buf - Output buffer
 *   bufLen - Size of output buffer (unused)
 *
 * RETURNS:
 *   Pointer to output buffer
 *
 ****/

char *utfConvert(const char *keyString, int keyLen, char *buf,
                 const int bufLen __attribute__((unused)))
{
  int i;
  /* XXX should check for buf len */
  for (i = 0; i < (keyLen / 2); i++)
  {
    buf[i] = keyString[(i * 2)];
  }
  buf[i] = '\0';

  return buf;
}

/****
 *
 * Get hash table size
 *
 * DESCRIPTION:
 *   Returns current size (number of buckets) of hash table.
 *
 * PARAMETERS:
 *   hash - Hash table
 *
 * RETURNS:
 *   Size of hash table, or 0 if NULL
 *
 ****/

uint32_t getHashSize(struct hash_s *hash)
{
  if (hash != NULL)
    return hash->size;
  return 0;  /* Return 0 instead of FAILED for unsigned type */
}
