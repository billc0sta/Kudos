#include "http_headers.h"
#define LOAD_FACTOR_MAX 0.6
#define LOAD_FACTOR_MIN 0.1
#define INITIAL_BUCKETS 16
#define MULTIPLY_SPACE (1 * 2) 
#define HEADERS_SEED 0

#define STATE_UNUSED 0
#define STATE_USED 1
#define STATE_DELETED 2

http_headers* http_headers_make(void) {
  http_headers* ret = (http_headers*)malloc(sizeof(http_headers));
  if (!ret) {
    HTTP_LOG(HTTP_LOGERR, "[make_headers] failed to allocate memory.\n");
    return NULL;
  }
  ret->buckets = (struct bucket*)calloc(INITIAL_BUCKETS, sizeof(struct bucket));
  if (!ret->buckets) {
    free(ret);
    HTTP_LOG(HTTP_LOGERR, "[make_headers] failed to allocate memory.\n");
    return NULL;
  }
  ret->cap = INITIAL_BUCKETS;
  ret->len = 0;
  return ret;
}

static inline unsigned int murmur_scramble(unsigned int k) {
  k *= 0xcc9e2d51;
  k = (k << 15) | (k >> 17);
  k *= 0x1b873593;
  return k;
}

static unsigned int hashstring_murmur(const char* key, size_t size)
{
  unsigned int h = HEADERS_SEED;
  unsigned int k = 0;
  for (size_t i = size >> 2; i; i--) {
    k |= tolower(*key++) & 0xff;
    k |= ((tolower(*key++) & 0xff) << 8);
    k |= ((tolower(*key++) & 0xff) << 16);
    k |= (((unsigned)tolower(*key++) & 0xff) << 24);
    h ^= murmur_scramble(k);
    h = (h << 13) | (h >> 19);
    h = h * 5 + 0xe6546b64;
  }
  k = 0;
  for (size_t i = size & 3; i; i--) {
    k <<= 8;
    k |= tolower(key[i - 1]);
  }
  h ^= murmur_scramble(k);
  h ^= size;
  h ^= h >> 16;
  h *= 0x85ebca6b;
  h ^= h >> 13;
  h *= 0xc2b2ae35;
  h ^= h >> 16;
  return h;
}

static int compare(const char* str1, const char* str2) {
  while (*str1 && *str2) {
    if (tolower(*str1) != tolower(*str2))
      return *str1 - *str2;
    ++str1;
    ++str2;
  }
  return *str1 - *str2;
}

static struct bucket* http_headers_find(http_headers* map, const char* key)
{
  unsigned int i = hashstring_murmur(key, strlen(key)) & (map->cap - 1);
  struct bucket* bucket;
  while ((bucket = &map->buckets[i])->state != STATE_UNUSED && compare(key, bucket->key.v) != 0)
    i = (i + 1) & (map->cap - 1);

  return bucket;
}

static int http_headers_resize(http_headers* map, size_t resize_by)
{
  if (!map) {
    HTTP_LOG(HTTP_LOGERR, "[resize_headers] passed NULL pointers for mandatory parameters.\n");
    return HTTP_FAILURE;
  }
  if (resize_by == 0)
    resize_by = INITIAL_BUCKETS;

  if (resize_by == map->cap)
    return HTTP_SUCCESS;

  map->len = 0;
  int old_cap = (int)map->cap;
  map->cap = resize_by;
  struct bucket* old_buckets = map->buckets;
  struct bucket* new_buckets = (struct bucket*)calloc(map->cap, sizeof(struct bucket));
  if (!new_buckets) {
    HTTP_LOG(HTTP_LOGERR, "[resize_headers] failed to allocate memory.\n");
    return HTTP_FAILURE;
  }

  map->buckets = new_buckets;
  for (int i = 0; i < old_cap; ++i) {
    struct bucket* curr = &old_buckets[i];
    if (curr->state == STATE_USED) {
      struct bucket* bucket = http_headers_find(map, curr->key.v); // safe
      *bucket = *curr; 
    }

    else if (curr->state == STATE_DELETED) {
      free(curr->key.v);
      http_hdv* val  = curr->val;
      http_hdv* next = NULL;
      while(val) {
        next = val->next;
        free(val);
        val = next;
      }
    }
  }
  free(old_buckets);

  return HTTP_SUCCESS;
}

