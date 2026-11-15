#include <cstdio>

#ifndef _HASHMGR_HXX_
#define _HASHMGR_HXX_

#include "htypes.hxx"

class HashMgr
{
  bool            ready;
  int             tablesize;
  struct hentry * tableptr;

public:
  HashMgr(FILE *rawdict);
  ~HashMgr();
  bool loaded() const { return ready; }

  struct hentry * lookup(const char *) const;
  int hash(const char *) const;
  struct hentry * walk_hashtable(int & col, struct hentry * hp) const;

private:
  HashMgr( const HashMgr & ); // not implemented
  HashMgr &operator=( const HashMgr & ); // not implemented
  int load_tables(FILE *rawdict);
  int add_word(const char * word, int wl, const char * ap, int al);

};

#endif