int http_headers_set(http_headers* map, const char* key, const char* val) {
  if (!map || !key || !val) {
    HTTP_LOG(HTTP_LOGERR, "[set_header] passed NULL pointers for mandatory parameters.\n");
    return HTTP_FAILURE;
  }
  unsigned int i = hashstring_murmur(key, strlen(key)) & (map->cap - 1);
  struct bucket* deleted_bucket = NULL;
  struct bucket* bucket = NULL;

  while ((bucket = &map->buckets[i])->state != STATE_UNUSED && compare(key, bucket->key.v) != 0) {
    if (bucket->state == STATE_DELETED)
      deleted_bucket = bucket;
    i = (i + 1) & (map->cap - 1);
  }
  if (deleted_bucket) {
    if (bucket->state == STATE_USED)
      bucket->state = STATE_DELETED;
    bucket = deleted_bucket;
  }

  size_t keylen = strlen(key);
  size_t vallen = strlen(val);
  http_hdv* v = (http_hdv*)malloc(sizeof(http_hdv) + vallen + 1);
  if (!v) {
    HTTP_LOG(HTTP_LOGERR, "[set_header] failed to allocate memory.\n");
    return HTTP_FAILURE;
  }
  v->next = NULL;
  v->v = (char*)(v + 1);
  v->v[vallen] = 0;
  v->len = vallen;
  memcpy(v->v, val, vallen);
  if (bucket->state == STATE_UNUSED) {
    bucket->key.v = (char*)malloc(keylen + 1);
    if (!bucket->key.v) {
      free(v);
      HTTP_LOG(HTTP_LOGERR, "[set_header] failed to allocate memory.\n");
      return HTTP_FAILURE;
    }
  }
  else	{
    if (bucket->key.len < keylen) {
      free(bucket->key.v);
      bucket->key.v = (char*)malloc(keylen + 1);
      if (!bucket->key.v) {
        free(v);
        HTTP_LOG(HTTP_LOGERR, "[set_header] failed to allocate memory.\n");
        return HTTP_FAILURE;
      }
    }
    if (bucket->state == STATE_DELETED) {
      http_hdv* valhdv = bucket->val;
      http_hdv* next = NULL;
      while (valhdv) {
        next = valhdv->next;
        free(valhdv); // also deallocates the string
        valhdv = next;
      }
      bucket->val = NULL; 
    }
  }
  bucket->key.len       = keylen; 
  bucket->key.v[keylen] = 0; 
  memcpy(bucket->key.v, key, keylen);
  bucket->state = STATE_USED;
  if (!bucket->val)
    bucket->val = v;
  else {
    v->next = bucket->val;
    bucket->val = v;
  }
  ++map->len;

  if ((float)map->len / map->cap >= LOAD_FACTOR_MAX) {
    if (http_headers_resize(map, map->cap * MULTIPLY_SPACE)) {
      HTTP_LOG(HTTP_LOGERR, "[set_header] hashmap_resize failed.\n");
      return HTTP_FAILURE;
    }
  }
  return HTTP_SUCCESS; 
}

http_hdv* http_headers_get(http_headers* map, const char* key)
{
  if (!map || !key) {
    HTTP_LOG(HTTP_LOGERR, "[get_header] passed NULL pointers for mandatory parameters.\n");
    return NULL;
  } 
  struct bucket* found = http_headers_find(map, key); 
  if (found->state == STATE_USED)
    return found->val;

  return NULL;
}

int http_headers_remove(http_headers* map, const char* key)
{
  if (!map || key) {
    HTTP_LOG(HTTP_LOGERR, "[remove_header] passed NULL pointers for mandatory parameters.\n");
    return HTTP_FAILURE;
  }

  struct bucket* found = http_headers_find(map, key);
  if (found->state != STATE_USED)
    return HTTP_FAILURE;

  found->state = STATE_DELETED;
  --map->len;

  if (map->cap > INITIAL_BUCKETS && (float)map->len / map->cap <= LOAD_FACTOR_MIN) {
    if (http_headers_resize(map, map->cap / MULTIPLY_SPACE)) {
      HTTP_LOG(HTTP_LOGERR, "[remove_header] hashmap_resize failed.\n");
      return HTTP_FAILURE;
    }
  }

  return HTTP_SUCCESS;
}

int http_headers_next(http_headers* map, size_t* iter, http_hdk* key, http_hdv** val)
{
  if (!map || !iter || !key || !val) {
    HTTP_LOG(HTTP_LOGERR, "[next_header] passed NULL pointers for mandatory parameters.\n");
    return HTTP_FAILURE;
  }

  while (*iter < map->cap) {
    struct bucket* bucket = &map->buckets[*iter];
    ++(*iter);
    if (bucket->state == STATE_USED)
      {
        *key = bucket->key;
        *val = bucket->val;
        return HTTP_SUCCESS;
      }
  }
  return HTTP_FAILURE;
}

int http_headers_reset(http_headers* map)
{
  if (!map) {
    HTTP_LOG(HTTP_LOGERR, "[reset_headers] passed NULL pointers for mandatory parameters.\n");
    return HTTP_FAILURE;
  }
  for (int i = 0; i < map->cap; ++i) {
    struct bucket* bucket = &map->buckets[i];
    if (bucket->state != STATE_UNUSED)
      bucket->state = STATE_DELETED;
  }
  map->len = 0;
  return HTTP_SUCCESS;
}

int http_headers_free(http_headers* map) {
  if (!map) {
    HTTP_LOG(HTTP_LOGERR, "[free_headers] passed NULL pointers for mandatory parameters.\n");
    return HTTP_FAILURE;
  }
  http_headers_reset(map);
  free(map->buckets);
  free(map);
  return HTTP_SUCCESS;
}